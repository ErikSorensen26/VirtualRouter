#include "DhcpClient.h"
#include <shared_mutex>
#include <Interface.h>
#include <Global.h>
#include <VirtualRouter.h>

// Constructor for the DhcpClient class, initializes with a reference to an Interface object
Protocol::DhcpClient::DhcpClient(Interface* iface, bool reduced) // Reduced is used for testing
    : currentInterface(iface),
      stopFlag(true),
      offered(false),
      acked(false),
      naked(false),
      leaseStart(0)
{
    leaseStart = secondsSinceEpoch();
    if (!reduced)
    {
        if (!iface->shutdownFlag.load(std::memory_order_relaxed))
        {
            initializeDhcp();
        }
    }
}

// Destructor to clean up threads and resources
Protocol::DhcpClient::~DhcpClient()
{
    shutdown();
}

void Protocol::DhcpClient::shutdown()
{
    if (stopFlag.load(std::memory_order_relaxed)) return;
    stopFlag.store(true);
    cv.notify_all();

    if (acked)
    {
        sendDhcpRelease();
    }

    if (dhcpThread.joinable())
    {
        dhcpThread.join();
    }
}

// Initializes DHCP, sends discover requests, handles offers, and sends requests and acknowledgments
void Protocol::DhcpClient::initializeDhcp() 
{
    if (!stopFlag.load(std::memory_order_relaxed) || currentInterface->configs.ipv4.getAddress().empty()) return;
    stopFlag.store(false, std::memory_order_release);
    ByteString mac;
    mac = currentInterface->configs.getMac();
    std::string hostname = currentInterface->routingInstance->global.getHostname();
    // Start the DHCP handling thread
    dhcpThread = std::thread(&DhcpClient::dhcpHandler, this, hostname, mac);
}

// Main DHCP handling loop running in a seperate thread
void Protocol::DhcpClient::dhcpHandler(std::string hostname, ByteString hardwareAddress)
{
    sendDhcpDiscover(hostname, hardwareAddress);
    processDhcpOffer(hostname, hardwareAddress);

    while (!stopFlag.load())
    {
        if (offered)
        {
            processDhcpOffer(hostname, hardwareAddress);
        }
        
        // Wait for ACK, NACK, DECLINE, or stop signal
        {
            std::unique_lock<std::mutex> lock(dhcpMutex);
            cv.wait(lock, [this]() { return acked || naked || stopFlag.load(); });
            if (stopFlag.load()) break;
        }

        if (acked)
        {
            handleLeaseRenewal(hardwareAddress, hostname);
        }

        if (naked)
        {
            std::lock_guard<std::mutex> lock(dhcpMutex);
            resetDhcpState();
        }
    }
}

// Sends a DHCP Discover message
void Protocol::DhcpClient::sendDhcpDiscover(const std::string& hostname, ByteString& hardwareAddress)
{
    if (!configs.dhcpServer.empty())
    {
        return; // No need for dhcp discover
    }

    if (!currentInterface->shutdownFlag.load(std::memory_order_relaxed))
    {
        if (!currentInterface->configs.ipv4.getAddress().empty())
        {
            return; // No need for dhcp discover
        }
    }
    else
    {
        return;
    }

    if (!configs.leaseTime.empty() && leaseStart + Functions::byteToNum(configs.leaseTime) < secondsSinceEpoch())
    {
        return; // No need for dhcp discover
    }

    constexpr int maxRetries = 4; // Max number of retries
    int retryCount = 0;
    auto timeout = std::chrono::seconds(4); // Initial timneout duration

    PacketInfo discoverPacket;
    {
        std::lock_guard<std::mutex> lock(dhcpMutex);
        discoverPacket = dhcpDiscover(dhcpBody(hardwareAddress), hostname, hardwareAddress);
    }

    while (retryCount < maxRetries)
    {
        currentInterface->enqueuePacket(discoverPacket);

        // Wait for the offer packet or timeout
        std::unique_lock<std::mutex> lock(dhcpMutex);
        if (cv.wait_for(lock, timeout, [&]() { return offered || stopFlag.load(); }))
        {
            return;
        }
        retryCount++;
        timeout *= 2;
    }
}

