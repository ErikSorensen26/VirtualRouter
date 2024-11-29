#include <Eigrp.h>
#include <Encapsulation.h>
#include <algorithm>

#define MAX_RETRANSMISSIONS 16
#define FULL_UPDATE "FULL_UPDATE"
#define EMPTY_UPDATE "EMPTY_UPDATE"
#pragma region Eigrp

// STORE BACKUPS IN MAIN, INTERFACES GET DELETED

// Global mutex for EIGRP operations
mutex globalEigrpMutex;

namespace Protocol
{

    Eigrp::Eigrp(int &as, AddressFamily af) : addressFamily(af), asNumber(as) {}

    Eigrp::~Eigrp()
    {
        Shutdown();
    }

    void Eigrp::InitializeEigrp()
    {
        EigrpConfigs::EigrpConfigs processConfigs;
        processConfigs.wideMetric = 10000000;
        processConfigs.variance = 1;
        processConfigs.kvalue.k1_Bandwidth = 1;
        processConfigs.kvalue.k2_Load = 0;
        processConfigs.kvalue.k3_Delay = 1;
        processConfigs.kvalue.k4_Reliability = 0;
        processConfigs.kvalue.k5_MTU = 0;
        processConfigs.kvalue.k6_Power = 0;        

        configs = processConfigs;

        topologyTable = std::make_unique<TopologyTable>(this);

        UpdateInterfaceList();
        UpdateRoutingTableForConnected();

        for (auto& [interfaceId, interface] : eigrpInterfaceList)
        {
            interface->StartHelloHelper();

            for (auto& neighbor : interface->getNeighbors())
            {
                // Preserve existing communication mode if already exists
                if (neighbor->mode == EigrpConfigs::CommunicationMode::UNICAST)
                {
                    neighbor->mode = EigrpConfigs::CommunicationMode::UNICAST;
                }
                else
                {
                    neighbor->mode = EigrpConfigs::CommunicationMode::MULTICAST;
                }

                interface->SendHelloPacket(/*update=*/true, 0, neighbor->ipAddress);
                interface->StartHoldTimer(neighbor->ipAddress, interface->getConfigs()->holdTime);
            }
        }
        
        Logger::getInstance().info() << "EIGRP process initiated.";
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
            paramTLV.value = CalculateParameters(eigrpInt->getConfigs()->holdTime);
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

        // Retreive NeighborInfo
        auto neighbor = eigrpInt->GetNeighborInfo(neighborIp);
        if (neighbor.has_value() && neighbor.value()->authenticationEnabled)
        {
            eigrpHeader::Option authTLV = eigrpInt->GenerateAuthenticatedTLV(eigrp, neighbor.value());
            if (authTLV.option != string("\x00", 1))
            {
                eigrp.options.push_back(authTLV);
            }
        }
    }

    void Eigrp::EigrpUpdate(eigrpHeader &eigrp, int sequenceNum, bool init, bool conditional, bool restart, bool endoftable, bool query, bool reply)
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

        for (const auto &network : configs.networks)
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
        if (bandwidth == 0) return std::numeric_limits<unsigned int>::infinity();

        // Calculate individual components of the metric
        double bandwidthMetric = configs.wideMetric / bandwidth;
        double delayMetric = delay / 10.0;
        double loadMetric = (configs.kvalue.k2_Load * bandwidth) / (256.0 - load);

        double reliabilityFactor = (reliability + configs.kvalue.k4_Reliability);
        double compositeMetric = (configs.kvalue.k1_Bandwidth * bandwidthMetric) +
                                 loadMetric +
                                 (configs.kvalue.k3_Delay * delayMetric);

        // Account for K5 (optional scaling)
        if (configs.kvalue.k5_MTU != 0 && reliabilityFactor > 0) {
            compositeMetric *= configs.kvalue.k5_MTU / reliabilityFactor;
        }

        // Final scaling for the metric
        return std::min(compositeMetric * 256, 4294967295.0); // Max metric value
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
                        std::shared_ptr<EigrpInterface> instance;
                        if (!interface.second->eigrpInterfaceList[asNumber])
                        {
                            std::shared_ptr<EigrpInterfaceInstance> interfaceInstance = std::make_shared<EigrpInterfaceInstance>();
                            interface.second->eigrpInterfaceList[asNumber] = interfaceInstance;
                        }
                        if (getAddressFamily() == AddressFamily::IPv6)
                        {
                            instance = std::make_shared<EigrpInterface>(*this, interface.second);
                            interface.second->eigrpInterfaceList[asNumber]->IPv6 = instance;
                        }
                        else if (getAddressFamily() == AddressFamily::IPv4)
                        {
                            instance = std::make_shared<EigrpInterface>(*this, interface.second);
                            interface.second->eigrpInterfaceList[asNumber]->IPv4 = instance;
                        }
                        eigrpInterfaceList[interface.second->Get().id] = instance;

                        if (configs.autoSummarizationEnabled)
                        {
                            std::string networkIt = interface.second->Get().ip;
                            auto majorNetwork = Functions::findClassfullNetwork(networkIt);
                            AddSummaryRoute(majorNetwork, Functions::getDefaultMask(majorNetwork));
                        }
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
        params += Functions::numToByte(configs.kvalue.k1_Bandwidth, 1);
        params += Functions::numToByte(configs.kvalue.k2_Load, 1);
        params += Functions::numToByte(configs.kvalue.k3_Delay, 1);
        params += Functions::numToByte(configs.kvalue.k4_Reliability, 1);
        params += Functions::numToByte(configs.kvalue.k5_MTU, 1);
        params += Functions::numToByte(configs.kvalue.k6_Power, 1);
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
                std::lock_guard<std::mutex> lock(eigrpInterfacePtr->eigrpInterfaceMutex);
                eigrpBw = eigrpInterfacePtr->getConfigs()->bandwidth;
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
                    connectedRoute.load = configs.variance;
                    connectedRoute.network = connectedNetwork;
                    connectedRoute.mask = Functions::byteMaskToNum(interfaceInfo.subnet);
                    connectedRoute.nextHop = std::string(connectedNetwork.size(), '\x00'); // indicates directly connected
                    connectedRoute.metric = CalculateMetric(eigrpBw, 0, interfaceInfo.delay, 255);
                    connectedRoute.routeType = "connected";


                bool hasChanged = false;

                {
                    std::lock_guard<std::mutex> lock(eigrpInterfacePtr->advertisedRouteMutex);
                    auto it = eigrpInterfacePtr->getAdvertisedRoutes().find(connectedNetwork);
                    if (it == eigrpInterfacePtr->getAdvertisedRoutes().end() || it->second.metric != connectedRoute.metric)
                    {
                        // Route is new or has changed
                        hasChanged = true;
                        eigrpInterfacePtr->getAdvertisedRoutes()[connectedNetwork] = connectedRoute;
                    }
                }

