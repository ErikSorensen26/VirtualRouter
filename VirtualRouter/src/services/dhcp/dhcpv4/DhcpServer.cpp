#include "DhcpServer.h"
#include <Interface.h>
#include <Functions.h>
#include <random>
#include <chrono>
#include <thread>
#include <LeaseManager.h>

Protocol::DhcpServer::DhcpServer()
{
    stopFlag.store(false);
}

Protocol::DhcpServer::~DhcpServer()
{
    stopServer();
}

void Protocol::DhcpServer::startServer()
{
    stopFlag.store(false);
    serverThread = std::thread(&DhcpServer::dhcpHandler, this);
}

void Protocol::DhcpServer::stopServer()
{
    stopFlag.store(true);

    clearOfferTimeouts();

    {
        std::lock_guard<std::mutex> lock(configMutex);
        // Remove all Networks
        for (auto& [_, config] : dhcpNetworks)
        {
            if (config)
            {
                delete config;
            }
        }
        dhcpNetworks.clear();
    }
}

void Protocol::DhcpServer::handleDhcpPacket(const PacketInfo& packet)
{
    if (packet.Layer5.empty())
        return;
    const DhcpHeader* dhcpHeader = nullptr;
    if (std::holds_alternative<DhcpHeader>(packet.Layer5[0]))
    {
        dhcpHeader = &std::get<DhcpHeader>(packet.Layer5[0]);
    }
    if (!dhcpHeader)
        return; // Invalid DHCP packet

    ByteString messageType = getOption(dhcpHeader->options, Variable::Dhcp::Option::type);
    if (messageType.empty())
    {
        return; // No message found
    }

    if (messageType == Variable::Dhcp::Type::discover)
        processDiscover(*dhcpHeader);
    else if (messageType == Variable::Dhcp::Type::request)
        processRequest(*dhcpHeader);
    else if (messageType == Variable::Dhcp::Type::release)
        processRelease(*dhcpHeader);
    else if (messageType == Variable::Dhcp::Type::inform)
        processInform(*dhcpHeader);
    else if (messageType == Variable::Dhcp::Type::decline)
        processDecline(*dhcpHeader);
}

void Protocol::DhcpServer::dhcpHandler()
{
    while (!stopFlag.load())
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        // Cleanup expired leases through LeaseManager for each network.
        for (auto& [net, config] : dhcpNetworks)
        {
            config->lease->cleanupExpiredLeases();
        }
    }
}

void Protocol::DhcpServer::processDiscover(const DhcpHeader& dhcpHeader)
{
    ByteString matchingNetwork = findMatchingNetwork(dhcpHeader);
    if (matchingNetwork.empty() || dhcpNetworks.find(matchingNetwork) == dhcpNetworks.end())
        return; // No matching network

    DhcpNetwork* config = dhcpNetworks[matchingNetwork];
    auto& lm = dhcpNetworks[matchingNetwork]->pool;
    ByteString allocatedIP = lm->allocateTempIP(dhcpHeader.clientMacAddress);
    if (!allocatedIP.empty())
    {
        PacketInfo offerPacket = buildDhcpOffer(dhcpHeader, config->config, allocatedIP);
        sendPacket(offerPacket, config->config->interface);
        scheduleTimeout(Dhcp::TimerType::IP_OFFER_TIMEOUT, dhcpHeader.clientMacAddress, allocatedIP, matchingNetwork, 60.0);
    }
    else
    {
        sendNak(dhcpHeader, config->config->interface);
    }
}