// Sends a DHCP request message
void Protocol::DhcpClient::sendDhcpRequest(PacketInfo& requestPacket)
{
    constexpr int maxRetries = 4; // Max number of retries
    int retryCount = 0;
    auto timeout = std::chrono::seconds(4);

    while (retryCount < maxRetries)
    {
        currentInterface->enqueuePacket(requestPacket);

        // Wait for the offer packet or timeout
        std::unique_lock<std::mutex> dhcpLock(dhcpMutex);
        if (cv.wait_for(dhcpLock, timeout, [&]() { return acked || stopFlag.load(); }))
        {
            return;
        }
        retryCount++;
        timeout *= 2;
    }
}

// Processes a received DHCP offer message
void Protocol::DhcpClient::processDhcpOffer(const std::string& hostname, ByteString& hardwareAddress)
{
    std::unique_lock<std::mutex> lock(dhcpMutex);
    if (!dhcpOffer.Layer2.empty() && !dhcpOffer.Layer5.empty())
    {
        auto& header = dhcpOffer.Layer5[0];
        if (std::holds_alternative<DhcpHeader>(header))
        {
            DhcpHeader* dhcpPtr = &(std::get<DhcpHeader>(header));
            ExtractOptions(dhcpPtr->options);

            PacketInfo requestPacket = dhcpRequest(
                dhcpBody(hardwareAddress),
                *dhcpPtr,
                hostname,
                hardwareAddress,
                Variable::IPv4::source,
                configs.dhcpServer
            );

            // Handle retries
            lock.unlock();
            sendDhcpRequest(requestPacket);
            offered = false;
        }
    }
}

// Handles DHCP Lease Renewal process
void Protocol::DhcpClient::handleLeaseRenewal(ByteString& hardwareAddress, const std::string& hostname)
{
    std::unique_lock<std::mutex> lock(dhcpMutex);
    double currentTime = secondsSinceEpoch();
    uint32_t renewalTime = Functions::byteToNum(configs.renewalTime);

    if (leaseStart + renewalTime < currentTime)
    {
        ByteString dhcpIP;
        if (!currentInterface->shutdownFlag.load(std::memory_order_relaxed))
        {
            dhcpIP = currentInterface->configs.ipv4.getAddress();
        }
        DhcpHeader header;
        header.clientIP = dhcpIP;
        header.yourClientIP = Variable::IPv4::source;
        header.transID = generateDhcpTransid();

        PacketInfo requestPacket = dhcpRequest(
            dhcpBody(hardwareAddress),
            header,
            hostname,
            hardwareAddress,
            dhcpIP,
            configs.dhcpServer
        );
        
        lock.unlock();
        sendDhcpRequest(requestPacket);
        leaseStart = currentTime;
    }
}

// Send DHCP release
void Protocol::DhcpClient::sendDhcpRelease()
{
    std::lock_guard<std::mutex> lock(dhcpMutex);
    if (!acked)
    {
        // No active lease to release
        return;
    }
    
    if (!currentInterface->shutdownFlag.load(std::memory_order_relaxed))
    {
        ByteString mac = currentInterface->configs.getMac();
        PacketInfo releasePacket = dhcpRelease(dhcpBody(mac), mac);

        currentInterface->enqueuePacket(releasePacket);

        resetDhcpState();
    }
}

// Resets the DHCP client state upon receiving a NAC or DECLINE
void Protocol::DhcpClient::resetDhcpState()
{
    acked = false;
    naked = false;
    leaseStart = 0;
}