                if (hasChanged)
                {
                    // Insert into Routing Table
                    routingTable.AddEigrp(connectedRoute, addressFamily);
                    
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
                routingTable.RemoveEigrp(connectedNetwork, mask, getAddressFamily());

                // Notify neighbors of route removal
                RoutingTable::Eigrp removedRoute;
                removedRoute.network = connectedNetwork;
                removedRoute.mask = mask;
                removedRoute.nextHop = std::string(connectedNetwork.size(), '\xff');
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

    void Eigrp::OnInterfaceChange(Interface *interfacePtr, AddressFamily af)
    {
        ipInfo interfaceInfo = interfacePtr->Get();
        std::lock_guard<std::mutex> lock(eigrpMutex);
        RoutingTable &routingTable = RoutingTable::getInstance();

        int mask = Functions::byteMaskToNum(interfaceInfo.subnet);

        // Compute the connected network
        std::string connectedNetwork = Functions::computeNetworkAddress(interfaceInfo.ip, mask);

        // Check if the route already exists
        bool routeExists = false;

        auto eigrpTable = routingTable.GetAllEigrpRoutes(getAddressFamily());

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
                    connectedRoute.load = configs.variance;
                    connectedRoute.network = connectedNetwork;
                    connectedRoute.mask = Functions::byteMaskToNum(interfaceInfo.subnet);
                    connectedRoute.nextHop = std::string(connectedNetwork.size(), '\x00'); // indicates directly connected
                    connectedRoute.metric = CalculateMetric(interfaceInfo.bandwidth, 0, interfaceInfo.delay, 255);
                    connectedRoute.routeType = "connected";

                RoutingTable::getInstance().AddEigrp(connectedRoute, getAddressFamily());

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
                routingTable.RemoveEigrp(connectedNetwork, mask, getAddressFamily());

                // Notify neighbors of route removal
                RoutingTable::Eigrp removedRoute;
                removedRoute.network = connectedNetwork;
                removedRoute.mask = mask;
                removedRoute.nextHop = std::string(connectedNetwork.size(), '\x00');
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

        // Adjust summaries based on added/removed routes
        for (const auto &[_, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            auto neighborsCopy = eigrpInterfacePtr->getNeighbors();
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
                        eigrpInterfacePtr->SendUpdateToNeighbor(neighborEntry->ipAddress, updatedRoutes, EigrpConfigs::UpdateType::SPECIFIC, false);
                    }
                    else 
                    {
                        eigrpInterfacePtr->SendUpdateToNeighbor(neighborEntry->ipAddress, updatedRoutes, EigrpConfigs::UpdateType::SPECIFIC, isRemoval);
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
            eigrpInterface->SendHelloPacket(false, 0, std::string("\xff\xff\xff\xff", 4)); // Termination message 
            eigrpInterface->StopHello();
        }
        eigrpInterfaceList.clear();
        topologyTable.reset(); // Clear the topology table
        Logger::getInstance().info() << "EIGRP shutdown complete.";
    }

    void Eigrp::RedistributeRoute(const std::string &destination, int mask, const std::string &protocol)
    {
        RoutingTable& routingTable = RoutingTable::getInstance();
        auto route = routingTable.GetEigrpRoute(destination, mask, getAddressFamily());

        if (route.has_value())
        {
            // Convert route to external EIGRP and notify neighbors
            RoutingTable::Eigrp externalRoute = route.value();
            externalRoute.routeType = "external";
            externalRoute.metric += configs.redistributionMetricOffset;

            routingTable.UpdateEigrp(externalRoute, getAddressFamily());
            NotifyRoutingChange({externalRoute}, false);
        }
    }

    void Eigrp::AddNetwork(const EigrpConfigs::network& newNetwork)
    {
        configs.networks.push_back(newNetwork);

        if (configs.autoSummarizationEnabled)
        {
            std::string tempNetwork = newNetwork.ip;
            std::string network = Functions::findClassfullNetwork(tempNetwork);
            int networkMask = Functions::getDefaultMask(network);

            // Check of the summary route allready exists
            if (!IsRouteSummarized(network, networkMask) && network != std::string(newNetwork.ip.size(), '\x00'))
            {
                AddSummaryRoute(network, networkMask);
            }
        }

        // Update interfaces and routing table after adding the network
        UpdateInterfaceList();
        UpdateRoutingTableForConnected();
    }

    void Eigrp::AddSummaryRoute(const std::string& network, int mask, bool isAuto)
    {
        std::lock_guard<std::mutex> lock(eigrpMutex);

        // Validate network and mask
        if (!Functions::compareNetworkWithMask(network, mask))
        {
            // Mask is not valid
            return;
        }

        // Check for overlapping summary routes
        if (IsRouteSummarized(network, mask))
        {
            return; // Already summarized
        }
        // Add new summary route
        EigrpConfigs::SummaryRoute summaryRoute;
        summaryRoute.network = network;
        summaryRoute.mask = mask;
        summaryRoute.isAuto = isAuto;
        summaryRoutes.push_back(summaryRoute);

        // Inject the summary route into the routing table as an internal summary route
        RoutingTable::Eigrp internalSummaryRoute;
        internalSummaryRoute.network = network;
        internalSummaryRoute.mask = mask;
        internalSummaryRoute.nextHop = std::string(network.size(), '\x00');
        internalSummaryRoute.metric = CalculateMetric(0, 0, 0, 255);
        internalSummaryRoute.routeType = "summary";

        RoutingTable::getInstance().AddEigrp(internalSummaryRoute, getAddressFamily());

        // Update interfaces to advertise the new summary route
        UpdateInterfacesWithSummaryRoute(summaryRoute);
    }

    void Eigrp::RemoveSummaryRoute(const std::string& network, int mask)
    {
        std::lock_guard<std::mutex> lock(eigrpMutex); // Esures thread safety
        
        auto it = std::remove_if(summaryRoutes.begin(), summaryRoutes.end(),
            [&](const EigrpConfigs::SummaryRoute& sr) {
                return sr.network == network && sr.mask == mask;
            });
        if (it != summaryRoutes.end())
        {
            summaryRoutes.erase(it);

            // Remove from routing table
            RoutingTable::getInstance().RemoveEigrp(network, mask, getAddressFamily());

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

    void Eigrp::EnableAutoSummary(bool enable)
    {
        std::lock_guard<std::mutex> lock(eigrpMutex);

        if (enable == configs.autoSummarizationEnabled)
        {
            return; // No change
        }

        configs.autoSummarizationEnabled = enable;
        
        if (enable)
        {
            // Add summary routes for all calssfull networks
            for (const auto& network : configs.networks)
            {
                std::string networkIt = network.ip;
                std::string majorNetwork = Functions::findClassfullNetwork(networkIt);
                int defaultMask = Functions::getDefaultMask(majorNetwork);

                if (!IsRouteSummarized(majorNetwork, defaultMask))
                {
                    AddSummaryRoute(majorNetwork, defaultMask, true);
                }
            }
        }
        else 
        {
            // Remove all summary Routes
            for (const auto& summaryRoute : summaryRoutes)
            {
                if (summaryRoute.isAuto)
                {
                    RemoveSummaryRoute(summaryRoute.network, summaryRoute.mask);
                }
            }
        }
    }
   
    void Eigrp::SetStub(bool isStub, bool advertiseConnected, bool advertiseStatic, bool advertiseSummary, bool advertiseRedistributed)
    {
        std::lock_guard<std::mutex> lock(eigrpMutex);
        configs.stubConfig.isStub = isStub;
        configs.stubConfig.advertiseConnected = advertiseConnected;
        configs.stubConfig.advertiseStatic = advertiseStatic;
        configs.stubConfig.advertiseSummary = advertiseSummary;
        configs.stubConfig.advertiseRedistributed = advertiseRedistributed;

        // Update stub routes across all interfaces
        for (auto & [id, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            eigrpInterfacePtr->HandleStubRouteUpdates();
        }
    }

    int Eigrp::GetLowestBandwidth()
    {
        unsigned int lowestBW = std::numeric_limits<unsigned int>::infinity();
        for (const auto eigrpInterfacePtr : eigrpInterfaceList)
        {
            if (eigrpInterfacePtr.second->getConfigs()->bandwidth < lowestBW)
            {
                lowestBW = eigrpInterfacePtr.second->getConfigs()->bandwidth;
            }
        }
        return lowestBW;
    }

    void Eigrp::AddDefaultRoute()
    {
        if (configs.advertiseDefault)
            return;

        unsigned int lowestBW = GetLowestBandwidth();
        if (lowestBW == std::numeric_limits<unsigned int>::infinity()) {
            Logger::getInstance().error() << "No active interfaces available to advertise the default route.";
            return;
        }

        std::shared_ptr<EigrpInterface> selectedInterface;
        for (const auto& [id, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            if (eigrpInterfacePtr->getConfigs()->bandwidth == lowestBW)
            {
                selectedInterface = eigrpInterfacePtr;
                break;
            }
        }

        if (!selectedInterface)
        {
            Logger::getInstance().error() << "Selected interface for default route not found.";
            return;
        }
        // Calculate metric
        double metric = CalculateMetric(selectedInterface->getConfigs()->bandwidth, selectedInterface->getConfigs()->load, selectedInterface->getConfigs()->delay, 255, 0);

        RoutingTable::Eigrp defaultRoute;
        defaultRoute.network = configs.defaultNetwork;
        defaultRoute.nextHop = std::string(defaultRoute.network.size(), '\x00');
        defaultRoute.mask = configs.defaultMask;
        defaultRoute.metric = metric;
        defaultRoute.routeType = "default";

        // Add to routing table
        RoutingTable::getInstance().AddEigrp(defaultRoute, getAddressFamily());

        // Notify neighbors about the new default route
        NotifyRoutingChange({defaultRoute}, /*isRemoval=*/false);

        Logger::getInstance().info() << "Default route (" << defaultRoute.network << "/" << defaultRoute.mask << ") added with metric " << defaultRoute.metric;
    }

    void Eigrp::RemoveDefaultRoute()
    {
        if (!configs.advertiseDefault)
            return;
        
        auto defaultRoute = RoutingTable::getInstance().GetEigrpRoute(configs.defaultNetwork, configs.defaultMask, getAddressFamily());
        if (defaultRoute.has_value())
        {
            RoutingTable::getInstance().RemoveEigrp(configs.defaultNetwork, configs.defaultMask, getAddressFamily());
            NotifyRoutingChange({defaultRoute.value()}, /*isRemoval=*/true);
        }
    }

    void Eigrp::SetVariance(int var)
    {
        std::lock_guard<std::mutex> lock(eigrpMutex);
        configs.variance = var;
    }

    void Eigrp::RecalculateRoutes()
    {
        // Iterate through the topology table and update routing table based on new Variance
        for (auto& [destination, entry] : topologyTable->GetTopologyEntries())
        {
            // Find all feasible successors within the Variance
            for (const auto& [neighbor, routeInfo] : entry.routesByNeighbor)
            {
                if (routeInfo.feasibleDistance <= entry.bestFD && routeInfo.feasibleDistance <= entry.bestFD * configs.variance)
                {
                    // Add or update route in the routing table
                    RoutingTable::Eigrp newRoute;
                    newRoute.network = destination;
                    newRoute.mask = entry.prefixLength;
                    newRoute.nextHop = neighbor;
                    newRoute.metric = routeInfo.feasibleDistance;
                    newRoute.routeType = "internal";

                    RoutingTable::getInstance().AddEigrp(newRoute, getAddressFamily());
                }
            }
        }
    }

    void Eigrp::GracefulRestart()
    {
        std::lock_guard<std::mutex> lock(eigrpMutex);

        Logger::getInstance().info() << "Initiating Graceful Restart";

        // Notify neighbors of restart
        for (auto& [id, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            for (const auto& neighborInfo : eigrpInterfacePtr->getNeighbors())
            {
                eigrpInterfacePtr->SendHelloPacket(false, 0, neighborInfo->ipAddress); // Send restart flag
            }
        }

        Logger::getInstance().info() << "EIGRP Graceful Restart initialted.";
    }

    void Eigrp::Restart()
    {
        std::lock_guard<std::mutex> lock(eigrpMutex);
    
        Logger::getInstance().info() << "Restarting EIGRP process...";

        // Step 1: Backup neighbors    for (const auto& [interfaceId, interfacePtr] : eigrpInterfaceList) {
        {
            std::lock_guard<std::mutex> lock(backupMutex);
            neighborBackup.clear();
            for (const auto& [interfaceId, interfacePtr] : eigrpInterfaceList) 
            {
                std::string ipAddress = interfacePtr->currentInterface->Get().ip;
                for (const auto& neighbor : interfacePtr->getNeighbors())
                {
                    auto backup = make_shared<EigrpConfigs::NeighborInfo>();
                    backup->ipAddress = neighbor->ipAddress;
                    backup->authKey = neighbor->authKey;
                    backup->authType = neighbor->authType;
                    backup->authenticationEnabled = neighbor->authenticationEnabled;
                    backup->holdTime = neighbor->holdTime;
                    backup->mode = neighbor->mode;

                    neighborBackup[ipAddress][neighbor->ipAddress] = backup; // Store neighbor in backup
                }
            }
        }
        
        // Step 2: Shutdown current state
        Shutdown();

        // Step 3: Reinitialize the EIGRP process
        InitializeEigrp();

        Logger::getInstance().info() << "EIGRP process restarted successfully.";
    }

    void Eigrp::Cleanup()
    {
        std::lock_guard<std::mutex> lock(eigrpMutex);
        for (auto& [id, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            eigrpInterfacePtr->StopHello();
            eigrpInterfacePtr->CancelStuckInActive();
        }
        eigrpInterfaceList.clear();
        configs.networks.clear();
        summaryRoutes.clear();
        Logger::getInstance().info() << "EIGRP Cleanup complete";
    }

    void Eigrp::PeriodicMaintenance()
    {
        topologyTable->PruneStaleRoutes();
    }

#pragma endregion

#pragma region EigrpInterface

    EigrpInterface::EigrpInterface(Eigrp &eigrpSystem, std::shared_ptr<Interface> interface)
        : currentInterface(interface),
          eigrpProcess(&eigrpSystem),
          helloStartTime(std::chrono::steady_clock::now())
    {
        configs.bandwidth = currentInterface->Get().bandwidth;
        StartHelloHelper();

        // Create Eigrp DistributionList
    }

    EigrpInterface::~EigrpInterface()
    {
    }

    PacketInfo EigrpInterface::EigrpBody(string unicastAddress)
    {
        // Determine the address family from the EIGRP process
        AddressFamily af = eigrpProcess->getAddressFamily();
        ethernetHeader eth;
        PacketInfo packet;
        eth.sourceMac = currentInterface->macAddress;

        if (af == AddressFamily::IPv6)
        {        
            eth.destinationMac = variable.multicast.eigrp.macv6;
            eth.type = variable.ethernet.ipv6;

            ipv6Header ip;
            ip.version = "6";
            ip.trafficClass = Functions::numToHex(configs.DSCP, 2);
            ip.flowLabel = Functions::numToHex(currentInterface->Get().ipv6FlowLabel, 5);
            ip.payloadLength = std::string("\x00\x00", 2); // Will be calculated later
            ip.protocol = variable.ipv6.eigrp;
            ip.hopLimit = std::string("\xff", 1);
            ip.sourceAddress = currentInterface->Get().ipv6;
            ip.destinationAddress = variable.multicast.eigrp.addressv6;

            if (!unicastAddress.empty())
            {
                ip.destinationAddress = unicastAddress;
                if (neighbors[unicastAddress]->hasMac)
                {
                    eth.destinationMac = neighbors[unicastAddress]->macAddress;
                }
            }
            else

            packet.Layer3.emplace_back(ip);

        }
        else if (af == AddressFamily::IPv4)
        {
            eth.destinationMac = variable.multicast.eigrp.mac;
            eth.type = variable.ethernet.ipv4;

            ipv4Header ip;
            ip.version = "4";
            ip.headerLength = "5";
            ip.serviceField = Functions::numToByte(configs.DSCP, 1);
            ip.totalLength = std::string("\x00\x00", 2);
            ip.identification = std::string("\x00\x00", 2);
            ip.fragmentFlag.reserved = "0";
            ip.fragmentFlag.fragment = "0";
            ip.fragmentFlag.moreFragment = "0";
            ip.fragmentFlag.fragment = "0000000000000";
            ip.TTL = std::string("\xff", 1);
            ip.protocol = variable.ipv4.eigrp;
            ip.checksum = std::string("\x00\x00", 2);
            ip.sourceAddress = currentInterface->Get().ip;
            ip.destinationAddress = variable.multicast.eigrp.address;
            
            if (!unicastAddress.empty())
            {
                ip.destinationAddress = unicastAddress;
                if (neighbors[unicastAddress]->hasMac)
                {
                    eth.destinationMac = neighbors[unicastAddress]->macAddress;
                }
            }

            packet.Layer3.emplace_back(ip);
        }

        packet.Layer2.emplace_back(eth);
        return packet;
    }

    void EigrpInterface::ProcessPacket(const eigrpHeader *eigrpPacket, const std::string& neighborIp)
    {
        {
            std::lock_guard<std::mutex> lock(neighborMutex);
            auto neighborIt = neighbors.find(neighborIp);

//            if (neighborIt == neighbors.end())
//            {
//                Logger::getInstance().warn() << "Received packet from unknown sender: " << Functions::byteToHex(neighborIp);
//                return;
//            }
            
//            auto neighbor = neighborIt->second;
        }
        if (configs.isPassive)
        {
            Logger::getInstance().info() << "Interface is passive. Incoming EIGRP packet ignored.";
            return;
        }

        if (eigrpPacket->opcode == variable.eigrp.type.hello)
        {
            if (Functions::byteToNum(eigrpPacket->ack) == 0)
            {
                ProcessHello(eigrpPacket, neighborIp);
            }
            else
            {
                ProcessAck(eigrpPacket->ack, neighborIp);
            }
        }
        else if (eigrpPacket->opcode == variable.eigrp.type.update)
        {
            ProcessUpdate(eigrpPacket, neighborIp);
        }
        else if (eigrpPacket->opcode == variable.eigrp.type.reply)
        {
            ProcessReply(eigrpPacket, neighborIp);
        }
        else if (eigrpPacket->opcode == variable.eigrp.type.query)
        {
            ProcessQuery(eigrpPacket, neighborIp);
        }
    }

    void EigrpInterface::ProcessHello(const eigrpHeader *receivedHello, const std::string& neighborIp)
    {
        if (configs.interfaceMode == EigrpConfigs::Mode::POINT_TO_POINT)
        {
            // In point-to-point mode, ensure there's only one neighbor
            if (!neighbors.empty() && neighbors.find(neighborIp) == neighbors.end())
            {
                Logger::getInstance().warn() << "Point-to-point interface already has a neighbor: " << neighbors.begin()->first;
                return;
            }
        }

        // Validate Autonomous System Number
        if (Functions::byteToNum(receivedHello->autonomousSystem) != eigrpProcess->getAsNumber())
        {
            // Drop the packet - AS number mismatch
            return;
        }

        // Extract Hold Time from options
        int recievedHoldTime = configs.holdTime; // Default holdtime
        for (const auto& opt : receivedHello->options)
        {
            if (opt.option == variable.eigrp.options.parameter)
            {
                string parameters = eigrpProcess->CalculateParameters(configs.holdTime);
                if (opt.value.substr(0, 5) != parameters.substr(0, 5)) return;
                // Extract Holdtime
                recievedHoldTime = Functions::byteToNum(opt.value.substr(6, 2));

                // Check for Peer Termination
                if (parameters.substr(0, 5) == std::string("\xFF\xFF\xFF\xFF\xFF", 5))
                {
                    HandleNeighborDown(neighborIp);
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
                std::string ipAddress = currentInterface->Get().ip;
                std::lock_guard<std::mutex> lock(eigrpProcess->backupMutex);
                auto backupIt = eigrpProcess->neighborBackup[ipAddress].find(neighborIp);
                if (backupIt != eigrpProcess->neighborBackup[ipAddress].end())
                {
                    neighbors[neighborIp] = backupIt->second;
                    neighbors[neighborIp]->lastHeard = std::chrono::steady_clock::now();

                    Logger::getInstance().info() << "Restored neighbor from backup: " << neighborIp;

                    eigrpProcess->neighborBackup[ipAddress].erase(backupIt);
                }
            }
            auto neighbor = neighbors[neighborIp];

            bool isNewNeighbor = (neighbor->holdTimerId == 0 || !neighbor->hasMac);

            neighbor->ipAddress = neighborIp;
            neighbor->holdTime = recievedHoldTime;
            neighbor->lastHeard = std::chrono::steady_clock::now();

            if (eigrpProcess->getAddressFamily() == AddressFamily::IPv6) 
            {
                // Get neighbor mac Address
                // NEEDS IMPLEMENTATION
            }
            if (eigrpProcess->getAddressFamily() == AddressFamily::IPv4)
            {
                // Get neighbor mac Address
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

            vector<RoutingTable::Eigrp> eigrpTable = RoutingTable::getInstance().GetAllEigrpRoutes(eigrpProcess->getAddressFamily());

            // If it's a new neighbor, send a full update
            if (isNewNeighbor && !neighbor->adjacency)
            {
                neighbor->adjacency = true;
                neighbor->globalSequenceNumber = 1;
                SendHelloPacket(true, neighbor->globalSequenceNumber, neighborIp);
                vector<RoutingTable::Eigrp> empty;
                if (neighbor->receivedInitUpdate)
                {
                    SendUpdateToNeighbor(neighborIp, empty, EigrpConfigs::UpdateType::EMPTY, false);
                }
                else
                {
                    bool end = true;
                    for (const auto& update : eigrpTable) 
                    {
                        if (!Functions::compareNetworkWithIp(update.network, neighbor->ipAddress))
                        {
                            end = false;
                        }
                    }
                    if (end)
                    {
                        SendUpdateToNeighbor(neighborIp, empty, EigrpConfigs::UpdateType::CONDITIONAL, false);
                    }
                }
            }
            else if (neighbor->adjacency && neighbor->hasMac && !neighbor->isInit && neighbor->receivedInitUpdate && !neighbor->sendInitUpdate)
            {
                SendUpdateToNeighbor(neighborIp, eigrpTable, EigrpConfigs::UpdateType::FULL, false);
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
        if (receivedUpdate->flags.restart == "1") { HandleNeighborRestart(neighborIp); return; } // CAUSING FUCKING PROBLEMS
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
                
                route = DecodeRoute(option.value, (option.option == variable.eigrp.options.externalRoute), false);

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
                routingTable.AddEigrp(route, eigrpProcess->getAddressFamily());
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
            Logger::getInstance().warn() << "Received ACK from unknown neighbor: " << Functions::byteToHex(neighborIp);
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

            Logger::getInstance().debug() << "ACK processed for neighbor " << Functions::byteToHex(neighborIp) << " sequence number " << ackSequenceNumber;
        }
        else
        {
            Logger::getInstance().warn() << "Received ACK for unknown sequence number " << ackSequenceNumber << " from neighbor " << Functions::byteToHex(neighborIp);
        }
    }

    void EigrpInterface::ProcessQuery(const eigrpHeader *receivedQuery, const std::string &neighborIp)
    {
        auto eigrpProcess = this->eigrpProcess;
        
        // Stub routing query blocking
        if (eigrpProcess->IsStub())
        {
            // Extract the queries network from the query options
            std::vector<RoutingTable::Eigrp> replyRoutes;

            for (const auto& option : receivedQuery->options)
            {
                if (option.option == variable.eigrp.options.internalRoute)
                {
                    // Decode the queried route
                    RoutingTable::Eigrp queriedRoute = DecodeRoute(option.value, /*external=*/false, /*summary=*/false);

                    // Check if the queried route exists
                    std::optional<RoutingTable::Eigrp> existingRoute = RoutingTable::getInstance().GetEigrpRoute(queriedRoute.network, queriedRoute.mask, eigrpProcess->getAddressFamily());
                    if (existingRoute)
                    {
                        replyRoutes.push_back(existingRoute.value());
                    }
                    else
                    {
                        queriedRoute.metric = std::numeric_limits<unsigned int>::infinity();
                        replyRoutes.push_back(queriedRoute);
                    }
                }
            }
            
            // Send replies to the querying neighbor
            SendReplyToNeighbor(neighborIp, replyRoutes);
            return; // Do not propagate the query further;
        }

        vector<RoutingTable::Eigrp> queryRoutes;
        vector<RoutingTable::Eigrp> validQueryRoutes;
        int queryId = Functions::byteToNum(receivedQuery->sequence);

        if (outstandingReplies.count(queryId) > 0)
        {
            Logger::getInstance().info() << "Duplicate query received from neighbor: " << neighborIp;
            return;
        }

        // Extract the destination network network and mask from the Query options
        for (const auto& opt : receivedQuery->options)
        {
            RoutingTable::Eigrp route;
            if (opt.option == variable.eigrp.options.internalRoute)
            {
                route = DecodeRoute(opt.value, /*external=*/false, /*summary=*/false);
            }
            else
            {
                continue;
            }

            std::optional<RoutingTable::Eigrp> routeIt = RoutingTable::getInstance().GetEigrpRoute(route.network, route.mask, eigrpProcess->getAddressFamily());
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
                RoutingTable::Eigrp route = DecodeRoute(opt.value, /*external*/false, /*summary=*/false);
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

    size_t EigrpInterface::CalculateMaxRoutesPerPacket(AddressFamily af, bool isExternal)
    {
        size_t maxPacketSize = configs.mtu;
        size_t headerSize = 60; // Estimate size of EIGRP header and base overhead
        size_t routeSize = 0;

        if (af == AddressFamily::IPv4)
        {
            routeSize = 20; // Base size for IPv4 route
            if (isExternal)
            {
                routeSize += 20; // Additional size for external routes
            }
        }
        else if (af == AddressFamily::IPv6)
        {
            routeSize = 40; // Base size for IPv6 route
            if (isExternal)
            {
                routeSize += 20; // Additional size for external routes
            }
        }

        return (maxPacketSize - headerSize) / routeSize;
    }

    void EigrpInterface::SendAckToNeighbor(const std::string &neighborIp, int sequenceNumber)
    {
        // Retrieve neighbor information
        auto neighborIt = neighbors.find(neighborIp);
        if (neighborIt == neighbors.end())
        {
            Logger::getInstance().warn() << "Attempted to send Ack to unknown neighbor: " << neighborIp;
            return;
        }
        auto neighbor = neighborIt->second;
        auto eigrpProcess = this->eigrpProcess;

        // Create the EIGRP Ack packet
        PacketInfo eigrpAckPacketStructure;
        eigrpHeader eigrp;

        if (eigrpProcess->getAddressFamily() == AddressFamily::IPv6)
        {
            // Set the destination MAC and IP to the neighbors
            // NEEDS IMPLEMENTATION
        }
        else
        {
            // Set the destination MAC and IP to the neighbors
            std::lock_guard<std::mutex> lock(neighbor->macMutex);
            if (neighbor->macAddress.empty())
            {
                auto mac = RoutingTable::getInstance().ArpLookup(neighborIp);
                if (mac.has_value())
                {
                    neighbor->macAddress = mac->mac;
                }
                else
                {
                    Logger::getInstance().info() << "MAC address for neighbor " << neighborIp << " not found. Sending ARP request.";
                    currentInterface->arp->sendRequest(neighborIp);
                    return;
                }
            }
        }

        eigrpAckPacketStructure = EigrpBody();
        eigrpProcess->EigrpHello(eigrp, this, true, false, sequenceNumber, neighborIp);

        // Generate and append Authentication TLV if enabled for this neighbor
        if (neighbor->authenticationEnabled)
        {
            eigrpHeader::Option authTLV = GenerateAuthenticatedTLV(eigrp, neighbor);
            if (authTLV.option != std::string("\x00", 1))
            {
                eigrp.options.push_back(authTLV);
            }
        }

        // Assemble the packet
        eigrpAckPacketStructure.Layer3.push_back(eigrp);

        // Convert to raw packet
        string eigrpAckPacket = Encapsulate(eigrpAckPacketStructure);

        // Enqueue for transmission
        currentInterface->packetOutQueue.enqueue(eigrpAckPacket);
    }

    void EigrpInterface::SendUpdateToNeighbor(const std::string &neighborIp, const std::vector<RoutingTable::Eigrp> &routes, EigrpConfigs::UpdateType updateType, bool restart)
    {
        auto neighborIt = neighbors.find(neighborIp);
        if (neighborIt == neighbors.end())
        {
            Logger::getInstance().warn() << "Attempted to send update to unknown neighbor: " << neighborIp;
            return;
        }

        auto neighbor = neighborIt->second;
        auto eigrpProcess = this->eigrpProcess;

        // Determine target IP based on communication mode
        std::string targetIp = (neighbor->mode == EigrpConfigs::CommunicationMode::UNICAST) ? neighborIp : "";

        // Get the next sequence number
        int sequenceNumber = GetNextSequenceNumber(neighbor);

        // Filter routes based on stub configuration
        std::vector<RoutingTable::Eigrp> filteredRoutes;
        for (const auto& route : routes)
        {
            if (!(configs.splitHorizon && route.nextHop == neighborIp))
            {
                if (eigrpProcess->IsStub())
                {
                    if ((route.routeType == "connected" && eigrpProcess->AdvertiseConnected()) ||
                        (route.routeType == "static" && eigrpProcess->AdvertiseStatic()) ||
                        (route.routeType == "summary" && eigrpProcess->AdvertiseSummary()) ||
                        (route.routeType == "external" && eigrpProcess->AdvertiseRedistributed()))
                    {
                        filteredRoutes.push_back(route);
                    }
                }
                else
                {
                    filteredRoutes.push_back(route);
                }
            }
        }

        // Create the EIGRP Update packet structure
        PacketInfo eigrpPacket = EigrpBody(neighborIp);
        eigrpHeader eigrp;

        size_t maxRoutesPerPacket = CalculateMaxRoutesPerPacket(eigrpProcess->getAddressFamily(), /*isExernal=*/false);
        size_t routeCount = 0;
        auto it = filteredRoutes.begin();

        // Send in batches if there are too many routes
        bool endOfTable = false;
        bool isConditional = false;
        bool isInit = (updateType == EigrpConfigs::UpdateType::FULL || updateType == EigrpConfigs::UpdateType::CONDITIONAL);
        do
        {
            eigrp.options.clear();
            routeCount = 0;
            
            // Add routes to the packet
            for (; it != filteredRoutes.end() && routeCount < maxRoutesPerPacket; ++it, ++routeCount)
            {
                eigrpHeader::Option routeOptions;
                routeOptions.option = variable.eigrp.options.internalRoute;

                if (it->routeType == "external")
                {
                    routeOptions.value = EncodeExternalRouteOption(*it);
                    routeOptions.option = (eigrpProcess->getAddressFamily() == AddressFamily::IPv4) ? variable.eigrp.options.externalRoute : variable.eigrp.options.externalRouteV6;
                }
                else if (it->routeType == "internal")
                {
                    routeOptions.value = EncodeRouteOption(*it);
                    routeOptions.option = (eigrpProcess->getAddressFamily() == AddressFamily::IPv4) ? variable.eigrp.options.externalRoute : variable.eigrp.options.externalRouteV6;
                }
                routeOptions.length = Functions::numToByte(routeOptions.value.size() + 4, 2);
                eigrp.options.push_back(routeOptions);
            }

            endOfTable = (it == filteredRoutes.end());
            isConditional = !neighbor->isInit && updateType == EigrpConfigs::UpdateType::CONDITIONAL;

            // Set flags based on update type and stage
            endOfTable = (it == filteredRoutes.end());
            eigrpProcess->EigrpUpdate(eigrp, sequenceNumber, {}, 
                                      /*init=*/(updateType == EigrpConfigs::UpdateType::FULL),
                                      /*conditional=*/isConditional, 
                                      /*restart=*/restart, 
                                      /*endOfTable*/(endOfTable && filteredRoutes.size() > 1));

            // Handle Acknowledgments
            if (!neighbor->pendingAcks.empty())
            {
                eigrp.ack = Functions::numToByte(neighbor->pendingAcks.front(), 4);
                neighbor->pendingAcks.erase(neighbor->pendingAcks.begin());
            }

            // Add authentication TLV if enabled
            if (neighbor->authenticationEnabled)
            {
                eigrpHeader::Option authTLV = GenerateAuthenticatedTLV(eigrp, neighbor);
                if (authTLV.option != std::string("\x00", 1))
                {
                    eigrp.options.push_back(authTLV);
                }
            }

            // Assemble and send the packet
            eigrpPacket.Layer3.clear();
            eigrpPacket.Layer3.push_back(eigrp);

            std::string eigrpRawPacket = Encapsulate(eigrpPacket);
            currentInterface->packetOutQueue.enqueue(eigrpRawPacket);

            SetupReliablePacket(neighbor, eigrpRawPacket, sequenceNumber);
        }
        while (!endOfTable);

        // Mark neighbor as initialized if this is a full update
        if (updateType == EigrpConfigs::UpdateType::FULL && endOfTable)
        {
            neighbor->isInit = true;
            Logger::getInstance().info() << "Neighbor " << Functions::byteToHex(neighborIp) << " marked as initialized.";
        }

        Logger::getInstance().info() << "Update sent to neighbor " << Functions::byteToHex(neighborIp) << " with sequence number " << sequenceNumber;
    }

    void EigrpInterface::SendQueryToNeighbors(const vector<RoutingTable::Eigrp>& failedRoutes, const std::string& originNeighborIp)
    {
        std::lock_guard<std::mutex> lock(neighborMutex);
        auto eigrpProcess = this->eigrpProcess;

        if (eigrpProcess->IsStub())
        {
            // Instead of propagating the query, respond with route unavailable
            for (const auto& route : failedRoutes)
            {
                eigrpProcess->NotifyRoutingChange({route}, /*isRemoval=*/true, /*init=*/false);
            }
        }

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
        auto neighborIt = neighbors.find(neighborIp);
        if (neighborIt == neighbors.end())
        {
            Logger::getInstance().warn() << "Attempted to send query to unknown neighbor: " << Functions::byteToHex(neighborIp);
            return;
        }
        auto neighbor = neighborIt->second;
        auto eigrpProcess = this->eigrpProcess;

        // Increment sequence number for this route/query
        int currentSeqNum = GetNextSequenceNumber(neighbor);

        // Create the EIGRP Query Packet
        PacketInfo eigrpQueryPacketStructure = EigrpBody(neighborIp);
        eigrpHeader eigrp;

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
        eigrp.virtualRouterID = eigrpProcess->getVirtualRouterID();
        eigrp.autonomousSystem = Functions::numToByte(eigrpProcess->getAsNumber(), 2);

        // Generate and append Authentication TLV if enabled for this neighbor
        if (neighbor->authenticationEnabled)
        {
            eigrpHeader::Option authTLV = GenerateAuthenticatedTLV(eigrp, neighbor);
            if (authTLV.option != std::string("\x00", 1))
            {
                eigrp.options.push_back(authTLV);
            }
        }

        // Assemble the packet
        eigrpQueryPacketStructure.Layer3.push_back(eigrp);

        // Convert to raw packet string
        std::string eigrpQueryPacket = Encapsulate(eigrpQueryPacketStructure);
        currentInterface->packetOutQueue.enqueue(eigrpQueryPacket);

        // Store the packet for possible retransmission (relieable delivery)
        SetupReliablePacket(neighbor, eigrpQueryPacket, currentSeqNum);

        Logger::getInstance().info() << "Sent query to neighbor " << Functions::byteToHex(neighborIp) << " with sequence number " << currentSeqNum;
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
        auto neighborIt = neighbors.find(neighborIp);
        if (neighborIt == neighbors.end())
        {
            Logger::getInstance().warn() << "Attempted to send reply to unknown neighbor: " << neighborIp;
            return;
        }
        auto neighbor = neighborIt->second;
        auto eigrpProcess = this->eigrpProcess;

        // Increment sequence number for this route/reply
        int currentSeqNum = GetNextSequenceNumber(neighbor);

        // Create the EIGRP Reply Packet
        PacketInfo eigrpReplyPacketStructure = EigrpBody(neighborIp);
        eigrpHeader eigrp;

        // Construct the Reply option with the route information
        for (const auto& route : routes)
        {
            eigrpHeader::Option replyOption;
            replyOption.option = variable.eigrp.options.internalRoute;
            replyOption.value = EncodeRouteOption(route);
            replyOption.length = Functions::numToByte(replyOption.value.size() + 4, 2);
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
        eigrp.virtualRouterID = eigrpProcess->getVirtualRouterID();
        eigrp.autonomousSystem = Functions::numToByte(eigrpProcess->getAsNumber(), 2);

        // Generate and append Authentication TLV if enabled for this neighbor
        if (neighbor->authenticationEnabled)
        {
            eigrpHeader::Option authTLV = GenerateAuthenticatedTLV(eigrp, neighbor);
            if (authTLV.option != std::string("\x00", 1))
            {
                eigrp.options.push_back(authTLV);
            }
        }

        // Assemble the packet
        eigrpReplyPacketStructure.Layer3.push_back(eigrp);

        // Convert to raw packet string
        std::string eigrpReplyPacket = Encapsulate(eigrpReplyPacketStructure);

        // Enqueue for transmission
        currentInterface->packetOutQueue.enqueue(eigrpReplyPacket);

        // Store the packet for possible retransmission (reliable delivery)
        SetupReliablePacket(neighbor, eigrpReplyPacket, currentSeqNum);

        Logger::getInstance().info() << "Sent reply to neighbor " << Functions::byteToHex(neighborIp) << " with sequence number " << currentSeqNum;
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

    std::string EigrpInterface::EncodeExternalRouteOption(const RoutingTable::Eigrp& route) const
    {
        std::string encoded;
        encoded += Functions::changeSize(route.originRouter, 4);
        encoded += Functions::numToByte(route.originAS, 4);
        encoded += Functions::numToByte(route.routeTag, 4);
        encoded += Functions::numToByte(route.delay, 4);
        encoded += Functions::numToByte(route.bandwidth, 4);
        encoded += Functions::numToByte(route.mtu, 3);
        encoded += Functions::numToByte(route.hopCount, 1);
        encoded += Functions::numToByte(route.reliability, 1);
        encoded += Functions::numToByte(route.load, 1);
        encoded += Functions::numToByte(route.mask, 1);
        encoded += Functions::compactNetworkAddress(route.network, route.mask);
        return encoded;
    }

    void EigrpInterface::AdvertiseSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute)
    {
        std::lock_guard<std::mutex> lock(neighborMutex);

        // Construct the route to advertise
        RoutingTable::Eigrp summaryEigrpRoute = EncodeSummaryRoute(summaryRoute);

        for (const auto &[neighborIp, neighborInfo] : neighbors) {
            if (neighborInfo->isInit) {
                // Send the update directly
                SendUpdateToNeighbor(neighborIp, {summaryEigrpRoute}, EigrpConfigs::UpdateType::SPECIFIC, /*removal=*/false);
            }
        }
    }

    void EigrpInterface::WithdrawSummaryRoute(const std::string& network, int mask)
    {    
        std::lock_guard<std::mutex> lock(neighborMutex);

        RoutingTable::Eigrp withdrawRoute;
        withdrawRoute.network = network;
        withdrawRoute.mask = mask;
        withdrawRoute.metric = std::numeric_limits<unsigned int>::infinity(); // Indicate route is withdrawn

        for (const auto &[neighborIp, neighborInfo] : neighbors) {
            if (neighborInfo->isInit) {
                // Send the update directly
                SendUpdateToNeighbor(neighborIp, {withdrawRoute}, EigrpConfigs::UpdateType::SPECIFIC, /*removal=*/true);
            }
        }
    }

    void EigrpInterface::HandleStubRouteUpdates()
    {
        std::lock_guard<std::mutex> lock(advertisedRouteMutex);
        auto eigrpProcess = this->eigrpProcess;

        // Identify routes that should no longer be advertised
        std::vector<RoutingTable::Eigrp> routesToWithdraw;
        for (const auto& [routeKey, advertisedRoute] : advertisedRoutes)
        {
            bool shouldAdvertise = false;

            if (eigrpProcess->IsStub())
            {
                if (advertisedRoute.routeType == "connected" && eigrpProcess->AdvertiseConnected())
                    shouldAdvertise = true;
                if (advertisedRoute.routeType == "static" && eigrpProcess->AdvertiseStatic())
                    shouldAdvertise = true;
                if (advertisedRoute.routeType == "summary" && eigrpProcess->AdvertiseSummary())
                    shouldAdvertise = true;
                if (advertisedRoute.routeType == "external" &&  eigrpProcess->AdvertiseRedistributed())
                    shouldAdvertise = true;
            }
            else
            {
                shouldAdvertise = true;
            }

            if (!shouldAdvertise)
            {
                // Prepare to withdraw this route
                RoutingTable::Eigrp withdrawRoute = advertisedRoute;
                withdrawRoute.metric = std::numeric_limits<unsigned int>::infinity();
                withdrawRoute.nextHop = std::string(advertisedRoute.network.size(), '\xff');
                withdrawRoute.routeType = "withdrawn";

                routesToWithdraw.push_back(withdrawRoute);

                // Remove from advertised routes
                advertisedRoutes.erase(routeKey);
            }
        }
        
        if (!routesToWithdraw.empty())
        {
            // Withdraw routes to all neighbors
            for (const auto& [neighborIp, neighborInfo] : neighbors)
            {
                if (!neighborInfo->isInit) continue;

                // Send withdraw updates
                SendUpdateToNeighbor(neighborIp, routesToWithdraw, EigrpConfigs::UpdateType::SPECIFIC, /*removal=*/true);
            }
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

        if (helloTimerId != 0)
        {
            return; // Timer already active
        }

        if (helloStartTime.time_since_epoch().count() == 0)
        {
            helloStartTime = std::chrono::steady_clock::now();
        }

        auto nextExpiration = helloStartTime + std::chrono::seconds(configs.helloTime);

        helloTimerId = TimeManager::getInstance().AddTimer(nextExpiration, [this]()
        {
            try
            {
                SendHelloPacket();
            }
            catch (const std::exception &e)
            {
                Logger::getInstance().error() << "[StartHello] Exception in SendHelloPacket: " << e.what() << std::endl;
            }
            catch (...)
            {
                Logger::getInstance().error() << "[StartHello] Unknown exception in SendHelloPacket." << std::endl;
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
        Logger::getInstance().info() << "Hello timer started with expiration at " << std::chrono::duration_cast<std::chrono::seconds>(nextExpiration.time_since_epoch()).count();
    }

    void EigrpInterface::SendHelloPacket(bool update, int sequenceNum, string neighborIp)
    {
        if (configs.isPassive)
        {
            Logger::getInstance().info() << "Interface is passive. Hello packet not sent.";
            return;
        }

        AddressFamily af = eigrpProcess->getAddressFamily();
        if (!neighborIp.empty())
        {
            std::string targetIp = (neighbors[neighborIp]->mode == EigrpConfigs::CommunicationMode::UNICAST) ? neighborIp : "";
        }

        PacketInfo eigrpHello = EigrpBody(neighborIp);
        eigrpHeader eigrp;

        eigrpProcess->EigrpHello(eigrp, this, false, update, sequenceNum, neighborIp);

        // Add stub flags
        if (eigrpProcess->IsStub())
        {
            eigrpHeader::Option stubOption;
            stubOption.option = variable.eigrp.options.stub;
            stubOption.value = EncodeStubOption(eigrpProcess->getConfigs()->stubConfig);
            stubOption.length = Functions::numToByte(stubOption.value.size() + 4, 2);
            eigrp.options.push_back(stubOption);
        }

        eigrpHello.Layer3.push_back(eigrp);

        std::string eigrpHelloPacket = Encapsulate(eigrpHello);
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
        if (neighbor->holdTimerId != 0)
        {
            TimeManager::getInstance().CancelTimer(neighbor->holdTimerId);
        }

        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(holdTime);
        neighbor->holdTimerId = TimeManager::getInstance().AddTimer(expirationTime, [this, neighborIp]()
                                                                   { HandleHoldTimeExpire(neighborIp); });
    }

    void EigrpInterface::HandleHoldTimeExpire(const std::string &neighborIp)
    {
        //std::lock_guard<std::mutex> lock(neighborMutex);
        //auto it = neighbors.find(neighborIp);
        //if (it != neighbors.end())
        //{
            //HandleNeighborDown(neighborIp);
        //}
    }

    void EigrpInterface::StartActiveTimer(const RoutingTable::Eigrp& route)
    {
        if (!eigrpProcess->getConfigs()->activeTimerEnabled) return;
        auto key = route.network + "/" + std::to_string(route.mask);
        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(eigrpProcess->getConfigs()->activeTime);

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
        eigrpProcess->topologyTable->HandleRouteFailure(route.network, route.nextHop);

        // Step 3: Remove the route from the routing table
        RoutingTable& routingTable = RoutingTable::getInstance();
        routingTable.RemoveEigrp(route.network, route.mask, eigrpProcess->getAddressFamily());

        // Step 4: Notify neighbors about the route removal
        RoutingTable::Eigrp removedRoute = route;
        removedRoute.metric = std::numeric_limits<unsigned int>::infinity();
        removedRoute.nextHop = std::string(route.nextHop.size(), '\xff');
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
        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(eigrpProcess->getConfigs()->stuckInActiveTime);

        // Schedule Stuck In Active timer
        stuckInActiveTimerId = TimeManager::getInstance().AddTimer(expirationTime, [this]()
        {
            HandleStuckInActive();
        });
    }

    void EigrpInterface::HandleStuckInActive()
    {
        std::lock_guard<std::mutex> lock(activeTimerMutex);

        for (const auto& [key, timerId] : activeTimers)
        {
            auto delimiter = key.find('/');
            if (delimiter == std::string::npos) continue;

            std::string network = key.substr(0, delimiter);
            int mask = std::stoi(key.substr(delimiter + 1));   

            // Log stuck-in-active route
            Logger::getInstance().warn() << "Handling stuck-in-active for network: " << network << "/" << mask;

            // Cancel timer
            CancelActiveTimer(network, mask);

            // Try recalculating a new route
            auto entry = eigrpProcess->topologyTable->FindBestRoute(network, eigrpProcess->getConfigs()->variance);
            if (entry && !entry->successors.empty())
            {
                UpdateRoutingTableForDestination(network);
            }
            else
            {
                // Log route removal
                Logger::getInstance().info() << "Removing stuck-in-active route: " << network << "/" << mask;

                // make sure route exists
                std::string neighborIp;
                std::optional<RoutingTable::Eigrp> route = RoutingTable::getInstance().GetEigrpRoute(network, mask, eigrpProcess->getAddressFamily());
                if (route.has_value())
                {
                    neighborIp = route.value().nextHop;
                }
                
                RoutingTable::Eigrp routeToRemove;
                routeToRemove.network = network;
                routeToRemove.mask = mask;
                routeToRemove.metric = std::numeric_limits<unsigned int>::infinity();
                routeToRemove.nextHop = std::string(network.size(), '\xff');
                routeToRemove.routeType = "internal";

                eigrpProcess->NotifyRoutingChange({routeToRemove}, true);
                eigrpProcess->topologyTable->HandleRouteFailure(network, neighborIp);
                RoutingTable::getInstance().RemoveEigrp(network, mask, eigrpProcess->getAddressFamily());
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
        if (timeout <= 0.0)
        {
            Logger::getInstance().error() << "Invalid timeout value for retransmission timer.";
            return 0;
        }
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor;
        {
            std::lock_guard<std::mutex> lock(neighborMutex);
            auto it = neighbors.find(neighborIp);
            if (it == neighbors.end())
            {
                Logger::getInstance().warn() << "Cannot start retransmission for unknown neighbor: " << neighborIp;
                return 0;
            }
            neighbor = it->second;
        }
        
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
        {
            std::lock_guard<std::mutex> lock(neighbor->neighborDataMutex);
            neighbor->reliablePackets[sequenceNumber].timerId = timerId;
        }

        Logger::getInstance().debug() << "Retransmission timer started for neighbor: " << Functions::byteToHex(neighborIp) << " with sequence number: " << sequenceNumber << " and timeout: " << timeout << " seconds";

        return timerId;
    }

    void EigrpInterface::HandleRetransmissionTimeout(const std::string &neighborIp, const int& sequenceNumber)
    {
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor;
        {
            std::lock_guard<std::mutex> lock(neighborMutex);
            auto it = neighbors.find(neighborIp);
            if (it == neighbors.end()) {
                Logger::getInstance().warn() << "Retransmission timeout for unknown neighbor: " << neighborIp;
                return;
            }
            neighbor = it->second;
        }

        EigrpConfigs::NeighborInfo::ReliablePacketInfo pktInfo;
        {
            std::lock_guard<std::mutex> dataLock(neighbor->neighborDataMutex);
            auto pktIt = neighbor->reliablePackets.find(sequenceNumber);
            if (pktIt == neighbor->reliablePackets.end())
            {
                Logger::getInstance().warn() << "No reliable packet found for sequence number: " << sequenceNumber << " with neighbor: " << Functions::byteToHex(neighborIp);
                return;
            }
            pktInfo = pktIt->second;
        }

        // Check retransmission count
        if (pktInfo.retransmissionCount >= MAX_RETRANSMISSIONS)
        {
            Logger::getInstance().info() << "Max retransmission reached for neighbor " << Functions::byteToHex(neighborIp) << " sequence number " << sequenceNumber << ". marking neighbor as down.";
            if (pktInfo.timerId != 0)
            {
                TimeManager::getInstance().CancelTimer(pktInfo.timerId);
            }
            HandleNeighborDown(neighborIp);
            return;
        }

        // Resend the packet
        currentInterface->packetOutQueue.enqueue(pktInfo.packet);
        Logger::getInstance().info() << "Resent packet to neighbor " << Functions::byteToHex(neighborIp) << " for sequence number " << sequenceNumber << ". Retransmission count: " << pktInfo.retransmissionCount + 1 << ".";

        // Restart the retransmission timer
        
        StartRetransmissionTimer(neighborIp, sequenceNumber, neighbor->rto);

        // Increment retransmission count and update RTT estimates
        {
            std::lock_guard<std::mutex> dataLock(neighbor->neighborDataMutex);
            neighbor->reliablePackets[sequenceNumber].retransmissionCount += 1;
            neighbor->rto = std::min(neighbor->rto * 2.0, 60.0); // Exponential backoff with a cap
        }
    }
    RoutingTable::Eigrp EigrpInterface::DecodeRoute(string value, bool external, bool summary)
    {
        RoutingTable::Eigrp route;
        size_t start = 0;
        
        try
        {
            // Parse next hop based on address family
            if (eigrpProcess->getAddressFamily() == AddressFamily::IPv4)
            {
                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for IPv4 next Hop");
                route.nextHop = value.substr(start, 4);
                start += 4;
            }
            else if (eigrpProcess->getAddressFamily() == AddressFamily::IPv6)
            {
                if (value.size() < start + 16) throw std::runtime_error("Insufficient date for IPv6 next Hop");
                route.nextHop = value.substr(start, 16);
                start += 16;
            }
            else
            {
                throw std::runtime_error("Unsupported AddressFamily");
            }

            if (!external) {
                // Internal Route Parsing
                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Delay.");
                route.delay = Functions::byteToNum(value.substr(start, 4));
                start += 4;

                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Bandwidth.");
                route.bandwidth = Functions::byteToNum(value.substr(start, 4));
                start += 4;

                if (value.size() < start + 3) throw std::runtime_error("Insufficient data for MTU.");
                route.mtu = Functions::byteToNum(value.substr(start, 3));
                start += 3;
    
                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Hop Count.");
                route.hopCount = Functions::byteToNum(value.substr(start, 1));
                start += 1;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Reliability.");
                route.reliability = Functions::byteToNum(value.substr(start, 1));
                start += 1;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Load.");
                route.load = Functions::byteToNum(value.substr(start, 1));
                start += 1;

                if (value.size() < start + 2) throw std::runtime_error("Insufficient data for Route Tag.");
                route.routeTag = Functions::byteToNum(value.substr(start, 1));
                start += 2;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Mask.");
                route.mask = Functions::byteToNum(value.substr(start, 1));
                start += 1;

                if (value.size() < start + route.mask) throw std::runtime_error("Insufficient data for Network Address.");
                route.network = value.substr(start, route.mask);
                start += route.mask;

                route.routeType = summary ? "summary" : "internal";
            }
            else 
            {
                // External Route Parsing
                if (eigrpProcess->getAddressFamily() == AddressFamily::IPv4) 
                {
                    if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Origin Router (IPv4).");
                    route.originRouter = Functions::byteToNum(value.substr(start, 4));
                    start += 4;
                }
                else if (eigrpProcess->getAddressFamily() == AddressFamily::IPv6) 
                {
                    if (value.size() < start + 16) throw std::runtime_error("Insufficient data for Origin Router (IPv6).");
                    route.originRouter = Functions::byteToNum(value.substr(start, 16));
                    start += 16;
                }

                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Origin AS.");
                route.originAS = Functions::byteToNum(value.substr(start, 4));
                start += 4;

                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Route Tag (External).");
                route.routeTag = Functions::byteToNum(value.substr(start, 4));
                start += 4;

                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Extended Metric.");
                route.extendedMetric = Functions::byteToNum(value.substr(start, 4));
                start += 4;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Extended ID.");
                route.extendedId = Functions::byteToNum(value.substr(start, 1));
                start += 1;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Flags.");
                route.flags = value.substr(start, 1);
                start += 1;

                // Parsing additional fields if necessary...
                // Ensure all fields are parsed based on EIGRP specifications

                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Delay (External).");
                route.delay = Functions::byteToNum(value.substr(start, 4));
                start += 4;

                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Bandwidth (External).");
                route.bandwidth = Functions::byteToNum(value.substr(start, 4));
                start += 4;

                if (value.size() < start + 3) throw std::runtime_error("Insufficient data for MTU (External).");
                route.mtu = Functions::byteToNum(value.substr(start, 3));
                start += 3;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Hop Count (External).");
                route.hopCount = Functions::byteToNum(value.substr(start, 1));
                start += 1;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Reliability (External).");
                route.reliability = Functions::byteToNum(value.substr(start, 1));
                start += 1;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Load (External).");
                route.load = Functions::byteToNum(value.substr(start, 1));
                start += 1;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Mask (External).");
                route.mask = Functions::byteToNum(value.substr(start, 1));
                start += 1;

                if (value.size() < start + route.mask) throw std::runtime_error("Insufficient data for Network Address (External).");
                route.network = value.substr(start, route.mask);
                start += route.mask;

                route.routeType = "external";
            }

            // Pad network address to standard length
            size_t standardLength = (eigrpProcess->getAddressFamily() == AddressFamily::IPv4) ? 4 : 16;
            if (route.network.size() < standardLength) 
            {
                route.network += std::string(standardLength - route.network.size(), '\x00');
            }

            // Calculate Feasible Distance and Composite Metric
            double neighborRD = eigrpProcess->CalculateMetric(route.bandwidth, route.load, route.delay, route.reliability);
            route.reportedDistance = neighborRD;
    
            // Calculate FD = Local Link Cost + RD
            double localLinkCost = CalculateLocalLinkCost();
            route.feasibleDistance = localLinkCost + route.reportedDistance;
    
            // Calculate the composite metric for internal use
            route.metric = route.feasibleDistance;

            // Set the administrative distance
            route.adminDistance = (route.routeType == "internal" || route.routeType == "summary")
                ? eigrpProcess->getConfigs()->adminDistance
                : eigrpProcess->getConfigs()->externalAdminDistance;

            return route;
        }
        catch (const std::exception& e)
        {
            Logger::getInstance().error() << "DecodedRoute Error: " << e.what();
            throw;
        }
    }

    double EigrpInterface::CalculateLocalLinkCost()
    {
        const ipInfo& ifaceInfo = currentInterface->Get();

        double bandwidthMetric = (static_cast<double>(eigrpProcess->getConfigs()->wideMetric) / ifaceInfo.bandwidth);
        double delayMetric = static_cast<double>(ifaceInfo.delay) / 10.0;

        double loadMetric = 0.0;
        if (eigrpProcess->getConfigs()->kvalue.k2_Load != 0 && (256.0 - configs.load) != 0)
        {
            loadMetric = (static_cast<double>(eigrpProcess->getConfigs()->kvalue.k2_Load) * configs.load) / (256 - configs.load);
        }

        // Calculate link cost using K-values
        double linkCost = (eigrpProcess->getConfigs()->kvalue.k1_Bandwidth * bandwidthMetric) +
                          loadMetric +
                          (eigrpProcess->getConfigs()->kvalue.k3_Delay * delayMetric);

        // Apply scaling factor and reliability
        double reliabilitySum = configs.reliability + eigrpProcess->getConfigs()->kvalue.k4_Reliability;
        if (reliabilitySum > 0 && eigrpProcess->getConfigs()->kvalue.k5_MTU != 0)
        {
            linkCost *= (eigrpProcess->getConfigs()->kvalue.k5_MTU) / reliabilitySum;
        }

        return linkCost;
    }

    void EigrpInterface::UpdateRoutingTable(const vector<RoutingTable::Eigrp> routes, bool init, const std::string& neighborIp) {
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

            // Add or update the route in the topology table
            eigrpProcess->topologyTable->AddOrUpdateRoute(route.network, route.mask, routeInfo, neighborIp);

            // Apply Duel algorithm to determine the best route
            TopologyTable::TopologyEntry *bestRouteEntry = eigrpProcess->topologyTable->FindBestRoute(route.network, eigrpProcess->getConfigs()->variance);
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

                    if (route.routeType == "internal")
                    {
                        newRoute.adminDistance = eigrpProcess->getConfigs()->adminDistance;
                    }
                    else if (route.routeType == "external")
                    {
                        newRoute.adminDistance = eigrpProcess->getConfigs()->externalAdminDistance;
                    }
                    else if (route.routeType == "default")
                    {
                        newRoute.adminDistance = eigrpProcess->getConfigs()->externalAdminDistance;
                    }

                    // Access the routing table to check existing conditions
                    std::optional<RoutingTable::Eigrp> existingRoute = RoutingTable::getInstance().GetEigrpRoute(route.network, route.mask, eigrpProcess->getAddressFamily());
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
                        // Set administrative distance  based on route type
                        if (newRoute.routeType == "internal")
                        {
                            newRoute.adminDistance = eigrpProcess->getConfigs()->adminDistance;
                        }
                        else if (newRoute.routeType == "external")
                        {
                            newRoute.adminDistance = eigrpProcess->getConfigs()->externalAdminDistance;
                        }
                        else if  (newRoute.routeSource == "summary")
                        {
                            newRoute.adminDistance = eigrpProcess->getConfigs()->summaryAdminDistance;
                        }
                        else if (newRoute.routeSource == "default")
                        {
                            newRoute.adminDistance = eigrpProcess->getConfigs()->defaultAdminDistance;
                        }
                        else 
                        {
                            Logger::getInstance().error() << "Unknown route type: " << route.routeType;
                            continue; // Skip unknown route type
                        }
                        
                        // Update the global EIGRP table
                        RoutingTable::getInstance().AddEigrp(newRoute, eigrpProcess->getAddressFamily());

                        // Notify neighbors about the route change
                        updatedRoutes.push_back(newRoute);
                    }
                }
            }
            else
            {
                Logger::getInstance().warn() << "No feasible successors for route: " << Functions::hexToByte(route.network) << "/" << route.mask;
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
        if (neighbors.find(neighborIp) == neighbors.end())
        {
            Logger::getInstance().warn() << "Attempted to handle down state for non-existent neighbor: " << neighborIp;
            return;
        }
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
        neighbor->reliablePackets.clear();
        neighbor->sequenceList.clear();

        // Removed Routes
        vector<RoutingTable::Eigrp> removedRoutes;
        eigrpProcess->topologyTable->RemoveRoutesFromNeighbor(neighborIp);

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
                    eigrpProcess->topologyTable->RemoveRoutesFromNeighbor(destination);

                    // Notify neighbors of the trade
                    RoutingTable::Eigrp removedRoute;
                    removedRoute.network = destination;
                    removedRoute.mask = entry.prefixLength;
                    removedRoute.nextHop = std::string(destination.size(), '\xff');
                    removedRoute.metric = std::numeric_limits<unsigned int>::infinity();
                    removedRoute.routeType = "internal";
                    removedRoutes.push_back(removedRoute);
                    
                    Logger::getInstance().info() << "Removed destination " << destination << " from topology table due to neighbor down.";
                }
                else
                {
                    // Find new successor and update routing table
                    UpdateRoutingTableForDestination(destination);
                }
            }
        }

        if (!removedRoutes.empty())
        {
            eigrpProcess->NotifyRoutingChange(removedRoutes, /*isRemoval=*/true);
        }

        // Remove neighbor from neighbor map
        neighbors.erase(neighborIp);

        Logger::getInstance().info() << "Neighbor " << neighborIp << " marked as down. Topology and routing tables updated.";
    }

    void EigrpInterface::HandleNeighborRestart(const std::string &neighborIp)
    {
        auto neighborIt = neighbors.find(neighborIp);
        if (neighborIt == neighbors.end()) return;

        auto neighbor = neighborIt->second;

        // Reset sequence numbers and reliable packets
        {
            std::lock_guard<std::mutex> dataLock(neighbor->neighborDataMutex);
            neighbor->reliablePackets.clear();
            neighbor->lastReceivedSequenceNumber = 0;
            neighbor->globalSequenceNumber = 1;
        }

        // Cancel existing timers
        if (neighbor->holdTimerId != 0)
        {
            TimeManager::getInstance().CancelTimer(neighbor->holdTimerId);
            neighbor->holdTimerId = 0;
        }
        for (const auto& [seqNum, pktInfo] : neighbor->reliablePackets)
        {
            if (pktInfo.timerId != 0)
            {
                TimeManager::getInstance().CancelTimer(pktInfo.timerId);
            }
        }

        neighbor->reliablePackets.clear();
        neighbor->sequenceList.clear();

        // Reinitialize neighbor state
        neighbor->receivedInitUpdate = false;
        neighbor->isInit = false;

        // Send a hello packet to re-establish communication
        SendHelloPacket(false, 0, neighborIp);

        // Restart the hold timer
        StartHoldTimer(neighborIp, neighbor->holdTime);
    }

    void EigrpInterface::UpdateRoutingTableForDestination(const std::string &destination)
    {
        TopologyTable::TopologyEntry *entry = eigrpProcess->topologyTable->FindBestRoute(destination, eigrpProcess->getConfigs()->variance);
        if (!entry) return;
        
        // Find the successor route
        auto successorIt = std::find_if(entry->routesByNeighbor.begin(), entry->routesByNeighbor.end(),
                                        [](const auto &pair) { return pair.second.isSuccessor; });

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
            routingTable.AddEigrp(newRoute, eigrpProcess->getAddressFamily());

            eigrpProcess->NotifyRoutingChange({newRoute}, /*isRemoval*/ false);
        }
        else
        {
            RoutingTable &routingTable = RoutingTable::getInstance();
            routingTable.RemoveEigrp(destination, entry->prefixLength, eigrpProcess->getAddressFamily());
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
        std::lock_guard<std::mutex> lock(neighbor->neighborDataMutex);
        
        if (neighbor->reliablePackets.count(sequenceNum) == 0)
        {
            neighbor->reliablePackets[sequenceNum] = {packet, std::chrono::steady_clock::now(), 0, 0};
        }
        else
        {
            Logger::getInstance().warn() << "Packet with sequence number " << sequenceNum << " already exists for neighbor " << Functions::byteToHex(neighbor->ipAddress);
        }

        // Create ReliablePacketInfo
        EigrpConfigs::NeighborInfo::ReliablePacketInfo pktInfo;
        pktInfo.packet = packet;
        pktInfo.sendTime = std::chrono::steady_clock::now();
        pktInfo.retransmissionCount = 0;

        // Start retransmission timer
        pktInfo.timerId = StartRetransmissionTimer(neighbor->ipAddress, sequenceNum, neighbor->rto);

        // Store the packet
        neighbor->reliablePackets[sequenceNum] = pktInfo;

        Logger::getInstance().debug() << "Reliable packet setup for neighbor " << Functions::byteToHex(neighbor->ipAddress) << " with sequence number " << sequenceNum;
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

    RoutingTable::Eigrp EigrpInterface::EncodeSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute)
    {
        RoutingTable::Eigrp route;
        route.nextHop = std::string(summaryRoute.network.size(), '\x00');
        route.bandwidth = (10000000 / currentInterface->Get().bandwidth) * 256;
        route.delay = (currentInterface->Get().delay / 10) * 256;
        route.mtu = currentInterface->Get().mtu;
        route.hopCount = 0;
        route.reliability = 255;
        route.load = configs.load;
        route.routeTag = 0;
        route.mask = summaryRoute.mask;
        route.network = summaryRoute.network;
        route.routeType = "summary";

        return route;
    }

    std::string EigrpInterface::EncodeStubOption(const EigrpConfigs::StubConfig stub)
    {
        std::string binStub = "0000000000";
        binStub += "0"; // RECEIVE ONLY
        binStub += "0"; // LEAK MAP
        if (stub.advertiseRedistributed) binStub += "1";
        else binStub += "0";
        if (stub.advertiseSummary) binStub += "1";
        else binStub += "0";
        if (stub.advertiseStatic) binStub += "1";
        else binStub += "0";
        if (stub.advertiseConnected) binStub += "1";
        else binStub += "0";
        
        return Functions::binToByte(binStub, 2);
    }

    bool EigrpInterface::isNeighborAuthenticated(const std::string& neighborIp)
    {
        std::lock_guard<std::mutex> lock(neighborMutex);
        auto neighborIt = neighbors.find(neighborIp);
        if (neighborIt != neighbors.end())
        {
            return neighborIt->second->authenticationEnabled;
        }
        return false;
    }

    void EigrpInterface::ConfigureAuthentication(const std::string& neighborIp, int keyId, const std::string& key, bool enable)
    {
        std::lock_guard<std::mutex> lock(neighborMutex);
        
        auto neighborIt = neighbors.find(neighborIp);
        if (neighborIt != neighbors.end())
        {
            auto neighbor = neighborIt->second;
            neighbor->authKeyId = keyId;
            neighbor->authKey = key;
            neighbor->authenticationEnabled = enable;
        }
    }

    std::string EigrpInterface::SerializeEigrpHeader(const eigrpHeader& eigrp, bool exclusiveAuthTLV)
    {
        std::string serialized;
        serialized += eigrp.version;
        serialized += eigrp.opcode;
        serialized += eigrp.checksum;
        serialized += Functions::binToByte("0000000000000000000000000000" + eigrp.flags.endOfTable + eigrp.flags.restart + eigrp.flags.conditionalRecieve + eigrp.flags.init);
        serialized += eigrp.sequence;
        serialized += eigrp.ack;
        serialized += eigrp.virtualRouterID;
        serialized += eigrp.autonomousSystem;

        for (const auto& option : eigrp.options)
        {
            if (exclusiveAuthTLV && option.option == variable.eigrp.options.authentication)
            {
                continue;
            }
            serialized += option.option;
            serialized += option.length;
            serialized += option.value;
        }
        return serialized;
    }

    eigrpHeader::Option EigrpInterface::GenerateAuthenticatedTLV(const eigrpHeader& eigrp, const std::shared_ptr<EigrpConfigs::NeighborInfo>& neighbor)
    {
        if (!neighbor->authenticationEnabled || neighbor->authKey.empty())
        {
            return eigrpHeader::Option{};
        }

        // Create Authentication TLV with HMAC set to zero
        eigrpHeader::Option authTLV;
        authTLV.option = variable.eigrp.options.authentication;

        // Key ID (1 byte) + HMAC placeholder (16 bytes of zeros)
        int hmacLength = (neighbor->authType == EigrpConfigs::AuthType::MD5) ? MD5_DIGEST_LENGTH : SHA_DIGEST_LENGTH;
        authTLV.value = Functions::numToByte(neighbor->authKeyId, 1) + std::string(MD5_DIGEST_LENGTH, '\x00');
        authTLV.length = Functions::numToByte(authTLV.value.size() + 4, 2);

        // Temporarily add the zeroed Authentication TLV to the EIGRP header
        eigrpHeader tempEigrp = eigrp;
        tempEigrp.options.push_back(authTLV);

        // Serialize the entire EIGRP header including the zeroed Authentication TLV
        std::string serializedHeader = SerializeEigrpHeader(tempEigrp, /*exclusiveAuthTLV=*/false);

        // Compute HMAC-MD5 over the serialed header
        std::string computedHMAC;
        if (neighbor->authType == EigrpConfigs::AuthType::MD5)
        {
            computedHMAC = Authentication::GenerateMD5(serializedHeader, neighbor->authKey);
        }
        else if (neighbor->authType == EigrpConfigs::AuthType::SHA1)
        {
            computedHMAC = Authentication::GenerateHMAC(serializedHeader, neighbor->authKey, "SHA1");
        }

        // Replace the zeroed HMAC with the real computed HMAC
        authTLV.value.replace(1, MD5_DIGEST_LENGTH, computedHMAC);

        return authTLV;
    }

    void EigrpInterface::SetPassive(bool passive)
    {
        std::lock_guard<std::mutex> lock (eigrpInterfaceMutex);

        if (passive)
        {
            StopHello();
        }
        else
        {
            StartHelloHelper();
        }

        Logger::getInstance().info() << "Interface set to " << (passive ? "passive" : "active") << " modeo.";
    }

    void EigrpInterface::addNeighbor(const std::string& ipAddress, const std::string& macAddress, EigrpConfigs::CommunicationMode mode)
    {
        std::lock_guard<std::mutex> lock(neighborMutex);

        // Add neighbor only if it doesn't already exist
        if (neighbors.find(ipAddress) == neighbors.end())
        {
            auto neighbor = std::make_shared<EigrpConfigs::NeighborInfo>();
            neighbor->ipAddress = ipAddress;
            neighbor->macAddress = macAddress;
            neighbor->mode = mode;
            neighbors[ipAddress] = neighbor;
        }
    }

    std::optional<std::shared_ptr<EigrpConfigs::NeighborInfo>> EigrpInterface::GetNeighborInfo(const std::string& neighborIp)
    {
        auto it = neighbors.find(neighborIp);
        if (it != neighbors.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

#pragma endregion

#pragma region ClassicEigrp

    

    void ClassicEigrp::InitializeEigrp()
    {
        // Call base class initialziation
        Eigrp::InitializeEigrp();

        // Classic-specific configuration
        configs.autoSummarizationEnabled = true;
        Logger::getInstance().info() << "Classic EIGRP-specific initialization complete";
    }

    void ClassicEigrp::Shutdown()
    {
        Logger::getInstance().info() << "Shutting down Classic EIGRP.";

        // Call base class shutdown
        Eigrp::Shutdown();

        // Remove auto-summarized routes
        if (configs.autoSummarizationEnabled)
        {
            for (const auto& route : configs.summaryRoutes) {
                RemoveSummaryRoute(route.network, route.mask);
            }
            Logger::getInstance().info() << "Cleared auto-summarized routes.";
        }

        // Clear network configurations
        configs.networks.clear();
        Logger::getInstance().info() << "Cleared all Classic-configured networks.";
    }

#pragma endregion

#pragma region NamedEigrp

    NamedEigrp::NamedEigrp(int& as, AddressFamily af, const std::string& name)
        : Eigrp(as, af), processName(name) {}

    void NamedEigrp::InitializeEigrp() {
        // Call base class initialization
        Eigrp::InitializeEigrp();

        // Named-soecific configuration
        configs.autoSummarizationEnabled = false;
        Logger::getInstance().info() << "Initialized Named EIGRP (" << processName << ").";
    }

    void NamedEigrp::Shutdown() {
        Logger::getInstance().info() << "Shutting down Named EIGRP (" << processName << ").";

        // Call base class shutdown
        Eigrp::Shutdown();

        // Clear interface configurations
        interfaceConfigs.clear();
        Logger::getInstance().info() << "Cleared interface-specific configurations.";
    }

#pragma endregion

#pragma region Topology

    TopologyTable::TopologyTable(Eigrp* process)
    {
        eigrpProcess = std::shared_ptr<Eigrp>(process);
    }

    void TopologyTable::AddOrUpdateRoute(const std::string &destination, int prefixLength, const RouteInfo &routeInfo, const std::string &neighborIp)
    {
        std::lock_guard<std::mutex> lock(tableMutex);
        TopologyEntry &entry = topologyEntries[destination];
        entry.destination = destination;
        entry.prefixLength = prefixLength;
        entry.routesByNeighbor[neighborIp] = routeInfo;

        // Update the age parameter for this route
        auto now = std::chrono::steady_clock::now();
        entry.routesByNeighbor[neighborIp].lastUpdate = now;

        // Update Successor and feasibile successor flags
        double bestFD = std::numeric_limits<unsigned int>::infinity();
        entry.successors.clear();
        entry.feasibleSuccessors.clear();

        for (auto &[neighbor, route] : entry.routesByNeighbor)
        {
            if (route.feasibleDistance < bestFD)
            {
                bestFD = route.feasibleDistance;
            }
        }

        for (auto &[neighbor, route] : entry.routesByNeighbor)
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
            if (route.reportedDistance <= bestFD)
            {
                route.isFeasibleSuccessor = true;
                entry.feasibleSuccessors.push_back(neighbor);
            }
            else
            {
                route.isFeasibleSuccessor = false;
            }
        }

        Logger::getInstance().debug() << "Update topology for destination: " << destination << " Best FD: " << bestFD;
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
                Logger::getInstance().info() << "Removed topology entry for destination: " << destination;
            }
        }
    }

    TopologyTable::TopologyEntry *TopologyTable::FindBestRoute(const std::string &destination, int variance)
    {
        std::lock_guard<std::mutex> lock(tableMutex);

        auto it = topologyEntries.find(destination);
        if (it == topologyEntries.end())
        {
            Logger::getInstance().warn() << "No topology entry found for destination: " << Functions::byteToHex(destination);
            return nullptr;
        }

        TopologyEntry& entry = it->second;

        // Use the stored bestFD
        double bestFD = std::numeric_limits<unsigned int>::infinity();
        int bestAD = std::numeric_limits<int >::max();

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
        
        // Apply traffic-share logic and determine successors
        if (eigrpProcess->getConfigs()->trafficShareMode == EigrpConfigs::TrafficShareMode::Minimum)
        {
            entry.successors.clear();
            auto minRoute = std::min_element(entry.routesByNeighbor.begin(), entry.routesByNeighbor.end(),
                 [](const auto& a, const auto& b) {
                     return a.second.feasibleDistance < b.second.feasibleDistance;
                 });
            if (minRoute != entry.routesByNeighbor.end())
            {
                minRoute->second.isSuccessor = true;
                entry.successors.push_back(minRoute->first);
            }
        }
        else
        {
            for (auto& [neighbor, route] : entry.routesByNeighbor)
            {
                if (route.feasibleDistance <= bestFD * variance && route.adminDistance == bestAD)
                {
                    route.isSuccessor = true;
                    entry.successors.push_back(neighbor);
                }
                else
                {
                    route.isSuccessor = false;
                }

                // Feasibility Condition
                if (route.reportedDistance <= bestFD)
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

        Logger::getInstance().debug() << "Best feasible distance for destination: " << Functions::byteToHex(destination) << ": " << bestFD;
        Logger::getInstance().debug() << "Successors for destination: " << Functions::byteToHex(destination) << ": " << entry.successors.size();
        Logger::getInstance().debug() << "Feasible Successors for destination" << Functions::byteToHex(destination) << ": " << entry.feasibleSuccessors.size();

        return &entry;
    }

    void TopologyTable::HandleRouteFailure(const std::string &destination, const std::string& failedNeighborIp)
    {
        std::lock_guard<std::mutex> lock(tableMutex);
        auto it = topologyEntries.find(destination);
        if (it != topologyEntries.end())
        {
            TopologyEntry &entry = it->second;

            // Remove the failed neighbor's routes
            entry.routesByNeighbor.erase(failedNeighborIp);

            // Check for feasible successors
            double bestFD = std::numeric_limits<unsigned int>::infinity();
            for (const auto& [neighbor, route] : entry.routesByNeighbor)
            {
                if (route.feasibleDistance < bestFD)
                {
                    bestFD = route.feasibleDistance;
                }
            }

            entry.successors.clear();
            entry.feasibleSuccessors.clear();

            for (auto& [neighbor, route] : entry.routesByNeighbor)
            {
                if (route.feasibleDistance <= bestFD)
                {
                    route.isSuccessor = true;
                    entry.successors.push_back(neighbor);
                }
                else
                {
                    route.isSuccessor = false;
                }

                if (route.reportedDistance <= bestFD)
                {
                    route.isFeasibleSuccessor = true;
                    entry.feasibleSuccessors.push_back(neighbor);
                }
                else
                {
                    route.isFeasibleSuccessor = false;
                }
            }
            Logger::getInstance().debug() << "Route falure handled for destination: " << destination;
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

    void TopologyTable::PruneStaleRoutes()
    {
        std::lock_guard<std::mutex> lock(tableMutex);

        auto now = std::chrono::steady_clock::now();
        for (auto it = topologyEntries.begin(); it != topologyEntries.end();)
        {
            TopologyEntry &entry = it->second;

            // Iterate through all neighbors for this destination
            for (auto neighborIt = entry.routesByNeighbor.begin(); neighborIt != entry.routesByNeighbor.end();)
            {
                auto age = std::chrono::duration_cast<std::chrono::seconds>(now - neighborIt->second.lastUpdate).count();
                if (age > staleThreshold) // Check against the stale threshold
                {
                    Logger::getInstance().info() << "Pruning stale route to destination: " << entry.destination
                                                 << " learned from neighbor: " << neighborIt->first
                                                 << " (Age: " << age << " seconds)";
                    neighborIt = entry.routesByNeighbor.erase(neighborIt); // Remove stale route
                }
                else
                {
                    ++neighborIt;
                }
            }

            // Remove the entry if no neighbors remain
            if (entry.routesByNeighbor.empty())
            {
                Logger::getInstance().info() << "Removing topology entry for destination: " << entry.destination << " (no valid routes)";
                it = topologyEntries.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    
    void TopologyTable::HandleNeighborDown(const std::string &neighborIp)
    {
        std::lock_guard<std::mutex> lock(tableMutex);
    
        for (auto &entry : topologyEntries)
        {
            entry.second.routesByNeighbor.erase(neighborIp); // Remove routes from this neighbor
        }

        PruneStaleRoutes(); // Remove any empty destinations
    }
}

// Global function to update active EIGRP processes for current interface
void UpdateEigrpInterface(Interface *interface)
{
    lock_guard<mutex> lock(globalEigrpMutex);
    for (auto& instance : eigrpList)
    {
        if (instance.second)
        {
            for (auto as : instance.second->autonomousSystems)
            {
                if (as.second->addressFamilies[AddressFamily::IPv4] && !as.second->addressFamilies[AddressFamily::IPv4]->getConfigs()->networks.empty() && !as.second->addressFamilies[AddressFamily::IPv4]->TestAddress(interface->Get().ip))
                {
                    interface->eigrpInterfaceList.erase(as.second->addressFamilies[AddressFamily::IPv4]->getAsNumber());
                    as.second->addressFamilies[AddressFamily::IPv4]->eigrpInterfaceList.erase(interface->Get().id);
                }
            }
        }
    }
}

EigrpConfigs::CommunicationMode* currentCommunicationMode = new EigrpConfigs::CommunicationMode;
std::shared_ptr<Protocol::Eigrp> currentEigrp;
std::shared_ptr<Protocol::EigrpInstance> currentEigrpInstance;
std::map<std::string, std::shared_ptr<Protocol::EigrpInstance>> eigrpList;
map<int, std::weak_ptr<Protocol::EigrpAutonomousSystems>> eigrpAutonomousSystems;

#pragma endregion