void Protocol::DhcpServer::processRequest(const DhcpHeader& dhcpHeader)
{
    // Ensure that the requested IP option is present.
    ByteString requestedIP = getOption(dhcpHeader.options, Variable::Dhcp::Option::requestIP);
    if (requestedIP.empty())
        return;

    // Determine the network this reqiest belongs to.
    ByteString matchingNetwork = findMatchingNetwork(dhcpHeader);
    auto it = dhcpNetworks.find(matchingNetwork);
    if (it == dhcpNetworks.end()) return;
    DhcpNetwork* config = it->second;
    if (!requestedIP.empty() && !matchingNetwork.empty() && config->pool->isTemporarilyOffered(requestedIP))
    {
        config->lease->activateLeaseFromTemp(dhcpHeader.clientMacAddress, config->config->leaseTime, config->config->t1Percentage, config->config->t2Percentage);
        cancelTimeout(Dhcp::TimerType::IP_OFFER_TIMEOUT, dhcpHeader.clientMacAddress, requestedIP);
    }
    else if (!requestedIP.empty() && !matchingNetwork.empty() && config->pool->isAllocated(requestedIP))
    {
        config->lease->renewLease(requestedIP);
    }
    else
    {
        sendNak(dhcpHeader, config->config->interface);
        return;
    }

    // Build and send the DHCPACK packet.
    PacketInfo ackPacket = buildDhcpAck(dhcpHeader, config->config, requestedIP);
    sendPacket(ackPacket, config->config->interface);

    // Reset the lease time
    config->lease->renewLease(requestedIP);
}

void Protocol::DhcpServer::processRelease(const DhcpHeader& dhcpHeader)
{
    ByteString releaseIP = dhcpHeader.clientIP;
    ByteString matchingNetwork = findMatchingNetwork(dhcpHeader);
    if (!matchingNetwork.empty() && dhcpNetworks.find(matchingNetwork) != dhcpNetworks.end())
        dhcpNetworks[matchingNetwork]->lease->releaseIP(releaseIP);
}

void Protocol::DhcpServer::processDecline(const DhcpHeader& dhcpHeader)
{
    ByteString declinedIP = getOption(dhcpHeader.options, Variable::Dhcp::Option::requestIP);
    if (declinedIP.empty()) return; // No valid request IP option

    ByteString matchingNetwork = findMatchingNetwork(dhcpHeader);
    dhcpNetworks[matchingNetwork]->pool->setConflicted(declinedIP);
    scheduleTimeout(Dhcp::TimerType::DECLINE_HOLD, dhcpHeader.clientMacAddress, declinedIP, matchingNetwork, 20);
}

void Protocol::DhcpServer::processInform(const DhcpHeader& dhcpHeader)
{
    ByteString matchingNetwork = findMatchingNetwork(dhcpHeader);
    if (matchingNetwork.empty())
        return; // No matching network found
    std::vector<ByteString> requestedOptions = getRequestedOptions(dhcpHeader.options);
    PacketInfo informAck = buildDhcpAckForInform(dhcpHeader, dhcpNetworks[matchingNetwork]->config, requestedOptions);
    sendPacket(informAck, dhcpNetworks[matchingNetwork]->config->interface);
}

std::vector<ByteString> Protocol::DhcpServer::getRequestedOptions(const std::vector<DhcpHeader::Option>& options)
{
    std::vector<ByteString> requestedOptions;

    // Iterate through the list of options to find option 55 (Parameter Request List)
    for (const auto& option : options)
    {
        if (option.option == Variable::Dhcp::Option::requestList)
        {
            // Option 55 found; split its value into individual requested options
            for (const auto& byte : option.value)
            {
                requestedOptions.emplace_back(ByteString(1, byte));
            }
            break; // No need to continue after finding the required list
        }
    }

    return requestedOptions;
}

ByteString Protocol::DhcpServer::findMatchingNetwork(const DhcpHeader& dhcpHeader)
{
    auto findMatchingNetworkAgainstIP([&](const ByteString& ip) {
        for (const auto& [network, config] : dhcpNetworks)
        {
            if (Functions::compareNetworkWithIp(config->config->network, ip, config->config->subnetPrefix))
            {
                return network;
            }
        }

        return ByteString();
    });

    // Match based on the relay agent IP (if present)
    ByteString relayMatch = findMatchingNetworkAgainstIP(dhcpHeader.relayAgentIP);
    if (!relayMatch.empty())
    {
        return relayMatch;
    }

    // Fall back to client IP matching
    ByteString clientMatch = findMatchingNetworkAgainstIP(dhcpHeader.clientIP);
    if (!clientMatch.empty())
    {
        return clientMatch;
    }

    return {}; // No match found
}

