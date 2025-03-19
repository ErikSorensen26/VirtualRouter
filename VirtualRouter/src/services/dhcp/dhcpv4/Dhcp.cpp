#include <Dhcp.h>
#include <Interface.h>
#include <random>

namespace Protocol {

#pragma region RelayAgent

    DhcpRelay::DhcpRelay(Interface* interface)
        : associatedInterface(interface)
    {}

    DhcpRelay::~DhcpRelay() {}

    void DhcpRelay::addHelperAddress(const ByteString& helperAddress)
    {
        std::lock_guard<std::mutex> lock(relayMutex);
        helperAddresses.push_back(helperAddress);
    }

    void DhcpRelay::removeHelperAddress(const ByteString& helperAddress)
    {
        std::lock_guard<std::mutex> lock(relayMutex);
        helperAddresses.erase(std::remove(helperAddresses.begin(), helperAddresses.end(), helperAddress), helperAddresses.end());
    }

    void DhcpRelay::forwardToHelper(PacketInfo& packet)
    {
        std::lock_guard<std::mutex> lock(relayMutex);
        
        if (helperAddresses.empty())
        {
            return; // No helper addresses configured.
        }

        modifyGiaddr(packet);

        for (const auto& helperAddress : helperAddresses)
        {
            if (std::holds_alternative<IPv4Header>(packet.Layer3.front()))
            {
                if (auto *ipv4Header = &(std::get<IPv4Header>(packet.Layer3.front())))
                {
                    ipv4Header->destinationAddress = helperAddress;
                    associatedInterface->enqueuePacket(packet);
                }
            }
        }
    }

    void DhcpRelay::forwardToClient(PacketInfo& packet)
    {
        if (std::holds_alternative<IPv4Header>(packet.Layer3.front()))
        {
            if (auto *ipv4Header = &(std::get<IPv4Header>(packet.Layer3.front())))
            {
                ipv4Header->destinationAddress = extractAddress(packet);
                associatedInterface->enqueuePacket(packet);
            }
        }
    }

    void DhcpRelay::modifyGiaddr(PacketInfo& packet)
    {
        if (packet.Layer5.empty() || !std::holds_alternative<DhcpHeader>(packet.Layer5[0]))
        {
            return; // Invalid DHCP packet.
        }

        auto& dhcpHeader = std::get<DhcpHeader>(packet.Layer5[0]);
        if (associatedInterface->Get())
        {
            std::shared_lock<std::shared_mutex> lock(associatedInterface->Get()->ipMutex);
            dhcpHeader.relayAgentIP = associatedInterface->Get()->ipv4.ipAddress;
        }
    }

    ByteString DhcpRelay::extractAddress(PacketInfo& packet) const
    {
        if (packet.Layer5.empty() || !std::holds_alternative<DhcpHeader>(packet.Layer5[0]))
        {
            return {}; // Invalid or unsupported packet structure.
        }

        const auto& dhcpHeader = std::get<DhcpHeader>(packet.Layer5[0]);

        // Prefer 'yourClientIP' if available, otherwise fall back to 'clientIP'
        return !dhcpHeader.yourClientIP.empty() ? dhcpHeader.yourClientIP : dhcpHeader.clientIP;
    }


#pragma endregion
#pragma region IPPool

    IPPool::IPPool(const ByteString& network, const ByteString& subnetMask, const ByteString& gateway) : gateway(gateway)
    {
        uint32_t networkInt = Functions::byteToNum(network);
        uint32_t maskInt = Functions::byteToNum(subnetMask);
        uint32_t broadcastInt = networkInt | ~maskInt;

        baseAddress = Functions::numToByte(networkInt);
        broadcastAddress = Functions::numToByte(broadcastInt);
        poolSize = Functions::binToNum((Functions::reverseBinary(Functions::byteToBin(subnetMask)))) - 1; // Total amount of usable IPs
        excludeIP(gateway);

        currentAddress = baseAddress; // Start allocation at the base address
    }

    ByteString IPPool::allocateIP(const ByteString& macAddress)
    {
        // Check if MAC already is assigned an address
        for (auto& [ipAddress, mac] : allocatedIPs)
        {
            if (macAddress == mac)
            {
                return ipAddress;
            }
        }

        // Allocate dynamically from the pool
        for (uint32_t i = 0; i < poolSize; ++i)
        {
            ByteString canidateIP = Functions::numToByte(
                (Functions::byteToNum(baseAddress) + 1 + i) & (Functions::byteToNum(broadcastAddress))
            );
            if (!isAllocatedOrExcluded(canidateIP))
            {
                allocatedIPs[canidateIP] = macAddress;
                return canidateIP;
            }
        }
        return {}; // No available IPs
    }

    void IPPool::releaseIP(const ByteString& ip)
    {
        allocatedIPs.erase(ip);
    }

    bool IPPool::excludeIP(const ByteString& ip)
    {
        if (isExcluded(ip))
        {
            return false; // IP already or excluded
        }
        excludedAddresses.insert(ip);
        return true;
    }

    bool IPPool::setConflicted(const ByteString& ip)
    {
        if (allocatedIPs.find(ip) == allocatedIPs.end())
        {
            return false; // Allocation does not exist
        }
        else
        {
            // Clear allocated IPs mac
            allocatedIPs[ip].clear();
            return true;
        }
    }

    bool IPPool::removeExclusion(const ByteString& ip)
    {
        return excludedAddresses.erase(ip) > 0;
    }

