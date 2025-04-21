#include "DhcpServer.h"
#include <Interface.h>
#include <Functions.h>
#include <random>
#include <chrono>
#include <thread>
#include <LeaseManager.h>

Protocol::DhcpServer::DhcpServer() {}

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

void Protocol::DhcpServer::handleDhcpPacket(const PacketInfo& packet, Interface* iface)
{
    if (packet.Layer5.empty()) return;
    
    const DhcpHeader* dhcpHeader = nullptr;
    if (std::holds_alternative<DhcpHeader>(packet.Layer5[0]))
    {
        dhcpHeader = &std::get<DhcpHeader>(packet.Layer5[0]);
    }
    if (!dhcpHeader) return; // Invalid DHCP packet

    ByteString messageType = getOption(dhcpHeader->options, Variable::Dhcp::Option::type);
    if (messageType.empty()) return; // No message found

    // Snooping
    if (!isTrustedInterface(iface))
    {
        bool allowed = false;
        {
            std::shared_lock<std::shared_mutex> lock(globalConfig.configMutex);
            if (!globalConfig.snooping.vlans.empty())
            {
                uint16_t ifaceVlan = iface->configs.vlan.load(std::memory_order_relaxed);
                for (const auto& [vlan, size] : globalConfig.snooping.vlans)
                {
                    if (ifaceVlan >= vlan && ifaceVlan <= vlan + *size.rbegin())
                    {
                        allowed = true;
                    }
                }
            }
        }

        if (allowed)
        {
            if (messageType == Variable::Dhcp::Type::offer ||
                messageType == Variable::Dhcp::Type::ack ||
                messageType == Variable::Dhcp::Type::nak)
            {
                if (!globalConfig.snooping.allowUntrusted.load(std::memory_order_relaxed))
                    return;
            }

            if (globalConfig.snooping.verifyMac.load(std::memory_order_relaxed))
            {
                ByteString sourceMac;
                if (packet.Layer2.empty() && std::holds_alternative<EthernetHeader>(packet.Layer2[0]))
                {
                    sourceMac = std::get<EthernetHeader>(packet.Layer2[0]).sourceMac;
                }

                if (!sourceMac.empty() && sourceMac != dhcpHeader->clientMacAddress)
                    return; // MAC mismatch
            }

            if (globalConfig.snooping.verifyGiaddr.load(std::memory_order_relaxed))
            {
                if (!dhcpHeader->relayAgentIP.empty() &&
                    dhcpHeader->relayAgentIP != iface->configs.getIPv4())
                {
                    return; // Giaddr mismatch
                }
            }
        }
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
    int cleanupTick = 0;
    int forceRenewTick = 0;

    while (!stopFlag.load())
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        ++cleanupTick;
        ++forceRenewTick;

        // Cleanup expired leases through LeaseManager for each network.
        if (cleanupTick >= globalConfig.bindingCleanup.load(std::memory_order_release))
        {
            std::lock_guard<std::mutex> lock(configMutex);
            for (auto& [net, config] : dhcpNetworks)
            {
                config->lease->cleanupExpiredLeases();
            }
            cleanupTick = 0;
            {
                std::lock_guard<std::mutex> lock(snoopingMutex);
                auto now = std::chrono::steady_clock::now();
                for (auto it = snoopingTable.begin(); it != snoopingTable.end();)
                {
                    if (now > it->second.expiration)
                        it = snoopingTable.erase(it);
                    else
                        ++it;
                }
            }
        }

        if (globalConfig.forceRenewInterval > 0 && forceRenewTick >= globalConfig.forceRenewInterval)
        {
            for (auto& [_, config] : dhcpNetworks)
            {
                ByteString clientIP;
                {
                    clientIP = config->config->interface->configs.getIPv4();
                }

                PacketInfo forceRenewPacket = dhcpBody(
                    clientIP,
                    Variable::IPv4::broadcast,
                    config->config->interface->configs.macAddress
                );

                DhcpHeader header = buildDhcpHeader(
                    Variable::Dhcp::Type::forceRenew,
                    clientIP,
                    {},
                    generateDhcpTransid()
                );

                forceRenewPacket.Layer5.push_back(std::move(header));
                sendPacket(forceRenewPacket, config->config->interface);
            }

            forceRenewTick = 0;
        }
    }
}

