#include <Eigrp.h>
#include <Encapsulation.h>

#define MAX_RETRANSMISSIONS 5
#pragma region Eigrp

// Global mutex for EIGRP operations
mutex globalEigrpMutex;

namespace Protocol
{

    // EIGRP constructor initializing AS number and starting Hello timer
    Eigrp::Eigrp(int &as) : asNumber(as), wideMetric(10000000)
    {
        kvalue.k1_Bandwidth = 1;
        kvalue.k2_Load = 0;
        kvalue.k3_Delay = 1;
        kvalue.k4_Reliability = 0;
        kvalue.k5_MTU = 0;
        kvalue.k6_Power = 0; 

        UpdateInterfaceList();
        UpdateRoutingTableForConnected();
    }

    // EIGRP destructor stopping all timers
    Eigrp::~Eigrp()
    {
        networks.clear();
        for (auto &eigrpInterface : eigrpInterfaceList)
        {
            eigrpInterface.second->StopHello();
            eigrpInterface.second->StopStuckInActive();
        }
    }

    // Configures EIGRP Hello header fields
    void Eigrp::EigrpHello(eigrpHeader &eigrp, EigrpInterface *eigrpInt, bool ack, bool update, int sequenceNumber, string neighborIp)
    {
        eigrp.version = std::string("\x02", 1);
        eigrp.opcode = variable.eigrp.type.hello;
        eigrp.checksum = std::string("\x00\x00", 2);
        eigrp.flags.init = "0";
        eigrp.flags.conditionalRecieve = "0";
        eigrp.flags.restart = "0";
        eigrp.flags.endOfTable = "0";
        eigrp.sequence = std::string("\x00\x00\x00\x00", 4);
        eigrp.ack = ack ? Functions::numToByte(sequenceNumber, 4) : std::string("\x00\x00\x00\x00", 4);
        eigrp.virtualRouterID = virtualRouterID;
        eigrp.autonomousSystem = Functions::numToByte(asNumber, 2);

        // Construct TLVs
        if (!ack)
        {
            // Parameter TLV (K-values and Hold Time)
            eigrpHeader::Option paramTLV;
            paramTLV.option = variable.eigrp.options.parameter;
            paramTLV.value = CalculateParameters(eigrpInt->holdTime);
            paramTLV.length = Functions::numToByte(paramTLV.value.size() + 4, 2);
            eigrp.options.push_back(paramTLV);

            // Version TLV
            eigrpHeader::Option versionTLV;
            versionTLV.option = variable.eigrp.options.version;
            versionTLV.value = variable.eigrp.version.release + variable.eigrp.version.tls;
            versionTLV.length = Functions::numToByte(versionTLV.value.size() + 4, 2);
            eigrp.options.push_back(versionTLV);

            // Sequence TLV
            if (update)
            {
                eigrpHeader::Option sequenceTLV;
                sequenceTLV.option = variable.eigrp.options.sequence;
                sequenceTLV.value = Functions::numToByte(sequenceNumber, 4) + neighborIp;
                sequenceTLV.length = Functions::numToByte(sequenceTLV.value.size() + 4, 2);
                eigrp.options.push_back(sequenceTLV);

                eigrpHeader::Option multicastSeqTLV;
                multicastSeqTLV.option = variable.eigrp.options.multicastSequence;
                multicastSeqTLV.value = Functions::numToByte(sequenceNumber, 4);
                multicastSeqTLV.length = Functions::numToByte(multicastSeqTLV.value.size() + 4, 2);
                eigrp.options.push_back(multicastSeqTLV);
            }
        }
    }

    // Configures EIGRP update packet with specific settings
    void Eigrp::EigrpUpdate(eigrpHeader &eigrp, int sequenceNum, vector<EigrpConfigs::NetworksDistributed> &internalRoutes, bool init, bool conditional, bool restart, bool endoftable, bool query, bool reply)
    {
        eigrp.version = std::string("\x02", 1);
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
        eigrp.checksum = std::string("\x00\x00", 2); // will be calculated later
        eigrp.flags.init = init ? "1" : "0";
        eigrp.flags.conditionalRecieve = conditional ? "1" : "0";
        eigrp.flags.restart = restart ? "1" : "0";
        eigrp.flags.endOfTable = endoftable ? "1" : "0";
        eigrp.sequence = Functions::numToByte(sequenceNum, 4);
        eigrp.ack = std::string("\x00\x00\x00\x00", 4);
        eigrp.virtualRouterID = virtualRouterID;
        eigrp.autonomousSystem = Functions::numToByte(asNumber, 2);

        eigrp.options.clear();

        for (EigrpConfigs::NetworksDistributed intRoute : internalRoutes)
        {
            eigrpHeader::Option option;
            option.option = variable.eigrp.options.internalRoute;

            // Build the value
            std::string value;
            value += intRoute.route.nextHop;                                                              // Next Hop
            value += Functions::numToByte(intRoute.route.delay, 4);                                       // Scaled Bandwidth
            value += Functions::numToByte(intRoute.route.bandwidth, 4);                                   // Scaled Delay
            value += Functions::numToByte(intRoute.route.mtu, 3);                                         // MTU
            value += Functions::numToByte(intRoute.route.hopCount, 1);                                    // Hop Count
            value += Functions::numToByte(intRoute.route.reliability);                                    // Reliability
            value += Functions::numToByte(intRoute.route.load);                                           // Load
            value += Functions::numToByte(intRoute.route.routeTag);                                       // Route Tag
            value += std::string("\x00", 1);                                                              // Flags
            value += Functions::numToByte(intRoute.route.mask);                                           // Prefix Length
            value += Functions::compactNetworkAddress(intRoute.route.network, intRoute.route.mask);       // Destination

            option.value = value;
            option.length = Functions::numToByte(value.size() + 4, 2);

            eigrp.options.push_back(option);
        }
    }

    // Tests if an IP address matches any of the configured networks
    bool Eigrp::TestAddress(const std::string &testIp)
    {
        if (testIp.empty())
        {
            return false;
        }

        auto hexToUint32 = [](const std::string &hexStr) -> uint32_t
        {
            uint32_t result = 0;
            std::stringstream ss(hexStr);
            ss >> std::hex >> result;
            return result;
        };

        uint32_t testIpInt = hexToUint32(Functions::byteToHex(testIp));

        for (const auto &network : networks)
        {
            uint32_t ip = hexToUint32(Functions::byteToHex(network.ip));
            uint32_t wildcardMask = hexToUint32(Functions::byteToHex(network.mask));
            uint32_t ipMasked = ip & ~wildcardMask;
            uint32_t testIpMasked = testIpInt & ~wildcardMask;

            if (ipMasked == testIpMasked)
            {
                return true; // Match found
            }
        }

        return false; // No matches found
    }

    // Calculate EIGRP metric
    double Eigrp::CalculateMetric(int bandwidth, int load, int delay, int reliability, int hopCount)
    {
        if (bandwidth == 0)
        {
            return std::numeric_limits<double>::infinity();
        }

        // Calculate Bandwidth component
        double bandwidthMetric = (wideMetric / bandwidth); // Bandwidth in kbps

        // Calculate Load component (only if K2 is not 0)
        double loadMetric = 0.0;
        if (kvalue.k2_Load != 0)
        {
            loadMetric = (kvalue.k2_Load  * bandwidth) / (256.0 - load);
        }

        // Calculate Delay component
        double delayMetric = (delay / 10.0); // Delay in tens of microseconds

        // Sum the component
        double metric = (kvalue.k1_Bandwidth * bandwidthMetric) + loadMetric + (kvalue.k3_Delay * delayMetric);

        // Apply the scalling factor and reliability
        if (kvalue.k5_MTU != 0) // Assuming K5 is used as scaling factor
        {
            metric *= (static_cast<double>(kvalue.k5_MTU) / (reliability + kvalue.k4_Reliability));
        }
        
        // Endure metric does not exceed meximum EIGRP value
        return std::min(metric, 4294967295.0);
    }