PacketInfo Protocol::DhcpServer::dhcpBody(const ByteString& sourceIP, const ByteString& destinationIP, const ByteString& sourceMac)
{
    PacketInfo dhcpPacket;

    EthernetHeader eth;
    eth.destinationMac = Variable::Mac::broadcast; 
    eth.sourceMac = sourceMac; 
    eth.type = Variable::Ethernet::ipv4;
    dhcpPacket.Layer2.emplace_back(std::move(eth));

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
    ip.sourceAddress = sourceIP;
    ip.destinationAddress = destinationIP;
    dhcpPacket.Layer3.emplace_back(std::move(ip));

    UdpHeader udp;
    udp.sourcePort = Variable::Udp::dhcpClient;
    udp.destinationPort = Variable::Udp::dhcpServer;
    udp.length = std::string("\x01\x00", 2);
    udp.checksum = std::string("\x00\x00", 2); 
    dhcpPacket.Layer4.emplace_back(std::move(udp)); 

    return dhcpPacket; 
}

DhcpHeader Protocol::DhcpServer::buildDhcpHeader(const ByteString& messageType, const ByteString& clientIP, const ByteString& relayAgentIP, const ByteString& transID)
{
    DhcpHeader dhcp;

    dhcp.boot = messageType;
    dhcp.hardwareType = std::string("\x01", 1);
    dhcp.hardwareAddressLength = std::string("\x06", 1);
    dhcp.hops = std::string("\x00", 1);
    dhcp.transID = transID;
    dhcp.secondsElapsed = std::string("\x00\x00", 2);
    dhcp.bootpFlags.broadcast = "0";
    dhcp.bootpFlags.reserved = std::string("000000000000000", 15);
    dhcp.clientIP = clientIP; 
    dhcp.yourClientIP = clientIP;
    dhcp.nextServerIP = Variable::IPv4::source;
    dhcp.relayAgentIP = relayAgentIP;
    dhcp.clientMacAddress = std::string("\x00\x00\x00\x00\x00\x00", 6);
    dhcp.clientHardwareAddressPadding = Variable::Dhcp::clientHardwareAddressPadding;
    dhcp.serverHostName = Variable::Dhcp::serverHostName;
    dhcp.bootFile = Variable::Dhcp::bootfile;
    dhcp.magicCookie = Variable::Dhcp::magicCookie;

    // Options
    dhcp.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::type,
        std::string("\x01", 1),
        messageType
    });

    dhcp.end = Variable::Dhcp::end;
    return dhcp;
}

PacketInfo Protocol::DhcpServer::buildDhcpOffer(const DhcpHeader& dhcpHeader, const DhcpNetworkConfig* config, const ByteString& ipAddress)
{
    PacketInfo packet = dhcpBody(Variable::IPv4::source, Variable::IPv4::broadcast, Variable::Mac::source);
    DhcpHeader offerHeader = buildDhcpHeader(Variable::Dhcp::Type::offer, ipAddress, dhcpHeader.relayAgentIP, dhcpHeader.transID);
    offerHeader.boot = Variable::Dhcp::Type::offer;

    // Add DHCP Options
    offerHeader.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::leaseTime,
        std::string("\x04", 1),
        Functions::numToByte(static_cast<uint32_t>(config->leaseTime), 4)
    });
    offerHeader.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::mask, 
        std::string("\x04", 1),
        Functions::binToByte(Functions::numMaskToBin(config->subnetPrefix))
    });
    offerHeader.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::router, 
        std::string("\x04", 1),
        config->defaultGateway
    });
    for (const auto& dns : config->dnsServer) {
        offerHeader.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::domainServer,
            std::string("\x04", 1),
            dns
        });
    }

    packet.Layer5.emplace_back(std::move(offerHeader));
    return packet;
}