// Creates a DHCP packet body with Ethernet, IP, and UDP headers
PacketInfo Protocol::DhcpClient::dhcpBody(ByteString& hardwareAddress)
{
    PacketInfo dhcpPacket;

    EthernetHeader eth;
    eth.destinationMac = Variable::Mac::broadcast; 
    eth.sourceMac = hardwareAddress; 
    eth.type = Variable::Ethernet::ipv4;
    dhcpPacket.Layer2.push_back(std::move(eth));

    IPv4Header ip;
    ip.version = "4";
    ip.headerLength = "5";
    ip.serviceField = std::string("\x00", 1);
    ip.totalLength = std::string("\x00\x00", 2); 
    ip.identification = std::string("\x00\x00", 2); 
    ip.fragmentFlag.reserved = "0"; 
    ip.fragmentFlag.fragment = "0";
    ip.fragmentFlag.moreFragment = "0";
    ip.fragmentFlag.fragment = "0000000000000";
    ip.TTL = std::string("\x40", 1);
    ip.protocol = Variable::IP::udp; 
    ip.checksum = std::string("\x00\x00", 2);
    ip.sourceAddress = Variable::IPv4::source;
    ip.destinationAddress = Variable::IPv4::broadcast;
    dhcpPacket.Layer3.push_back(std::move(ip));

    UdpHeader udp;
    udp.sourcePort = Variable::Udp::dhcpClient;
    udp.destinationPort = Variable::Udp::dhcpServer;
    udp.length = std::string("\x01\x00", 2);
    udp.checksum = std::string("\x00\x00", 2); 
    dhcpPacket.Layer4.push_back(std::move(udp)); 

    return dhcpPacket; 
}

// Creates a DHCP discover packet
PacketInfo Protocol::DhcpClient::dhcpDiscover(PacketInfo packet, const std::string& hostname, ByteString& hardwareAddress) 
{
    DhcpHeader dhcp;

    dhcp.boot = Variable::Dhcp::Type::discover; 
    dhcp.hardwareType = std::string("\x01", 1);
    dhcp.hardwareAddressLength = std::string("\x06", 1);
    dhcp.hops = std::string("\x00", 1);
    dhcp.transID = generateDhcpTransid(); 
    dhcp.secondsElapsed = std::string("\x00\x00", 2);
    dhcp.bootpFlags.broadcast = "0";
    dhcp.bootpFlags.reserved = "000000000000000";
    dhcp.clientIP = Variable::IPv4::source; 
    dhcp.yourClientIP = Variable::IPv4::source;
    dhcp.nextServerIP = Variable::IPv4::source;
    dhcp.relayAgentIP = Variable::IPv4::source;
    dhcp.clientMacAddress = hardwareAddress;
    dhcp.clientHardwareAddressPadding = Variable::Dhcp::clientHardwareAddressPadding;
    dhcp.serverHostName = Variable::Dhcp::serverHostName;
    dhcp.bootFile = Variable::Dhcp::bootfile;
    dhcp.magicCookie = Variable::Dhcp::magicCookie; 

    dhcp.options.reserve(4);

    dhcp.options.emplace_back(
        Variable::Dhcp::Option::type,
        std::string("\x01", 1),
        Variable::Dhcp::Type::discover
    );

    dhcp.options.emplace_back(
        Variable::Dhcp::Option::clientID,
        Functions::numToByte(hardwareAddress.size(), 1),
        hardwareAddress
    );

    dhcp.options.emplace_back(
        Variable::Dhcp::Option::maxSize,
        std::string("\x02", 1),
        std::string("\x02\x04", 2)
    );

    dhcp.options.emplace_back(
        Variable::Dhcp::Option::hostname,
        Functions::numToByte(static_cast<uint32_t>(hostname.length())),
        hostname
    );

    dhcp.end = Variable::Dhcp::end; 

    packet.Layer5.push_back(std::move(dhcp));

    return packet; 
}