    // Updates the EIGRP interface list based on configured networks
    void Eigrp::UpdateInterfaceList()
        {
        std::lock_guard<std::mutex> lock(globalEigrpMutex);

        // Iterate through all interfaces
        for (const auto& outer : InterfaceList)
        {
            for (const auto& interface : outer.second)
            {
                if (TestAddress(interface.second->Get().ip))
               {
                    // Add interface to eigrp
                    if (interface.second->eigrpInterfaceList.find(asNumber) == interface.second->eigrpInterfaceList.end() && 
                        eigrpInterfaceList.find(interface.second->Get().id) == eigrpInterfaceList.end())
                    {
                        std::shared_ptr<EigrpInterface> instance = std::make_shared<EigrpInterface>(*this, interface.second);
                        eigrpInterfaceList[interface.second->Get().id] = instance;
                        interface.second->eigrpInterfaceList[asNumber] = instance;
                    }
                }
                else
                {
                    // Check and remove interface from eigrp
                    if (interface.second->eigrpInterfaceList.count(asNumber) > 0)
                    {
                        eigrpInterfaceList[interface.second->Get().id]->StopHello();
                        eigrpInterfaceList.erase(interface.second->Get().id);
                        interface.second->eigrpInterfaceList.erase(asNumber);
                    }
                }
            }
        }
    }

    // Calculate Parameters
    string Eigrp::CalculateParameters(int holdTime)
    {
        string hold = Functions::numToByte(holdTime, 2);
        return Functions::numToByte(kvalue.k1_Bandwidth) + 
            Functions::numToByte(kvalue.k2_Load) + 
            Functions::numToByte(kvalue.k3_Delay) + 
            Functions::numToByte(kvalue.k4_Reliability) + 
            Functions::numToByte(kvalue.k5_MTU) + 
            Functions::numToByte(kvalue.k6_Power) + 
            hold;
    }

    void Eigrp::UpdateRoutingTableForConnected()
    {

        std::lock_guard<std::mutex> lock(eigrpMutex);
        RoutingTable &routingTable = RoutingTable::getInstance();

        for (const auto& [id, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            auto interfacePtr = eigrpInterfacePtr->currentInterface;
            ipInfo interfaceInfo = interfacePtr->Get();
            int eigrpBw;
            {
                std::lock_guard<std::mutex> lock(eigrpInterfacePtr->eigrpMutex);
                eigrpBw = eigrpInterfacePtr->bandwidth;
            }

            if (interfacePtr && !interfacePtr->Get().ip.empty())
            {
                // Compute the connected network
                std::string connectedNetwork = Functions::computeNetworkAddress(interfaceInfo.ip, Functions::byteMaskToNum(interfaceInfo.subnet));

                // Create EIGRP route entry
                RoutingTable::Eigrp connectedRoute;
                    connectedRoute.bandwidth = (10000000 / eigrpBw);
                    connectedRoute.delay = (interfaceInfo.delay / 10);
                    connectedRoute.hopCount = 0;
                    connectedRoute.mtu = interfaceInfo.mtu;
                    connectedRoute.reliability = 255;
                    connectedRoute.load = eigrpInterfacePtr->variance;
                    connectedRoute.network = connectedNetwork;
                    connectedRoute.mask = Functions::byteMaskToNum(interfaceInfo.subnet);
                    connectedRoute.nextHop = std::string("\x00\x00\x00\x00", 4); // indicates directly connected
                    connectedRoute.metric = CalculateMetric(eigrpBw, 0, interfaceInfo.delay, 255);
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
                    NotifyRoutingChange(connectedRoute, /*isRemoval*/ false);
                }
            }
            else if (interfacePtr && !interfacePtr->shutdown)
            {
                // Compute the connected network
                std::string connectedNetwork = Functions::computeNetworkAddress(interfaceInfo.ip, Functions::byteMaskToNum(interfaceInfo.ip));
                int mask = Functions::byteMaskToNum(interfaceInfo.subnet);

                // Remove the connected route from the routing table
                routingTable.RemoveEigrp(connectedNetwork, mask);

                // Notify neighbors of route removal
                RoutingTable::Eigrp removedRoute;
                removedRoute.network = connectedNetwork;
                removedRoute.mask = mask;
                removedRoute.nextHop = std::string("\x00\x00\x00\x00", 4);
                removedRoute.routeType = "connected";

                NotifyRoutingChange(removedRoute, /*isRemoval*/ true);
            }
        }
    }

    void Eigrp::OnInterfaceChange(Interface *interfacePtr)
    {
        ipInfo interfaceInfo = interfacePtr->Get();
        std::lock_guard<std::mutex> lock(eigrpMutex);
        RoutingTable &routingTable = RoutingTable::getInstance();

        int mask = Functions::byteMaskToNum(interfaceInfo.subnet);

        // Compute the connected network
        std::string connectedNetwork = Functions::computeNetworkAddress(interfaceInfo.ip, mask);

        // Check if the route already exists
        bool routeExists = false;

        auto eigrpTable = routingTable.GetAllEigrpRoutes();

        for (const auto &route : eigrpTable)
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
                    connectedRoute.bandwidth = ( 10000000 / interfaceInfo.bandwidth );
                    connectedRoute.delay = (interfaceInfo.delay / 10);
                    connectedRoute.hopCount = 1;
                    connectedRoute.mtu = interfaceInfo.mtu;
                    connectedRoute.reliability = 0;
                    connectedRoute.load = interfacePtr->eigrpInterfaceList[asNumber]->variance;
                    connectedRoute.network = connectedNetwork;
                    connectedRoute.mask = Functions::byteMaskToNum(interfaceInfo.subnet);
                    connectedRoute.nextHop = std::string("\x00\x00\x00\x00", 4); // indicates directly connected
                    connectedRoute.metric = CalculateMetric(interfaceInfo.bandwidth, 0, interfaceInfo.delay, 255);
                    connectedRoute.routeType = "connected";

                routingTable.UpdateEigrp(connectedRoute);

                // Advertise to neighbors
                NotifyRoutingChange(connectedRoute, /*isRemoval*/ false);
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
                removedRoute.nextHop = std::string("\x00\x00\x00\x00", 4);
                removedRoute.routeType = "connected";

                NotifyRoutingChange(removedRoute, /*isRemoval*/ true);
            }
        }
    }

    // Update from route change
    void Eigrp::NotifyRoutingChange(const RoutingTable::Eigrp &changeRoute, bool isRemoval, bool init)
    {
        for (const auto &eigrpInterfacePtr : eigrpInterfaceList)
        {
            vector<std::shared_ptr<EigrpConfigs::NeighborInfo>> neighborsCopy;
            {
                std::lock_guard<std::mutex> lock(eigrpInterfacePtr.second->neighborMutex);
                for (const auto& neighborEntry : eigrpInterfacePtr.second->neighbors)
                {
                    neighborsCopy.emplace_back(neighborEntry.second);
                }
            }

            // Iterate over all neighbors
            for (const auto &neighborEntry : neighborsCopy)
            {
                // send an Update Packet to the neighbor
                {
                    std::lock_guard<std::mutex> lock(neighborEntry->macMutex);
                    if (!neighborEntry->hasMac || neighborEntry->macAddress.empty())
                    {
                        auto mac = RoutingTable::getInstance().ArpLookup(neighborEntry->ipAddress);
                        if (mac.has_value())
                        {
                            neighborEntry->macAddress = mac->mac;
                            neighborEntry->hasMac = true;
                        }
                        else
                        {
                            // Initialize ARP request and skip sending the update until MAC is resolved
                            eigrpInterfacePtr.second->currentInterface->arp->sendRequest(neighborEntry->ipAddress);
                            continue;
                        }
                    }
                } 

                const auto &neighborIp = neighborEntry->ipAddress;
                bool shouldSendRemoval = isRemoval;

                if (init) 
                {
                    if (changeRoute.nextHop == neighborEntry->ipAddress)
                    {
                        eigrpInterfacePtr.second->SendUpdateToNeighbor(neighborIp, changeRoute, false);
                    }
                }
                else 
                {
                    shouldSendRemoval = (changeRoute.nextHop == neighborEntry->ipAddress);
                    eigrpInterfacePtr.second->SendUpdateToNeighbor(neighborIp, changeRoute, shouldSendRemoval);
                }
            }
        }
    }

#pragma endregion