PacketInfo Protocol::DhcpServer::buildDhcpAck(const DhcpHeader& dhcpHeader, const DhcpNetworkConfig* config, const ByteString& ipAddress)
{
    PacketInfo packet = dhcpBody(Variable::IPv4::source, Variable::IPv4::broadcast, Variable::Mac::source);
    DhcpHeader ackHeader = buildDhcpHeader(Variable::Dhcp::Type::ack, ipAddress, dhcpHeader.relayAgentIP, dhcpHeader.transID);
    ackHeader.boot = Variable::Dhcp::Type::ack;

    // Add DHCP Options
    ackHeader.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::leaseTime,
        std::string("\x04", 1),
        Functions::numToByte(static_cast<uint32_t>(config->leaseTime), 4)
    });
    ackHeader.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::mask, 
        std::string("\x04", 1),
        Functions::binToByte(Functions::numMaskToBin(config->subnetPrefix))
    });
    ackHeader.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::router, 
        std::string("\x04", 1),
        config->defaultGateway
    });
    for (const auto& dns : config->dnsServer) {
        ackHeader.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::domainServer,
            std::string("\x04", 1),
            dns
        });
    }

    packet.Layer5.emplace_back(std::move(ackHeader));
    return packet;
}

PacketInfo Protocol::DhcpServer::buildDhcpAckForInform(const DhcpHeader& dhcpHeader, const DhcpNetworkConfig* config, const std::vector<ByteString>& requestedOptions)
{
    PacketInfo packet = dhcpBody(Variable::IPv4::source, dhcpHeader.clientIP, Variable::Mac::source);

    DhcpHeader ackHeader = buildDhcpHeader(
        Variable::Dhcp::Type::ack,
        dhcpHeader.clientIP,
        dhcpHeader.relayAgentIP,
        dhcpHeader.transID
    );

    ackHeader.boot = Variable::Dhcp::Type::ack;

    // Add the requested options
    auto options = buildRequestedOptions(requestedOptions, config);
    ackHeader.options.insert(ackHeader.options.end(), options.begin(), options.end());

    packet.Layer5.emplace_back(std::move(ackHeader));
    return packet;
}

void Protocol::DhcpServer::sendNak(const DhcpHeader& dhcpHeader, Interface* interface)
{
    PacketInfo packet = dhcpBody(Variable::IPv4::source, Variable::IPv4::broadcast, Variable::Mac::source);
    DhcpHeader nakHeader = buildDhcpHeader(Variable::Dhcp::Type::nak, ByteString(4, '\x00'), dhcpHeader.relayAgentIP, dhcpHeader.transID);
    nakHeader.boot = Variable::Dhcp::Type::nak;
    packet.Layer5.emplace_back(std::move(nakHeader));
    sendPacket(packet, interface);
}

ByteString Protocol::DhcpServer::getOption(const std::vector<DhcpHeader::Option>& options, const ByteString& optionType)
{
    for (const auto& opt : options)
    {
        if (opt.option == optionType)
        {
            return opt.value;
        }
    }
    return {};
}

void Protocol::DhcpServer::sendPacket(PacketInfo& packet, Interface* interface)
{
    if (interface)
    {
        interface->enqueuePacket(packet);
    }
}