PacketInfo Protocol::DhcpClient::dhcpRequest(PacketInfo packet, DhcpHeader& header, const std::string& hostname, ByteString hardwareAddress, ByteString requestedIP, ByteString serverID) 
{
    DhcpHeader dhcp;

    dhcp.boot = Variable::Dhcp::Type::discover; 
    dhcp.hardwareType = std::string("\x01", 1); 
    dhcp.hardwareAddressLength = std::string("\x06", 1); 
    dhcp.hops = std::string("\x00", 1); 
    dhcp.transID = header.transID; 
    dhcp.secondsElapsed = std::string("\x00\x00", 2); 
    dhcp.bootpFlags.broadcast = "0"; 
    dhcp.bootpFlags.reserved = "000000000000000"; 
    dhcp.clientIP = Variable::IPv4::source; 
    dhcp.relayAgentIP = Variable::IPv4::source; 
    dhcp.clientMacAddress = hardwareAddress; 
    dhcp.clientHardwareAddressPadding = Variable::Dhcp::clientHardwareAddressPadding; 
    dhcp.serverHostName = Variable::Dhcp::serverHostName; 
    dhcp.bootFile = Variable::Dhcp::bootfile; 
    dhcp.magicCookie = Variable::Dhcp::magicCookie; 

    dhcp.options.reserve(7); 

    dhcp.options.emplace_back(
        Variable::Dhcp::Option::type,
        std::string("\x01", 1),
        Variable::Dhcp::Type::request
    );

    dhcp.options.emplace_back(
        Variable::Dhcp::Option::clientID,
        std::string(hardwareAddress.size(), 1),
        hardwareAddress
    );

    dhcp.options.emplace_back(
        Variable::Dhcp::Option::serverIdentifier,
        std::string(configs.dhcpServer.size(), 1),
        configs.dhcpServer
    );

    dhcp.options.emplace_back(
        Variable::Dhcp::Option::requestIP,
        std::string(header.yourClientIP.size(), 1),
        header.yourClientIP
    );

    dhcp.options.emplace_back(
        Variable::Dhcp::Option::leaseTime,
        Functions::numToByte(static_cast<uint32_t>(configs.leaseTime.size())),
        configs.leaseTime
    );

    dhcp.options.emplace_back(
        Variable::Dhcp::Option::hostname,
        Functions::numToByte(static_cast<uint32_t>(hostname.length())),
        hostname
    );

    dhcp.options.emplace_back(
        Variable::Dhcp::Option::requestList,
        std::string("\x0d", 1),
        Variable::Dhcp::Option::mask +
        Variable::Dhcp::Option::broadcast + 
        Variable::Dhcp::Option::timeOffset + 
        Variable::Dhcp::Option::router + 
        Variable::Dhcp::Option::domainName + 
        Variable::Dhcp::Option::domainServer +
        Variable::Dhcp::Option::domainSearch +
        Variable::Dhcp::Option::hostname +
        Variable::Dhcp::Option::netbiosNameServer +
        Variable::Dhcp::Option::mtu +
        Variable::Dhcp::Option::classlessStateRoute + 
        Variable::Dhcp::Option::ntp
    );

    dhcp.end = Variable::Dhcp::end; 

    packet.Layer5.push_back(std::move(dhcp));

    return packet; 
}