    bool IPPool::isAllocated(const ByteString& ip) const
    {
        return allocatedIPs.find(ip) != allocatedIPs.end();
    }

    bool IPPool::isExcluded(const ByteString& ip) const
    {
        return excludedAddresses.find(ip) != excludedAddresses.end();
    }

    bool IPPool::isAllocatedOrExcluded(const ByteString& ip) const
    {
        return isAllocated(ip) || isExcluded(ip);
    }

    bool IPPool::matchMacToIP(const ByteString ip, const ByteString& mac)
    {
        if (allocatedIPs.find(ip) != allocatedIPs.end() && allocatedIPs[ip] == mac)
        {
            return true;
        }
        return false;
    }

    void IPPool::adjustPool(const ByteString& network, const ByteString& subnetMask, const ByteString& newGatway)
    {
        uint32_t networkInt = Functions::byteToNum(network);
        uint32_t maskInt = Functions::byteToNum(subnetMask);
        uint32_t broadcastInt = networkInt | ~maskInt;

        baseAddress = Functions::numToByte(networkInt);
        broadcastAddress = Functions::numToByte(broadcastInt);
        poolSize = Functions::binToNum((Functions::reverseBinary(Functions::byteToBin(subnetMask)))) - 1;

        // Check for gatway change
        if (gateway != newGatway)
        {
            releaseIP(gateway); // Release old gateway
            excludeIP(newGatway); // Reserve new gateway
            gateway = newGatway;
        }

        currentAddress = baseAddress;

        // Remove allocated and reserved IPs outside of the new range
        for (auto it = allocatedIPs.begin(); it != allocatedIPs.end();)
        {
            uint32_t ipInt = Functions::byteToNum(it->first);
            if (ipInt < networkInt + 1 || ipInt >= broadcastInt)
            {
                it = allocatedIPs.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }


    ByteString IPPool::findNextAvailableIP()
    {
        uint32_t start = Functions::byteToNum(currentAddress);
        uint32_t end = Functions::byteToNum(broadcastAddress);

        for (uint32_t ip = start + 1; ip <= end; ++ip)
        {
            ByteString canidate = Functions::numToByte(ip);
            if (!isAllocatedOrExcluded(canidate))
            {
                currentAddress = Functions::numToByte(ip + 1); // Update pointer
                return canidate;
            }
        }

        // Wrap around and return from the base address
        for (uint32_t ip = Functions::byteToNum(baseAddress); ip < start; ++ip)
        {
            ByteString canidate = Functions::numToByte(ip);
            if (!isAllocatedOrExcluded(canidate))
            {
                currentAddress = Functions::numToByte(ip + 1); // Update pointer
                return canidate;
            }
        }

        return {};
    }

#pragma endregion
#pragma region DhcpServer

    DhcpServer::DhcpServer() : stopFlag(false) {}

    DhcpServer::~DhcpServer()
    {
        stopServer();
    }

    void DhcpServer::startServer()
    {
        stopFlag.store(false);
        dhcpThread = std::thread(&DhcpServer::dhcpHandler, this);
    }

    void DhcpServer::stopServer()
    {
        cleanupTimeouts();
        stopFlag.store(true);
        if (dhcpThread.joinable())
        {
            dhcpThread.join();
        }
    }

    void DhcpServer::addNetwork(const NetworkConfig& config)
    {
        std::lock_guard<std::mutex> lock(configMutex);
        if (networkConfigs.find(config.network) != networkConfigs.end())
        {
            return; // Network already exists
        }

        networkConfigs[config.network] = config;
        ipPools.emplace(config.network, IPPool(config.network, config.subnetMask, config.defaultGateway));
    }

    void DhcpServer::updateNetworkConfig(const ByteString& network, const NetworkConfig& newConfig, const std::optional<std::vector<ByteString>>& dnsToRemove, const std::optional<std::vector<ByteString>>& winsToRemove, const std::optional<std::vector<ByteString>>& helperAddressesToRemove)
    {
        std::lock_guard<std::mutex> lock(configMutex);

        // Check if the network exists in the configuration map
        if (networkConfigs.find(network) == networkConfigs.end())
        {
            return; // Network does not exist
        }

        // Adjust the IP pool based on the new configuration
        ipPools[network].adjustPool(newConfig.network, newConfig.subnetMask, newConfig.defaultGateway);

        // Remove invalid leases that are no longer in the updated IP pool
        for (auto it = leases.begin(); it != leases.end();)
        {
            if (!ipPools[network].isAllocated(it->second.ipAddress))
            {
                it = leases.erase(it); // Erase invalid lease
            }
            else
            {
                ++it; // Move to the next lease
            }
        }

        // Update the current network configuration
        NetworkConfig& currentConfig = networkConfigs[network];

        // Update individual fields only if they are provided in the new configuration
        if (!newConfig.network.empty()) { currentConfig.network = newConfig.network; }
        if (!newConfig.subnetMask.empty()) { currentConfig.subnetMask = newConfig.subnetMask; }
        if (!newConfig.defaultGateway.empty()) { currentConfig.defaultGateway = newConfig.defaultGateway; }
        if (!newConfig.renewalTime.empty()) { currentConfig.renewalTime = newConfig.renewalTime; }
        if (!newConfig.rebindingTime.empty()) { currentConfig.rebindingTime = newConfig.rebindingTime; }
        if (!newConfig.domainName.empty()) { currentConfig.domainName = newConfig.domainName; }
        if (!newConfig.netbiosName.empty()) { currentConfig.netbiosName = newConfig.netbiosName; }
        if (!newConfig.ntpServer.empty()) { currentConfig.ntpServer = newConfig.ntpServer; }
        if (!newConfig.tftpServer.empty()) { currentConfig.tftpServer = newConfig.tftpServer; }
        if (!newConfig.broadcastAddress.empty()) { currentConfig.broadcastAddress = newConfig.broadcastAddress; }
        if (!newConfig.arpTimeout.empty()) { currentConfig.arpTimeout = newConfig.arpTimeout; }

        // Update non-default values
        if (newConfig.leaseTime > 0.0) { currentConfig.leaseTime = newConfig.leaseTime; }
        if (newConfig.interface) { currentConfig.interface = newConfig.interface; }
        if (newConfig.mtu > 0) { currentConfig.mtu = newConfig.mtu; }
        if (newConfig.allowDynamicUpdates) {currentConfig.allowDynamicUpdates = newConfig.allowDynamicUpdates;}

        // Append to vector fields (avoid duplication)
        currentConfig.dnsServer.insert(currentConfig.dnsServer.end(), newConfig.dnsServer.begin(), newConfig.dnsServer.end());
        currentConfig.winsServer.insert(currentConfig.winsServer.end(), newConfig.winsServer.begin(), newConfig.winsServer.end());
        currentConfig.helperAddresses.insert(currentConfig.helperAddresses.end(), newConfig.helperAddresses.begin(), newConfig.helperAddresses.end());
        currentConfig.staticRoutes.insert(currentConfig.staticRoutes.end(), newConfig.staticRoutes.begin(), newConfig.staticRoutes.end());

        // Remove entries from vectors based on optional parameters
        if (dnsToRemove)
        {
            for (const auto& dns : *dnsToRemove)
            {
                currentConfig.dnsServer.erase(
                    std::remove(currentConfig.dnsServer.begin(), currentConfig.dnsServer.end(), dns),
                    currentConfig.dnsServer.end()
                );
            }
        }

        if (winsToRemove)
        {
            for (const auto& wins : *winsToRemove)
            {
                currentConfig.winsServer.erase(
                    std::remove(currentConfig.winsServer.begin(), currentConfig.winsServer.end(), wins),
                    currentConfig.winsServer.end()
                );
            }
        }

        if (helperAddressesToRemove)
        {
            for (const auto& helper : *helperAddressesToRemove)
            {
                currentConfig.helperAddresses.erase(
                    std::remove(currentConfig.helperAddresses.begin(), currentConfig.helperAddresses.end(), helper),
                    currentConfig.helperAddresses.end()
                );
            }
        }

        // Replace or merge metadata fields
        if (!newConfig.description.empty()) { currentConfig.description = newConfig.description; }
        currentConfig.isPrivate = newConfig.isPrivate;
        currentConfig.isEnabled = newConfig.isEnabled;
    }


    void DhcpServer::handleDhcpPacket(const PacketInfo& packet)
    {
        const DhcpHeader* dhcpHeader = nullptr;
        if (!packet.Layer5.empty() && std::holds_alternative<DhcpHeader>(packet.Layer5[0]))
        {
            dhcpHeader = &std::get<DhcpHeader>(packet.Layer5[0]);
        }

        if (!dhcpHeader)
        {
            return; // Invalid DHCP packet
        }

        ByteString messageType = getOption(dhcpHeader->options, Variable::Dhcp::Option::type);
        if (messageType.empty())
        {
            return; // No message found
        }

        if (messageType == Variable::Dhcp::Type::discover)
        {
            processDiscover(*dhcpHeader);
        }
        else if (messageType == Variable::Dhcp::Type::request)
        {
            processRequest(*dhcpHeader);
        }
        else if (messageType == Variable::Dhcp::Type::release)
        {
            processRelease(*dhcpHeader);
        }
        else if (messageType == Variable::Dhcp::Type::inform)
        {
            processInform(*dhcpHeader);
        }
        else if (messageType == Variable::Dhcp::Type::decline)
        {
            processDecline(*dhcpHeader);
        }
    }

    void DhcpServer::dhcpHandler()
    {
        while (!stopFlag.load())
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            cleanupExpiredLeases();
        }
    }

    void DhcpServer::cleanupExpiredLeases()
    {
        std::lock_guard<std::mutex> lock(dhcpMutex);

        double currentTime = secondsSinceEpoch();
        for (auto it = leases.begin(); it != leases.end();)
        {
            if (it->second.leaseStart + it->second.leaseDuration < currentTime)
            {
                releaseIPAddress(findMatchingNetworkAgainstIP(it->second.ipAddress), it->second.ipAddress);
                it = leases.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void DhcpServer::processDiscover(const DhcpHeader& dhcpHeader)
    {
        ByteString matchingNetwork = findMatchingNetwork(dhcpHeader);

        if (matchingNetwork.empty())
        {
            return; // No matching network
        }

        ByteString allocatedIP = allocateIPAddress(matchingNetwork, dhcpHeader.clientMacAddress);

        if (!allocatedIP.empty())
        {
            PacketInfo offerPacket = buildDhcpOffer(dhcpHeader, networkConfigs[matchingNetwork], allocatedIP);
            sendPacket(offerPacket, networkConfigs[matchingNetwork].interface);
            startOfferTimeout(allocatedIP, dhcpHeader.clientMacAddress, matchingNetwork, 60.0);
        }
        else
        {
            sendNak(dhcpHeader, networkConfigs[matchingNetwork].interface);
        }
    }

    ByteString DhcpServer::allocateIPAddress(const ByteString& network, const ByteString& macAddress)
    {
        auto poolIt = ipPools.find(network);
        if (poolIt == ipPools.end())
        {
            return {}; // No pool for the network
        }

        return poolIt->second.allocateIP(macAddress); // Allocate an IP from the pool
    }

    void DhcpServer::releaseIPAddress(const ByteString& network, const ByteString& ipAddress)
    {
        auto poolIt = ipPools.find(network);
        if (poolIt == ipPools.end())
        {
            return; // No pools for the network
        }

        poolIt->second.releaseIP(ipAddress); // Release the IP back into the pool
    }

    void DhcpServer::processRequest(const DhcpHeader& dhcpHeader)
    {
        if (getOption(dhcpHeader.options, Variable::Dhcp::Option::requestIP).empty()) return;
        ByteString requestedIP = getOption(dhcpHeader.options, Variable::Dhcp::Option::requestIP);
        ByteString matchingNetwork = findMatchingNetwork(dhcpHeader);

        if (requestedIP.empty() || matchingNetwork.empty() || !ipPools[matchingNetwork].isAllocatedOrExcluded(requestedIP) || !ipPools[matchingNetwork].matchMacToIP(requestedIP, dhcpHeader.clientMacAddress))
        {
            sendNak(dhcpHeader, networkConfigs[matchingNetwork].interface);
            return;
        }
        
        {
            std::lock_guard<std::mutex> lock(dhcpMutex);
            leases[requestedIP] = {requestedIP, dhcpHeader.clientMacAddress, secondsSinceEpoch(), networkConfigs[matchingNetwork].leaseTime};
        }

        // Build and send DHCP ACK outside of mutex
        PacketInfo ackPacket = buildDhcpAck(dhcpHeader, networkConfigs[matchingNetwork], requestedIP);
        sendPacket(ackPacket, networkConfigs[matchingNetwork].interface);

        // Notify the timeout for this specific IP
        std::shared_ptr<OfferTimeout> offerTimeout;
        {
            std::lock_guard<std::mutex> lock(dhcpMutex);
            auto it = offerTimeouts.find(requestedIP);
            if (it != offerTimeouts.end())
            {
                offerTimeout = it->second;
            }
        }

        if (offerTimeout)
        {
            {
                std::lock_guard<std::mutex> offerLock(offerTimeout->mutex);
                offerTimeout->requestReceived = true;
            }
            offerTimeout->cv.notify_all();
        }
    }

    void DhcpServer::cleanupTimeouts()
    {
        std::vector<std::shared_ptr<OfferTimeout>> timeoutsToNoify;

        {
            std::lock_guard<std::mutex> lock(dhcpMutex);

            for (auto& [ip, offerTimeout] : offerTimeouts)
            {
                timeoutsToNoify.push_back(offerTimeout);
            }
        }

        // Notify all relavent OfferTimeouts outside the dhcpMutex lock
        for (auto& offerTimeout : timeoutsToNoify)
        {
            {
                std::lock_guard<std::mutex> offerLock(offerTimeout->mutex);
                offerTimeout->requestReceived = true;
            }
            offerTimeout->cv.notify_all();
        }
    }

    void DhcpServer::processRelease(const DhcpHeader& dhcpHeader)
    {
        ByteString releaseIP = dhcpHeader.clientIP;
        if (!releaseIP.empty() && releaseIP != ByteString("\x00\x00\x00\x00", 4))
        {
            releaseIPAddress(findMatchingNetwork(dhcpHeader), releaseIP);
        }
        leases.erase(releaseIP);
    }

    void DhcpServer::processDecline(const DhcpHeader& dhcpHeader)
    {
        ByteString declinedIP = getOption(dhcpHeader.options, Variable::Dhcp::Option::requestIP);
        if (declinedIP.empty()) return; // No valid request IP option

        ByteString matchingNetwork = findMatchingNetwork(dhcpHeader);
        ipPools[matchingNetwork].setConflicted(declinedIP);
    }

    void DhcpServer::processInform(const DhcpHeader& dhcpHeader)
    {
        ByteString matchingNetwork = findMatchingNetwork(dhcpHeader);
        if (matchingNetwork.empty())
        {
            return; // No matching network found
        }

        auto requestedOptions = getRequestedOptions(dhcpHeader.options);

        PacketInfo informAck = buildDhcpAckForInform(dhcpHeader, networkConfigs[matchingNetwork], requestedOptions);

        sendPacket(informAck, networkConfigs[matchingNetwork].interface);
    }

    std::vector<ByteString> DhcpServer::getRequestedOptions(const std::vector<DhcpHeader::Option>& options)
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

    void DhcpServer::startOfferTimeout(const ByteString& ipAddress, const ByteString& macAddress, const ByteString& network, double timeoutSeconds)
    {
        std::shared_ptr<OfferTimeout> offerTimeout;

        // Scope to limit the duration of dhcpMutex lock
        {
            std::lock_guard<std::mutex> lock(dhcpMutex);
            // Check if timeout already exists
            if (offerTimeouts.find(ipAddress) != offerTimeouts.end())
            {
                return; // Timeout already running
            }

            // Create a new OfferTimeout object for this IP
            offerTimeout = std::make_shared<OfferTimeout>();
            offerTimeouts.emplace(ipAddress, offerTimeout);
        }

        // Launch the timeout in a seperate thread
        std::thread([this, ipAddress, network, timeoutSeconds, offerTimeout]() {
            // Lock the per-offer mutex
            std::unique_lock<std::mutex> lock(offerTimeout->mutex);

            // Wait for the timeout duration or until the condition variable is signaled
            bool received = offerTimeout->cv.wait_for(lock, std::chrono::duration<double>(timeoutSeconds), [&offerTimeout]() {
                return offerTimeout->requestReceived;
            });

            if (!received)
            {
                std::lock_guard<std::mutex> dhcpLock(dhcpMutex);
                releaseIPAddress(network, ipAddress);
            }

            {
                std::lock_guard<std::mutex> dhcpLock(dhcpMutex);
                if (offerTimeouts.find(ipAddress) != offerTimeouts.end())
                {
                    offerTimeouts.erase(ipAddress);
                }
            }
        }).detach();
    }

    ByteString DhcpServer::generateTransactionID()
    {
        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<uint32_t> dist(0, UINT32_MAX);
        return Functions::numToByte(dist(rng), 2);
    }

    ByteString DhcpServer::findMatchingNetwork(const DhcpHeader& dhcpHeader)
    {
        ByteString relayAgentIP = dhcpHeader.relayAgentIP;

        // Match based on the relay agent IP (if present)
        ByteString relayMatch = findMatchingNetworkAgainstIP(relayAgentIP);
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

    ByteString DhcpServer::findMatchingNetworkAgainstIP(const ByteString& ip)
    {
        for (const auto& [network, config] : networkConfigs)
        {
            if (Functions::compareNetworkWithIp(config.network, ip, Functions::byteMaskToNum(config.subnetMask)))
            {
                return network;
            }
        }

        return {};
    }

    PacketInfo DhcpServer::dhcpBody(const ByteString& sourceIP, const ByteString& destinationIP, const ByteString& sourceMac)
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
        udp.sourcePort = Variable::Udp::dhcpSource;
        udp.destinationPort = Variable::Udp::dhcpDestination;
        udp.length = std::string("\x01\x00", 2);
        udp.checksum = std::string("\x00\x00", 2); 
        dhcpPacket.Layer4.emplace_back(std::move(udp)); 

        return dhcpPacket; 
    }

    DhcpHeader DhcpServer::buildDhcpHeader(const ByteString& messageType, const ByteString& clientIP, const ByteString& relayAgentIP, const ByteString& transID)
    {
        DhcpHeader dhcp;

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

    PacketInfo DhcpServer::buildDhcpOffer(const DhcpHeader& dhcpHeader, const NetworkConfig& config, const ByteString& ipAddress)
    {
        PacketInfo packet = dhcpBody(Variable::IPv4::source, Variable::IPv4::broadcast, Variable::Mac::source);

        DhcpHeader offerHeader = buildDhcpHeader(Variable::Dhcp::Type::offer, ipAddress, dhcpHeader.relayAgentIP, dhcpHeader.transID);

        offerHeader.boot = Variable::Dhcp::Type::offer;

        // Add DHCP Options
        offerHeader.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::leaseTime,
            std::string("\x04", 1),
            Functions::numToByte(static_cast<uint32_t>(config.leaseTime), 4)
        });
        offerHeader.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::mask, 
            std::string("\x04", 1),
            config.subnetMask
        });
        offerHeader.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::router, 
            std::string("\x04", 1),
            config.defaultGateway
        });
        for (const auto& dns : config.dnsServer) {
            offerHeader.options.emplace_back(DhcpHeader::Option{
                Variable::Dhcp::Option::domainServer,
                std::string("\x04", 1),
                dns
            });
        }

