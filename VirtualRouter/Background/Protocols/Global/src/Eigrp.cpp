    #include <Eigrp.h>
    #include <Encapsulation.h>

    #define MAX_RETRANSMISSIONS 5


    // Global mutex for EIGRP operations
    mutex globalEigrpMutex;

    namespace Protocol 
    {

        // EIGRP constructor initializing AS number and starting Hello timer
        Eigrp::Eigrp(int& as) : asNumber(as) 
        {
            UpdateInterfaceList();
            UpdateRoutingTableForConnected();
        }
        
        // EIGRP destructor stopping all timers
        Eigrp::~Eigrp() 
        {
            networks.clear();
            for (auto& eigrpInterface : eigrpInterfaceList) {
                eigrpInterface.second->StopHello();
                eigrpInterface.second->StopStuckInActive();
            }
        }
        
        // Configures EIGRP Hello header fields
        void Eigrp::EigrpHello(eigrpHeader& eigrp, string virtualRouterID, EigrpInterface* eigrpInt, bool ack, bool update, int sequenceNum)
        {
            eigrp.version = function->hexToByte("02");
            eigrp.opcode = variable.eigrp.type.hello;
            eigrp.checksum = function->hexToByte("0000");
            eigrp.flags.init = "0";
            eigrp.flags.conditionalRecieve = "0";
            eigrp.flags.restart = "0";
            eigrp.flags.endOfTable = "0";
            eigrp.sequence = function->hexToByte("00000000");
            eigrp.ack = ack ? function->hexToByte(function->changeSize(function->intToHex(sequenceNum), 8)) : function->hexToByte("00000000");
            eigrp.virtualRouterID = function->hexToByte(virtualRouterID);
            eigrp.autonomousSystem = function->hexToByte(function->changeSize(function->intToHex(asNumber), 4));

            // Check for acknowlegement
            if (!ack)
            {
                int optionCount = 2 + (update ? 2 : 0);
                eigrp.options.resize(optionCount);
            
                // Set option 0 for K-values and hold time
                eigrp.options[0].option = variable.eigrp.options.parameter;
                eigrp.options[0].length = function->hexToByte("000c");
                eigrp.options[0].value = CalculateParameters(eigrpInt->holdTime);
            
                // Set option 1 for version information
                eigrp.options[1].option = variable.eigrp.options.version;
                eigrp.options[1].length = function->hexToByte("0008");
                eigrp.options[1].value = variable.eigrp.version.release + variable.eigrp.version.tls;

                if (update)
                {

                    string ip;
                    eigrp.options[2].option = variable.eigrp.options.sequence;
                    eigrp.options[2].value = function->intToByte(ip.size()) + function->hexToByte("");
                    eigrp.options[2].length = function->intToByte(eigrp.options[2].option.size() + eigrp.options[2].value.size() + 2);

                    // Set option 3 for next multicast sequence
                    eigrp.options[3].option = variable.eigrp.options.multicastSequence;
                    eigrp.options[3].length = function->hexToByte("0008");
                    string num = function->intToHex(sequenceNum);
                    while (num.size() < 8) 
                    {
                        num = "0" + num;
                    }
                    eigrp.options[3].value = function->hexToByte(num);
                }
            }
        }

        // Configures EIGRP update packet with specific settings
        void Eigrp::EigrpUpdate(eigrpHeader& eigrp, string virtualRouterID, int sequenceNum, vector<EigrpConfigs::NetworksDistributed>& internalRoutes, bool init, bool conditional, bool restart, bool endoftable, bool query, bool reply) {
            eigrp.version = function->hexToByte("02");
            if (reply)
            {
                eigrp.opcode = variable.eigrp.type.reply;
            }
            else if (query)
            {
                eigrp.opcode = variable.eigrp.type.query;
            }
            else
            {
                eigrp.opcode = variable.eigrp.type.update;
            }
            eigrp.checksum = function->hexToByte("0000"); // will be calculated later
            eigrp.flags.init = init ? "1" : "0";
            eigrp.flags.conditionalRecieve = conditional ? "1" : "0";
            eigrp.flags.restart = restart ? "1" : "0";
            eigrp.flags.endOfTable = endoftable ? "1" : "0";
            eigrp.sequence = function->hexToByte(function->changeSize(function->intToHex(sequenceNum), 8));
            eigrp.ack = function->hexToByte("00000000");
            eigrp.virtualRouterID = function->hexToByte(virtualRouterID);
            eigrp.autonomousSystem = function->hexToByte(function->changeSize(function->intToHex(asNumber), 4));

            eigrp.options.clear();

            for (EigrpConfigs::NetworksDistributed intRoute : internalRoutes) {   
                eigrpHeader::Option option;
                option.option = variable.eigrp.options.internalRoute;
                
                // Build the value
                std::string value;
                value += function->hexToByte(intRoute.route.nextHop); // Next Hop
                value += function->hexToByte(function->changeSize(function->intToHex(kvalue.k1_Bandwidth * intRoute.route.bandwidth), 8)); // Scaled Delay
                value += function->hexToByte(function->changeSize(function->intToHex(kvalue.k3_Delay * intRoute.route.delay), 8)); // Scaled Bandwidth
                value += function->hexToByte(function->changeSize(function->intToHex(kvalue.k5_MTU), 6)); // MTU
                value += function->hexToByte(function->changeSize(function->intToHex(intRoute.route.hopCount), 2)); // Hop Count
                value += function->intToByte(kvalue.k4_Reliability); // Reliability
                value += function->intToByte(kvalue.k2_Load); // Load
                value += function->intToByte(intRoute.route.routeTag); // Route Tag
                value += function->hexToByte("00"); // Flags
                value += function->intToByte(intRoute.route.mask); // Prefix Length
                value += function->hexToByte(intRoute.route.network); // Destination

                option.value = value;
                
                // Option length = Option Type (2 bytes) + Length (2 bytes) + Value
                int optionLength = 2 + 2 + value.size();
                option.length = function->hexToByte(function->changeSize(function->intToHex(optionLength), 4));
                
                eigrp.options.push_back(option);
            }
        }
        
        // Tests if an IP address matches any of the configured networks
        bool Eigrp::TestAddress(const std::string& testIp) 
        {
            if (testIp.empty()) { return false; }

            auto hexToUint32 = [](const std::string& hexStr) -> uint32_t 
            {
                uint32_t result = 0;
                std::stringstream ss(hexStr);
                ss >> std::hex >> result;
                return result;
            };

            uint32_t testIpInt = hexToUint32(testIp);

            for (const auto& network : networks) {
                uint32_t ip = hexToUint32(network.ip);
                uint32_t wildcardMask = hexToUint32(network.mask);
                uint32_t ipMasked = ip & ~wildcardMask;
                uint32_t testIpMasked = testIpInt & ~wildcardMask;

                if (ipMasked == testIpMasked) {
                    return true;  // Match found
                }
            }

            return false;  // No matches found
        }

        // Calculate EIGRP metric
        double Eigrp::CalculateMetric(EigrpConfigs::KValue k, int bandwidth, int load, int delay, int reliability) {
            // Ensure bandwidth is on kilobitz per second
            int tempBandwidth = 1;

            int scaledBandwidth;

            if (bandwidth == 0) 
            {
                return std::numeric_limits<double>::infinity();
            }
            else
            {
                scaledBandwidth = (10000000 / bandwidth) * 256;
            }

            int scaledDelay = (delay / 10) * 256;
        
            int metric = 0;
            if (k.k5_MTU == 0)
            {
                metric = ((k.k1_Bandwidth * scaledBandwidth) +
                        (k.k2_Load * load) +
                        (k.k3_Delay * scaledDelay)) / 256;
            }
            else
            {
                metric = (((k.k1_Bandwidth * scaledBandwidth) +
                        (k.k2_Load * load)) + k.k5_MTU) / ((reliability + k.k4_Reliability) * 256);
            }

            return metric;
        }
        
        // Updates the EIGRP interface list based on configured networks
        void Eigrp::UpdateInterfaceList()
        {
            lock_guard<mutex> lock(globalEigrpMutex);

            static std::chrono::steady_clock::time_point lastUpdate = std::chrono::steady_clock::now() - std::chrono::seconds(10);
            auto now = std::chrono::steady_clock::now();

            // if the last update was less then 1 second ago, skip this update
            if (std::chrono::duration_cast<std::chrono::seconds>(now - lastUpdate).count() < 1)
            {
                return;
            }

            for (const auto& outer : InterfaceList)
            {
                for (const auto& interface : outer.second)
                {
                    if (TestAddress(interface.second->ipAddress))
                    {
                        // Add interface to eigrp
                        if (interface.second->eigrpInterfaceList.find(asNumber) == interface.second->eigrpInterfaceList.end() && eigrpInterfaceList.find(interface.second->id) == eigrpInterfaceList.end())
                        {
                            std::shared_ptr<EigrpInterface> instance = std::make_shared<EigrpInterface>(*this, interface.second);
                            eigrpInterfaceList[interface.second->id] = instance;
                            interface.second->eigrpInterfaceList[asNumber] = instance;
                        }
                    }
                    else
                    {
                        // Check and remove interface from eigrp
                        if (interface.second->eigrpInterfaceList.count(asNumber) > 0)
                        {
                            eigrpInterfaceList[interface.second->id]->StopHello();
                            eigrpInterfaceList.erase(interface.second->id);
                            interface.second->eigrpInterfaceList.erase(asNumber);
                        }
                    }
                }
            }
        }

        // Calculate Parameters
        string Eigrp::CalculateParameters(int holdTime) 
        {
            string hold = function->intToHex(holdTime);
            while (hold.size() < 4) { hold = "0" + hold; }
            return function->intToByte(kvalue.k1_Bandwidth) + function->intToByte(kvalue.k2_Load) + function->intToByte(kvalue.k3_Delay) + function->intToByte(kvalue.k4_Reliability) + function->intToByte(kvalue.k5_MTU) + function->intToByte(kvalue.k6_Power) + function->hexToByte(hold);
        }

        void Eigrp::UpdateRoutingTableForConnected()
        {
            std::lock_guard<std::mutex> lock(eigrpMutex);
            RoutingTable& routingTable = RoutingTable::getInstance();

            for (const auto [id, eigrpInterfacePtr] : eigrpInterfaceList)
            {
                auto interfacePtr = eigrpInterfacePtr->currentInterface;

                if (interfacePtr && !interfacePtr->ipAddress.empty())
                {
                    // Compute the connected network
                    std::string connectedNetwork = function->computeNetworkAddress(interfacePtr->ipAddress, function->hexMaskToInt(interfacePtr->mask));

                    // Create EIGRP route entry
                    RoutingTable::Eigrp connectedRoute;
                    connectedRoute.network = connectedNetwork;
                    connectedRoute.mask = function->hexMaskToInt(interfacePtr->mask);
                    connectedRoute.nextHop = "00000000"; // indicates directly connected
                    connectedRoute.metric = CalculateMetric(kvalue, interfacePtr->bandwidth, 0, interfacePtr->delay, 255);
                    connectedRoute.routeType = "connected";

                    bool hasChanged = false;

                    {
                        std::lock_guard<std::mutex> lock(eigrpInterfacePtr->advertizedRouteMutex);
                        auto it = eigrpInterfacePtr->advertisedRoutes.find(connectedNetwork);
                        if (it == eigrpInterfacePtr->advertisedRoutes.end() || it->second.metric != connectedRoute.metric)
                        {
                            // Route is new or has changed
                            hasChanged = true;
                            eigrpInterfacePtr->advertisedRoutes[connectedNetwork] = connectedRoute;
                        }
                    }

                    if (hasChanged)
                    {
                        // Insert into Routing Table
                        routingTable.UpdateEigrp(connectedRoute);

                        // Advertise the connected route to eigrp neighbors
                        NotifyRoutingChange(connectedRoute, /*isRemoval*/false);
                        }
                }
                else if (interfacePtr && !interfacePtr->shutdown)
                {
                    // Compute the connected network
                    std::string connectedNetwork = function->computeNetworkAddress(interfacePtr->ipAddress, function->hexMaskToInt(interfacePtr->mask));
                    int mask = function->hexMaskToInt(interfacePtr->mask);

                    // Remove the connected route from the routing table
                    routingTable.RemoveEigrp(connectedNetwork, mask);

                    // Notify neighbors of route removal
                    RoutingTable::Eigrp removedRoute;
                    removedRoute.network = connectedNetwork;
                    removedRoute.mask = mask;
                    removedRoute.nextHop = "00000000";
                    removedRoute.routeType = "connected";

                    NotifyRoutingChange(removedRoute, /*isRemoval*/true);
                }
            }
        }

        void Eigrp::OnInterfaceChange(Interface* interfacePtr)
        {
            std::lock_guard<std::mutex> lock(eigrpMutex);
            RoutingTable& routingTable = RoutingTable::getInstance();

            int mask = function->hexMaskToInt(interfacePtr->mask);

            // Compute the connected network
            std::string connectedNetwork = function->computeNetworkAddress(interfacePtr->ipAddress, mask);

            // Check if the route already exists
            bool routeExists = false;

            auto eigrpTable = routingTable.GetAllEigrpRoutes();

            for (const auto& route : eigrpTable)
            {
                if (route.network == connectedNetwork && route.mask == mask)
                {
                    routeExists = true;
                    break;
                }
            }

            if (!interfacePtr->shutdown)
            {
                if (!routeExists)
                {
                    // Create and insert new connected route
                    RoutingTable::Eigrp connectedRoute;
                    connectedRoute.network = connectedNetwork;
                    connectedRoute.mask = mask;
                    connectedRoute.nextHop = "00000000";
                    connectedRoute.metric = CalculateMetric(kvalue, interfacePtr->bandwidth, 0, interfacePtr->delay, 255);
                    connectedRoute.routeType = "connected";

                    routingTable.UpdateEigrp(connectedRoute);

                    // Advertise to neighbors
                    NotifyRoutingChange(connectedRoute, /*isRemoval*/false);
                }
            }
            else
            {
                if (routeExists)
                {
                    // Remove the connected route from the routing table
                    routingTable.RemoveEigrp(connectedNetwork, mask);

                    // Notify neighbors of route removal
                    RoutingTable::Eigrp removedRoute;
                    removedRoute.network = connectedNetwork;
                    removedRoute.mask = mask;
                    removedRoute.nextHop = "00000000";
                    removedRoute.routeType = "connected";

                    NotifyRoutingChange(removedRoute, /*isRemoval*/true);
                }
            }
        }

        // Update from route change
        void Eigrp::NotifyRoutingChange(const RoutingTable::Eigrp& changeRoute, bool isRemoval = false)
        {
            for (const auto& eigrpInterfacePtr : eigrpInterfaceList)
                {
                std::lock_guard<std::mutex> lock(eigrpInterfacePtr.second->neighborMutex);

                // Iterate over all neighbors
                for (const auto& neighborEntry : eigrpInterfacePtr.second->neighbors) 
                {
                    const auto& neighborIp = neighborEntry.first;
                    const auto& neighbor = neighborEntry.second;

                    //send an Update Packet to the neighbor
                    if (!neighbor.hasMac) { continue;; }
                    if (changeRoute.nextHop != neighborIp)
                    {
                        eigrpInterfacePtr.second->SendUpdateToNeighbor(neighborIp, changeRoute, isRemoval);
                    }
                }
            }
        }

        



















        // Constructor initializing AS number and starting Hello timer
        EigrpInterface::EigrpInterface(Eigrp& eigrpSystem, std::shared_ptr<Interface> interface)
            : currentInterface(interface), 
            eigrpProcess(&eigrpSystem), 
            bandwidth(currentInterface ? currentInterface->bandwidth : 0), 
            helloStartTime(std::chrono::steady_clock::now()),
            topologyTable(std::make_unique<Protocol::TopologyTable>())
        {
            StartHelloHelper();

            // Create Eigrp DistributionList
        }

        // Destructor stopping all timers
        EigrpInterface::~EigrpInterface() {

        }

        // Configures EIGRP body with Ethernet and IPv4 headers
        void EigrpInterface::EigrpBody(ethernetHeader& eth, ipv4Header& ip, string mac)
        {
            eth.sourceMac = function->hexToByte(mac);
            eth.destinationMac = variable.multicast.eigrp.mac;
            eth.type = variable.ethernet.ipv4; 
        
            ip.version = "4";
            ip.headerLength = "5"; 
            ip.serviceField = function->hexToByte("00");
            ip.totalLength = function->hexToByte("0000");
            ip.identification = function->hexToByte("0000");
            ip.fragmentFlag.reserved = "0";
            ip.fragmentFlag.fragment = "0";
            ip.fragmentFlag.moreFragment = "0";
            ip.fragmentFlag.fragment = "0000000000000";
            ip.TTL = function->hexToByte("40");
            ip.protocol = variable.ipv4.eigrp;
            ip.checksum = function->hexToByte("0000");
            ip.sourceAddress = function->hexToByte(currentInterface->ipAddress);
            ip.destinationAddress = variable.multicast.eigrp.address;
        }

        // Process Packet
        void EigrpInterface::ProcessPacket(const eigrpHeader* eigrpPacket, const ipv4Header* ipPacket)
        {
            std::string neighborIp = function->byteToHex(ipPacket->sourceAddress);

            if (eigrpPacket->opcode == variable.eigrp.type.hello)
            {
                if (function->byteToNum(eigrpPacket->ack) == 0) 
                {
                    ProcessHello(eigrpPacket, ipPacket);
                }
                else
                {
                    ProcessAck(eigrpPacket, ipPacket->sourceAddress);
                }
            }
            else if (eigrpPacket->opcode == variable.eigrp.type.update)
            {
                ProcessUpdate(eigrpPacket, function->byteToHex(ipPacket->sourceAddress));
            }
            else if (eigrpPacket->opcode == variable.eigrp.type.reply)
            {
                ProcessReply(eigrpPacket, ipPacket->sourceAddress);
            }
            else if (eigrpPacket->opcode == variable.eigrp.type.query)
            {
                ProcessQuery(eigrpPacket, ipPacket->sourceAddress);
            }
        }

        // Processes EIGRP Hello packet
        void EigrpInterface::ProcessHello(const eigrpHeader* receivedHello, const ipv4Header* recievedIP) 
        {
            if (function->byteToNum(receivedHello->autonomousSystem) != eigrpProcess->asNumber) 
            {
                // Drop the packet - AS number mismatch
                return;
            }

            int recievedHoldTime = holdTime; // Default holdtime

            // Process TLV parameters
            for (eigrpHeader::Option opt : receivedHello->options) {
                if (opt.option == variable.eigrp.options.parameter) {
                    string parameters = eigrpProcess->CalculateParameters(holdTime);
                    
                    // Validate K-values
                    if (opt.value.substr(0, 5) != parameters.substr(0, 5)) {
                        return;
                    }

                    // Extract Holdtime
                    recievedHoldTime = function->byteToNum(opt.value.substr(6, 2));
                }
            }

            std::string neighborIp = function->byteToHex(recievedIP->sourceAddress);

            // Add or update neighbor
            {
                std::lock_guard<std::mutex> lock(neighborMutex);
                auto& neighbor = neighbors[neighborIp];

                bool isNewNeighbor = (neighbor.holdTimerId == 0 || !neighbor.hasMac);

                RoutingTable& routingTable = RoutingTable::getInstance();

                // Get neighbor mac Address
                if (!neighbor.hasMac) {
                    auto mac = routingTable.ArpLookup(neighbor.ipAddress);
                    if (mac.has_value()) { 
                        neighbor.macAddress = mac->mac; 
                        neighbor.hasMac = true;
                    } 
                    else 
                    { 
                        currentInterface->arp->sendRequest(neighborIp); 
                    }
                }

                neighbor.ipAddress = neighborIp;
                neighbor.holdTime = recievedHoldTime;
                neighbor.lastHeard = std::chrono::steady_clock::now();
                neighbor.sequenceNumber = 0;
                neighbor.lastReceivedSequenceNumber = 0;
                neighbor.reliablePackets.clear();
                neighbor.retransmissionTimers.clear();
                neighbor.retransmissions = 0;
                neighbor.srtt = 1.0;
                neighbor.rttvar = 0.5;
                neighbor.rto = 1.5;

                // Cancel existing hold timer
                if (neighbor.holdTimerId != 0)
                {
                    TimeManager::getInstance().CancelTimer(neighbor.holdTimerId);
                    neighbor.holdTimerId = 0;
                }

                // Schedule a new Hold Timer
                StartHoldTimer(neighborIp, neighbor.holdTime);

                // If it's a new neighbor, send a full update
                if (isNewNeighbor && neighbor.hasMac)
                {
                    SendFullUpdateToNeighbor(neighborIp);
                }
            }
        }

        // Processes EIGRP Update Packet
        void EigrpInterface::ProcessUpdate(const eigrpHeader* receivedUpdate, const std::string& neighborIp) 
        {
            // Check flags
            bool initFlag = (receivedUpdate->flags.init == "1");
            bool endOfTableFlag = (receivedUpdate->flags.endOfTable == "1");

            if (initFlag) {
                // Start update sequence
                routeBuffer.clear();
            }

            // Process routes in the update packet
            for (const auto& option : receivedUpdate->options) {
                if (option.option == variable.eigrp.options.internalRoute ||
                    option.option == variable.eigrp.options.externalRoute) 
                {
                    RoutingTable::Eigrp route = DecodeRoute(neighborIp, option.value, (option.option == variable.eigrp.options.externalRoute));
                    routeBuffer.push_back(route);
                }
            }

            if (endOfTableFlag) {
                // End of update sequence
                for (const auto& route : routeBuffer)
                {
                    UpdateRoutingTable(route);
                }
                routeBuffer.clear();
            }

            //send ACK to neighbor
            int sequenceNumber = function->byteToNum(receivedUpdate->sequence);
            SendAckToNeighbor(neighborIp, sequenceNumber);
        }

        // Process Ack
        void EigrpInterface::ProcessAck(const eigrpHeader* recievedAck, const std::string& neighborIp)
        {
            int ackSequenceNumber = function->byteToNum(recievedAck->ack);

            std::lock_guard<std::mutex> lock(neighborMutex);
            auto neighborIt = neighbors.find(neighborIp);
            if (neighborIt != neighbors.end())
            {
                EigrpConfigs::NeighborInfo& neighbor = neighbors[neighborIp];

                // Remove this acknowledged packet
                neighbor.reliablePackets.erase(ackSequenceNumber);

                // Cancel the retransmission timer
                auto timerIt = neighbor.retransmissionTimers.find(ackSequenceNumber);
                if (timerIt != neighbor.retransmissionTimers.end())
                {
                    TimeManager::getInstance().CancelTimer(timerIt->second);
                    neighbor.retransmissionTimers.erase(timerIt);
                }

                // Update RTT estimates
                UpdateRTTEstimate(neighbor, ackSequenceNumber);
            }
        }

        // Process query
        void EigrpInterface::ProcessQuery(const eigrpHeader* receivedQuery, const std::string& neighborIp)
        {
            // Extract routes from the query
            for (const auto& option : receivedQuery->options) 
            {
                if (option.option == variable.eigrp.options.internalRoute ||
                option.option == variable.eigrp.options.externalRoute)
                {
                    RoutingTable::Eigrp route = DecodeRoute(neighborIp, option.value, (option.option == variable.eigrp.options.externalRoute));

                    TopologyTable::TopologyEntry* entry = topologyTable->FindBestRoute(route.network);
                    if (entry && !entry->isActive && route.nextHop != neighborIp)
                    {
                        // Send Reply with our route information
                        SendReplyToNeighbor(neighborIp, route);
                    }
                    else
                    {
                        // Propagate the query to other neighbors
                        for (const auto& neighborEntry : neighbors) 
                        {
                            if (neighborEntry.first != neighborIp && route.nextHop != neighborIp) 
                            {
                                SendQueryToNeighbor(neighborEntry.first, route);
                            }
                        }

                        // Start active timer
                        StartActiveTimer(route.network);
                    }
                }
            }
        }

        // Process reply
        void EigrpInterface::ProcessReply(const eigrpHeader* recievedReply, const std::string& neighborIp)
        {
            // Extract routes from the reply
            for (const auto& option : recievedReply->options)
            {
                if (option.option == variable.eigrp.options.internalRoute ||
                    option.option == variable.eigrp.options.externalRoute)
                {
                    RoutingTable::Eigrp route = DecodeRoute(neighborIp, option.value, (option.option == variable.eigrp.options.externalRoute));

                    // Update the topology table with the recieves route
                    UpdateRoutingTable(route);

                    // Transition route to passive state if all replies recieved
                    topologyTable->MarkRouteAsPassive(route.network, this);
                }
            }
        }

        // Send Ack to neighbors
        void EigrpInterface::SendAckToNeighbor(const std::string& neighborIp, int sequenceNumber)
        {   
            // Create the EIGRP Ack packet
            PacketInfo eigrpAckPacketStructure;
            ethernetHeader eth;
            ipv4Header ip;
            eigrpHeader eigrp;

            // Construct Ethernet and IPv4 headers
            EigrpBody(eth, ip, currentInterface->macAddress);

            // Set the destination MAC and IP to the neighbors
            eth.destinationMac = function->hexToByte(neighbors[neighborIp].macAddress);
            ip.destinationAddress = function->hexToByte(neighbors[neighborIp].ipAddress);

            // Construct EIGRP Ack packet
            eigrpProcess->EigrpHello(eigrp, eigrpProcess->virtualRouterID, this, true, false, sequenceNumber);

            // Assemble the packet
            eigrpAckPacketStructure.Layer2.push_back(eth);
            eigrpAckPacketStructure.Layer3.push_back(ip);
            eigrpAckPacketStructure.Layer3.push_back(eigrp);

            // Convert to raw packet
            string eigrpAckPacket = Encapsulate(eigrpAckPacketStructure);

            // Enqueue for transmission
            currentInterface->packetOutQueue.enqueue(eigrpAckPacket);
        }

        // Send Update to neighbor
        void EigrpInterface::SendUpdateToNeighbor(const std::string& neighborIp, const RoutingTable::Eigrp& route, bool removal)
        {
            // Increment sequence number for reliabile delivery
            EigrpConfigs::NeighborInfo& neighbor = neighbors[neighborIp];

            neighbor.sequenceNumber++;

            // Create the Eigrp Update Packet
            PacketInfo eigrpUpdatePacketStructure;
            ethernetHeader eth;
            ipv4Header ip;
            eigrpHeader eigrp;

            // Construct Ethernet and IPv4 headers
            EigrpBody(eth, ip, currentInterface->macAddress);

            // Set the destination MAC and IP to the neighbor
            eth.destinationMac = function->hexToByte(neighbor.macAddress);
            ip.destinationAddress = function->hexToByte(neighbor.ipAddress);

            RoutingTable::Eigrp routeToSend = route;
            if (removal)
            {
                // Mark the route as unreachable by setting the metric to infinity
                routeToSend.reportedDistance = std::numeric_limits<int>::max();
                routeToSend.metric = std::numeric_limits<int>::max() - routeToSend.reportedDistance;
            }

            // Construct EIGRP Update packet
            vector<EigrpConfigs::NetworksDistributed> routesToSend = { EigrpConfigs::NetworksDistributed{ .route = route } };
            eigrpProcess->EigrpUpdate(eigrp, eigrpProcess->virtualRouterID, neighbor.sequenceNumber, routesToSend, /*init=*/false, /*conditional=*/false, /*restart=*/false, /*endOfTable=*/true);

            // Assemble the packet
            eigrpUpdatePacketStructure.Layer2.push_back(eth);
            eigrpUpdatePacketStructure.Layer3.push_back(ip);
            eigrpUpdatePacketStructure.Layer3.push_back(eigrp);

            // Convert to raw packet string
            string eigrpUpdatePacket = Encapsulate(eigrpUpdatePacketStructure);

            // Enque for transmission
            currentInterface->packetOutQueue.enqueue(eigrpUpdatePacket);

            // Store the packet for possible retransmission (reliable delivery)
            neighbor.reliablePackets[neighbor.sequenceNumber] = eigrpUpdatePacket;
            neighbor.packetSendTimes[neighbor.sequenceNumber] = std::chrono::steady_clock::now();

            // Start a retransmission timer for this packet
            StartRetransmissionTimer(neighborIp, neighbor.sequenceNumber, neighbor.rto);

        }

        // Send full update to neighbor
        void EigrpInterface::SendFullUpdateToNeighbor(const std::string& neighborIp)
        {   
            RoutingTable& routingTable = RoutingTable::getInstance();

            // Increment sequence number for reliabile delivery
            EigrpConfigs::NeighborInfo& neighbor = neighbors[neighborIp];
            neighbor.sequenceNumber++;

            // Create the Eigrp Update Packet
            PacketInfo eigrpUpdatePacketStructure;
            ethernetHeader eth;
            ipv4Header ip;
            eigrpHeader eigrp;

            // Construct Ethernet and IPv4 headers
            EigrpBody(eth, ip, currentInterface->macAddress);

            // Set the destination MAC and IP to the neighbor
            eth.destinationMac = function->hexToByte(neighbor.macAddress);
            ip.destinationAddress = function->hexToByte(neighbor.ipAddress);

            // Get all EIGRP routes from the routing table
            vector<EigrpConfigs::NetworksDistributed> routesToSend;

            auto eigrpTable = routingTable.GetAllEigrpRoutes();

            for (const auto& route : eigrpTable)
            {
                if (route.nextHop != neighborIp) {
                    routesToSend.push_back(EigrpConfigs::NetworksDistributed{ .route = route });
                }
            }

            // Construct EIGRP Update packet
            eigrpProcess->EigrpUpdate(eigrp, eigrpProcess->virtualRouterID, neighbor.sequenceNumber, routesToSend, /*init=*/true, /*conditional=*/false, /*restart=*/false, /*endOfTable=*/true);

            // Assemble the packet
            eigrpUpdatePacketStructure.Layer2.push_back(eth);
            eigrpUpdatePacketStructure.Layer3.push_back(ip);
            eigrpUpdatePacketStructure.Layer3.push_back(eigrp);

            // Convert to raw packet string
            string eigrpUpdatePacket = Encapsulate(eigrpUpdatePacketStructure);

            // Enque for transmission
            currentInterface->packetOutQueue.enqueue(eigrpUpdatePacket);

            // Store the packet for possible retransmission (reliable delivery)
            neighbor.reliablePackets[neighbor.sequenceNumber] = eigrpUpdatePacket;
            neighbor.packetSendTimes[neighbor.sequenceNumber] = std::chrono::steady_clock::now();

            // Start a retransmission timer for this packet
            StartRetransmissionTimer(neighborIp, neighbor.sequenceNumber, neighbor.rto);
        }

        // Send query to neighbor
        void EigrpInterface::SendQueryToNeighbor(const std::string& neighborIp, const RoutingTable::Eigrp& route)
        {
            // Increment sequence number for reliabile delivery
            EigrpConfigs::NeighborInfo& neighbor = neighbors[neighborIp];
            neighbor.sequenceNumber++;

            // Create the Eigrp Update Packet
            PacketInfo eigrpQueryPacketStructure;
            ethernetHeader eth;
            ipv4Header ip;
            eigrpHeader eigrp;

            // Construct Ethernet and IPv4 headers
            EigrpBody(eth, ip, currentInterface->macAddress);

            // Set the destination MAC and IP to the neighbor
            eth.destinationMac = function->hexToByte(neighbor.macAddress);
            ip.destinationAddress = function->hexToByte(neighbor.ipAddress);

            // Construct EIGRP Update packet
            vector<EigrpConfigs::NetworksDistributed> routesToSend = { EigrpConfigs::NetworksDistributed{ .route = route } };
            eigrpProcess->EigrpUpdate(eigrp, eigrpProcess->virtualRouterID, neighbor.sequenceNumber, routesToSend, /*init=*/false, /*conditional=*/false, /*restart=*/false, /*endOfTable=*/true, /*query*/true, /*reply*/false);

            // Assemble the packet
            eigrpQueryPacketStructure.Layer2.push_back(eth);
            eigrpQueryPacketStructure.Layer3.push_back(ip);
            eigrpQueryPacketStructure.Layer3.push_back(eigrp);

            // Convert to raw packet string
            string eigrpQueryPacket = Encapsulate(eigrpQueryPacketStructure);

            // Enque for transmission
            currentInterface->packetOutQueue.enqueue(eigrpQueryPacket);

            // Store the packet for possible retransmission (reliable delivery)
            neighbor.reliablePackets[neighbor.sequenceNumber] = eigrpQueryPacket;
            neighbor.packetSendTimes[neighbor.sequenceNumber] = std::chrono::steady_clock::now();

            // Start a retransmission timer for this packet
            StartRetransmissionTimer(neighborIp, neighbor.sequenceNumber, neighbor.rto);

        }

        // Send reply to neighbor
        void EigrpInterface::SendReplyToNeighbor(const std::string& neighborIp, const RoutingTable::Eigrp& route)
        {
            // Increment sequence number for reliabile delivery
            EigrpConfigs::NeighborInfo& neighbor = neighbors[neighborIp];
            neighbor.sequenceNumber++;

            // Create the Eigrp Update Packet
            PacketInfo eigrpReplyPacketStructure;
            ethernetHeader eth;
            ipv4Header ip;
            eigrpHeader eigrp;

            // Construct Ethernet and IPv4 headers
            EigrpBody(eth, ip, currentInterface->macAddress);

            // Set the destination MAC and IP to the neighbor
            eth.destinationMac = function->hexToByte(neighbor.macAddress);
            ip.destinationAddress = function->hexToByte(neighbor.ipAddress);

            // Construct EIGRP Update packet
            vector<EigrpConfigs::NetworksDistributed> routesToSend = { EigrpConfigs::NetworksDistributed{ .route = route } };
            eigrpProcess->EigrpUpdate(eigrp, eigrpProcess->virtualRouterID, neighbor.sequenceNumber, routesToSend, /*init=*/false, /*conditional=*/false, /*restart=*/false, /*endOfTable=*/true, /*query*/false, /*reply*/true);

            // Assemble the packet
            eigrpReplyPacketStructure.Layer2.push_back(eth);
            eigrpReplyPacketStructure.Layer3.push_back(ip);
            eigrpReplyPacketStructure.Layer3.push_back(eigrp);

            // Convert to raw packet string
            string eigrpReplyPacket = Encapsulate(eigrpReplyPacketStructure);

            // Enque for transmission
            currentInterface->packetOutQueue.enqueue(eigrpReplyPacket);

            // Store the packet for possible retransmission (reliable delivery)
            neighbor.reliablePackets[neighbor.sequenceNumber] = eigrpReplyPacket;
            neighbor.packetSendTimes[neighbor.sequenceNumber] = std::chrono::steady_clock::now();

            // Start a retransmission timer for this packet
            StartRetransmissionTimer(neighborIp, neighbor.sequenceNumber, neighbor.rto);

        }

        void EigrpInterface::StartHelloHelper()
        {
            if (helloTimerActive)
            {
                // Hello timer is already active
                return;
            }
            helloTimerActive = true;

            StartHello();
        }

        // Starts the Hello timer thread
        void EigrpInterface::StartHello() {
            std::lock_guard<std::mutex> lock(helloTimerMutex);

            // Prevent rescheduling if already scheduled
            if (helloTimerId != 0) {
                return;
            }

            if (helloStartTime.time_since_epoch().count() == 0) {
                helloStartTime = std::chrono::steady_clock::now();
            }

            auto nextExpiration = helloStartTime + std::chrono::seconds(helloTime);

            helloTimerId = TimeManager::getInstance().AddTimer(nextExpiration, [this]() {
                PacketInfo eigrpHelloPacketStructure;
                ethernetHeader eth;
                ipv4Header ip; 
                eigrpHeader eigrp;

                EigrpBody(eth, ip, currentInterface->macAddress);
                eigrpProcess->EigrpHello(eigrp, eigrpProcess->virtualRouterID, this);

                eigrpHelloPacketStructure.Layer2.push_back(eth);
                eigrpHelloPacketStructure.Layer3.push_back(ip);
                eigrpHelloPacketStructure.Layer3.push_back(eigrp);

                std::string eigrpHelloPacket = Encapsulate(eigrpHelloPacketStructure);
                currentInterface->packetOutQueue.enqueue(eigrpHelloPacket);

                // Reset and reschedule Hello timer
                {
                    std::lock_guard<std::mutex> lock(helloTimerMutex);
                    helloTimerId = 0; // Clear timer ID after packet is sent
                    helloStartTime = std::chrono::steady_clock::now();
                    helloTimerActive = false;
                }

            StartHello(); // Reschedule
            });
        }
        
        // Stops the Hello timer thread
        void EigrpInterface::StopHello() 
        {
            if (helloTimerId != 0) {
                TimeManager::getInstance().CancelTimer(helloTimerId);
                helloTimerId = 0;
            }
        }
        
        // EIGRP Hold timer thread function
        void EigrpInterface::StartHoldTimer(const std::string& neighborIp, int holdTime)
        {
            EigrpConfigs::NeighborInfo& neighbor = neighbors[neighborIp];

            auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(holdTime);
            neighbor.holdTimerId = TimeManager::getInstance().AddTimer(expirationTime, [this, neighborIp]() {
                HandleHoldTimeExpire(neighborIp);
            });
        }

        // Handles expired hold time
        void EigrpInterface::HandleHoldTimeExpire(const std::string& neighborIp)
        {
            std::lock_guard<std::mutex> lock(neighborMutex);
            auto it = neighbors.find(neighborIp);
            if (it != neighbors.end())
            {
                HandleNeighborDown(neighborIp);
            }
        }
        
        // Starts the Active timer thread
        void EigrpInterface::StartActiveTimer(const std::string& destination) 
        {
            auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(activeTime);

            // Schedule Active timer
            int timerId = TimeManager::getInstance().AddTimer(expirationTime, [this, destination]() {
                HandleActiveTimeExpire(destination);
            });

            TopologyTable::TopologyEntry* entry = topologyTable->FindBestRoute(destination);
            if (entry)
            {
                entry->activeTimerId = timerId;
            }
        }
        
        // Stops the Active timer thread
        void EigrpInterface::HandleActiveTimeExpire(const std::string& destination) 
        {
            // Handle Stuck-In-Active (SIA) condition
            // Take appropriate actions such as resetting neighbor relationships
            // Remove or invalidate the route
            topologyTable->HandleRouteFailure(destination);
        }   
        
        // Starts the Stuck In Active timer thread
        void EigrpInterface::StartStuckInActive() {
            auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(stuckInActiveTime);

            // Schedule Stuck In Active timer
            stuckInActiveTimerId = TimeManager::getInstance().AddTimer(expirationTime, [this]() {
                // Handle stuck in active timer expiration
            });
        }
        
        // Stops the Stuck In Active timer thread
        void EigrpInterface::StopStuckInActive() {
            if (stuckInActiveTimerId != 0) {
                TimeManager::getInstance().CancelTimer(stuckInActiveTimerId);
                stuckInActiveTimerId = 0;
            }
        }

        // Starts a retransmission timer for an update packet
        void EigrpInterface::StartRetransmissionTimer(const std::string& neighborIp, int sequenceNumber, double timeout)
        {
            EigrpConfigs::NeighborInfo& neighbor = neighbors[neighborIp];

            std::lock_guard<std::mutex> lock(retransmissionMutex);

            // Cancel any existing retransmission timers for this sequence number
            if (neighbor.retransmissionTimers.count(sequenceNumber))
            {
                //TimeManager::getInstance().CancelTimer(neighbor.retransmissionTimers[sequenceNumber]);
                neighbor.retransmissionTimers.erase(sequenceNumber);
            }

            // Schedule a retransmission timer
            auto expirationTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(timeout * 1000));
            int timerId = TimeManager::getInstance().AddTimer(expirationTime, [this, neighborIp, sequenceNumber]() {
                HandleRetransmissionTimeout(neighborIp, sequenceNumber);
            });

            neighbor.retransmissionTimers[sequenceNumber] = timerId;
        }

        // Retransmit a packet
        void EigrpInterface::HandleRetransmissionTimeout(const std::string& neighborIp, int sequenceNumber)
        {
            std::lock_guard<std::mutex> lock(neighborMutex);
            EigrpConfigs::NeighborInfo& neighbor = neighbors[neighborIp];

            // Check if the packet is still pending acknowledgment
            auto pktIt = neighbor.reliablePackets.find(sequenceNumber);
            if (pktIt != neighbor.reliablePackets.end())
            {
                // Increment retransmission timeout
                neighbor.retransmissions++;

                // Check if retransmissions exceed a threshhold
                if (neighbor.retransmissions > MAX_RETRANSMISSIONS)
                {
                    // Consider neighbor down or take other action
                    HandleNeighborDown(neighborIp);
                    return;
                }

                // Resend the packet
                const string& packet = neighbor.reliablePackets[sequenceNumber];
                currentInterface->packetOutQueue.enqueue(packet);

                // Double the RTO for exponential backoff
                neighbor.rto = std::min(neighbor.rto * 2, 60.0);

                // Restart the retransmission timer
                StartRetransmissionTimer(neighborIp, sequenceNumber, neighbor.rto);
            }
        }

        // Add EIGRP routing entry
        RoutingTable::Eigrp EigrpInterface::DecodeRoute(string ip, string value, bool external) {
            if (value.size() < 21) {
                // Handle error: insufficient data
                throw std::runtime_error("DecodeRoute: Insufficient data to decode route.");
            }

            // Extract and pad destination
            string destination = value.substr(21);
            while (destination.size() < 4) {
                destination =  destination + function->hexToByte("00");
            }

            // Creating a new route
            RoutingTable::Eigrp route;

            // Decapsulating internal route value
            route.bandwidth = function->byteToNum(value.substr(8, 4));
            route.delay = function->byteToNum(value.substr(4, 4));
            route.hopCount = function->byteToNum(value.substr(15, 1));
            route.load = function->byteToNum(value.substr(17, 1));
            route.mask = function->byteToNum(value.substr(20, 1));
            route.mtu = function->byteToNum(value.substr(12, 3));
            route.network = function->byteToHex(destination);
            route.nextHop = ip;
            route.reliability = function->byteToNum(value.substr(16, 1));
            route.reportedDistance = eigrpProcess->CalculateMetric(eigrpProcess->kvalue, route.bandwidth, route.load, route.delay, route.reliability);
            route.metric = 0;
            route.feasibleDistance = route.reportedDistance + route.metric;
            route.routeType = external ? "external" : "internal";

            return route;
        }

        // Updates EIGRP routing table
        void EigrpInterface::UpdateRoutingTable(const RoutingTable::Eigrp& route)
        {
            // Access the topology table and update it with new routes
            TopologyTable::RouteInfo routeInfo;
            routeInfo.feasibleDistance = route.feasibleDistance;
            routeInfo.reportedDistance = route.reportedDistance;
            routeInfo.nextHop = route.nextHop;
            routeInfo.hopCount = route.hopCount;
            routeInfo.isSuccessor = false;
            routeInfo.isFeasibleSuccessor = false;

            topologyTable->AddOrUpdateRoute(route.network, route.mask, routeInfo, route.nextHop);

            // Apply Duel algorithm to determine the best route
            TopologyTable::TopologyEntry* bestRouteEntry = topologyTable->FindBestRoute(route.network);
            if (bestRouteEntry)
            {
                // Find successor route
                auto successorIt = std::find_if(bestRouteEntry->routesByNeighbor.begin(), bestRouteEntry->routesByNeighbor.end(),
                    [](const auto& pair) { return pair.second.isSuccessor; });
                
                if (successorIt != bestRouteEntry->routesByNeighbor.end())
                {
                    // Update the routing table accordingly
                    RoutingTable& routingTable = RoutingTable::getInstance();

                    RoutingTable::Eigrp newRoute = route;
                    newRoute.nextHop = successorIt->second.nextHop;
                    newRoute.metric = successorIt->second.feasibleDistance;

                    // Update the global routing table
                    routingTable.UpdateEigrp(newRoute);

                    // Notify neighbors about the route change
                    eigrpProcess->NotifyRoutingChange(route, /*isRemoval*/false);
                }
            }
            else
            {
                eigrpProcess->NotifyRoutingChange(route, /*isRemoval*/false);
            }
        }

        // Handles when the neighbor goes down
        void EigrpInterface::HandleNeighborDown(const std::string& neighborIp)
        {
            std::lock_guard<std::mutex> lock(neighborMutex);

            // Remove neighbor from the neighbor table
            EigrpConfigs::NeighborInfo& neighbor = neighbors[neighborIp];

            // Cancel any pending timers
            if (neighbor.holdTimerId != 0)
            {
                TimeManager::getInstance().CancelTimer(neighbor.holdTimerId);
            }
            for (const auto& timerEntry : neighbor.retransmissionTimers) 
            {
                TimeManager::getInstance().CancelTimer(timerEntry.second);
            }

            // for each affected destination, re-run DUEL
            for (auto& [destination, entry] : topologyTable->GetTopologyEntries())
            {
                // Check if the route was learned from the failed neighbor
                if (entry.routesByNeighbor.count(neighborIp))
                {
                    // Remove the roite from the topology table
                    entry.routesByNeighbor.erase(neighborIp);

                    // If no other routes are available, remove the route from the routing table
                    if (entry.routesByNeighbor.empty())
                    {
                        // Remove the route from the routing table
                        RoutingTable& routingTable = RoutingTable::getInstance();
                        routingTable.RemoveEigrp(destination, entry.prefixLength);

                        // Notify neighbors of the trade
                        RoutingTable::Eigrp removedRoute;
                        removedRoute.network = destination;
                        removedRoute.mask = entry.prefixLength;
                        eigrpProcess->NotifyRoutingChange(removedRoute, /*isRemoval*/true);

                        // Remove the entry from the topology table
                        topologyTable->RemoveEntry(destination);
                    }
                    else
                    {
                        // Find new successor and update routing table
                        UpdateRoutingTableForDestination(destination);
                    }
                }
            }

            // Remove neighbor from neighbor map
            neighbors.erase(neighborIp);
        }

        // Update routing table for destination
        void EigrpInterface::UpdateRoutingTableForDestination(const std::string& destination)
        {
            TopologyTable::TopologyEntry* entry = topologyTable->FindBestRoute(destination);
            if (entry)
            {
                // Find the successor route
                auto successorIt = std::find_if(entry->routesByNeighbor.begin(), entry->routesByNeighbor.end(),
                    [](const auto& pair) { return pair.second.isSuccessor; });

                if (successorIt != entry->routesByNeighbor.end())
                {
                    // Update the routing table accordingly
                    RoutingTable& routingTable = RoutingTable::getInstance();

                    RoutingTable::Eigrp newRoute;
                    newRoute.network = destination;
                    newRoute.mask = entry->prefixLength;
                    newRoute.nextHop = successorIt->second.nextHop;
                    newRoute.metric = successorIt->second.feasibleDistance;

                    // Update the global routing table
                    routingTable.UpdateEigrp(newRoute);

                    eigrpProcess->NotifyRoutingChange(newRoute, /*isRemoval*/false);
                }
                else
                {
                    // No successor found, remove the route
                    RoutingTable& routingTable = RoutingTable::getInstance();
                    std::lock_guard<std::mutex> rtLock(routingTable.tableMutex);
                    routingTable.RemoveEigrp(destination, entry->prefixLength);
                }
            }
        }

        void EigrpInterface::UpdateRTTEstimate(EigrpConfigs::NeighborInfo& neighbor, int sequenceNumber)
        {
            // Assume we have stored the send time when the packet was sent
            auto sendTime = neighbor.packetSendTimes[sequenceNumber];
            auto now = std::chrono::steady_clock::now();
            double rttSample = std::chrono::duration<double>(now - sendTime).count();

            // Update srtt and rttvar using standard algorithms
            double alpha = 1.0 / 8.0;
            double beta = 1.0 / 4.0;

            neighbor.rttvar = (1 - beta) * neighbor.rttvar + beta * std::abs(neighbor.srtt - rttSample);
            neighbor.srtt = (1 - alpha) * neighbor.srtt + alpha * rttSample;

            // Update RTO
            neighbor.rto = neighbor.srtt + std::max(0.1, 4 * neighbor.rttvar);
            neighbor.rto = std::clamp(neighbor.rto, 1.0, 60.0);

            // Reset retransmissions count
            neighbor.retransmissions = 0;
        }


























        void TopologyTable::AddOrUpdateRoute(const std::string& destination, int prefixLength, const RouteInfo& routeInfo, const std::string& neighborIp)
        {
            std::lock_guard<std::mutex> lock(tableMutex);
            TopologyEntry& entry = topologyEntries[destination];
            entry.destination = destination;
            entry.prefixLength = prefixLength;
            entry.routesByNeighbor[neighborIp] = routeInfo;

            // Restet Active state if necessary
            entry.isActive = false;

            // Determine successors and feasible successors
            int lowestFeasibleDistance = INT_MAX;
            for (const auto& [neighbor, route] : entry.routesByNeighbor)
            {
                if (route.feasibleDistance < lowestFeasibleDistance) {
                    lowestFeasibleDistance = route.feasibleDistance;
                }
            }

            // Update Successor and feasibile successor flags
            for (auto& [neighbor, route] : entry.routesByNeighbor) {
                if (route.feasibleDistance == lowestFeasibleDistance) {
                    route.isSuccessor = true;
                }
                else
                {
                    route.isSuccessor = false;
                }
                // Feasibility Condition
                if (route.reportedDistance <= lowestFeasibleDistance)
                {
                    route.isFeasibleSuccessor = true;
                }
                else
                {
                    route.isFeasibleSuccessor = false;
                }
            }

            // Notify routing table about new successor
            // this could involve interacting with the RoutingTable
        }

        void TopologyTable::RemoveRoutesFromNeighbor(const std::string& neighborIp)
        {
            std::lock_guard<std::mutex> lock(tableMutex);
            for (auto& [destination, entry] : topologyEntries)
            {
                entry.routesByNeighbor.erase(neighborIp);
                // If no routes remain, remove the entry
                if (entry.routesByNeighbor.empty())
                {
                    topologyEntries.erase(destination);
                }
            }
        }

        TopologyTable::TopologyEntry* TopologyTable::FindBestRoute(const std::string& destination)
        {
            std::lock_guard<std::mutex> lock(tableMutex);
            auto it = topologyEntries.find(destination);
            if (it != topologyEntries.end()) {
                return &it->second;
            }
            return nullptr;
        }

        void TopologyTable::HandleRouteFailure(const std::string& destination)
        {
            std::lock_guard<std::mutex> lock(tableMutex);
            auto it = topologyEntries.find(destination);
            if (it != topologyEntries.end())
            {
                TopologyEntry& entry = it->second;
                entry.isActive = true;

                // Remove current successor
                for (auto& [neighbor, route] : entry.routesByNeighbor) {
                    route.isSuccessor = false;
                }

                // Implement logic to start active and SIA timers if needed
                // This will be integrated with EigrpInterface's timers
            }
        }

        // Marks a route as passive
        void TopologyTable::MarkRouteAsPassive(const std::string& destination, EigrpInterface* eigrp)
        {
            std::lock_guard<std::mutex> lock(tableMutex);
            auto it = topologyEntries.find(destination);
            if (it != topologyEntries.end())
            {
                TopologyEntry& entry = it->second;
                entry.isActive = false;

                // Cancel Active timer if running
                if (entry.activeTimerId != 0)
                {
                    TimeManager::getInstance().CancelTimer(entry.activeTimerId);
                    entry.activeTimerId = 0;
                }
            }
        }

        void TopologyTable::RemoveEntry(const std::string& destination)
        {
            topologyEntries.erase(destination);
        }

    }

    // Global function to update active EIGRP processes for current interface
    void UpdateEigrpInterface(Interface* interface)
    {
        lock_guard<mutex> lock(globalEigrpMutex);
        for (auto& instance : eigrpList)
        {
            if (!instance.second->TestAddress(interface->ipAddress) && !instance.second->networks.empty())
            {
                interface->eigrpInterfaceList.erase(instance.second->asNumber);
                instance.second->eigrpInterfaceList.erase(interface->id);
            }
        }
    }

    Protocol::Eigrp* currentEigrp;
    map<int, std::shared_ptr<Protocol::Eigrp>> eigrpList;