void Protocol::DhcpServer::processDiscover(const DhcpHeader& dhcpHeader)
{
    ByteString matchingNetwork = findMatchingNetwork(dhcpHeader);
    if (matchingNetwork.empty() || dhcpNetworks.find(matchingNetwork) == dhcpNetworks.end())
        return; // No matching network

    Dhcp::DhcpNetwork* config = dhcpNetworks[matchingNetwork];
    auto& lm = dhcpNetworks[matchingNetwork]->pool;
    ByteString allocatedIP = allocateWithValidation(matchingNetwork, lm, dhcpHeader);
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
    Dhcp::DhcpNetwork* config = it->second;
    if (!requestedIP.empty() && !matchingNetwork.empty() && config->pool->isTemporarilyOffered(requestedIP))
    {
        config->lease->activateLeaseFromTemp(requestedIP, config->config->leaseTime, config->config->t1Percentage, config->config->t2Percentage, &dhcpHeader.clientMacAddress);
        cancelTimeout(Dhcp::TimerType::IP_OFFER_TIMEOUT, dhcpHeader.clientMacAddress, requestedIP);
        if (globalConfig.logAsciiClientID.load(std::memory_order_release))
        {
            ByteString clientID = getOption(dhcpHeader.options, Variable::Dhcp::Option::clientID);
            //TODO log client id
        }
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

    if (!globalConfig.snooping.allowUntrusted.load(std::memory_order_relaxed))
    {
        addSnoopingEntry(dhcpHeader, requestedIP, config->config->interface, config->config->leaseTime);
    }

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
    scheduleTimeout(Dhcp::TimerType::DECLINE_HOLD, dhcpHeader.clientMacAddress, declinedIP, matchingNetwork, globalConfig.declineQuarintine.load(std::memory_order_relaxed));
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

ByteString Protocol::DhcpServer::allocateWithValidation(const ByteString& networkID, IPPool* pool, const DhcpHeader& dhcpHeader)
{
    std::shared_lock lock(globalConfig.configMutex);

    if (globalConfig.limitLeases)
    {
        auto config = dhcpNetworks[networkID];
        if (config->lease->getActiveLeases().size() >= globalConfig.leasesPerInterface.load(std::memory_order_relaxed))
            return {};
    }

    for (int retry = 0; retry < globalConfig.conflictRetry.load(std::memory_order_relaxed); ++retry)
    {
        ByteString ip = pool->allocateIP(&dhcpHeader.clientMacAddress);
        if (ip.empty()) return {};

        if (globalConfig.pingTimeout > 0)
        {
            bool pingSuccessful = false;
            for (int attempt = 0; attempt < globalConfig.pingRetryCount.load(std::memory_order_relaxed); ++attempt)
            {
                //if (Functions::ping(ip, globalConfig.pingTimeout.load(std::memory_order_relaxed)))
                {
                    pingSuccessful = true;
                    break;
                }
            }

            if (pingSuccessful)
            {
                if (globalConfig.logConflicts)
                    std::cout << "[DHCP] Conflict IP " << Functions::byteAddressToNumAddress(ip) << " is in use." << std::endl;

                pool->setConflicted(ip);
                std::this_thread::sleep_for(std::chrono::seconds(globalConfig.conflictInterval.load(std::memory_order_relaxed)));
                continue;
            }
        }

        return ip;
    }

    return {};
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
                requestedOptions.emplace_back(1, byte);
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
            if (Functions::compareNetworkWithIp(config->config->getNetwork(), ip, config->config->getPrefixLen()))
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
    ip.sourceAddress = sourceIP;
    ip.destinationAddress = destinationIP;
    dhcpPacket.Layer3.push_back(std::move(ip));

    UdpHeader udp;
    udp.sourcePort = Variable::Udp::dhcpClient;
    udp.destinationPort = Variable::Udp::dhcpServer;
    udp.length = std::string("\x01\x00", 2);
    udp.checksum = std::string("\x00\x00", 2); 
    dhcpPacket.Layer4.push_back(std::move(udp)); 

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
    dhcp.options.emplace_back(
        Variable::Dhcp::Option::type,
        std::string(messageType.size(), 1),
        messageType
    );

    dhcp.end = Variable::Dhcp::end;
    return dhcp;
}

PacketInfo Protocol::DhcpServer::buildDhcpOffer(const DhcpHeader& dhcpHeader, const Dhcp::DhcpNetworkConfig* config, const ByteString& ipAddress)
{
    PacketInfo packet = dhcpBody(Variable::IPv4::source, Variable::IPv4::broadcast, Variable::Mac::source);
    DhcpHeader offerHeader = buildDhcpHeader(Variable::Dhcp::Type::offer, ipAddress, dhcpHeader.relayAgentIP, dhcpHeader.transID);
    auto requestOptions = buildRequestedOptions(getRequestedOptions(dhcpHeader.options), config);
    offerHeader.boot = Variable::Dhcp::Type::offer;

    // Add DHCP Options
    ByteString leaseTime = Functions::numToByte(static_cast<uint32_t>(config->leaseTime), 4);
    offerHeader.options.emplace_back(
        Variable::Dhcp::Option::leaseTime,
        std::string(leaseTime.size(), 1),
        std::move(leaseTime)
    );
    ByteString mask = Functions::binToByte(Functions::numMaskToBin(config->getPrefixLen()));
    offerHeader.options.emplace_back(
        Variable::Dhcp::Option::mask, 
        Functions::numToByte(mask.size(), 1),
        std::move(mask)
    );
    offerHeader.options.emplace_back(
        Variable::Dhcp::Option::router, 
        Functions::numToByte(config->getGateway().size(), 1),
        config->getGateway()
    );

    offerHeader.options.insert(offerHeader.options.end(), requestOptions.begin(), requestOptions.end());
    auto options = buildDnsAndIdentityOptions(dhcpHeader.options, config, false);
    if (!options.empty())
    {
        offerHeader.options.insert(offerHeader.options.end(), options.begin(), options.end());
    }

    packet.Layer5.push_back(std::move(offerHeader));
    return packet;
}

PacketInfo Protocol::DhcpServer::buildDhcpAck(const DhcpHeader& dhcpHeader, const Dhcp::DhcpNetworkConfig* config, const ByteString& ipAddress)
{
    PacketInfo packet = dhcpBody(Variable::IPv4::source, Variable::IPv4::broadcast, Variable::Mac::source);
    DhcpHeader ackHeader = buildDhcpHeader(Variable::Dhcp::Type::ack, ipAddress, dhcpHeader.relayAgentIP, dhcpHeader.transID);
    auto requestOptions = buildRequestedOptions(getRequestedOptions(dhcpHeader.options), config);
    ackHeader.boot = Variable::Dhcp::Type::ack;

    // Add DHCP Options
    ByteString leaseTime = Functions::numToByte(static_cast<uint32_t>(config->leaseTime), 4);
    ackHeader.options.emplace_back(
        Variable::Dhcp::Option::leaseTime,
        Functions::numToByte(leaseTime.size(), 1),
        std::move(leaseTime)
    );
    ByteString mask = Functions::binToByte(Functions::numMaskToBin(config->getPrefixLen()));
    ackHeader.options.emplace_back(
        Variable::Dhcp::Option::mask, 
        Functions::numToByte(mask.size()),
        std::move(mask)
    );
    ackHeader.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::router, 
        Functions::numToByte(config->getGateway().size()),
        config->getGateway()
    });

    ackHeader.options.insert(ackHeader.options.end(), requestOptions.begin(), requestOptions.end());
    auto options = buildDnsAndIdentityOptions(dhcpHeader.options, config, true);
    if (!options.empty())
    {
        ackHeader.options.insert(ackHeader.options.end(), options.begin(), options.end());
    }

    packet.Layer5.push_back(std::move(ackHeader));

    if (!globalConfig.updateDNS.before.load(std::memory_order_relaxed))
    {
        //TODO add dns entry
    }
    return packet;
}

