#include <Eigrp.h>
#include <Encapsulation.h>

#define MAX_RETRANSMISSIONS 16
#define FULL_UPDATE "FULL_UPDATE"
#define EMPTY_UPDATE "EMPTY_UPDATE"
#pragma region Eigrp

// Global mutex for EIGRP operations
mutex globalEigrpMutex;

namespace Protocol
{

    Eigrp::Eigrp(int &as) : asNumber(as), wideMetric(10000000)
    {
        kvalue.k1_Bandwidth = 1;
        kvalue.k2_Load = 0;
        kvalue.k3_Delay = 1;
        kvalue.k4_Reliability = 0;
        kvalue.k5_MTU = 0;
        kvalue.k6_Power = 0;

        topologyTable = std::make_unique<TopologyTable>();
        topologyTable->SetVariance(1);

        UpdateInterfaceList();
        UpdateRoutingTableForConnected();
    }

    Eigrp::~Eigrp()
    {
        networks.clear();
        for (auto &eigrpInterface : eigrpInterfaceList)
        {
            eigrpInterface.second->StopHello();
            eigrpInterface.second->CancelStuckInActive();
        }
    }

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

    double Eigrp::CalculateMetric(int bandwidth, int load, int delay, int reliability, int hopCount)
    {
        if (bandwidth == 0)
        {
            return std::numeric_limits<double>::infinity();
        }

        // Calculate Bandwidth component
        double bandwidthMetric = static_cast<double>(kvalue.k1_Bandwidth) * (wideMetric / bandwidth); // Bandwidth in kbps

        // Calculate Delay component
        double delayMetric = static_cast<double>(kvalue.k3_Delay) * (delay / 10.0); // Delay in tens of microseconds

        // Calculate Load component (only if K2 is not 0)
        double loadMetric = 0.0;
        if (kvalue.k2_Load != 0)
        {
            loadMetric = (kvalue.k2_Load * bandwidth) / (256.0 - load);
        }

        // Calculate Reliability component
        double reliabilityMetric = static_cast<double>(kvalue.k4_Reliability) * reliability;

        // Sum the components
        double metric = bandwidthMetric + loadMetric + delayMetric + reliabilityMetric;

        // Ensure metric does not exceed maximum EIGRP value
        return std::min(metric, 4294967295.0);
    }

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

    string Eigrp::CalculateParameters(int holdTime)
    {
        std::string params;
        params += Functions::numToByte(kvalue.k1_Bandwidth, 1);
        params += Functions::numToByte(kvalue.k2_Load, 1);
        params += Functions::numToByte(kvalue.k3_Delay, 1);
        params += Functions::numToByte(kvalue.k4_Reliability, 1);
        params += Functions::numToByte(kvalue.k5_MTU, 1);
        params += Functions::numToByte(kvalue.k6_Power, 1);
        params += Functions::numToByte(holdTime, 2);
        return params;
    }