// Creates a DHCP Release packet to release the leased IP address
PacketInfo Protocol::DhcpClient::dhcpRelease(PacketInfo packet, const ByteString& hardwareAddress)
{
    DhcpHeader dhcp;

    if (currentInterface->shutdownFlag.load(std::memory_order_relaxed)) return packet;

    dhcp.boot = Variable::Dhcp::Type::release;
    dhcp.hardwareType = std::string("\x01", 1);
    dhcp.hardwareAddressLength = std::string("\x06", 1);
    dhcp.hops = std::string("\x00", 1);
    dhcp.transID = generateDhcpTransid();
    dhcp.secondsElapsed = std::string("\x00\x00", 2);
    dhcp.bootpFlags.broadcast = "0";
    dhcp.bootpFlags.reserved = "000000000000000"; 
    dhcp.clientIP = std::string("\x00\x00\x00\x00", 4);
    dhcp.yourClientIP = currentInterface->configs.ipv4.getAddress();
    dhcp.nextServerIP = std::string("\x00\x00\x00\x00", 4);
    dhcp.relayAgentIP = Variable::IPv4::source; 
    dhcp.clientMacAddress = hardwareAddress; 
    dhcp.clientHardwareAddressPadding = Variable::Dhcp::clientHardwareAddressPadding; 
    dhcp.serverHostName = Variable::Dhcp::serverHostName; 
    dhcp.bootFile = Variable::Dhcp::bootfile; 
    dhcp.magicCookie = Variable::Dhcp::magicCookie; 
    
    dhcp.options.reserve(2);

    dhcp.options.emplace_back(
        Variable::Dhcp::Option::type,
        std::string("\x01", 1),
        Variable::Dhcp::Type::release
    );

    dhcp.options.emplace_back(
        Variable::Dhcp::Option::clientID,
        std::string(hardwareAddress.size(), 1),
        hardwareAddress
    );

    dhcp.end = Variable::Dhcp::end;

    packet.Layer5.push_back(std::move(dhcp));

    return packet;
}

// Creates a DHCP Inform packet to request local configuration parameters
PacketInfo Protocol::DhcpClient::dhcpInform(PacketInfo packet, std::string& hostname, ByteString& hardwareAddress) 
{
    DhcpHeader dhcp;

    ByteString ipAddr;
    if (!currentInterface->shutdownFlag.load(std::memory_order_relaxed))
    {
        ipAddr = currentInterface->configs.ipv4.getAddress();
    }
    else
    {
        return packet;
    }

    dhcp.boot = Variable::Dhcp::Type::inform; 
    dhcp.hardwareType = std::string("\x01", 1);
    dhcp.hardwareAddressLength = std::string("\x06", 1);
    dhcp.hops = std::string("\x00", 1);
    dhcp.transID = generateDhcpTransid(); 
    dhcp.secondsElapsed = std::string("\x00\x00", 2);
    dhcp.bootpFlags.broadcast = "0";
    dhcp.bootpFlags.reserved = std::string("000000000000000", 15);
    dhcp.clientIP = ipAddr;
    dhcp.yourClientIP = Variable::IPv4::source;
    dhcp.nextServerIP = Variable::IPv4::source;
    dhcp.relayAgentIP = Variable::IPv4::source;
    dhcp.clientMacAddress = hardwareAddress;
    dhcp.clientHardwareAddressPadding = Variable::Dhcp::clientHardwareAddressPadding;
    dhcp.serverHostName = Variable::Dhcp::serverHostName;
    dhcp.bootFile = Variable::Dhcp::bootfile;
    dhcp.magicCookie = Variable::Dhcp::magicCookie; 

    dhcp.options.reserve(3);

    dhcp.options.emplace_back(
        Variable::Dhcp::Option::type,
        std::string("\x01", 1),
        Variable::Dhcp::Type::inform
    );

    dhcp.options.emplace_back(
        Variable::Dhcp::Option::clientID,
        std::string(hardwareAddress.size(), 1),
        hardwareAddress
    );

    dhcp.options.emplace_back(
        Variable::Dhcp::Option::hostname,
        Functions::numToByte(static_cast<uint32_t>(hostname.length())),
        hostname
    );

    dhcp.end = Variable::Dhcp::end; 

    packet.Layer5.push_back(std::move(dhcp));

    return packet; 
}