std::vector<DhcpHeader::Option> Protocol::DhcpServer::buildRequestedOptions(const std::vector<ByteString>& requestedOptions, const DhcpNetworkConfig* config)
{
    std::vector<DhcpHeader::Option> options;

    for (const auto& opt : requestedOptions)
    {
        // Option 1: Subnet Mask
        if (opt == Variable::Dhcp::Option::mask && config->subnetPrefix != 0)
        {
            options.emplace_back(DhcpHeader::Option{
                Variable::Dhcp::Option::mask,
                ByteString("\x04", 1),
                Functions::binToByte(Functions::numMaskToBin(config->subnetPrefix))
            });
        }
        // Option 3: Router (Gateway)
        if (opt == Variable::Dhcp::Option::router && !config->defaultGateway.empty())
        {
            options.emplace_back(DhcpHeader::Option{
                Variable::Dhcp::Option::router,
                ByteString("\x04", 1),
                config->defaultGateway
            });
        }
        // Option 6: DNS Servers
        if (opt == Variable::Dhcp::Option::domainServer && !config->dnsServer.empty())
        {
            for (const auto& dns : config->dnsServer)
            {
                options.emplace_back(DhcpHeader::Option{
                    Variable::Dhcp::Option::domainServer,
                    ByteString("\x06", 1),
                    dns
                });
            }
        }
        // Option 15: Domain Name
        if (opt == Variable::Dhcp::Option::domainName && !config->domainName.empty())
        {
            options.emplace_back(DhcpHeader::Option{
                Variable::Dhcp::Option::domainName,
                ByteString(1, static_cast<unsigned char>(config->domainName.size())),
                config->domainName
            });
        }
        // Option 44: WINS Servers
        if (opt == Variable::Dhcp::Option::netbiosNameServer && !config->winsServer.empty())
        {
            for (const auto& wins : config->winsServer)
            {
                options.emplace_back(DhcpHeader::Option{
                    Variable::Dhcp::Option::netbiosNameServer,
                    ByteString("\x04", 1),
                    wins
                });
            }
        }
        // Option 66: TFTP Server Name
        if (opt == Variable::Dhcp::Option::tftpServer && !config->tftpServer.empty())
        {
            options.emplace_back(DhcpHeader::Option{
                Variable::Dhcp::Option::tftpServer,
                ByteString(1, static_cast<unsigned char>(config->tftpServer.size())),
                config->tftpServer
            });
        }
        // Option 67: Bootfile Name
        if (opt == Variable::Dhcp::Option::bootfile && !config->bootfile.empty())
        {
            options.emplace_back(DhcpHeader::Option{
                Variable::Dhcp::Option::bootfile,
                ByteString(1, static_cast<unsigned char>(config->bootfile.size())),
                config->bootfile
            });
        }
        // Option 121: Classless Static Routes
        if (opt == Variable::Dhcp::Option::classlessStateRoute && !config->staticRoutes.empty())
        {
            for (const auto& route : config->staticRoutes)
            {
                options.emplace_back(DhcpHeader::Option{
                    Variable::Dhcp::Option::classlessStateRoute,
                    ByteString("\x04", 1),
                    route
                });
            }
        }
        // Option 26: MTU
        if (opt == Variable::Dhcp::Option::mtu && config->mtu > 0)
        {
            options.emplace_back(DhcpHeader::Option{
                Variable::Dhcp::Option::mtu,
                ByteString("\x02", 1),
                Functions::numToByte(config->mtu, 2)
            });
        }
        // Option 51: IP Address Lease Time
        if (opt == Variable::Dhcp::Option::leaseTime && config->leaseTime > 0)
        {
            options.emplace_back(DhcpHeader::Option{
                Variable::Dhcp::Option::leaseTime,
                ByteString("\x04", 1),
                Functions::numToByte(static_cast<uint32_t>(config->leaseTime), 4)
            });
        }
        // Option 58:
        if (opt == Variable::Dhcp::Option::renewalTime && !config->renewalTime.empty())
        {
            options.emplace_back(DhcpHeader::Option{
                Variable::Dhcp::Option::renewalTime,
                ByteString("\x04", 1),
                config->renewalTime
            });
        }
        // Option 59: Rebinding Time (T2)
        if (opt == Variable::Dhcp::Option::rebindingTime && !config->rebindingTime.empty())
        {
            options.emplace_back(DhcpHeader::Option{
                Variable::Dhcp::Option::rebindingTime,
                ByteString("\x04", 1),
                config->rebindingTime
            });
        }
        // Option 42: NTP Servers
        if (opt == Variable::Dhcp::Option::ntp && !config->ntpServer.empty())
        {
            options.emplace_back(DhcpHeader::Option{
                Variable::Dhcp::Option::ntp,
                ByteString("\x04", 1),
                config->ntpServer
            });
        }
    }

    // End Option (Option 255)
    options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::end,
        ByteString(),
        ByteString()
    });

    return options;
}