    void Eigrp::UpdateRoutingTableForConnected()
    {

        std::lock_guard<std::mutex> lock(eigrpMutex);
        RoutingTable &routingTable = RoutingTable::getInstance();
        vector<RoutingTable::Eigrp> updatedRoutes{};
        vector<RoutingTable::Eigrp> removedRoutes{};

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
                    connectedRoute.bandwidth = (10000000 / eigrpBw) * 256;
                    connectedRoute.delay = (interfaceInfo.delay / 10) * 256;
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
                    routingTable.AddEigrp(connectedRoute);

                    // Advertise the connected route to eigrp neighbors
                    updatedRoutes.push_back(connectedRoute);
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

                removedRoutes.push_back(removedRoute);
            }
        }

        // Notify neighbors
        if (!updatedRoutes.empty())
        {
            NotifyRoutingChange(updatedRoutes, /*isRemoval*/false);
        }
        if (!removedRoutes.empty())
        {
            NotifyRoutingChange(removedRoutes, /*isRemoved*/true);
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

                routingTable.AddEigrp(connectedRoute);

                // Advertise to neighbors
                vector<RoutingTable::Eigrp> updatedRoutes{connectedRoute};
                NotifyRoutingChange(updatedRoutes, /*isRemoval*/ false);
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

                // Advertise to neighbors
                vector<RoutingTable::Eigrp> updatedRoutes{removedRoute};
                NotifyRoutingChange(updatedRoutes, /*isRemoval*/ true);
            }
        }
    }

    void Eigrp::NotifyRoutingChange(const vector<RoutingTable::Eigrp> &changedRoutes, bool isRemoval, bool init)
    {
        std::scoped_lock lock(globalEigrpMutex);
        for (const auto &[_, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            std::vector<std::shared_ptr<EigrpConfigs::NeighborInfo>> neighborsCopy;

            {
                std::lock_guard<std::mutex> lock(eigrpInterfacePtr->neighborMutex);
                for (const auto& [_, neighborEntry] : eigrpInterfacePtr->neighbors)
                {
                    neighborsCopy.emplace_back(neighborEntry);
                }
            }

            // Iterate over all neighbors
            for (const auto &neighborEntry : neighborsCopy)
            {
                vector<RoutingTable::Eigrp> updatedRoutes;
                // Ensure neighbor's MAC address is known
                {
                    std::lock_guard<std::mutex> lock(neighborEntry->macMutex);
                    if (!neighborEntry->hasMac || neighborEntry->macAddress.empty())
                    {
                        auto mac = RoutingTable::getInstance().ArpLookup(neighborEntry->ipAddress);
                        if (!mac.has_value())
                        {
                            // Initialize ARP request and skip sending the update until MAC is resolved
                            eigrpInterfacePtr->currentInterface->arp->sendRequest(neighborEntry->ipAddress);
                            continue;
                        }
                        neighborEntry->macAddress = mac->mac;
                        neighborEntry->hasMac = true;
                    }
                }
                for (const auto& route : changedRoutes)
                {
                    if (route.nextHop != neighborEntry->ipAddress)
                    {
                        updatedRoutes.push_back(route);
                    }
                }
                if (!updatedRoutes.empty())
                {
                    if (init)
                    {
                        eigrpInterfacePtr->SendUpdateToNeighbor(neighborEntry->ipAddress, updatedRoutes, false);
                    }
                    else 
                    {
                        eigrpInterfacePtr->SendUpdateToNeighbor(neighborEntry->ipAddress, updatedRoutes, isRemoval);
                    }
                }
            }
        }
    }

    void Eigrp::Shutdown()
    {
        std::lock_guard<std::mutex> lock(globalEigrpMutex);
        for (const auto& [id, eigrpInterface] : eigrpInterfaceList)
        {
            // Send termination message
            eigrpInterface->SendHelloPacket(false, 0, std::string("\xff\xff\xff\xff", 4));
            eigrpInterface->StopHello();
        }
        eigrpInterfaceList.clear();
        networks.clear();
    }

    void Eigrp::RedistributeRoute(const std::string &destination, int mask, const std::string &protocol)
    {
        RoutingTable& routingTable = RoutingTable::getInstance();
        auto route = routingTable.GetEigrpRoute(destination, mask);

        if (route.has_value())
        {
            // Convert route to external EIGRP and notify neighbors
            RoutingTable::Eigrp externalRoute = route.value();
            externalRoute.routeType = "external";
            externalRoute.metric += redistributionMetricOffset;

            routingTable.UpdateEigrp(externalRoute);
            NotifyRoutingChange({externalRoute}, false);
        }
    }

    void Eigrp::AddNetwork(const EigrpConfigs::network newNetwork)
    {
        networks.push_back(newNetwork);

        // Update interfaces and routing table after adding the network
        UpdateInterfaceList();
        UpdateRoutingTableForConnected();
    }

    void Eigrp::AddSummaryRoute(const std::string& network, int mask)
    {
        std::lock_guard<std::mutex> lock(eigrpMutex);

        // Validate network and mask
        if (!Functions::compareNetworkWithMask(network, mask))
        {
            // Mask is not valid
            return;
        }

        // Check for overlapping summary routes
        for (const auto& sr : summaryRoutes)
        {
            if (Functions::isSubnetOf(sr.network, sr.mask, network, mask) || Functions::isSubnetOf(network, mask, sr.network, sr.mask))
            {
                // Conflicting summary route
                return;
            }
        }

        // Checks if the summary route already exists
        for (const auto& sr : summaryRoutes) 
        {
            if (sr.network == network && sr.mask == mask)
            {
                // Summary already exists
                return;
            }
        }

        // Add new summary route
        EigrpConfigs::SummaryRoute summaryRoute{ .network = network, .mask = mask };
        summaryRoutes.push_back(summaryRoute);

        // Inject the summary route into the routing table as an internal summary route
        RoutingTable::Eigrp internalSummaryRoute;
        internalSummaryRoute.network = network;
        internalSummaryRoute.mask = mask;
        internalSummaryRoute.nextHop = std::string("\x00\x00\x00\x00", 4);
        internalSummaryRoute.metric = CalculateMetric(0, 0, 0, 255);
        internalSummaryRoute.routeType = "summary";

        RoutingTable::getInstance().AddEigrp(internalSummaryRoute);

        // Update interfaces to advertise the new summary route
        UpdateInterfacesWithSummaryRoute(summaryRoute);
    }

    void Eigrp::RemoveSummaryRoute(const std::string& network, int mask)
    {
        std::lock_guard<std::mutex> lock(eigrpMutex); // Esures thread safety
        
        auto originalSize = summaryRoutes.size();
        summaryRoutes.erase(
            std::remove_if(summaryRoutes.begin(), summaryRoutes.end(),
                [&](const EigrpConfigs::SummaryRoute& sr) {
                    return sr.network == network && sr.mask == mask;
                }),
            summaryRoutes.end()
        );

        if (summaryRoutes.size() < originalSize)
        {
            // Remove the summary route from the routing table
            RoutingTable::getInstance().RemoveEigrp(network, mask);

            // Withdraw summary route from all neighbors
            UpdateInterfacesAfterRemovingSummaryRoute(network, mask);
        }
    }

    void Eigrp::UpdateInterfacesWithSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute)
    {
        for (auto& [id, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            eigrpInterfacePtr->AdvertiseSummaryRoute(summaryRoute);
        }
    }

    void Eigrp::UpdateInterfacesAfterRemovingSummaryRoute(const std::string& network, int mask)
    {
        for (auto& [id, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            eigrpInterfacePtr->WithdrawSummaryRoute(network, mask);
        }
    }
    
    bool Eigrp::IsRouteSummarized(const std::string& network, int mask)
    {
        for (const auto& sr : summaryRoutes)
        {
            if (Functions::isSubnetOf(network, mask, sr.network, sr.mask))
            {
                return true;
            }
        }
        return false;
    }

#pragma endregion

#pragma region EigrpInterface

    EigrpInterface::EigrpInterface(Eigrp &eigrpSystem, std::shared_ptr<Interface> interface)
        : currentInterface(interface),
          eigrpProcess(&eigrpSystem),
          helloStartTime(std::chrono::steady_clock::now())
    {
        bandwidth = currentInterface->Get().bandwidth;
        StartHelloHelper();

        // Create Eigrp DistributionList
    }

    EigrpInterface::~EigrpInterface()
    {
    }

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
                neighbor->globalSequenceNumber = 1;
                SendHelloPacket(true, neighbor->globalSequenceNumber, neighborIp);
                SendEmptyUpdateToNeighbor(neighborIp);
            }
            else if (neighbor->adjacency && neighbor->hasMac && !neighbor->isInit && neighbor->receivedInitUpdate && !neighbor->sendInitUpdate)
            {
                SendFullUpdateToNeighbor(neighborIp);
                neighbor->sendInitUpdate = true;
            }
        }
    }

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

        if (receivedSequenceNumber <= neighbor->lastReceivedSequenceNumber && neighbor->lastReceivedSequenceNumber != 0)
        {
            SendAckToNeighbor(neighborIp, receivedSequenceNumber);
            return; 
        }

        neighbor->lastReceivedSequenceNumber = receivedSequenceNumber;

        // Update flags if needed
        //if (receivedUpdate->flags.restart == "1") { HandleNeighborRestart(neighborIp); return; } // CAUSING FUCKING PROBLEMS
        if (receivedUpdate->flags.init == "1") { neighbor->sequenceList[receivedSequenceNumber].init = true; }
        if (receivedUpdate->flags.conditionalRecieve == "1") { neighbor->sequenceList[receivedSequenceNumber].conditionalReceive = true; }
        if (receivedUpdate->flags.endOfTable == "1") {neighbor->sequenceList[receivedSequenceNumber].endOfTable = true; }

        if (!neighbor->receivedInitUpdate && receivedUpdate->options.empty())
        {
            neighbor->receivedInitUpdate = true;
        }

        // Process ack if necessary
        if (receivedUpdate->ack != std::string("\x00\x00\x00\x00", 4))
        {
            ProcessAck(receivedUpdate->ack, neighborIp);
        }

        // Collect routes for batch processing
        int receivedUpdates = 0;
        for (const auto &option : receivedUpdate->options)
        {
            if (option.option == variable.eigrp.options.internalRoute ||
                option.option == variable.eigrp.options.externalRoute)
            {
                RoutingTable::Eigrp route;
                
                route = DecodeRoute(option.value, (option.option == variable.eigrp.options.externalRoute));

                if (route.delay != 0xFFFFFFFF)
                {
                    routeBuffer.push_back(route);
                }
                receivedUpdates++;
            }
        }

        if (!routeBuffer.empty())
        {
            RoutingTable& routingTable = RoutingTable::getInstance();

            for (const auto& route : routeBuffer)
            {
                routingTable.AddEigrp(route);
            }
            UpdateRoutingTable(routeBuffer, neighbor->sequenceList[receivedSequenceNumber].init, neighborIp);
        }

        // Assume end of table if I receive one route
        if (receivedUpdates == 1)
        {
            neighbor->sequenceList[receivedSequenceNumber].endOfTable = true;
        }

        if (neighbor->sequenceList[receivedSequenceNumber].endOfTable && !routeBuffer.empty())
        {
            // End of update sequence
            neighbor->sequenceList.erase(receivedSequenceNumber);
            routeBuffer.clear();
        }

        // send ACK to neighbor
        SendAckToNeighbor(neighborIp, receivedSequenceNumber);
    }

    void EigrpInterface::ProcessAck(const std::string sequenceNumber, const std::string &neighborIp)
    {
        int ackSequenceNumber = Functions::byteToNum(sequenceNumber);

        std::lock_guard<std::mutex> lock(neighborMutex);
        auto neighborIt = neighbors.find(neighborIp);
        if (neighborIt == neighbors.end())
        {
            return;
        }
        auto neighbor = neighborIt->second;

        // Remove the Acknowledged Packet from reliablePackets
        auto pktIt = neighbor->reliablePackets.find(ackSequenceNumber);
        if (pktIt != neighbor->reliablePackets.end())
        {
            // Cancel the Retransmission Timer
            if (pktIt->second.timerId != 0)
            {
                TimeManager::getInstance().CancelTimer(pktIt->second.timerId);
            }

            // Erase the Packet from reliablePackets
            neighbor->reliablePackets.erase(pktIt);

            // Update RTT and RTO Estimates if Necessary
            UpdateRTTEstimate(neighbor, ackSequenceNumber);
        }
    }

    void EigrpInterface::ProcessQuery(const eigrpHeader *receivedQuery, const std::string &neighborIp)
    {
        vector<RoutingTable::Eigrp> queryRoutes;
        vector<RoutingTable::Eigrp> validQueryRoutes;
        int queryId = Functions::byteToNum(receivedQuery->sequence);
        // Extract the destination network network and mask from the Query options
        for (const auto& opt : receivedQuery->options)
        {
            RoutingTable::Eigrp route;
            if (opt.option == variable.eigrp.options.internalRoute)
            {
                route = DecodeRoute(opt.value, false);
            }
            else
            {
                continue;
            }

            auto routeIt = RoutingTable::getInstance().GetEigrpRoute(route.network, route.mask);
            bool hasValidRoute = routeIt.has_value();
            if (hasValidRoute)
            {
                validQueryRoutes.push_back(routeIt.value());
            }
            else
            {
                queryRoutes.push_back(route);
            }
        }


        if (!validQueryRoutes.empty())
        {
            // Send a Reply to the quering neighbor with the route information
            SendReplyToNeighbor(neighborIp, validQueryRoutes);
        }

        if (!queryRoutes.empty())
        {
            // Propagate the Query to other neighbors except the originator
            {
                std::lock_guard<std::mutex> lock(replyTrackingMutex);
                outstandingReplies[queryId] = {neighborIp, static_cast<int>(neighbors.size() - 1)};
            }

            for (const auto& [_, neighborEntry] : neighbors)
            {
                if (neighborEntry->ipAddress != neighborIp)
                {
                    SendQueryToNeighbors(queryRoutes, neighborIp);
                }
            }
        }
    }

    void EigrpInterface::ProcessReply(const eigrpHeader *recievedReply, const std::string &neighborIp)
    {
        int queryId = Functions::byteToNum(recievedReply->sequence);
        vector<RoutingTable::Eigrp> receivedRoutes;

        // Extract the route information from the reply options
        for (const auto& opt : recievedReply->options)
        {
            if (opt.option == variable.eigrp.options.internalRoute)
            {
                RoutingTable::Eigrp route = DecodeRoute(opt.value, /*external*/false);
                receivedRoutes.push_back(route);
            }
        }

        if (!receivedRoutes.empty())
        {
            UpdateRoutingTable(receivedRoutes, /*init*/false, neighborIp);
        }

        {
            std::lock_guard<std::mutex> lock(replyTrackingMutex);
            auto it = outstandingReplies.find(queryId);
            if (it != outstandingReplies.end())
            {
                it->second.second--; // Decrement the count of pending replies

                if (it->second.second == 0) // All replies received
                {
                    std::string queryingNeighborIp = it->second.first;
                    outstandingReplies.erase(it);

                    if (!queryingNeighborIp.empty())
                    {
                        SendReplyToNeighbor(queryingNeighborIp, receivedRoutes);
                    }
                }
            }
        }

        SendAckToNeighbor(neighborIp, queryId);
    }

    void EigrpInterface::SendAckToNeighbor(const std::string &neighborIp, int sequenceNumber)
    {
        // Retrieve neighbor information
        if (neighbors.find(neighborIp) == neighbors.end()) { return; }
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor = neighbors[neighborIp];

        // Create the EIGRP Ack packet
        PacketInfo eigrpAckPacketStructure;
        ethernetHeader eth;
        ipv4Header ip;
        eigrpHeader eigrp;

        // Construct Ethernet and IPv4 headers
        EigrpBody(eth, ip, currentInterface->Get().mac);

        // Set the destination MAC and IP to the neighbors
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
                    return;
                }
            }
            else
            {
                eth.destinationMac = neighbor->macAddress;
            }
        }
        ip.destinationAddress = neighbor->ipAddress;

        // Construct EIGRP Ack packet
        eigrpProcess->EigrpHello(eigrp, this, true, false, sequenceNumber, neighborIp);

        // Assemble the packet
        eigrpAckPacketStructure.Layer2.push_back(eth);
        eigrpAckPacketStructure.Layer3.push_back(ip);
        eigrpAckPacketStructure.Layer3.push_back(eigrp);

        // Convert to raw packet
        string eigrpAckPacket = Encapsulate(eigrpAckPacketStructure);

        // Enqueue for transmission
        currentInterface->packetOutQueue.enqueue(eigrpAckPacket);
    }

    void EigrpInterface::SendUpdateToNeighbor(const std::string &neighborIp, const vector<RoutingTable::Eigrp> &routes, bool removal)
    {
        std::lock_guard<std::mutex> lock(neighborMutex);

        routeBuffer.clear();

        if (neighbors.find(neighborIp) == neighbors.end()) { return; }
        auto neighbor = neighbors[neighborIp];

        // Get and incrament sequence number
        int currentSeqNum = GetNextSequenceNumber(neighbor);

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
        vector<EigrpConfigs::NetworksDistributed> routesToSend;
        for (const auto& route : routes)
        {
            if (!splitHorizon || route.nextHop != neighborIp || !eigrpProcess->IsRouteSummarized(route.network, route.mask))
            {
                routesToSend.push_back(EigrpConfigs::NetworksDistributed{.route = route});
            }
        }
        
        eigrpProcess->EigrpUpdate(eigrp, currentSeqNum, routesToSend, /*init=*/false, /*conditional=*/false, /*restart=*/false, /*endOfTable=*/false);

        if (removal)
        {
            for (auto& route : routesToSend)
            {
                // EIGRP uses 4294967295 (0xFFFFFFFF) as infinity
                route.route.reportedDistance = 0xFFFFFFFF;
                route.route.feasibleDistance = 0xFFFFFFFF;
                route.route.metric = 0xFFFFFFFF;
                route.route.delay = 0xFFFFFFFF;
                route.route.nextHop = std::string("\xFF\xFF\xFF\xFF", 4);
            }
        }

        // Assemble the packet
        eigrpUpdatePacketStructure.Layer2.push_back(eth);
        eigrpUpdatePacketStructure.Layer3.push_back(ip);
        eigrpUpdatePacketStructure.Layer3.push_back(eigrp);

        string eigrpUpdatePacket = Encapsulate(eigrpUpdatePacketStructure);
        currentInterface->packetOutQueue.enqueue(eigrpUpdatePacket);

        // Store the packet for possible retransmission (reliable delivery)
        SetupReliablePacket(neighbor, eigrpUpdatePacket, currentSeqNum);
    }

    void EigrpInterface::SendFullUpdateToNeighbor(const std::string &neighborIp)
    {
        if (neighbors.find(neighborIp) == neighbors.end()) { return; }
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor = neighbors[neighborIp];

        // Get and incrament next sequence number
        int fullUpdateSeqNum = GetNextSequenceNumber(neighbor);

        // Create the Eigrp Update Packet
        PacketInfo eigrpUpdatePacketStructure;
        ethernetHeader eth;
        ipv4Header ip;
        eigrpHeader eigrp;

        // Construct Ethernet and IPv4 headers
        EigrpBody(eth, ip, currentInterface->Get().mac);
 
        // Get all EIGRP routes from the routing table
        vector<EigrpConfigs::NetworksDistributed> routesToSend;

        auto eigrpTable = RoutingTable::getInstance().GetAllEigrpRoutes();

        for (const auto &route : eigrpTable)
        {
            if (!splitHorizon || route.nextHop != neighbor->ipAddress)
            {
                routesToSend.push_back(EigrpConfigs::NetworksDistributed{.route = route});
            }
        }

        // Construct EIGRP Update packet
        eigrpProcess->EigrpUpdate(eigrp, fullUpdateSeqNum, routesToSend, /*init=*/true, /*conditional=*/false, /*restart=*/false, /*endOfTable=*/true);
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
        currentInterface->packetOutQueue.enqueue(eigrpUpdatePacket);

        // Store the packet for possible retransmission (reliable delivery)
        SetupReliablePacket(neighbor, eigrpUpdatePacket, fullUpdateSeqNum);
    }

    void EigrpInterface::SendEmptyUpdateToNeighbor(const std::string &neighborIp)
    {
        if (neighbors.find(neighborIp) == neighbors.end()) { return; }
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor = neighbors[neighborIp];

        // Get and incrament global sequence number
        int emptyUpdateSeqNum = GetNextSequenceNumber(neighbor);

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
            eigrpProcess->EigrpUpdate(eigrp, emptyUpdateSeqNum, routesToSend, /*init=*/true, /*conditional=*/false, /*restart=*/false, /*endOfTable=*/false, /*query=*/false, /*reply=*/false);
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
            eigrpProcess->EigrpUpdate(eigrp, emptyUpdateSeqNum, routesToSend, /*init=*/true, /*conditional=*/true, /*restart=*/false, /*endOfTable=*/end, /*query=*/false, /*reply=*/false);
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
        string eigrpEmptyUpdatePacket = Encapsulate(eigrpUpdatePacketStructure);
        currentInterface->packetOutQueue.enqueue(eigrpEmptyUpdatePacket);

        // Store the packet for possible retransmission (reliable delivery)
        SetupReliablePacket(neighbor, eigrpEmptyUpdatePacket, emptyUpdateSeqNum);
    }

    void EigrpInterface::SendQueryToNeighbors(const vector<RoutingTable::Eigrp>& failedRoutes, const std::string& originNeighborIp)
    {
        std::lock_guard<std::mutex> lock(neighborMutex);

        int queryId = GetNextSequenceNumber(neighbors.begin()->second); // Generate query ID
        outstandingReplies[queryId] = {originNeighborIp, static_cast<int>(neighbors.size() - 1)};

        for (const auto& [_, neighborEntry] : neighbors)
        {
            if (neighborEntry->ipAddress != originNeighborIp)
            {
                SendQueryToNeighbor(neighborEntry->ipAddress, failedRoutes);
            }
        }
        // Start Active Timer for the failed destination
        for (const auto& failedRoute : failedRoutes)
        {
            StartActiveTimer(failedRoute);
        }
    }

    void EigrpInterface::SendQueryToNeighbor(const std::string& neighborIp, const vector<RoutingTable::Eigrp>& failedRoutes)
    {
        if (neighbors.find(neighborIp) == neighbors.end()) { return; }
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor = neighbors[neighborIp];

        // Increment sequence number for this route/query
        int currentSeqNum = GetNextSequenceNumber(neighbor);

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
        for (const auto& route : failedRoutes)
        {
            eigrpHeader::Option queryOption;
            queryOption.option = variable.eigrp.options.internalRoute;
            queryOption.value = EncodeQueryOption(route);
            queryOption.length = Functions::numToByte(queryOption.value.size() + 4, 2);
            // Add the Query option to EIGRP header
            eigrp.options.push_back(queryOption);
        }

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
        currentInterface->packetOutQueue.enqueue(eigrpQueryPacket);

        // Store the packet for possible retransmission (relieable delivery)
        SetupReliablePacket(neighbor, eigrpQueryPacket, currentSeqNum);
    }

    std::string EigrpInterface::EncodeQueryOption(RoutingTable::Eigrp route)
    {
        std::string encoded;
        encoded += route.nextHop;
        encoded += std::string("\xff\xff\xff\xff", 4);
        encoded += Functions::numToByte(route.bandwidth, 4);
        encoded += Functions::numToByte(route.mtu, 3);
        encoded += Functions::numToByte(route.hopCount, 1);
        encoded += Functions::numToByte(route.reliability, 1);
        encoded += Functions::numToByte(route.load, 1);
        encoded += Functions::numToByte(route.routeTag, 1);
        encoded += std::string("\x00", 1);
        encoded += Functions::numToByte(route.mask, 1);
        encoded += Functions::compactNetworkAddress(route.network, route.mask);
        return encoded;
    }

    void EigrpInterface::SendReplyToNeighbor(const std::string& neighborIp, const vector<RoutingTable::Eigrp>& routes)
    {
        if (neighbors.find(neighborIp) == neighbors.end()) { return; }
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor = neighbors[neighborIp];

        // Increment sequence number for this route/reply
        int currentSeqNum = GetNextSequenceNumber(neighbor);

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
        for (const auto& route : routes)
        {
            eigrpHeader::Option replyOption;
            replyOption.option = variable.eigrp.options.internalRoute;
            replyOption.value = EncodeRouteOption(route);
            replyOption.length = Functions::numToByte(replyOption.value.size() + 4, 2);
            // Add the Reply option to EIGRP header
            eigrp.options.push_back(replyOption);
        }

        // Set other EIGRP header fields
        eigrp.version = std::string("\x02", 1);
        eigrp.opcode = variable.eigrp.type.reply;
        eigrp.checksum = std::string("\x00\x00", 2); // Will be calculated later
        eigrp.flags.init = "0";
        eigrp.flags.conditionalRecieve = "0";
        eigrp.flags.restart = "0";
        eigrp.flags.endOfTable = "1";
        eigrp.sequence = Functions::numToByte(currentSeqNum, 4);
        eigrp.ack = std::string("\x00\x00\x00\x00", 4);
        eigrp.virtualRouterID = eigrpProcess->virtualRouterID;
        eigrp.autonomousSystem = Functions::numToByte(eigrpProcess->asNumber, 2);

        // Assemble the packet
        eigrpReplyPacketStructure.Layer2.push_back(eth);
        eigrpReplyPacketStructure.Layer3.push_back(ip);
        eigrpReplyPacketStructure.Layer3.push_back(eigrp);

        // Convert to raw packet string
        std::string eigrpReplyPacket = Encapsulate(eigrpReplyPacketStructure);

        // Enqueue for transmission
        currentInterface->packetOutQueue.enqueue(eigrpReplyPacket);

        // Store the packet for possible retransmission (reliable delivery)
        SetupReliablePacket(neighbor, eigrpReplyPacket, currentSeqNum);
    }

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
        encoded += Functions::numToByte(route.routeTag, 1);
        encoded += std::string("\x00", 1);
        encoded += Functions::numToByte(route.mask, 1);
        encoded += Functions::compactNetworkAddress(route.network, route.mask);
        return encoded;
    }

    void EigrpInterface::AdvertiseSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute)
    {
        std::lock_guard<std::mutex> lock(neighborMutex); // Protext access to neighbors

        // Iterate over all neighbors to advertise the summary route
        for (const auto& [neighborIp, NeighborInfo] : neighbors)
        {
            if (!NeighborInfo->isInit) continue; // skip non-initialized neighbor

            int seqNumber = GetNextSequenceNumber(NeighborInfo);
            
            // Create the EIGRP Summary Route Packet
            PacketInfo eigrpSummaryPacketStructure;
            ethernetHeader eth;
            ipv4Header ip;
            eigrpHeader eigrpHeaderInstance;

            //Construct Ethernet and Ipv4 Headers
            EigrpBody(eth, ip, currentInterface->Get().mac);
            eth.destinationMac = NeighborInfo->macAddress;
            ip.destinationAddress = NeighborInfo->ipAddress;

            // Construct EIGRP Summary Route option
            eigrpHeader::Option summaryOption;
            summaryOption.option = variable.eigrp.options.internalRoute;
            summaryOption.value = EncodeSummaryRoute(summaryRoute);
            summaryOption.length = Functions::numToByte(summaryOption.value.size() + 4, 2);
            eigrpHeaderInstance.options.push_back(summaryOption);

            // Finish EIGRP Header
            vector<EigrpConfigs::NetworksDistributed> routes{};
            eigrpProcess->EigrpUpdate(eigrpHeaderInstance, seqNumber, routes);

            // Assemble the packet
            eigrpSummaryPacketStructure.Layer2.push_back(eth);
            eigrpSummaryPacketStructure.Layer3.push_back(ip);
            eigrpSummaryPacketStructure.Layer3.push_back(eigrpHeaderInstance);

            // Convert to raw packet string
            std::string eigrpSummaryPacket = Encapsulate(eigrpSummaryPacketStructure);
            currentInterface->packetOutQueue.enqueue(eigrpSummaryPacket);

            // Store the packet for possible retransmission (reliable delivery)
            SetupReliablePacket(neighbors[NeighborInfo->ipAddress], eigrpSummaryPacket, seqNumber);
        }
    }

    void EigrpInterface::WithdrawSummaryRoute(const std::string& network, int mask)
    {
        std::lock_guard<std::mutex> lock(neighborMutex); // Protext access to neighbors
        
        // Create a summary route with metric set to infinity
        RoutingTable::Eigrp withdrawnSummaryRoute;
        withdrawnSummaryRoute.network = network;
        withdrawnSummaryRoute.mask = mask;
        withdrawnSummaryRoute.metric = std::numeric_limits<double>::infinity();
        withdrawnSummaryRoute.nextHop = std::string("\xff\xff\xff\xff", 4);
        withdrawnSummaryRoute.routeType = "internal";

        for (const auto& [neighborIp, NeighborInfo] : neighbors)
        {
            if (!NeighborInfo->isInit) continue; // skip non-initialized neighbor

            int seqNumber = GetNextSequenceNumber(NeighborInfo);
            
            // Create the EIGRP Summary Route Packet
            PacketInfo eigrpSummaryPacketStructure;
            ethernetHeader eth;
            ipv4Header ip;
            eigrpHeader eigrpHeaderInstance;

            //Construct Ethernet and Ipv4 Headers
            EigrpBody(eth, ip, currentInterface->Get().mac);
            eth.destinationMac = NeighborInfo->macAddress;
            ip.destinationAddress = NeighborInfo->ipAddress;

            // Construct EIGRP Summary Route option
            eigrpHeader::Option summaryOption;
            summaryOption.option = variable.eigrp.options.internalRoute;
            summaryOption.value = EncodeRouteOption(withdrawnSummaryRoute);
            summaryOption.length = Functions::numToByte(summaryOption.value.size() + 4, 2);
            eigrpHeaderInstance.options.push_back(summaryOption);

            // Finish EIGRP Header
            vector<EigrpConfigs::NetworksDistributed> routes{};
            eigrpProcess->EigrpUpdate(eigrpHeaderInstance, seqNumber, routes);

            // Assemble the packet
            eigrpSummaryPacketStructure.Layer2.push_back(eth);
            eigrpSummaryPacketStructure.Layer3.push_back(ip);
            eigrpSummaryPacketStructure.Layer3.push_back(eigrpHeaderInstance);

            // Convert to raw packet string
            std::string eigrpSummaryPacket = Encapsulate(eigrpSummaryPacketStructure);
            currentInterface->packetOutQueue.enqueue(eigrpSummaryPacket);

            // Store the packet for possible retransmission (reliable delivery)
            SetupReliablePacket(neighbors[NeighborInfo->ipAddress], eigrpSummaryPacket, seqNumber);
        }
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

    void EigrpInterface::StopHello()
    {
        std::lock_guard<std::mutex> lock(helloTimerMutex);
        if (helloTimerId != 0)
        {
            TimeManager::getInstance().CancelTimer(helloTimerId);
            helloTimerId = 0;
        }
    }

    void EigrpInterface::StartHoldTimer(const std::string &neighborIp, int holdTime)
    {
        if (neighbors.find(neighborIp) == neighbors.end()) { return; }
        std::shared_ptr<EigrpConfigs::NeighborInfo> &neighbor = neighbors[neighborIp];

        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(holdTime);
        neighbor->holdTimerId = TimeManager::getInstance().AddTimer(expirationTime, [this, neighborIp]()
                                                                   { HandleHoldTimeExpire(neighborIp); });
    }

    void EigrpInterface::HandleHoldTimeExpire(const std::string &neighborIp)
    {
        std::lock_guard<std::mutex> lock(neighborMutex);
        auto it = neighbors.find(neighborIp);
        if (it != neighbors.end())
        {
            HandleNeighborDown(neighborIp);
        }
    }

    void EigrpInterface::StartActiveTimer(const RoutingTable::Eigrp& route)
    {
        auto key = route.network + "/" + std::to_string(route.mask);
        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(activeTime);

        // Schedule Active timer
        int timerId = TimeManager::getInstance().AddTimer(expirationTime, [this, route]() {
            HandleActiveTimeExpire(route);
        });

        {
            std::lock_guard<std::mutex> lock(activeTimerMutex);
            activeTimers[key] = timerId;
        }
    }

    void EigrpInterface::HandleActiveTimeExpire(const RoutingTable::Eigrp& route)
    {
        std::string key = route.network + "/" + std::to_string(route.mask);

        // Step 1: Remove the Active Timer from the map
        {
            std::lock_guard<std::mutex> lock(activeTimerMutex);
            auto it = activeTimers.find(key);
            if (it != activeTimers.end())
            {
                activeTimers.erase(it);
            }
        }

        // Step 2: Remove the route from the topology table
        eigrpProcess->topologyTable->HandleRouteFailure(route.network);

        // Step 3: Remove the route from the routing table
        RoutingTable& routingTable = RoutingTable::getInstance();
        routingTable.RemoveEigrp(route.network, route.mask);

        // Step 4: Notify neighbors about the route removal
        RoutingTable::Eigrp removedRoute = route;
        removedRoute.metric = std::numeric_limits<double>::infinity();
        removedRoute.nextHop = std::string("\xff\xff\xff\xff", 4);
        removedRoute.routeType = "internal"; // Ensure routeType is set appropriately

        eigrpProcess->NotifyRoutingChange({removedRoute}, /*isRemoval*/true);

        // Step 5: Trigger stuck-in-active
        StartStuckInActive();
    }

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

    void EigrpInterface::StartStuckInActive()
    {
        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(stuckInActiveTime);

        // Schedule Stuck In Active timer
        stuckInActiveTimerId = TimeManager::getInstance().AddTimer(expirationTime, [this]()
        {
            HandleStuckInActive();
        });
    }

    void EigrpInterface::HandleStuckInActive()
    {
        std::lock_guard<std::mutex> lock(activeTimerMutex);

        std::vector<std::string> activeRoutes;
        for (const auto& [key, timerId] : activeTimers)
        {
            activeRoutes.push_back(key);
        }

        for (const auto& routeKey : activeRoutes)
        {
            auto delimiter = routeKey.find('/');
            if (delimiter == std::string::npos) continue;

            std::string network = routeKey.substr(0, delimiter);
            int mask = std::stoi(routeKey.substr(delimiter + 1));

            CancelActiveTimer(network, mask);

            // Try recalculating a new route
            auto entry = eigrpProcess->topologyTable->FindBestRoute(network);
            if (entry && !entry->successors.empty())
            {
                UpdateRoutingTableForDestination(network);
            }
            else
            {
                RoutingTable::Eigrp routeToRemove;
                routeToRemove.network = network;
                routeToRemove.mask = mask;
                routeToRemove.metric = std::numeric_limits<double>::infinity();
                routeToRemove.nextHop = std::string("\xff\xff\xff\xff", 4);
                routeToRemove.routeType = "internal";

                eigrpProcess->NotifyRoutingChange({routeToRemove}, true);
                eigrpProcess->topologyTable->HandleRouteFailure(network);
                RoutingTable::getInstance().RemoveEigrp(network, mask);
            }
        }

        stuckInActiveTimerId = 0;
    }

    void EigrpInterface::CancelStuckInActive()
    {
        if (stuckInActiveTimerId != 0)
        {
            TimeManager::getInstance().CancelTimer(stuckInActiveTimerId);
            stuckInActiveTimerId = 0;
        }
    }

    int EigrpInterface::StartRetransmissionTimer(const std::string &neighborIp, const int& sequenceNumber, double timeout)
    {
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor = neighbors[neighborIp];
        std::lock_guard<std::mutex> lock(retransmissionMutex);

        // Cancel any existing retransmission timers for this sequence number
        auto pktIt = neighbor->reliablePackets.find(sequenceNumber);
        if (pktIt != neighbor->reliablePackets.end())
        {
            return pktIt->second.timerId;
        }

        // Schedule a retransmission timer
        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(timeout * 1000));
        int timerId = TimeManager::getInstance().AddTimer(expirationTime, [this, neighborIp, sequenceNumber]()
        {
            HandleRetransmissionTimeout(neighborIp, sequenceNumber);
        });

        // Update the timer ID in ReliablePacketInfo
        neighbor->reliablePackets[sequenceNumber].timerId = timerId;

        return timerId;
    }

    void EigrpInterface::HandleRetransmissionTimeout(const std::string &neighborIp, const int& sequenceNumber)
    {
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor;
        {
            std::lock_guard<std::mutex> lock(neighborMutex);
            auto neighborIt = neighbors.find(neighborIp);
            if (neighborIt == neighbors.end()) return;
            neighbor = neighborIt->second;
        }

        // Check if the packet is still pending acknowledgment
        auto pktIt = neighbor->reliablePackets.find(sequenceNumber);
        if (pktIt != neighbor->reliablePackets.end())
        {
            // Increment retransmission timeout
            pktIt->second.retransmissionCount++;

            // Check if retransmissions exceed a threshhold
            if (pktIt->second.retransmissionCount > MAX_RETRANSMISSIONS)
            {
                // Consider neighbor down or take other action
                HandleNeighborDown(neighborIp);
                return;
            }

            // Resend the packet
            const string &packet = pktIt->second.packet;
            currentInterface->packetOutQueue.enqueue(packet);

            {
                std::lock_guard<std::mutex> rtoLock(neighbor->neighborDataMutex);
                neighbor->rto = std::min(neighbor->rto * 2.0, 60.0);
            }

            // Restart the retransmission timer
            int timerId = StartRetransmissionTimer(neighborIp, sequenceNumber, neighbor->rto);
            pktIt->second.timerId = timerId;
        }
    }
    RoutingTable::Eigrp EigrpInterface::DecodeRoute(string value, bool external)
    {
        if (value.size() < 21)
        {
            throw std::runtime_error("DecodeRoute: Insufficient data to decode route.");
        }

        RoutingTable::Eigrp route;
        route.nextHop = value.substr(0, 4);
        if (!external)
        {
            route.delay = Functions::byteToNum(value.substr(4, 4));
            route.bandwidth = Functions::byteToNum(value.substr(8, 4));
            route.mtu = Functions::byteToNum(value.substr(12, 3));
            route.hopCount = Functions::byteToNum(value.substr(15, 1));
            route.reliability = Functions::byteToNum(value.substr(16, 1));
            route.load = Functions::byteToNum(value.substr(17, 1));
            route.mask = Functions::byteToNum(value.substr(20, 1));
            route.network = value.substr(21);
            route.routeType = "internal";
        }
        else if (external)
        {
            route.originRouter = Functions::byteToNum(value.substr(4, 4));
            route.originAS = Functions::byteToNum(value.substr(8, 4));
            route.routeTag = Functions::byteToNum(value.substr(12, 4));
            route.extendedMetric = Functions::byteToNum(value.substr(16, 4));
            route.extendedID = Functions::byteToNum(value.substr(22, 1));
            route.flags = value.substr(23, 1);
            route.delay = Functions::byteToNum(value.substr(24, 4));
            route.bandwidth = Functions::byteToNum(value.substr(28, 3));
            route.mtu = Functions::byteToNum(value.substr(29, 3));
            route.hopCount = Functions::byteToNum(value.substr(32, 1));
            route.reliability = Functions::byteToNum(value.substr(33, 1));
            route.load = Functions::byteToNum(value.substr(34, 1));
            route.mask = Functions::byteToNum(value.substr(37, 1));
            route.network = value.substr(38);
            route.routeType = "external";
        }

        // Extract the network address based on the mask
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

        return route;
    }

    double EigrpInterface::CalculateLocalLinkCost()
    {
        const ipInfo& ifaceInfo = currentInterface->Get();

        double bandwidthMetric = (static_cast<double>(eigrpProcess->wideMetric) / ifaceInfo.bandwidth);
        double delayMetric = static_cast<double>(ifaceInfo.delay) / 10.0;

        double loadMetric = 0.0;
        if (eigrpProcess->kvalue.k2_Load != 0 && (256.0 - load) != 0)
        {
            loadMetric = (static_cast<double>(eigrpProcess->kvalue.k2_Load) * load) / (256 - load);
        }

        // Calculate link cost using K-values
        double linkCost = (eigrpProcess->kvalue.k1_Bandwidth * bandwidthMetric) +
                          loadMetric +
                          (eigrpProcess->kvalue.k3_Delay * delayMetric);

        // Apply scaling factor and reliability
        double reliabilitySum = reliability + eigrpProcess->kvalue.k4_Reliability;
        if (reliabilitySum > 0 && eigrpProcess->kvalue.k5_MTU != 0)
        {
            linkCost *= (eigrpProcess->kvalue.k5_MTU) / reliabilitySum;
        }

        return linkCost;
    }

    void EigrpInterface::UpdateRoutingTable(const vector<RoutingTable::Eigrp> routes, bool init, const std::string& neighborIp)
    {
        vector<RoutingTable::Eigrp> updatedRoutes;

        for (const auto& route : routes)
        {
            // Access the topology table and update it with new routes
            TopologyTable::RouteInfo routeInfo;
            routeInfo.feasibleDistance = route.feasibleDistance;
            routeInfo.reportedDistance = route.reportedDistance;
            routeInfo.nextHop = neighborIp;
            routeInfo.hopCount = route.hopCount;
            routeInfo.isSuccessor = false;
            routeInfo.isFeasibleSuccessor = false;

            eigrpProcess->topologyTable->AddOrUpdateRoute(route.network, route.mask, routeInfo, neighborIp);

            // Apply Duel algorithm to determine the best route
            TopologyTable::TopologyEntry *bestRouteEntry = eigrpProcess->topologyTable->FindBestRoute(route.network);
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
                        if (existingRoute->metric != newRoute.metric || existingRoute->nextHop != newRoute.nextHop)
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
                        routingTable.AddEigrp(newRoute);

                        // Notify neighbors about the route change
                        updatedRoutes.push_back(newRoute);
                    }
                }
            }
            else
            {
                updatedRoutes.push_back(route);
            }
        }
        if (!updatedRoutes.empty())
        {
            // Notify neighbors about the updates routes
            eigrpProcess->NotifyRoutingChange(updatedRoutes, /*isRemoval*/ false, /*init*/init);
        }
    }

    void EigrpInterface::HandleNeighborDown(const std::string &neighborIp)
    {
        // Remove neighbor from the neighbor table
        if (neighbors.find(neighborIp) == neighbors.end()) { return; }
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor = neighbors[neighborIp];

        // Cancel any pending timers
        if (neighbor->holdTimerId != 0)
        {
            TimeManager::getInstance().CancelTimer(neighbor->holdTimerId);
        }
        for (const auto &timerEntry : neighbor->retransmissionTimers)
        {
            TimeManager::getInstance().CancelTimer(timerEntry.second);
        }

        neighbor->retransmissionTimers.clear();
        neighbor->reliablePackets.clear();
        neighbor->sequenceList.clear();

        // Removed Routes
        vector<RoutingTable::Eigrp> removedRoutes;

        // for each affected destination, re-run DUEL
        for (auto &[destination, entry] : eigrpProcess->topologyTable->GetTopologyEntries())
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

                    removedRoutes.push_back(removedRoute);

                    // Remove the entry from the topology table
                    eigrpProcess->topologyTable->RemoveEntry(destination);
                }
                else
                {
                    // Find new successor and update routing table
                    UpdateRoutingTableForDestination(destination);
                }
            }
        }
        // Notify neighbors to remove routes
        eigrpProcess->NotifyRoutingChange(removedRoutes, /*isRemoval*/ true);

        // Remove neighbor from neighbor map
        neighbors.erase(neighborIp);
    }

    void EigrpInterface::HandleNeighborRestart(const std::string &neighborIp)
    {
        // Locate the neighbor
        auto neighborIt = neighbors.find(neighborIp);
        if (neighborIt == neighbors.end()) return;
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor = neighborIt->second;
        
        // Reset sequence numbers and reliable packets
        {
            std::lock_guard<std::mutex> lock(neighbor->neighborDataMutex);
            neighbor->reliablePackets.clear();
            neighbor->lastReceivedSequenceNumber = 0;
        }

        SendHelloPacket(false, 0, neighborIp);

        // Clear any existing timers
        if (neighbor->holdTimerId != 0)
        {
            TimeManager::getInstance().CancelTimer(neighbor->holdTimerId);
            neighbor->holdTimerId = 0;
        }

        // Reinitialize hold timer
        StartHoldTimer(neighborIp, neighbor->holdTime);
    }

    void EigrpInterface::UpdateRoutingTableForDestination(const std::string &destination)
    {

        TopologyTable::TopologyEntry *entry = eigrpProcess->topologyTable->FindBestRoute(destination);
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
                routingTable.AddEigrp(newRoute);

                eigrpProcess->NotifyRoutingChange({newRoute}, /*isRemoval*/ false);
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
        auto sendTimeIt = neighbor->reliablePackets.find(sequenceNumber);
        if (sendTimeIt == neighbor->reliablePackets.end())
        {
            return neighbor->srtt;
        }

        auto sendTime = sendTimeIt->second.sendTime;
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
            neighbor->rttvar = (1.0 - beta) * neighbor->rttvar + beta * std::abs(neighbor->srtt - rttSample);
            neighbor->srtt = (1.0 - alpha) * neighbor->srtt + alpha * rttSample;
            neighbor->rto = neighbor->srtt + std::max(0.1, 4.0 * neighbor->rttvar);
            neighbor->rto = std::clamp(neighbor->rto, 1.0, 60.0); // Bounds: 1s to 60s
        }
    }

    int EigrpInterface::GetNextSequenceNumber(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor) {
        std::lock_guard<std::mutex> lock(neighbor->neighborDataMutex);
        if (neighbor->globalSequenceNumber == INT32_MAX) {
            neighbor->globalSequenceNumber = 1; // Reset or handle rollover appropriately
        } else {
            neighbor->globalSequenceNumber++;
        }
        return neighbor->globalSequenceNumber;
    }

    void EigrpInterface::SetupReliablePacket(std::shared_ptr<EigrpConfigs::NeighborInfo> &neighbor, const std::string &packet, int sequenceNum)
    {
        neighbor->reliablePackets[sequenceNum] = EigrpConfigs::NeighborInfo::ReliablePacketInfo{
            .packet = packet,
            .sendTime = std::chrono::steady_clock::now(),
            .retransmissionCount = 0,
            .timerId = 0
        };

        // Start retransmission timer
        int timerId = StartRetransmissionTimer(neighbor->ipAddress, sequenceNum, neighbor->rto);
        neighbor->reliablePackets[sequenceNum].timerId = timerId;
    }

    std::string EigrpInterface::FindQueryNeighbor(int queryId)
    {
        std::lock_guard<std::mutex> lock(replyTrackingMutex);
        auto it = outstandingReplies.find(queryId);
        if (it != outstandingReplies.end())
        {
            return it->second.first;
        }
        return "";
    }

    std::string EigrpInterface::EncodeSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute)
    {
        std::string route;
        route += std::string("\x00\x00\x00\x00", 4);
        route += Functions::numToByte((10000000 / currentInterface->Get().bandwidth) * 256, 4);
        route += Functions::numToByte((currentInterface->Get().delay / 10) * 256, 4);
        route += Functions::numToByte(currentInterface->Get().mtu, 3);
        route += std::string("\x00", 1);
        route += std::string("\xff", 1);
        route += Functions::numToByte(load, 1);
        route += std::string("\x00\x00", 2);
        route += Functions::numToByte(summaryRoute.mask, 1);
        route += Functions::compactNetworkAddress(summaryRoute.network, summaryRoute.mask);

        return route;
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
        entry.successors.clear();
        entry.feasibleSuccessors.clear();

        for (auto &[neighbor, route] : entry.routesByNeighbor)
        {
            if (route.feasibleDistance == bestFeasibleDistance)
            {
                route.isSuccessor = true;
                entry.successors.push_back(neighbor);
            }
            else
            {
                route.isSuccessor = false;
            }

            // Feasibility Condition
            if (route.reportedDistance <= bestFeasibleDistance)
            {
                route.isFeasibleSuccessor = true;
                entry.feasibleSuccessors.push_back(neighbor);
            }
            else
            {
                route.isFeasibleSuccessor = false;
            }
        }
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
        if (it == topologyEntries.end())
        {
            return nullptr;
        }

        TopologyEntry& entry = it->second;

        double bestFD = std::numeric_limits<double>::infinity();
        for (const auto& [neighbor, route] : entry.routesByNeighbor)
        {
            if (route.feasibleDistance < bestFD)
            {
                bestFD = route.feasibleDistance;
            }
        }

        // Select successors and feasible successors
        entry.successors.clear();
        entry.feasibleSuccessors.clear();

        for (auto& [neighbor, route] : entry.routesByNeighbor)
        {
            if (route.feasibleDistance == bestFD)
            {
                route.isSuccessor = true;
                entry.successors.push_back(neighbor);
            }
            else
            {
                route.isSuccessor = false;
            }

            // Feasibility Condition
            if (route.reportedDistance < bestFD)
            {
                route.isFeasibleSuccessor = true;
                entry.feasibleSuccessors.push_back(neighbor);
            }
            else
            {
                route.isFeasibleSuccessor = false;
            }
        }

        return &entry;
    }

    void TopologyTable::HandleRouteFailure(const std::string &destination)
    {
        std::lock_guard<std::mutex> lock(tableMutex);
        auto it = topologyEntries.find(destination);
        if (it != topologyEntries.end())
        {
            TopologyEntry &entry = it->second;
            entry.isActive = false;

            // Remove all neighbors from the destination
            entry.routesByNeighbor.clear();

            // Remove the topology table entry if no neighbors exist
            if (entry.routesByNeighbor.empty())
            {
                topologyEntries.erase(it);
            }
        }
    }

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
        std::lock_guard<std::mutex> lock(tableMutex);
        topologyEntries.erase(destination);
    }

    void TopologyTable::SetVariance(int var)
    {
        std::lock_guard<std::mutex> lock(tableMutex);
        variance = var;
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