// Processes received DHCP responses including ACK, NAK, DECLINE, INFORM
void Protocol::DhcpClient::processDhcpResponses(const std::string& hostname, ByteString& hardwareAddress)
{
    std::lock_guard<std::mutex> lock(dhcpMutex);

    // Handle DHCP Offer
    if (!dhcpOffer.Layer2.empty() && !dhcpOffer.Layer5.empty())
    {
        auto& header = dhcpOffer.Layer5[0];
        if (std::holds_alternative<DhcpHeader>(header))
        {
            if (DhcpHeader* dhcpPtr = &(std::get<DhcpHeader>(header)))
            {
                ExtractOptions(dhcpPtr->options);
                offered = true;
                cv.notify_all();
            } 
        }
    }

    // Handle DHCP ACK
    if (!dhcpAck.Layer2.empty() && !dhcpAck.Layer5.empty())
    {
        auto& header = dhcpAck.Layer5[0];
        if (std::holds_alternative<DhcpHeader>(header))
        {
            if (DhcpHeader* dhcpPtr = &(std::get<DhcpHeader>(header)))
            {
                ExtractOptions(dhcpPtr->options);
                currentInterface->setIPv4(dhcpPtr->yourClientIP, configs.subnetMask);
                leaseStart = secondsSinceEpoch();
                acked = true;
                cv.notify_all();
            }
        }
    }

    // Handle DHCP NAK
    if (!dhcpNak.Layer2.empty() && !dhcpNak.Layer5.empty())
    {
        auto& header = dhcpNak.Layer5[0];
        if (std::holds_alternative<DhcpHeader>(header))
        {
            if (DhcpHeader* dhcpPtr = &(std::get<DhcpHeader>(header)))
            {
                // Reset DHCP state and notify handler to restart discovery
                resetDhcpState();
                cv.notify_all();
            }
        }
    }

    // Handle DHCP DECLINE
    if (!dhcpDecline.Layer2.empty() && !dhcpDecline.Layer5.empty())
    {
        auto& header = dhcpDecline.Layer5[0];
        if (std::holds_alternative<DhcpHeader>(header))
        {
            if (DhcpHeader* dhcpPtr = &(std::get<DhcpHeader>(header)))
            {
                // Handle Decline by resetting state to and notifying neighbors
                resetDhcpState();
                cv.notify_all();
            }
        }
    }

    // Handle dhcp inform
    if (!dhcpInformPacket.Layer2.empty() && !dhcpInformPacket.Layer5.empty())
    {
        auto& header = dhcpInformPacket.Layer5[0];
        if (std::holds_alternative<DhcpHeader>(header))
        {
            if (DhcpHeader* dhcpPtr = &(std::get<DhcpHeader>(header)))
            {
                ExtractOptions(dhcpPtr->options);
                dhcpInformPacket = PacketInfo();
            }
        }
    }
}

// Method to extract DHCP options from a response
void Protocol::DhcpClient::ExtractOptions(std::vector<DhcpHeader::Option> options) 
{
    for (auto opt : options) 
    {
        if (opt.option == Variable::Dhcp::Option::serverIdentifier) 
        {
            configs.dhcpServer = opt.value;
        } 
        else if (opt.option == Variable::Dhcp::Option::leaseTime) 
        {
            configs.leaseTime = opt.value;
        } 
        else if (opt.option == Variable::Dhcp::Option::renewalTime) 
        {
            configs.renewalTime = opt.value;
        } 
        else if (opt.option == Variable::Dhcp::Option::rebindingTime) 
        {
            configs.rebindingTime = opt.value;
        } 
        else if (opt.option == Variable::Dhcp::Option::mask) 
        { 
            configs.subnetMask = Functions::byteMaskToNum(opt.value);
        } 
        else if (opt.option == Variable::Dhcp::Option::broadcast) 
        {
            configs.broadcast = opt.value;
        } 
        else if (opt.option == Variable::Dhcp::Option::domainServer && opt.value.size() % 4 == 0)
        {
            uint64_t it = 0;
            while (it <= opt.value.size())
            {
                configs.dnsServer.push_back(opt.value.substr(it, 4));
                it += 4;
            }
        } 
        else if (opt.option == Variable::Dhcp::Option::router) 
        {
            configs.router = opt.value;
        }
        else if (opt.value == Variable::Dhcp::Type::nak)
        {
            naked = true;
            cv.notify_all();
        }
    }
}

// Placeholder for a method to handle DHCP packets
void Protocol::DhcpClient::DhcpPacket(const DhcpHeader* header, ByteString& type)
{
    //TODO Implementation needed
}