        packet.Layer5.emplace_back(std::move(offerHeader));
        return packet;
    }

    PacketInfo DhcpServer::buildDhcpAck(const DhcpHeader& dhcpHeader, const NetworkConfig& config, const ByteString& ipAddress)
    {
        PacketInfo packet = dhcpBody(Variable::IPv4::source, Variable::IPv4::broadcast, Variable::Mac::source);

        DhcpHeader ackHeader = buildDhcpHeader(Variable::Dhcp::Type::ack, ipAddress, dhcpHeader.relayAgentIP, dhcpHeader.transID);

        ackHeader.boot = Variable::Dhcp::Type::ack;

        // Add DHCP Options
        ackHeader.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::leaseTime,
            std::string("\x04", 1),
            Functions::numToByte(static_cast<uint32_t>(config.leaseTime), 4)
        });
        ackHeader.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::mask, 
            std::string("\x04", 1),
            config.subnetMask
        });
        ackHeader.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::router, 
            std::string("\x04", 1),
            config.defaultGateway
        });
        for (const auto& dns : config.dnsServer) {
            ackHeader.options.emplace_back(DhcpHeader::Option{
                Variable::Dhcp::Option::domainServer,
                std::string("\x04", 1),
                dns
            });
        }

        packet.Layer5.emplace_back(std::move(ackHeader));
        return packet;
    }

    PacketInfo DhcpServer::buildDhcpAckForInform(const DhcpHeader& dhcpHeader, const DhcpServer::NetworkConfig& config, const std::vector<ByteString>& requestedOptions)
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

    std::vector<DhcpHeader::Option> DhcpServer::buildRequestedOptions(const std::vector<ByteString>& requestedOptions, const NetworkConfig& config)
    {
        std::vector<DhcpHeader::Option> options;

        for (const auto& opt : requestedOptions)
        {
            // Option 1: Subnet Mask
            if (opt == Variable::Dhcp::Option::mask && !config.subnetMask.empty())
            {
                options.emplace_back(DhcpHeader::Option{
                    Variable::Dhcp::Option::mask,
                    ByteString("\x04", 1),
                    config.subnetMask
                });
            }
            // Option 3: Router (Gateway)
            if (opt == Variable::Dhcp::Option::router && !config.defaultGateway.empty())
            {
                options.emplace_back(DhcpHeader::Option{
                    Variable::Dhcp::Option::router,
                    ByteString("\x04", 1),
                    config.defaultGateway
                });
            }
            // Option 6: DNS Servers
            if (opt == Variable::Dhcp::Option::domainServer && !config.dnsServer.empty())
            {
                for (const auto& dns : config.dnsServer)
                {
                    options.emplace_back(DhcpHeader::Option{
                        Variable::Dhcp::Option::domainServer,
                        ByteString("\x06", 1),
                        dns
                    });
                }
            }
            // Option 15: Domain Name
            if (opt == Variable::Dhcp::Option::domainName && !config.domainName.empty())
            {
                options.emplace_back(DhcpHeader::Option{
                    Variable::Dhcp::Option::domainName,
                    ByteString(1, static_cast<unsigned char>(config.domainName.size())),
                    config.domainName
                });
            }
            // Option 44: WINS Servers
            if (opt == Variable::Dhcp::Option::netbiosNameServer && !config.winsServer.empty())
            {
                for (const auto& wins : config.winsServer)
                {
                    options.emplace_back(DhcpHeader::Option{
                        Variable::Dhcp::Option::netbiosNameServer,
                        ByteString("\x04", 1),
                        wins
                    });
                }
            }
            // Option 66: TFTP Server Name
            if (opt == Variable::Dhcp::Option::tftpServer && !config.tftpServer.empty())
            {
                options.emplace_back(DhcpHeader::Option{
                    Variable::Dhcp::Option::tftpServer,
                    ByteString(1, static_cast<unsigned char>(config.tftpServer.size())),
                    config.tftpServer
                });
            }
            // Option 67: Bootfile Name
            if (opt == Variable::Dhcp::Option::bootfile && !config.bootfile.empty())
            {
                options.emplace_back(DhcpHeader::Option{
                    Variable::Dhcp::Option::bootfile,
                    ByteString(1, static_cast<unsigned char>(config.bootfile.size())),
                    config.bootfile
                });
            }
            // Option 121: Classless Static Routes
            if (opt == Variable::Dhcp::Option::classlessStateRoute && !config.staticRoutes.empty())
            {
                for (const auto& route : config.staticRoutes)
                {
                    options.emplace_back(DhcpHeader::Option{
                        Variable::Dhcp::Option::classlessStateRoute,
                        ByteString("\x04", 1),
                        route
                    });
                }
            }
            // Option 26: MTU
            if (opt == Variable::Dhcp::Option::mtu && config.mtu > 0)
            {
                options.emplace_back(DhcpHeader::Option{
                    Variable::Dhcp::Option::mtu,
                    ByteString("\x02", 1),
                    Functions::numToByte(config.mtu, 2)
                });
            }
            // Option 51: IP Address Lease Time
            if (opt == Variable::Dhcp::Option::leaseTime && config.leaseTime > 0)
            {
                options.emplace_back(DhcpHeader::Option{
                    Variable::Dhcp::Option::leaseTime,
                    ByteString("\x04", 1),
                    Functions::numToByte(static_cast<uint32_t>(config.leaseTime), 4)
                });
            }
            // Option 58:
            if (opt == Variable::Dhcp::Option::renewalTime && !config.renewalTime.empty())
            {
                options.emplace_back(DhcpHeader::Option{
                    Variable::Dhcp::Option::renewalTime,
                    ByteString("\x04", 1),
                    config.renewalTime
                });
            }
            // Option 59: Rebinding Time (T2)
            if (opt == Variable::Dhcp::Option::rebindingTime && !config.rebindingTime.empty())
            {
                options.emplace_back(DhcpHeader::Option{
                    Variable::Dhcp::Option::rebindingTime,
                    ByteString("\x04", 1),
                    config.rebindingTime
                });
            }
            // Option 42: NTP Servers
            if (opt == Variable::Dhcp::Option::ntp && !config.ntpServer.empty())
            {
                options.emplace_back(DhcpHeader::Option{
                    Variable::Dhcp::Option::ntp,
                    ByteString("\x04", 1),
                    config.ntpServer
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

    void DhcpServer::sendNak(const DhcpHeader& dhcpHeader, Interface* interface)
    {
        PacketInfo packet = dhcpBody(Variable::IPv4::source, Variable::IPv4::broadcast, Variable::Mac::source);

        DhcpHeader nakHeader = buildDhcpHeader(Variable::Dhcp::Type::nak, ByteString(4, '\x00'), dhcpHeader.relayAgentIP, dhcpHeader.transID);

        nakHeader.boot = Variable::Dhcp::Type::nak;

        packet.Layer5.emplace_back(std::move(nakHeader));
        sendPacket(packet, interface);
    }

    ByteString DhcpServer::getOption(const std::vector<DhcpHeader::Option>& options, const ByteString& optionType)
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

    void DhcpServer::sendPacket(PacketInfo& packet, Interface* interface)
    {
        if (interface)
        {
            interface->enqueuePacket(packet);
        }
    }

    double DhcpServer::secondsSinceEpoch()
    {
        return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

#pragma endregion
#pragma region DhcpClient

    // Initialize a static random generator for transaction IDs
    static std::mt19937& getTransidGenerator()
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        return gen;
    }

    // Constructor for the DhcpClient class, initializes with a reference to an Interface object
    DhcpClient::DhcpClient(Interface* CurrentInterface, bool reduced)
        : currentInterface(CurrentInterface),
          stopFlag(false),
          offered(false),
          acked(false),
          naked(false),
          leaseStart(0)
    {
        leaseStart = secondsSinceEpoch();
        if (!reduced)
        {
            ByteString mac;
            {
                std::shared_lock<std::shared_mutex> macMutex(currentInterface->Get()->ipMutex);
                mac = currentInterface->Get()->macAddress;
            }
            InitializeDhcp(mac);
        }
    }

    // Destructor to clean up threads and resources
    DhcpClient::~DhcpClient()
    {
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

    // Generates a random DHCP transaction ID
    ByteString DhcpClient::generateDhcpTransid() 
    {
        static std::uniform_int_distribution<uint32_t> dis(0, UINT32_MAX);
        uint32_t transId = dis(getTransidGenerator());

        ByteString transIdBytes;
        transIdBytes.reserve(4);
        transIdBytes.resize(4);
        transIdBytes[0] = static_cast<unsigned char>((transId >> 24) & 0xFF);
        transIdBytes[1] = static_cast<unsigned char>((transId >> 16) & 0xFF);
        transIdBytes[2] = static_cast<unsigned char>((transId >> 8) & 0xFF);
        transIdBytes[3] = static_cast<unsigned char>(transId & 0xFF);
        return transIdBytes;
    }

    // Initializes DHCP, sends discover requests, handles offers, and sends requests and acknowledgments
    void DhcpClient::InitializeDhcp(ByteString& hardwareAddress) 
    {
        std::string hostname = Global::getInstance().getHostname();

        // Start the DHCP handling thread
        dhcpThread = std::thread(&DhcpClient::dhcpHandler, this, hostname, hardwareAddress);
    }

    // Main DHCP handling loop running in a seperate thread
    void DhcpClient::dhcpHandler(std::string hostname, ByteString hardwareAddress)
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
    void DhcpClient::sendDhcpDiscover(const std::string& hostname, ByteString& hardwareAddress)
    {
        if (!configs.dhcpServer.empty())
        {
            return; // No need for dhcp discover
        }

        if (currentInterface->Get())
        {
            std::lock_guard<std::shared_mutex> lock(currentInterface->Get()->ipMutex);
            if (!currentInterface->Get()->ipv4.ipAddress.empty())
            {
                return; // No need for dhcp discover
            }
        }
        else
        {
            return;
        }

        if (leaseStart + Functions::byteToNum(configs.leaseTime) < secondsSinceEpoch())
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
    void DhcpClient::sendDhcpRequest(PacketInfo& requestPacket)
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
    void DhcpClient::processDhcpOffer(const std::string& hostname, ByteString& hardwareAddress)
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
    void DhcpClient::handleLeaseRenewal(ByteString& hardwareAddress, const std::string& hostname)
    {
        std::unique_lock<std::mutex> lock(dhcpMutex);
        double currentTime = secondsSinceEpoch();
        uint32_t renewalTime = Functions::byteToNum(configs.renewalTime);

        if (leaseStart + renewalTime < currentTime)
        {
            ByteString dhcpIP;
            if (currentInterface->Get())
            {
                std::lock_guard<std::shared_mutex> ipLock(currentInterface->Get()->ipMutex);
                dhcpIP = currentInterface->Get()->ipv4.ipAddress;
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
    void DhcpClient::sendDhcpRelease()
    {
        std::lock_guard<std::mutex> lock(dhcpMutex);
        if (!acked)
        {
            // No active lease to release
            return;
        }
        
        if (currentInterface->Get())
        {
            ByteString mac = currentInterface->Get()->macAddress;
            PacketInfo releasePacket = dhcpRelease(dhcpBody(mac), mac);

            currentInterface->enqueuePacket(releasePacket);

            resetDhcpState();
        }
    }

    // Resets the DHCP client state upon receiving a NAC or DECLINE
    void DhcpClient::resetDhcpState()
    {
        acked = false;
        naked = false;
        leaseStart = 0;
    }

    // Creates a DHCP packet body with Ethernet, IP, and UDP headers
    PacketInfo DhcpClient::dhcpBody(ByteString& hardwareAddress)
    {
        PacketInfo dhcpPacket;

        EthernetHeader eth;
        eth.destinationMac = Variable::Mac::broadcast; 
        eth.sourceMac = hardwareAddress; 
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
        ip.sourceAddress = Variable::IPv4::source;
        ip.destinationAddress = Variable::IPv4::broadcast;
        dhcpPacket.Layer3.emplace_back(std::move(ip));

        UdpHeader udp;
        udp.sourcePort = Variable::Udp::dhcpSource;
        udp.destinationPort = Variable::Udp::dhcpDestination;
        udp.length = std::string("\x01\x00", 2);
        udp.checksum = std::string("\x00\x00", 2); 
        dhcpPacket.Layer4.emplace_back(std::move(udp)); 

        return dhcpPacket; 
    }

    // Creates a DHCP discover packet
    PacketInfo DhcpClient::dhcpDiscover(PacketInfo packet, const std::string& hostname, ByteString& hardwareAddress) 
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

        dhcp.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::type,
            std::string("\x01", 1),
            Variable::Dhcp::Type::discover
        });

        dhcp.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::clientID,
            std::string("\x06", 1),
            hardwareAddress
        });

        dhcp.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::maxSize,
            std::string("\x02", 1),
            std::string("\x02\x04", 2)
        });

        dhcp.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::hostname,
            Functions::numToByte(static_cast<uint32_t>(hostname.length())),
            hostname
        });

        dhcp.end = Variable::Dhcp::end; 

        packet.Layer5.emplace_back(std::move(dhcp));

        return packet; 
    }

    PacketInfo DhcpClient::dhcpRequest(PacketInfo packet, DhcpHeader& header, const std::string& hostname, ByteString hardwareAddress, ByteString requestedIP, ByteString serverID) 
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

        dhcp.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::type,
            std::string("\x01", 1),
            Variable::Dhcp::Type::request
        });

        dhcp.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::clientID,
            std::string("\x06", 1),
            hardwareAddress
        });

        dhcp.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::serverIdentifier,
            std::string("\x04", 1),
            configs.dhcpServer
        });

        dhcp.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::requestIP,
            std::string("\x04", 1),
            header.yourClientIP
        });

        dhcp.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::leaseTime,
            Functions::numToByte(static_cast<uint32_t>(configs.leaseTime.size())),
            configs.leaseTime
        });

        dhcp.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::hostname,
            Functions::numToByte(static_cast<uint32_t>(hostname.length())),
            hostname
        });

        dhcp.options.emplace_back(DhcpHeader::Option{
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
        });

        dhcp.end = Variable::Dhcp::end; 

        packet.Layer5.emplace_back(std::move(dhcp));

        return packet; 
    }

    // Creates a DHCP Release packet to release the leased IP address
    PacketInfo DhcpClient::dhcpRelease(PacketInfo packet, const ByteString& hardwareAddress)
    {
        DhcpHeader dhcp;

        if (!currentInterface->Get()) return packet;

        dhcp.boot = Variable::Dhcp::Type::release;
        dhcp.hardwareType = std::string("\x01", 1);
        dhcp.hardwareAddressLength = std::string("\x06", 1);
        dhcp.hops = std::string("\x00", 1);
        dhcp.transID = generateDhcpTransid();
        dhcp.secondsElapsed = std::string("\x00\x00", 2);
        dhcp.bootpFlags.broadcast = "0";
        dhcp.bootpFlags.reserved = "000000000000000"; 
        dhcp.clientIP = std::string("\x00\x00\x00\x00", 4);
        dhcp.yourClientIP = currentInterface->Get()->ipv4.ipAddress; 
        dhcp.nextServerIP = std::string("\x00\x00\x00\x00", 4);
        dhcp.relayAgentIP = Variable::IPv4::source; 
        dhcp.clientMacAddress = hardwareAddress; 
        dhcp.clientHardwareAddressPadding = Variable::Dhcp::clientHardwareAddressPadding; 
        dhcp.serverHostName = Variable::Dhcp::serverHostName; 
        dhcp.bootFile = Variable::Dhcp::bootfile; 
        dhcp.magicCookie = Variable::Dhcp::magicCookie; 
        
        dhcp.options.reserve(2);

        dhcp.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::type,
            std::string("\x01", 1),
            Variable::Dhcp::Type::release
        });

        dhcp.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::clientID,
            std::string("\x06", 1),
            hardwareAddress
        });

        dhcp.end = Variable::Dhcp::end;

        packet.Layer5.emplace_back(std::move(dhcp));

        return packet;
    }

    // Creates a DHCP Inform packet to request local configuration parameters
    PacketInfo DhcpClient::dhcpInform(PacketInfo packet, std::string& hostname, ByteString& hardwareAddress) 
    {
        DhcpHeader dhcp;

        ByteString ipAddr;
        if (currentInterface->Get())
        {
            std::shared_lock<std::shared_mutex> lock(currentInterface->Get()->ipMutex);
            ipAddr = currentInterface->Get()->ipv4.ipAddress;
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

        dhcp.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::type,
            std::string("\x01", 1),
            Variable::Dhcp::Type::inform
        });

        dhcp.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::clientID,
            std::string("\x06", 1),
            hardwareAddress
        });

        dhcp.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::hostname,
            Functions::numToByte(static_cast<uint32_t>(hostname.length())),
            hostname
        });

        dhcp.end = Variable::Dhcp::end; 

        packet.Layer5.emplace_back(std::move(dhcp));

        return packet; 
    }

    // Processes received DHCP responses including ACK, NAK, DECLINE, INFORM
    void DhcpClient::processDhcpResponses(const std::string& hostname, ByteString& hardwareAddress)
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
    void DhcpClient::ExtractOptions(std::vector<DhcpHeader::Option> options) 
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
            else if (opt.option == Variable::Dhcp::Option::domainServer) 
            {
                configs.dnsServer.push_back(opt.value);
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
    void DhcpClient::DhcpPacket(const DhcpHeader* header, ByteString& type)
    {
        //TODO Implementation needed
    }

#pragma endregion
}