#pragma region EigrpInterface

    // Constructor initializing AS number and starting Hello timer
    EigrpInterface::EigrpInterface(Eigrp &eigrpSystem, std::shared_ptr<Interface> interface)
        : currentInterface(interface),
          eigrpProcess(&eigrpSystem),
          helloStartTime(std::chrono::steady_clock::now()),
          topologyTable(std::make_unique<Protocol::TopologyTable>())
    {
        bandwidth = currentInterface->Get().bandwidth;
        StartHelloHelper();

        // Create Eigrp DistributionList
    }

    // Destructor stopping all timers
    EigrpInterface::~EigrpInterface()
    {
    }

    // Configures EIGRP body with Ethernet and IPv4 headers
    void EigrpInterface::EigrpBody(ethernetHeader &eth, ipv4Header &ip, string mac)
    {
        eth.sourceMac = mac;
        eth.destinationMac = variable.multicast.eigrp.mac;
        eth.type = variable.ethernet.ipv4;

        ip.version = "4";
        ip.headerLength = "5";
        ip.serviceField = std::string("\x00", 1);
        ip.totalLength = std::string("\x00\x00", 2);
        ip.identification = std::string("\x00\x00", 2);
        ip.fragmentFlag.reserved = "0";
        ip.fragmentFlag.fragment = "0";
        ip.fragmentFlag.moreFragment = "0";
        ip.fragmentFlag.fragment = "0000000000000";
        ip.TTL = std::string("\x02", 1);
        ip.protocol = variable.ipv4.eigrp;
        ip.checksum = std::string("\x00\x00", 2);
        ip.sourceAddress = currentInterface->Get().ip;
        ip.destinationAddress = variable.multicast.eigrp.address;
    }

    // Process Packet
    void EigrpInterface::ProcessPacket(const eigrpHeader *eigrpPacket, const ipv4Header *ipPacket)
    {
        std::string neighborIp = ipPacket->sourceAddress;

        if (eigrpPacket->opcode == variable.eigrp.type.hello)
        {
            if (Functions::byteToNum(eigrpPacket->ack) == 0)
            {
                ProcessHello(eigrpPacket, ipPacket);
            }
            else
            {
                ProcessAck(eigrpPacket->ack, ipPacket->sourceAddress);
            }
        }
        else if (eigrpPacket->opcode == variable.eigrp.type.update)
        {
            ProcessUpdate(eigrpPacket, ipPacket->sourceAddress);
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

    void EigrpInterface::ProcessHello(const eigrpHeader *receivedHello, const ipv4Header *recievedIP)
    {
        std::string neighborIp = recievedIP->sourceAddress;

        // Validate Autonomous System Number
        if (Functions::byteToNum(receivedHello->autonomousSystem) != eigrpProcess->asNumber)
        {
            // Drop the packet - AS number mismatch
            return;
        }

        // Extract Hold Time from options
        int recievedHoldTime = holdTime; // Default holdtime
        for (const auto& opt : receivedHello->options)
        {
            if (opt.option == variable.eigrp.options.parameter)
            {
                string parameters = eigrpProcess->CalculateParameters(holdTime);
                if (opt.value.substr(0, 5) != parameters.substr(0, 5)) return;
                // Extract Holdtime
                recievedHoldTime = Functions::byteToNum(opt.value.substr(6, 2));

                // Check for Peer Termination
                if (parameters.substr(0, 5) == std::string("\xFF\xFF\xFF\xFF\xFF", 5))
                {
                    HandleNeighborDown(recievedIP->sourceAddress);
                    return;
                }
            }
        }

        // Add or update neighbor
        {
            std::lock_guard<std::mutex> lock(neighborMutex);
            if (neighbors.find(neighborIp) == neighbors.end())
            {
                neighbors[neighborIp] = std::make_shared<EigrpConfigs::NeighborInfo>();
            }
            auto neighbor = neighbors[neighborIp];

            bool isNewNeighbor = (neighbor->holdTimerId == 0 || !neighbor->hasMac);

            neighbor->ipAddress = neighborIp;
            neighbor->holdTime = recievedHoldTime;
            neighbor->lastHeard = std::chrono::steady_clock::now();

            // Get neighbor mac Address
            {
                std::lock_guard<std::mutex> lock(neighbor->macMutex);
                if (!neighbor->hasMac || neighbor->macAddress.empty())
                {
                    auto mac = RoutingTable::getInstance().ArpLookup(neighbor->ipAddress);
                    if (mac.has_value())
                    {
                        neighbor->macAddress = mac->mac;
                        neighbor->hasMac = true;
                    }
                    else
                    {
                        currentInterface->arp->sendRequest(neighborIp);
                    }
                }
            }

            // Update neighbor fields and start/renew hold timers
            neighbor->lastReceivedSequenceNumber = 0;
            neighbor->retransmissionCounts.clear();
            neighbor->srtt = 1.0;
            neighbor->rttvar = 0.5;
            neighbor->rto = 1.5;

            // Cancel existing hold timer
            if (neighbor->holdTimerId != 0)
            {
                TimeManager::getInstance().CancelTimer(neighbor->holdTimerId);
                neighbor->holdTimerId = 0;
            }

            // Schedule a new Hold Timer
            StartHoldTimer(neighborIp, neighbor->holdTime);

            // If it's a new neighbor, send a full update
            if (isNewNeighbor && !neighbor->adjacency)
            {
                neighbor->adjacency = true;
                neighbor->sequenceNumber = 1;
                SendHelloPacket(true, neighbor->sequenceNumber, neighborIp);
                SendEmptyUpdateToNeighbor(neighborIp);
            }
            else if (neighbor->adjacency && neighbor->hasMac && !neighbor->isInit && neighbor->receivedInitUpdate && !neighbor->sendInitUpdate)
            {
                SendFullUpdateToNeighbor(neighborIp);
                neighbor->sendInitUpdate = true;
            }
        }
    }

    // Processes EIGRP Update Packet
    void EigrpInterface::ProcessUpdate(const eigrpHeader *receivedUpdate, const std::string &neighborIp)
    {
        auto neighborIt = neighbors.find(neighborIp);
        if (neighborIt == neighbors.end())
        {
            return;
        }
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor = neighbors[neighborIp];

        // Get sequence number
        int receivedSequenceNumber = Functions::byteToNum(receivedUpdate->sequence);

        if (neighbor->lastReceivedSequenceNumber <= neighbor->conditionalReceive && neighbor->lastReceivedSequenceNumber != 0) 
        {
            SendAckToNeighbor(neighborIp, receivedSequenceNumber);
            return; 
        }

        neighbor->lastReceivedSequenceNumber = receivedSequenceNumber;

        // Update flags if needed
        if (receivedUpdate->flags.init == "1") { neighbor->sequenceList[receivedSequenceNumber].init = true; }
        if (receivedUpdate->flags.conditionalRecieve == "1") { neighbor->sequenceList[receivedSequenceNumber].conditionalReceive = true; }
        if (receivedUpdate->flags.restart == "1") { /*HandleNeighborDown(neighborIp);*/ return; } // CAUSING FUCKING PROBLEMS
        if (receivedUpdate->flags.endOfTable == "1") {neighbor->sequenceList[receivedSequenceNumber].endOfTable = true; }

        if (!neighbor->receivedInitUpdate && receivedUpdate->options.empty())
        {
            neighbor->receivedInitUpdate = true;
        }

        // Clear if new sequence number
        if (receivedSequenceNumber != neighbor->lastReceivedSequenceNumber)
        {
            routeBuffer.clear();
        }

        // Process ack if necessary
        if (receivedUpdate->ack != std::string("\x00\x00\x00\x00", 4))
        {
            ProcessAck(receivedUpdate->ack, neighborIp);
        }

        // Process routes in the update packet
        for (const auto &option : receivedUpdate->options)
        {
            if (option.option == variable.eigrp.options.internalRoute ||
                option.option == variable.eigrp.options.externalRoute)
            {
                RoutingTable::Eigrp route = DecodeRoute(neighborIp, option.value, (option.option == variable.eigrp.options.externalRoute));

                // Set Bandwidth if needed
                if (route.hopCount == 0)
                {
                    int newBw = 10000000 / (route.bandwidth/256);
                    if (newBw < currentInterface->Get().bandwidth)
                    {
                        
                    }
                }
                if (route.delay != 0xFFFFFFFF)
                {
                    routeBuffer.push_back(route);
                }
            }
        }

        if (neighbor->sequenceList[receivedSequenceNumber].endOfTable || (!neighbor->sequenceList[receivedSequenceNumber].init && !neighbor->sequenceList[receivedSequenceNumber].endOfTable))
        {
            // End of update sequence
            neighbor->sequenceList.erase(receivedSequenceNumber);
            for (const auto &route : routeBuffer)
            {
                UpdateRoutingTable(route, neighbor->sequenceList[receivedSequenceNumber].init);
            }
            routeBuffer.clear();
        }

        // send ACK to neighbor
        int sequenceNumber = Functions::byteToNum(receivedUpdate->sequence);
        SendAckToNeighbor(neighborIp, sequenceNumber);
    }

    // Process Ack
    void EigrpInterface::ProcessAck(const std::string sequenceNumber, const std::string &neighborIp)
    {
        int ackSequenceNumber = Functions::byteToNum(sequenceNumber);

        std::lock_guard<std::mutex> lock(neighborMutex);
        auto neighborIt = neighbors.find(neighborIp);
        if (neighborIt != neighbors.end())
        {
            if (neighbors.find(neighborIp) == neighbors.end()) { return; }
            std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor = neighbors[neighborIp];

            // Remove this acknowledged packet
            neighbor->reliablePackets.erase(ackSequenceNumber);

            // Cancel the retransmission timer
            auto timerIt = neighbor->retransmissionTimers.find(ackSequenceNumber);
            if (timerIt != neighbor->retransmissionTimers.end())
            {
                TimeManager::getInstance().CancelTimer(timerIt->second);
                neighbor->retransmissionTimers.erase(timerIt);
            }

            // Reset transmission count
            neighbor->retransmissionCounts.clear();

            // Update RTT estimates
            UpdateRTTEstimate(neighbor, ackSequenceNumber);
        }
    }

    // Process query
    void EigrpInterface::ProcessQuery(const eigrpHeader *receivedQuery, const std::string &neighborIp)
    {
        // Extract the destination network network and mask from the Query options
        std::string destination;
        int mask = 0;
        for (const auto& opt : receivedQuery->options)
        {
            if (opt.option == variable.eigrp.options.internalRoute)
            {
                // Assuming the query option contains the destination and mask
                // Adjust the parsing based on your TLV encoding
                // Example: First 4 bytes for destination IP, next byte for mask
                if (opt.value.size() >= 5)
                {
                    destination = opt.value.substr(0, 4);
                    mask = static_cast<int>(opt.value[4]);
                }
                break;
            }
        }

        if (destination.empty() || mask == 0)
        {
            // Invalid Query
            return;
        }

        // Check if this route has a valid route to the destination
        RoutingTable& routingTable = RoutingTable::getInstance();
        auto routeIt = routingTable.GetEigrpRoute(destination, mask);
        bool hasValidRoute = routeIt.has_value();

        if (hasValidRoute)
        {
            // Send a Reply to the quering neighbor with the route information
            SendReplyToNeighbor(neighborIp, *routeIt);
        }
        else
        {
            // Propagate the Query to other neighbors except the originator
            std::lock_guard<std::mutex> lock(neighborMutex);
            for (const auto& otherNeighborEntry : neighbors)
            {
                const std::string& otherNeighborIp = otherNeighborEntry.first;
                if (otherNeighborIp != neighborIp)
                {
                    SendQueryToNeighbor(otherNeighborIp, destination, mask);
                }
            }

            // Start the Active Timers for this destination to handle SIA
            StartActiveTimer(destination, mask);
        }
    }

    // Process reply
    void EigrpInterface::ProcessReply(const eigrpHeader *recievedReply, const std::string &neighborIp)
    {
        // Extract the sequence numbers from the Reply to acknowledge it
        int receivedSequenceNumber = Functions::byteToNum(recievedReply->sequence);
        SendAckToNeighbor(neighborIp, receivedSequenceNumber);

        // Extract the route information from the reply options
        RoutingTable::Eigrp route;
        bool routeFound = false;
        for (const auto& opt : recievedReply->options)
        {
            if (opt.option == variable.eigrp.options.internalRoute)
            {
                // Decode the route from the option value
                try
                {
                    route = DecodeRoute(neighborIp, opt.value, /*external*/false);
                    routeFound = true;
                }
                catch (const std::exception &e)
                {
                    return;
                }
                break;
            }
        }

        if (!routeFound)
        {
            return;
        }

        // Update the routing table with the new route
        UpdateRoutingTable(route, /*init*/false);

        // Cancel the Active Timer for this destination since a Reply was received
        CancelActiveTimer(route.network, route.mask);
    }

    // Send Ack to neighbors
    void EigrpInterface::SendAckToNeighbor(const std::string &neighborIp, int sequenceNumber)
    {
        // Retrieve neighbor information
        if (neighbors.find(neighborIp) == neighbors.end()) { return; }
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor = neighbors[neighborIp];

        if (!neighbor->isInit)
        {
            neighbor->pendingAcks.push_back(sequenceNumber);
            return;
        }
        
        // Create the EIGRP Ack packet
        PacketInfo eigrpAckPacketStructure;
        ethernetHeader eth;
        ipv4Header ip;
        eigrpHeader eigrp;

        // Construct Ethernet and IPv4 headers
        EigrpBody(eth, ip, currentInterface->Get().mac);

        // Set the destination MAC and IP to the neighbors
        {
            eth.destinationMac = neighbor->macAddress;
        }
        ip.destinationAddress = neighbor->ipAddress;

        // Construct EIGRP Ack packet
        eigrpProcess->EigrpHello(eigrp, this, true, false, sequenceNumber);

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
    void EigrpInterface::SendUpdateToNeighbor(const std::string &neighborIp, const RoutingTable::Eigrp &route, bool removal)
    {
        std::lock_guard<std::mutex> lock(neighborMutex);

        if (neighbors.find(neighborIp) == neighbors.end()) { return; }
        auto neighbor = neighbors[neighborIp];

        // Check Removal
        if (neighbor->sequenceNumber < 1)
        {
            return;
        }

        // Incrament sequence number for reliability
        if (neighbor->sequenceNumber >= INT_MAX)
        {
            neighbor->sequenceNumber = 1;
        }
        else
        {
            neighbor->sequenceNumber++;
        }
        int currentSeqNum = neighbor->sequenceNumber;

        // Create the Eigrp Update Packet
        PacketInfo eigrpUpdatePacketStructure;
        ethernetHeader eth;
        ipv4Header ip;
        eigrpHeader eigrp;

        // Construct Ethernet and IPv4 headers
        EigrpBody(eth, ip, currentInterface->Get().mac);
        eth.destinationMac = neighbor->macAddress;
        ip.destinationAddress = neighbor->ipAddress;

        // Construct EIGRP Update packet
        vector<EigrpConfigs::NetworksDistributed> routesToSend = {EigrpConfigs::NetworksDistributed{.route = route}};
        eigrpProcess->EigrpUpdate(eigrp, neighbor->sequenceNumber, routesToSend, /*init=*/false, /*conditional=*/false, /*restart=*/false, /*endOfTable=*/true);

        RoutingTable::Eigrp routeToSend = route;
        if (removal)
        {
            // EIGRP uses 4294967295 (0xFFFFFFFF) as infinity
            RoutingTable::Eigrp removalRoute = route;
            routeToSend.reportedDistance = 0xFFFFFFFF;
            routeToSend.feasibleDistance = 0xFFFFFFFF;
            routeToSend.metric = 0xFFFFFFFF;
            routeToSend.delay = 0xFFFFFFFF;
            // Set other feilds as needed
        }

        // Check for pending acks
        if (!neighbor->pendingAcks.empty())
        {
            int ackNum = neighbor->pendingAcks[0];
            eigrp.ack = Functions::numToByte(ackNum, 4);
            neighbor->pendingAcks.erase(neighbor->pendingAcks.begin(), neighbor->pendingAcks.begin() + 1);
        }

        // Assemble the packet
        eigrpUpdatePacketStructure.Layer2.push_back(eth);
        eigrpUpdatePacketStructure.Layer3.push_back(ip);
        eigrpUpdatePacketStructure.Layer3.push_back(eigrp);

        // Convert to raw packet string
        string eigrpUpdatePacket = Encapsulate(eigrpUpdatePacketStructure);

        // Enque for transmission
        currentInterface->packetOutQueue.enqueue(eigrpUpdatePacket);

        // Store the packet for possible retransmission (reliable delivery)
        neighbor->reliablePackets[neighbor->sequenceNumber] = eigrpUpdatePacket;
        neighbor->packetSendTimes[neighbor->sequenceNumber] = std::chrono::steady_clock::now();

        // Start a retransmission timer for this packet
        StartRetransmissionTimer(neighborIp, neighbor->sequenceNumber, neighbor->rto);
    }

    // Send full update to neighbor
    void EigrpInterface::SendFullUpdateToNeighbor(const std::string &neighborIp)
    {
        RoutingTable &routingTable = RoutingTable::getInstance();
        if (neighbors.find(neighborIp) == neighbors.end()) { return; }
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor = neighbors[neighborIp];

        // Incrament sequence number for reliable delivery
        neighbor->sequenceNumber = 2;

        // Create the Eigrp Update Packet
        PacketInfo eigrpUpdatePacketStructure;
        ethernetHeader eth;
        ipv4Header ip;
        eigrpHeader eigrp;

        // Construct Ethernet and IPv4 headers
        EigrpBody(eth, ip, currentInterface->Get().mac);

        // Get all EIGRP routes from the routing table
        vector<EigrpConfigs::NetworksDistributed> routesToSend;

        auto eigrpTable = routingTable.GetAllEigrpRoutes();

        for (const auto &route : eigrpTable)
        {
            if (!Functions::compareNetworkWithIp(route.network, neighbor->ipAddress))
            {
                routesToSend.push_back(EigrpConfigs::NetworksDistributed{.route = route});
            }
        }

        // Construct EIGRP Update packet
        eigrpProcess->EigrpUpdate(eigrp, neighbor->sequenceNumber, routesToSend, /*init=*/true, /*conditional=*/false, /*restart=*/false, /*endOfTable=*/true);
        neighbor->isInit = true;

        // Check for pending acks
        if (!neighbor->pendingAcks.empty())
        {
            int ackNum = neighbor->pendingAcks[0];
            eigrp.ack = Functions::numToByte(ackNum, 4);
            neighbor->pendingAcks.erase(neighbor->pendingAcks.begin(), neighbor->pendingAcks.begin() + 1);
        }

        // Assemble the packet
        eigrpUpdatePacketStructure.Layer2.push_back(eth);
        eigrpUpdatePacketStructure.Layer3.push_back(ip);
        eigrpUpdatePacketStructure.Layer3.push_back(eigrp);

        // Convert to raw packet string
        string eigrpUpdatePacket = Encapsulate(eigrpUpdatePacketStructure);

        // Enque for transmission
        currentInterface->packetOutQueue.enqueue(eigrpUpdatePacket);

        // Store the packet for possible retransmission (reliable delivery)
        neighbor->reliablePackets[neighbor->sequenceNumber] = eigrpUpdatePacket;
        neighbor->packetSendTimes[neighbor->sequenceNumber] = std::chrono::steady_clock::now();

        // Start a retransmission timer for this packet
        StartRetransmissionTimer(neighborIp, neighbor->sequenceNumber, neighbor->rto);
    }

    void EigrpInterface::SendEmptyUpdateToNeighbor(const std::string &neighborIp)
    {
        if (neighbors.find(neighborIp) == neighbors.end()) { return; }
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor = neighbors[neighborIp];

        // Create the EIGRP Update packet structure
        PacketInfo eigrpUpdatePacketStructure;
        ethernetHeader eth;
        ipv4Header ip;
        eigrpHeader eigrp;

        // Constuct Ethernet and IPv4 headers
        EigrpBody(eth, ip, currentInterface->Get().mac);

        // Check if MAC address exists
        {
            std::lock_guard<std::mutex> lock(neighbor->macMutex);
            if (neighbor->macAddress.empty())
            {
                auto mac = RoutingTable::getInstance().ArpLookup(neighborIp);
                if (mac.has_value())
                {
                    neighbor->macAddress = mac->mac;
                    eth.destinationMac = neighbor->macAddress;
                }
                else
                {
                    currentInterface->arp->sendRequest(neighborIp);
                }
            }
            else
            {
                eth.destinationMac = neighbor->macAddress;
            }
        }

        ip.destinationAddress = neighbor->ipAddress;

        // Inialize an empty vector
        vector<EigrpConfigs::NetworksDistributed> routesToSend{};

        // Construct EIGRP Update packet with no routes
        if (neighbor->receivedInitUpdate)
        {
            eigrpProcess->EigrpUpdate(eigrp, neighbor->sequenceNumber, routesToSend, /*init=*/true, /*conditional=*/false, /*restart=*/false, /*endOfTable=*/false, /*query=*/false, /*reply=*/false);
        }
        else
        {
            bool end = true;
            RoutingTable &routingTable = RoutingTable::getInstance();
            vector<RoutingTable::Eigrp> eigrpTable = routingTable.GetAllEigrpRoutes();
            for (const auto &update : eigrpTable)
            {
                if (!Functions::compareNetworkWithIp(update.network, neighbor->ipAddress))
                {
                    end = false;
                }
            }
            if (end)
            {
                neighbor->isInit = true;
            }
            neighbor->conditionalReceive = neighbor->lastReceivedSequenceNumber;
            eigrpProcess->EigrpUpdate(eigrp, neighbor->sequenceNumber, routesToSend, /*init=*/true, /*conditional=*/true, /*restart=*/false, /*endOfTable=*/end, /*query=*/false, /*reply=*/false);
        }

        // Check for pending acks
        if (!neighbor->pendingAcks.empty())
        {
            int ackNum = neighbor->pendingAcks[0];
            eigrp.ack = Functions::numToByte(ackNum, 4);
            neighbor->pendingAcks.erase(neighbor->pendingAcks.begin(), neighbor->pendingAcks.begin() + 1);
        }

        // Assemble the packet
        eigrpUpdatePacketStructure.Layer2.push_back(eth);
        eigrpUpdatePacketStructure.Layer3.push_back(ip);
        eigrpUpdatePacketStructure.Layer3.push_back(eigrp);

        // Convert to raw packet string
        string eigrpUpdatePacket = Encapsulate(eigrpUpdatePacketStructure);

        // Enque for transmission
        currentInterface->packetOutQueue.enqueue(eigrpUpdatePacket);

        // Store the packet for possible retransmission (reliable delivery)
        neighbor->reliablePackets[neighbor->sequenceNumber] = eigrpUpdatePacket;
        neighbor->packetSendTimes[neighbor->sequenceNumber] = std::chrono::steady_clock::now();

        // Start a retransmission timer for this packet
        StartRetransmissionTimer(neighborIp, neighbor->sequenceNumber, neighbor->rto);
    }

    // Send query to neighbors
    void EigrpInterface::SendQueryToNeighbors(const std::string& failedNeighborIp, const RoutingTable::Eigrp& failedRoute)
    {
        std::lock_guard<std::mutex> lock(neighborMutex);
        for (const auto& neighborEntry : neighbors)
        {
            const std::string& neighborIp = neighborEntry.first;
            if (neighborIp != failedNeighborIp)
            {
                SendQueryToNeighbor(neighborIp, failedRoute.network, failedRoute.mask);
            }
        }

        // Start Active Timer for the failed destination
        StartActiveTimer(failedRoute.network, failedRoute.mask);
    }

    // Send query to neighbor
    void EigrpInterface::SendQueryToNeighbor(const std::string&neighborIp, const std::string& destination, int mask)
    {
        if (neighbors.find(neighborIp) == neighbors.end()) { return; }
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor = neighbors[neighborIp];

        // Increase sequence numbers for reliability
        neighbor->sequenceNumber++;
        int currentSeqNum = neighbor->sequenceNumber;

        // Create the EIGRP Query Packet
        PacketInfo eigrpQueryPacketStructure;
        ethernetHeader eth;
        ipv4Header ip;
        eigrpHeader eigrp;

        // Construct Ethernet and IPv4 headers
        EigrpBody(eth, ip, currentInterface->Get().mac);
        eth.destinationMac = neighbor->macAddress;
        ip.destinationAddress = neighbor->ipAddress;

        // Construct the Query option
        eigrpHeader::Option queryOption;
        queryOption.option = variable.eigrp.options.internalRoute;
        queryOption.value = EncodeQueryOption(destination, mask);
        queryOption.length = Functions::numToByte(queryOption.value.size(), 2);

        // Add the Query option to EIGRP header
        eigrp.options.push_back(queryOption);

        // Set other EIGRP header feilds
        eigrp.version = std::string("\x02", 1);
        eigrp.opcode = variable.eigrp.type.query;
        eigrp.checksum = std::string("\x00\x00", 2); // Will be calculated later
        eigrp.flags.init = "0";
        eigrp.flags.conditionalRecieve = "0";
        eigrp.flags.restart = "0";
        eigrp.flags.endOfTable = "0";
        eigrp.sequence = Functions::numToByte(currentSeqNum, 4);
        eigrp.ack = std::string("\x00\x00\x00\x00", 4);
        eigrp.virtualRouterID = currentEigrp->virtualRouterID;
        eigrp.autonomousSystem = Functions::numToByte(eigrpProcess->asNumber, 2);

        // Assemble the packet
        eigrpQueryPacketStructure.Layer2.push_back(eth);
        eigrpQueryPacketStructure.Layer3.push_back(ip);
        eigrpQueryPacketStructure.Layer3.push_back(eigrp);

        // Convert to raw packet string
        std::string eigrpQueryPacket = Encapsulate(eigrpQueryPacketStructure);

        // Store the packet for possible retransmission (relieable delivery)
        neighbor->reliablePackets[currentSeqNum] = eigrpQueryPacket;
        neighbor->packetSendTimes[currentSeqNum] = std::chrono::steady_clock::now();

        // Start a retransmission timer for this packet
        StartRetransmissionTimer(neighborIp, currentSeqNum, neighbor->rto);
    }

    // Encode query option
    std::string EigrpInterface::EncodeQueryOption(const std::string& destination, int mask)
    {
        std::string encoded;
        encoded += destination;
        encoded += static_cast<char>(mask);
        return encoded;
    }

    // Send reply to neighbor
    void EigrpInterface::SendReplyToNeighbor(const std::string& neighborIp, const RoutingTable::Eigrp& route)
    {
        if (neighbors.find(neighborIp) == neighbors.end()) { return; }
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor = neighbors[neighborIp];

        // Increment sequence number for reliability
        neighbor->sequenceNumber++;
        int currentSeqNum = neighbor->sequenceNumber;

        // Create the EIGRP Reply Packet
        PacketInfo eigrpReplyPacketStructure;
        ethernetHeader eth;
        ipv4Header ip;
        eigrpHeader eigrp;

        // Construct Ethernet and IPv4 and IPv4 headers
        EigrpBody(eth, ip, currentInterface->Get().mac);
        eth.destinationMac = neighbor->macAddress;
        ip.destinationAddress = neighbor->ipAddress;

        // Construct the Reply option with the route information
        eigrpHeader::Option replyOption;
        replyOption.option = variable.eigrp.options.internalRoute;
        replyOption.value = EncodeRouteOption(route);
        replyOption.length = Functions::numToByte(replyOption.value.size(), 2);

        // Add the Reply option to EIGRP header
        eigrp.options.push_back(replyOption);

        // Set other EIGRP header fields
        eigrp.version = std::string("\x02", 1);
        eigrp.opcode = variable.eigrp.type.reply;
        eigrp.checksum = std::string("\x00\x00"); // Will be calculated later
        eigrp.flags.init = "0";
        eigrp.flags.conditionalRecieve = "0";
        eigrp.flags.restart = "0";
        eigrp.flags.endOfTable = "1";
        eigrp.sequence = Functions::numToByte(currentSeqNum, 4);
        eigrp.ack = std::string("\x00\x00\x00\x00");
        eigrp.virtualRouterID = eigrpProcess->virtualRouterID;
        eigrp.autonomousSystem = Functions::numToByte(eigrpProcess->asNumber);

        // Assemble the packet
        eigrpReplyPacketStructure.Layer2.push_back(eth);
        eigrpReplyPacketStructure.Layer3.push_back(ip);
        eigrpReplyPacketStructure.Layer3.push_back(eigrp);

        // Convert to raw packet string
        std::string eigrpReplyPacket = Encapsulate(eigrpReplyPacketStructure);

        // Enqueue for transmission
        currentInterface->packetOutQueue.enqueue(eigrpReplyPacket);

        // Store the packet for possible retransmission (reliable delivery)
        neighbor->reliablePackets[currentSeqNum] = eigrpReplyPacket;
        neighbor->packetSendTimes[currentSeqNum] = std::chrono::steady_clock::now();

        // Start a retransmission timer for this packet
        StartRetransmissionTimer(neighborIp, currentSeqNum, neighbor->rto);
    }

    // Encode reply option
    std::string EigrpInterface::EncodeRouteOption(const RoutingTable::Eigrp& route)
    {
        std::string encoded;
        encoded += route.nextHop;
        encoded += Functions::numToByte(route.delay, 4);
        encoded += Functions::numToByte(route.bandwidth, 4);
        encoded += Functions::numToByte(route.mtu, 3);
        encoded += Functions::numToByte(route.hopCount, 1);
        encoded += Functions::numToByte(route.reliability, 1);
        encoded += Functions::numToByte(route.load, 1);
        encoded += Functions::numToByte(route.routeTag, 4);
        encoded += std::string("\x00", 1);
        encoded += Functions::numToByte(route.mask, 1);
        encoded += Functions::compactNetworkAddress(route.network, route.mask);
        return encoded;
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
    void EigrpInterface::StartHello()
    {
        std::lock_guard<std::mutex> lock(helloTimerMutex);

        // Prevent rescheduling if already scheduled
        if (helloTimerId != 0)
        {
            return;
        }

        if (helloStartTime.time_since_epoch().count() == 0)
        {
            helloStartTime = std::chrono::steady_clock::now();
        }

        auto nextExpiration = helloStartTime + std::chrono::seconds(helloTime);

        helloTimerId = TimeManager::getInstance().AddTimer(nextExpiration, [this]()
        {
            try
            {
                SendHelloPacket();
            }
            catch (const std::exception &e)
            {
                std::cerr << "[StartHello] Exception in SendHelloPacket: " << e.what() << std::endl;
            }
            catch (...)
            {
                std::cerr << "[StartHello] Unknown exception in SendHelloPacket." << std::endl;
                // Handle unknown exceptions
            }

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

    void EigrpInterface::SendHelloPacket(bool update, int sequenceNum, string neighborIp)
    {
        PacketInfo eigrpHelloPacketStructure;
        ethernetHeader eth;
        ipv4Header ip;
        eigrpHeader eigrp;

        EigrpBody(eth, ip, currentInterface->Get().mac);
        eigrpProcess->EigrpHello(eigrp, this, false, update, sequenceNum, neighborIp);

        eigrpHelloPacketStructure.Layer2.push_back(eth);
        eigrpHelloPacketStructure.Layer3.push_back(ip);
        eigrpHelloPacketStructure.Layer3.push_back(eigrp);

        std::string eigrpHelloPacket = Encapsulate(eigrpHelloPacketStructure);
        currentInterface->packetOutQueue.enqueue(eigrpHelloPacket);
    }

    // Stops the Hello timer thread
    void EigrpInterface::StopHello()
    {
        if (helloTimerId != 0)
        {
            TimeManager::getInstance().CancelTimer(helloTimerId);
            helloTimerId = 0;
        }
    }

    // EIGRP Hold timer thread function
    void EigrpInterface::StartHoldTimer(const std::string &neighborIp, int holdTime)
    {
        if (neighbors.find(neighborIp) == neighbors.end()) { return; }
        std::shared_ptr<EigrpConfigs::NeighborInfo> &neighbor = neighbors[neighborIp];

        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(holdTime);
        neighbor->holdTimerId = TimeManager::getInstance().AddTimer(expirationTime, [this, neighborIp]()
                                                                   { HandleHoldTimeExpire(neighborIp); });
    }

    // Handles expired hold time
    void EigrpInterface::HandleHoldTimeExpire(const std::string &neighborIp)
    {
        std::lock_guard<std::mutex> lock(neighborMutex);
        auto it = neighbors.find(neighborIp);
        if (it != neighbors.end())
        {
            HandleNeighborDown(neighborIp);
        }
    }

    // Starts the Active timer thread
    void EigrpInterface::StartActiveTimer(const std::string &destination, int mask)
    {
        auto key = destination + "/" + std::to_string(mask);
        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(activeTime);

        // Schedule Active timer
        int timerId = TimeManager::getInstance().AddTimer(expirationTime, [this, destination, mask]() {
            HandleActiveTimeExpire(destination, mask);
        });

        {
            std::lock_guard<std::mutex> lock(activeTimerMutex);
            activeTimers[key] = timerId;
        }
    }

    // Stops the Active timer thread
    void EigrpInterface::HandleActiveTimeExpire(const std::string &destination, int mask)
    {
        // Remove Active Timer from the map
        {
            std::lock_guard<std::mutex> lock(activeTimerMutex);
            std::string key = destination + "/" + std::to_string(mask);
            activeTimers.erase(key);
        }

        // Remove the route from the routing rable
        RoutingTable &routingTable = RoutingTable::getInstance();
        routingTable.RemoveEigrp(destination, mask);

        // Notify neighbors about the route removal
        RoutingTable::Eigrp removedRoute;
        removedRoute.network = destination;
        removedRoute.mask = mask;
        removedRoute.nextHop = std::string("\x00\x00\x00\x00", 4);
        removedRoute.routeType = "internal";

        eigrpProcess->NotifyRoutingChange(removedRoute, /*isRemove*/true);
    }

    // Cancel active timer
    void EigrpInterface::CancelActiveTimer(const std::string &destination, int mask)
    {
        std::lock_guard<std::mutex> lock(activeTimerMutex);
        std::string key = destination + "/" + std::to_string(mask);
        auto it = activeTimers.find(key);
        if (it != activeTimers.end())
        {
            TimeManager::getInstance().CancelTimer(it->second);
            activeTimers.erase(it);
        }
    }

    // Starts the Stuck In Active timer thread
    void EigrpInterface::StartStuckInActive()
    {
        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(stuckInActiveTime);

        // Schedule Stuck In Active timer
        stuckInActiveTimerId = TimeManager::getInstance().AddTimer(expirationTime, [this]()
        {
            // Handle stuck in active timer expiration
        });
    }

    // Stops the Stuck In Active timer thread
    void EigrpInterface::StopStuckInActive()
    {
        if (stuckInActiveTimerId != 0)
        {
            TimeManager::getInstance().CancelTimer(stuckInActiveTimerId);
            stuckInActiveTimerId = 0;
        }
    }

    // Starts a retransmission timer for an update packet
    void EigrpInterface::StartRetransmissionTimer(const std::string &neighborIp, int sequenceNumber, double timeout)
    {
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor = neighbors[neighborIp];
        std::lock_guard<std::mutex> lock(retransmissionMutex);

        // Cancel any existing retransmission timers for this sequence number
        auto timerIt = neighbor->retransmissionTimers.find(sequenceNumber);
        if (timerIt != neighbor->retransmissionTimers.end())
        {
            TimeManager::getInstance().CancelTimer(neighbor->retransmissionTimers[sequenceNumber]);
            neighbor->retransmissionTimers.erase(sequenceNumber);
        }

        // Schedule a retransmission timer
        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(timeout * 1000));
        int timerId = TimeManager::getInstance().AddTimer(expirationTime, [this, neighborIp, sequenceNumber]() {
            HandleRetransmissionTimeout(neighborIp, sequenceNumber);
        });

        neighbor->retransmissionTimers[sequenceNumber] = timerId;
    }

    // Retransmit a packet
    void EigrpInterface::HandleRetransmissionTimeout(const std::string &neighborIp, int sequenceNumber)
    {
        std::lock_guard<std::mutex> lock(neighborMutex);
        auto neighborIt = neighbors.find(neighborIp);
        if (neighborIt == neighbors.end()) return;

        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor = neighbors[neighborIp];

        // Check if the packet is still pending acknowledgment
        auto pktIt = neighbor->reliablePackets.find(sequenceNumber);
        if (pktIt != neighbor->reliablePackets.end())
        {
            // Increment retransmission timeout
            neighbor->retransmissionCounts[sequenceNumber]++;

            // Check if retransmissions exceed a threshhold
            if (neighbor->retransmissionCounts[sequenceNumber] > MAX_RETRANSMISSIONS)
            {
                // Consider neighbor down or take other action
                HandleNeighborDown(neighborIp);
                return;
            }

            // Resend the packet
            const string &packet = pktIt->second;
            currentInterface->packetOutQueue.enqueue(packet);

            // Implement exponential backoff by doubleing RTO, capped at 60 seconds
            {
                std::lock_guard<std::mutex> dataLock(neighbor->neighborDataMutex);
                neighbor->rto = std::min(neighbor->rto * 2.0, 60.0);
            }

            // Restart the retransmission timer
            StartRetransmissionTimer(neighborIp, sequenceNumber, neighbor->rto);
        }
    }

    // Add EIGRP routing entry
    RoutingTable::Eigrp EigrpInterface::DecodeRoute(string ip, string value, bool external)
    {
        if (value.size() < 21)
        {
            // Handle error: insufficient data
            throw std::runtime_error("DecodeRoute: Insufficient data to decode route.");
        }

        // Creating a new route
        RoutingTable::Eigrp route;

        // Extract fields based on their exact byte position
        route.nextHop = value.substr(0, 4);
        route.delay = Functions::byteToNum(value.substr(4, 4));
        route.bandwidth = Functions::byteToNum(value.substr(8, 4));
        route.mtu = Functions::byteToNum(value.substr(12, 3));
        route.hopCount = Functions::byteToNum(value.substr(15, 1));
        route.reliability = Functions::byteToNum(value.substr(16, 1));
        route.load = Functions::byteToNum(value.substr(17, 1));
        route.mask = Functions::byteToNum(value.substr(20, 1));
        route.routeTag = Functions::byteToNum(value.substr(18, 2));

        // Extract the network address based on the mask
        route.network = value.substr(21);
        while (route.network.size() < 4)
        {
            route.network = route.network + std::string("\x00", 1);
        }

        double neighborRD = eigrpProcess->CalculateMetric(route.bandwidth, route.load, route.delay, route.reliability);

        route.reportedDistance = neighborRD;

        // Calculate FD = Local Link Cost + RD
        double localLinkCost = CalculateLocalLinkCost();
        route.feasibleDistance = localLinkCost + route.reportedDistance;

        // Calculate the composite metric for internal use
        route.metric = route.feasibleDistance;

        route.routeType = external ? "external" : "internal";

        return route;
    }

    // Calculate Local Link Cost (LLC)
    double EigrpInterface::CalculateLocalLinkCost()
    {
        const ipInfo& ifaceInfo = currentInterface->Get();

        // Bandwidth is in kbps
        double bandwidthMetric = (eigrpProcess->wideMetric / ifaceInfo.bandwidth);

        // Delay is in tens of microseconds
        double delayMetric = static_cast<double>(ifaceInfo.delay) / 10.0;

        // Calculate link cost using K-values
        double linkCost = (eigrpProcess->kvalue.k1_Bandwidth * bandwidthMetric) +
                          (eigrpProcess->kvalue.k2_Load * load) / (256.0 - load) +
                          (eigrpProcess->kvalue.k3_Delay * delayMetric);

        // Apply scaling factor and reliability
        linkCost *= (eigrpProcess->kvalue.k5_MTU / (reliability + eigrpProcess->kvalue.k4_Reliability));

        return linkCost;
    }

    // Updates EIGRP routing table
    void EigrpInterface::UpdateRoutingTable(const RoutingTable::Eigrp &route, bool init)
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
        TopologyTable::TopologyEntry *bestRouteEntry = topologyTable->FindBestRoute(route.network);
        if (bestRouteEntry)
        {
            // Find successor route
            auto successorIt = std::find_if(bestRouteEntry->routesByNeighbor.begin(), bestRouteEntry->routesByNeighbor.end(),
                                            [](const auto &pair) { return pair.second.isSuccessor; });

            if (successorIt != bestRouteEntry->routesByNeighbor.end())
            {
                // Update the routing table accordingly
                RoutingTable::Eigrp newRoute = route;
                newRoute.nextHop = successorIt->second.nextHop;
                newRoute.metric = successorIt->second.feasibleDistance;

                // Access the routing table to check existing conditions
                RoutingTable &routingTable = RoutingTable::getInstance();
                auto existingRoute = routingTable.GetEigrpRoute(route.network, route.mask);

                bool routeChange = false;

                if (existingRoute)
                {
                    if (existingRoute->metric != newRoute.metric || existingRoute->nextHop != newRoute.nextHop);
                    {
                        routeChange = true;
                    }
                }
                else
                {
                    routeChange = true;
                }

                if (routeChange) 
                {
                    // Update the global EIGRP table
                    routingTable.UpdateEigrp(newRoute);
                    
                    // Notify neighbors about the route change
                    eigrpProcess->NotifyRoutingChange(newRoute, /*isRemoval*/ false, /*init*/init);
                }
            }
        }
        else
        {
            eigrpProcess->NotifyRoutingChange(route, /*isRemoval*/ false, /*init*/init);
        }
    }

    // Handles when the neighbor goes down
    void EigrpInterface::HandleNeighborDown(const std::string &neighborIp)
    {
        // Remove neighbor from the neighbor table
        if (neighbors.find(neighborIp) == neighbors.end()) { return; }
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor = neighbors[neighborIp];

        // Cancel any pending timers
        if (neighbor->holdTimerId != 0)
        {
            TimeManager::getInstance().CancelTimer(neighbor->holdTimerId);
            neighbor->holdTimerId = 0;
        }
        for (const auto &timerEntry : neighbor->retransmissionTimers)
        {
            TimeManager::getInstance().CancelTimer(timerEntry.second);
        }
        neighbor->retransmissionTimers.clear();
        neighbor->retransmissionCounts.clear();
        neighbor->reliablePackets.clear();
        neighbor->packetSendTimes.clear();
        neighbor->sequenceList.clear();

        // for each affected destination, re-run DUEL
        for (auto &[destination, entry] : topologyTable->GetTopologyEntries())
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
                    RoutingTable &routingTable = RoutingTable::getInstance();
                    routingTable.RemoveEigrp(destination, entry.prefixLength);

                    // Notify neighbors of the trade
                    RoutingTable::Eigrp removedRoute;
                    removedRoute.network = destination;
                    removedRoute.mask = entry.prefixLength;
                    removedRoute.nextHop = std::string("\x00\x00\x00\x00", 4);
                    removedRoute.routeType = "internal";

                    eigrpProcess->NotifyRoutingChange(removedRoute, /*isRemoval*/ true);

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
    void EigrpInterface::UpdateRoutingTableForDestination(const std::string &destination)
    {
        TopologyTable::TopologyEntry *entry = topologyTable->FindBestRoute(destination);
        if (entry)
        {
            // Find the successor route
            auto successorIt = std::find_if(entry->routesByNeighbor.begin(), entry->routesByNeighbor.end(),
                                            [](const auto &pair)
                                            { return pair.second.isSuccessor; });

            if (successorIt != entry->routesByNeighbor.end())
            {
                // Update the routing table accordingly
                RoutingTable &routingTable = RoutingTable::getInstance();

                RoutingTable::Eigrp newRoute;
                newRoute.network = destination;
                newRoute.mask = entry->prefixLength;
                newRoute.nextHop = successorIt->second.nextHop;
                newRoute.metric = successorIt->second.feasibleDistance;

                // Update the global routing table
                routingTable.UpdateEigrp(newRoute);

                eigrpProcess->NotifyRoutingChange(newRoute, /*isRemoval*/ false);
            }
            else
            {
                // No successor found, remove the route
                RoutingTable &routingTable = RoutingTable::getInstance();
                std::lock_guard<std::mutex> rtLock(routingTable.tableMutex);
                routingTable.RemoveEigrp(destination, entry->prefixLength);
            }
        }
    }

    double EigrpInterface::CalculateRTT(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor, int sequenceNumber)
    {
        auto sendTimeIt = neighbor->packetSendTimes.find(sequenceNumber);
        if (sendTimeIt == neighbor->packetSendTimes.end())
        {
            return neighbor->srtt;
        }

        auto sendTime = sendTimeIt->second;
        auto now = std::chrono::steady_clock::now();
        double rttSample = std::chrono::duration<double>(now - sendTime).count();

        // Validate RTT sample
        if (rttSample <= 0.0 || rttSample > 60.0)
        {
            return neighbor->srtt;
        }

        return rttSample;
    }

    void EigrpInterface::UpdateRTTEstimate(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor, int sequenceNumber)
    {
        double rttSample = CalculateRTT(neighbor, sequenceNumber);

        // Update srtt and rttvar using standard algorithms
        double alpha = 1.0 / 8.0;
        double beta = 1.0 / 4.0;

        {
            std::lock_guard<std::mutex> lock(neighbor->neighborDataMutex);
            neighbor->rttvar - (1.0 - beta) * neighbor->rttvar + beta * std::abs(neighbor->srtt - rttSample);
            neighbor->srtt = (1.0 - alpha) * neighbor->srtt + alpha * rttSample;

            // Update RTO
            neighbor->rto = neighbor->srtt + std::max(0.1, 4.0 * neighbor->rttvar);
            neighbor->rto = std::clamp(neighbor->rto, 1.0, 60.0);
        }

        // Reset transmission count
        {
            std::lock_guard<std::mutex> lock(neighbor->retransmissionMutex);
            neighbor->retransmissionCounts.erase(sequenceNumber);

        }
    }

#pragma endregion

#pragma region Topology

    void TopologyTable::AddOrUpdateRoute(const std::string &destination, int prefixLength, const RouteInfo &routeInfo, const std::string &neighborIp)
    {
        std::lock_guard<std::mutex> lock(tableMutex);
        TopologyEntry &entry = topologyEntries[destination];
        entry.destination = destination;
        entry.prefixLength = prefixLength;
        entry.routesByNeighbor[neighborIp] = routeInfo;

        // Restet Active state if necessary
        entry.isActive = false;

        // Determine successors and feasible successors
        double bestFeasibleDistance = std::numeric_limits<double>::infinity();
        for (const auto &[neighbor, route] : entry.routesByNeighbor)
        {
            if (route.feasibleDistance < bestFeasibleDistance)
            {
                bestFeasibleDistance = route.feasibleDistance;
            }
        }

        // Update Successor and feasibile successor flags
        for (auto &[neighbor, route] : entry.routesByNeighbor)
        {
            if (route.feasibleDistance == bestFeasibleDistance)
            {
                route.isSuccessor = true;
            }
            else
            {
                route.isSuccessor = false;
            }
            // Feasibility Condition
            if (route.reportedDistance <= bestFeasibleDistance)
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

    void TopologyTable::RemoveRoutesFromNeighbor(const std::string &neighborIp)
    {
        std::lock_guard<std::mutex> lock(tableMutex);
        for (auto &[destination, entry] : topologyEntries)
        {
            entry.routesByNeighbor.erase(neighborIp);
            // If no routes remain, remove the entry
            if (entry.routesByNeighbor.empty())
            {
                topologyEntries.erase(destination);
            }
        }
    }

    TopologyTable::TopologyEntry *TopologyTable::FindBestRoute(const std::string &destination)
    {
        std::lock_guard<std::mutex> lock(tableMutex);
        auto it = topologyEntries.find(destination);
        if (it != topologyEntries.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    void TopologyTable::HandleRouteFailure(const std::string &destination)
    {
        std::lock_guard<std::mutex> lock(tableMutex);
        auto it = topologyEntries.find(destination);
        if (it != topologyEntries.end())
        {
            TopologyEntry &entry = it->second;
            entry.isActive = true;

            // Remove current successor
            for (auto &[neighbor, route] : entry.routesByNeighbor)
            {
                route.isSuccessor = false;
            }

            // Implement logic to start active and SIA timers if needed
            // This will be integrated with EigrpInterface's timers
        }
    }

    // Marks a route as passive
    void TopologyTable::MarkRouteAsPassive(const std::string &destination, EigrpInterface *eigrp)
    {
        std::lock_guard<std::mutex> lock(tableMutex);
        auto it = topologyEntries.find(destination);
        if (it != topologyEntries.end())
        {
            TopologyEntry &entry = it->second;
            entry.isActive = false;

            // Cancel Active timer if running
            if (entry.activeTimerId != 0)
            {
                TimeManager::getInstance().CancelTimer(entry.activeTimerId);
                entry.activeTimerId = 0;
            }
        }
    }

    void TopologyTable::RemoveEntry(const std::string &destination)
    {
        topologyEntries.erase(destination);
    }

}

// Global function to update active EIGRP processes for current interface
void UpdateEigrpInterface(Interface *interface)
{
    lock_guard<mutex> lock(globalEigrpMutex);
    for (auto &instance : eigrpList)
    {
        if (!instance.second->TestAddress(interface->Get().ip) && !instance.second->networks.empty())
        {
            interface->eigrpInterfaceList.erase(instance.second->asNumber);
            instance.second->eigrpInterfaceList.erase(interface->Get().id);
        }
    }
}

Protocol::Eigrp *currentEigrp;
map<int, std::shared_ptr<Protocol::Eigrp>> eigrpList;

#pragma endregion