PacketInfo Protocol::DhcpServer::buildDhcpAckForInform(const DhcpHeader& dhcpHeader, const Dhcp::DhcpNetworkConfig* config, const std::vector<ByteString>& requestedOptions)
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
    auto requestOptions = buildRequestedOptions(requestedOptions, config);
    ackHeader.options.insert(ackHeader.options.end(), requestOptions.begin(), requestOptions.end());

    auto options = buildDnsAndIdentityOptions(dhcpHeader.options, config, false);
    if (!options.empty())
    {
        ackHeader.options.insert(ackHeader.options.end(), options.begin(), options.end());
    }

    packet.Layer5.push_back(std::move(ackHeader));
    return packet;
}

void Protocol::DhcpServer::sendNak(const DhcpHeader& dhcpHeader, Interface* interface)
{
    PacketInfo packet = dhcpBody(Variable::IPv4::source, Variable::IPv4::broadcast, Variable::Mac::source);
    DhcpHeader nakHeader = buildDhcpHeader(Variable::Dhcp::Type::nak, ByteString(4, '\x00'), dhcpHeader.relayAgentIP, dhcpHeader.transID);
    nakHeader.boot = Variable::Dhcp::Type::nak;
    packet.Layer5.push_back(std::move(nakHeader));
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

std::vector<DhcpHeader::Option> Protocol::DhcpServer::buildRequestedOptions(const std::vector<ByteString>& requestedOptions, const Dhcp::DhcpNetworkConfig* config)
{
    std::vector<DhcpHeader::Option> options;

    bool override = globalConfig.updateDNS.override.load(std::memory_order_relaxed) || globalConfig.updateDNS.both.load(std::memory_order_relaxed);

    for (const auto& opt : requestedOptions)
    {
        // Option 1: Subnet Mask
        if (opt == Variable::Dhcp::Option::mask && config->getPrefixLen() != 0)
        {
            ByteString mask = Functions::binToByte(Functions::numMaskToBin(config->getPrefixLen()));
            options.emplace_back(
                Variable::Dhcp::Option::mask,
                Functions::numToByte(mask.size(), 1),
                std::move(mask)
            );
        }
        // Option 3: Router (Gateway)
        if (opt == Variable::Dhcp::Option::router && !config->getGateway().empty())
        {
            options.emplace_back(
                Variable::Dhcp::Option::router,
                Functions::numToByte(config->getGateway().size(), 1),
                config->getGateway()
            );
        }
        if (!override && opt == Variable::Dhcp::Option::hostname)
        {
            ByteString hostname = Global::getInstance().getHostname();
            options.emplace_back(
                Variable::Dhcp::Option::hostname,
                Functions::numToByte(hostname.size(), 1),
                std::move(hostname)
            );
        }
        // Option 6: DNS Servers
        if (!override && opt == Variable::Dhcp::Option::domainServer && !config->dnsServer.empty())
        {
            if (!config->dnsServer.empty())
            {
                ByteString dnsServers;
                for (const auto& dns : config->dnsServer)
                {
                    if (dns.size() == 4)
                        dnsServers += dns;
                }

                if (!dnsServers.empty())
                {
                    std::shared_lock<std::shared_mutex> lock(globalConfig.configMutex);
                    if (globalConfig.updateDNS.override.load(std::memory_order_relaxed))
                    {
                        options.emplace_back(
                            Variable::Dhcp::Option::domainServer,
                            ByteString(dnsServers.size(), 1),
                            std::move(dnsServers)
                        );
                    }
                    else if (globalConfig.updateDNS.before)
                    {
                        options.insert(options.begin(), {
                            Variable::Dhcp::Option::domainServer,
                            Functions::numToByte(dnsServers.size(), 1),
                            std::move(dnsServers)
                        });
                    }
                    else if (globalConfig.updateDNS.both)
                    {
                        options.emplace_back(
                            Variable::Dhcp::Option::domainServer,
                            ByteString(dnsServers.size(), 1),
                            std::move(dnsServers)
                        );
                    }
                }
            }
        }
        // Option 15: Domain Name
        if (!override && opt == Variable::Dhcp::Option::domainName && !config->domainName.empty())
        {
            //TODO
//             options.emplace_back(
//                 Variable::Dhcp::Option::domainName,
//                 Functions::numToByte(config->domainName.size(), 1),
//                 ByteString(config->domainName, 10)
//             );
        }
        // Option 44: WINS Servers
        if (opt == Variable::Dhcp::Option::netbiosNameServer && !config->winsServer.empty())
        {
            ByteString winsServers;
            for (const auto& wins : config->winsServer)
            {
                if (wins.size() == 4)
                {
                    winsServers += wins;
                }
            }
            if (!winsServers.empty())
            {
                options.emplace_back(
                    Variable::Dhcp::Option::netbiosNameServer,
                    Functions::numToByte(winsServers.size(), 1),
                    std::move(winsServers)
                );
            }
        }
        // Option 66: TFTP Server Name
        if (opt == Variable::Dhcp::Option::tftpServer && !config->tftpServer.empty())
        {
            ByteString tftpServers;
            for (const auto& tftp : config->tftpServer)
            {
                if (tftp.size() == 4)
                {
                    tftpServers += tftp;
                }
            }
            if (!tftpServers.empty())
            {
                options.emplace_back(
                    Variable::Dhcp::Option::tftpServer,
                    Functions::numToByte(tftpServers.size(), 1),
                    std::move(tftpServers)
                );
            }
        }
        // Option 67: Bootfile Name
        if (opt == Variable::Dhcp::Option::bootfile && !config->bootfile.empty())
        {
            options.emplace_back(
                Variable::Dhcp::Option::bootfile,
                Functions::numToByte(config->bootfile.size(), 1),
                config->bootfile
            );
        }
        // Option 121: Classless Static Routes
        if (opt == Variable::Dhcp::Option::classlessStateRoute && !config->staticRoutes.empty())
        {
            ByteString routes;
            for (const auto& route : config->staticRoutes)
            {
                routes += route;
            }
            if (!routes.empty())
            {
                options.emplace_back(
                    Variable::Dhcp::Option::classlessStateRoute,
                    Functions::numToByte(routes.size(), 1),
                    std::move(routes)
                );
            }
        }
        // Option 26: MTU
        if (opt == Variable::Dhcp::Option::mtu && config->mtu > 0)
        {
            options.emplace_back(
                Variable::Dhcp::Option::mtu,
                ByteString("\x02", 1),
                Functions::numToByte(config->mtu, 2)
            );
        }
        // Option 51: IP Address Lease Time
        if (opt == Variable::Dhcp::Option::leaseTime && config->leaseTime > 0)
        {
            ByteString leaseTime = Functions::numToByte(static_cast<uint32_t>(config->leaseTime), 4);
            options.emplace_back(
                Variable::Dhcp::Option::leaseTime,
                Functions::numToByte(leaseTime.size(), 1),
                std::move(leaseTime)
            );
        }
        // Option 58:
        if (opt == Variable::Dhcp::Option::renewalTime && !config->renewalTime.empty())
        {
            options.emplace_back(
                Variable::Dhcp::Option::renewalTime,
                Functions::numToByte(config->renewalTime.size(), 1),
                config->renewalTime
            );
        }
        // Option 59: Rebinding Time (T2)
        if (opt == Variable::Dhcp::Option::rebindingTime && !config->rebindingTime.empty())
        {
            options.emplace_back(
                Variable::Dhcp::Option::rebindingTime,
                Functions::numToByte(config->rebindingTime.size(), 1),
                config->rebindingTime
            );
        }
        // Option 42: NTP Servers
        if (opt == Variable::Dhcp::Option::ntp && !config->ntpServer.empty())
        {
            ByteString ntpServers;
            for (const auto& ntp : config->ntpServer)
            {
                if (ntp.size() == 4)
                {
                    ntpServers += ntp;
                }
            }
            if (!ntpServers.empty())
            {
                options.emplace_back(
                    Variable::Dhcp::Option::ntp,
                    Functions::numToByte(ntpServers.size(), 1),
                    std::move(ntpServers)
                );
            }
        }
    }

    // End Option (Option 255)
    options.emplace_back(
        Variable::Dhcp::end,
        ByteString(),
        ByteString()
    );

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

    Dhcp::DhcpNetwork* config = dhcpNetworks[matchingNetwork];

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

    forceRenewPacket.Layer5.push_back(std::move(header));
    sendPacket(forceRenewPacket, config->config->interface);
}

std::vector<DhcpHeader::Option> Protocol::DhcpServer::buildDnsAndIdentityOptions(const std::vector<DhcpHeader::Option>& clientOptions, const Dhcp::DhcpNetworkConfig* config, bool isAck)
{
    bool override = globalConfig.updateDNS.override.load(std::memory_order_relaxed);
    bool both = globalConfig.updateDNS.both.load(std::memory_order_relaxed);
    bool before = globalConfig.updateDNS.before.load(std::memory_order_relaxed);

    std::vector<DhcpHeader::Option> result;
    
    if (override || both)
    {
        std::string clientHostname;
        std::vector<ByteString> clientDnsServers;
        std::vector<std::string> clientDomains;

        for (const auto& opt : clientOptions)
        {
            if (opt.option == Variable::Dhcp::Option::hostname)
            {
                clientHostname = opt.value.toString();
            }
            else if (opt.option == Variable::Dhcp::Option::domainServer)
            {
                for (size_t i = 0; i + 4 <= opt.value.size(); i += 4)
                    clientDnsServers.emplace_back(opt.value.substr(i, 4));
            }
            else if (opt.option == Variable::Dhcp::Option::domainName)
            {
                std::string domains = opt.value.toString();
                size_t pos = 0;
                while ((pos = domains.find(' ')) != std::string::npos)
                {
                    clientDomains.push_back(domains.substr(0, pos));
                    domains.erase(0, pos + 1);
                }
                if (!domains.empty())
                    clientDomains.push_back(domains);
            }
        }

        std::string hostname = clientHostname;
        std::vector<ByteString> dnsList = clientDnsServers;
        std::vector<std::string> domainList = clientDomains;

        if (override)
        {
            hostname = Global::getInstance().getHostname();
            dnsList = config->dnsServer;
            domainList = config->domainName;
        }
        else
        {
            if (both)
            {
                dnsList.insert(dnsList.end(), config->dnsServer.begin(), config->dnsServer.end());
                domainList.insert(domainList.end(), config->domainName.begin(), config->domainName.end());
            }
        }


        // Hostname (Option 12)
        if (!hostname.empty())
        {
            ByteString h = hostname;
            result.emplace_back(
                Variable::Dhcp::Option::hostname,
                Functions::numToByte(h.size(), 1),
                std::move(h)
            );
        }

        ByteString flattened;
        for (const auto& ip : dnsList)
        {
            if (ip.size() == 4)
                flattened += ip;
        }

        // DNS servers (Option 6)
        if (!flattened.empty())
        {
            result.emplace_back(
                Variable::Dhcp::Option::domainServer,
                Functions::numToByte(flattened.size(), 1),
                std::move(flattened)
            );
        }

        // Domain Name (Option 15) 
        if (!domainList.empty())
        {
            std::string fullDomain;
            for (size_t i = 0; i < domainList.size(); ++i)
            {
                fullDomain += domainList[i];
                if (i + 1 < domainList.size())
                    fullDomain += ' ';
            }

            ByteString b = fullDomain;
            result.emplace_back(
                Variable::Dhcp::Option::domainName,
                Functions::numToByte(b.size(), 1),
                std::move(b)
            );
        }
    }

    if (isAck && before)
    {
        //TODO Add dns record
    }

    return result;
}

bool Protocol::DhcpServer::isTrustedInterface(Interface* iface)
{
    return iface && iface->configs.trusted.load(std::memory_order_relaxed);
}

void Protocol::DhcpServer::addSnoopingEntry(const DhcpHeader& header, const ByteString& ip, Interface* iface, uint32_t leaseTime)
{
    if (!iface) return;

    Dhcp::SnoopingEntry entry;
    entry.mac = header.clientMacAddress;
    entry.ip = ip;
    entry.interface = {iface->configs.interfaceType, iface->configs.id};
    entry.expiration = std::chrono::steady_clock::now() + std::chrono::seconds(leaseTime);

    std::lock_guard<std::mutex> lock(snoopingMutex);
    snoopingTable[entry.mac] = std::move(entry);
}