ByteString Protocol::DhcpServer::generateTransactionID()
{
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<uint32_t> dist(0, UINT32_MAX);
    return Functions::numToByte(dist(rng), 2);
}

bool Protocol::DhcpServer::validateMandatoryOptions(const std::vector<DhcpHeader::Option>& options)
{
    bool hasServerId = false;
    bool hasRequestedIp = false;

    for (const auto& option : options)
    {
        if (option.option == Variable::Dhcp::Option::serverIdentifier)
            hasServerId = true;
        else if (option.option == Variable::Dhcp::Option::requestIP)
            hasRequestedIp = true;
    }

    return hasServerId && hasRequestedIp;
}

void Protocol::DhcpServer::handleBootpRequest(const PacketInfo& packet)
{
    // Implementation of BOOTP backward compatibility
    if (packet.Layer5.empty()) return;

    const DhcpHeader* dhcpHeader = nullptr;
    if (std::holds_alternative<DhcpHeader>(packet.Layer5[0]))
    {
        dhcpHeader = &std::get<DhcpHeader>(packet.Layer5[0]);
    }
    if (!dhcpHeader) return;

    // Handle as a DHCPREQUEST equivalent
    processRequest(*dhcpHeader);
}

bool Protocol::DhcpServer::authenticateMessage(const DhcpHeader& dhcpHeader)
{
    ByteString authOption = getOption(dhcpHeader.options, Variable::Dhcp::Option::authentication);
    if (authOption.empty()) return false;

    // Get current timestamp for replay protection
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    // Check replay cache
    auto it = replayCache.find(dhcpHeader.clientMacAddress);
    if (it != replayCache.end() && timestamp <= it->second)
    {
        return false; // Possible replay attack
    }

    // Update replay cache
    replayCache[dhcpHeader.clientMacAddress] = timestamp;

    // TODO: Implement actual authentication logic using authenticationSecret
    return true;
}

void Protocol::DhcpServer::processForceRenew(const DhcpHeader& dhcpHeader)
{
    // Only process if authentication is successful (RFC 3203 requirement)
    if (!authenticateMessage(dhcpHeader))
        return;

    ByteString matchingNetwork = findMatchingNetwork(dhcpHeader);
    if (matchingNetwork.empty() || dhcpNetworks.find(matchingNetwork) == dhcpNetworks.end())
        return;

    DhcpNetwork* config = dhcpNetworks[matchingNetwork];

    ByteString ipAddress;
    {
        std::shared_lock<std::shared_mutex> lock(config->config->interface->configs.ipMutex);
        ipAddress = config->config->interface->configs.ipv4.ipAddress;
    }
    
    // Build and send FORCERENEW message
    PacketInfo forceRenewPacket = dhcpBody(
        ipAddress,
        dhcpHeader.clientIP,
        config->config->interface->configs.macAddress
    );

    DhcpHeader header = buildDhcpHeader(
        Variable::Dhcp::Type::forceRenew,
        dhcpHeader.clientIP,
        ByteString(),
        generateTransactionID()
    );

    forceRenewPacket.Layer5.emplace_back(header);
    sendPacket(forceRenewPacket, config->config->interface);
}
