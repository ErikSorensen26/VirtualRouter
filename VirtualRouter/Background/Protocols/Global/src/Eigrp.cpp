#include <Eigrp.h>
#include <Encapsulation.h>
#include <algorithm>

#define MAX_RETRANSMISSIONS 16
#define PACKET_TIMEOUT_MS 5000
#pragma region Eigrp

// Global mutex for EIGRP operations
std::shared_mutex globalEigrpMutex;

namespace Protocol
{

    Eigrp::Eigrp(int &as, AddressFamily af) : addressFamily(af), asNumber(as)
    {
        initializeEigrp();
    }

    Eigrp::~Eigrp()
    {
        shutdown();
    }

    void Eigrp::initializeEigrp()
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

        calculateRouterID();

        topologyTable = std::make_unique<TopologyTable>(this);

        updateInterfaceList();
        updateRoutingTableForConnected();

        for (auto& [interfaceId, interface] : eigrpInterfaceList)
        {
            std::shared_lock<std::shared_mutex> lock(interface->neighborMutex);
            for (auto& [address, neighbor] : interface->neighbors)
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
                interface->sendHelloPacket();
                interface->startHoldTimer(neighbor->ipAddress, interface->getConfigs()->holdTime);
            }
        }
        
        Logger::getInstance().info() << "EIGRP process initiated." << std::endl;
    }

    void Eigrp::eigrpHello(EigrpHeader &eigrp, EigrpInterface *eigrpInt, ByteString neighborIp, int sequenceNumber,  bool ack, bool update)
    {
        eigrp.version = ByteString(1, 0x02);
        eigrp.opcode = Variable::Eigrp::Type::hello;
        eigrp.checksum = ByteString(2, 0x00);
        eigrp.flags.init = "0";
        eigrp.flags.conditionalRecieve = "0";
        eigrp.flags.restart = "0";
        eigrp.flags.endOfTable = "0";
        eigrp.sequence = ByteString(4, 0x00);
        eigrp.ack = ack ? Functions::numToByte(sequenceNumber, 4) : ByteString(4, 0x00);
        eigrp.virtualRouterID = virtualRouterID;
        eigrp.autonomousSystem = Functions::numToByte(asNumber, 2);

        // Construct TLVs
        if (!ack)
        {
            // Parameter TLV (K-values and Hold Time)
            EigrpHeader::Option paramTLV;
            paramTLV.option = Variable::Eigrp::Option::parameter;
            paramTLV.value = calculateParameters(eigrpInt->getConfigs()->holdTime);
            paramTLV.length = Functions::numToByte(paramTLV.value.size() + 4, 2);
            eigrp.options.emplace_back(paramTLV);

            // Version TLV
            EigrpHeader::Option versionTLV;
            versionTLV.option = Variable::Eigrp::Option::version;
            versionTLV.value = Variable::Eigrp::Version::release + Variable::Eigrp::Version::tls;
            versionTLV.length = Functions::numToByte(versionTLV.value.size() + 4, 2);
            eigrp.options.emplace_back(versionTLV);

            // Sequence TLV
            if (update)
            {
                EigrpHeader::Option sequenceTLV;
                sequenceTLV.option = Variable::Eigrp::Option::sequence;
                sequenceTLV.value = Functions::numToByte(neighborIp.size(), 1) + neighborIp.toString();
                sequenceTLV.length = Functions::numToByte(sequenceTLV.value.size() + 4, 2);
                eigrp.options.emplace_back(sequenceTLV);

                EigrpHeader::Option multicastSeqTLV;
                multicastSeqTLV.option = Variable::Eigrp::Option::multicastSequence;
                multicastSeqTLV.value = Functions::numToByte(sequenceNumber, 4);
                multicastSeqTLV.length = Functions::numToByte(multicastSeqTLV.value.size() + 4, 2);
                eigrp.options.emplace_back(multicastSeqTLV);
            }
        }

        // Retreive NeighborInfo
        auto neighbor = eigrpInt->getNeighborInfo(neighborIp);
        if (neighbor.has_value() && neighbor.value()->authenticationEnabled)
        {
            EigrpHeader::Option authTLV = eigrpInt->generateAuthenticatedTLV(eigrp, neighbor.value());
            if (authTLV.option != ByteString(1, 0x00))
            {
                eigrp.options.emplace_back(authTLV);
            }
        }
    }

    void Eigrp::eigrpUpdate(EigrpHeader &eigrp, int sequenceNum, bool init, bool conditional, bool restart, bool endoftable, bool query, bool reply)
    {
        eigrp.version = ByteString(1, 0x02);
        if (reply)
        {
            eigrp.opcode = Variable::Eigrp::Type::reply;
        }
        else if (query)
        {
            eigrp.opcode = Variable::Eigrp::Type::query;
        }
        else
        {
            eigrp.opcode = Variable::Eigrp::Type::update;
        }
        eigrp.checksum = ByteString(2, 0x00); // will be calculated later
        eigrp.flags.init = init ? "1" : "0";
        eigrp.flags.conditionalRecieve = conditional ? "1" : "0";
        eigrp.flags.restart = restart ? "1" : "0";
        eigrp.flags.endOfTable = endoftable ? "1" : "0";
        eigrp.sequence = Functions::numToByte(sequenceNum, 4);
        eigrp.ack = ByteString(4, 0x00);
        eigrp.virtualRouterID = virtualRouterID;
        eigrp.autonomousSystem = Functions::numToByte(asNumber, 2);
    }

    bool Eigrp::testAddress(const ByteString &testIp)
    {
        if (testIp.empty())
        {
            return false;
        }

        auto hexToUint32 = [](const ByteString &hexStr) -> uint32_t
        {
            uint32_t result = 0;
            std::stringstream ss(hexStr.toString());
            ss >> std::hex >> result;
            return result;
        };

        uint32_t testIpInt = hexToUint32(testIp.toHex());

        for (const auto &network : configs.networks)
        {
            uint32_t ip = hexToUint32(network.ip.toHex());
            uint32_t wildcardMask = hexToUint32(network.mask.toHex());
            uint32_t ipMasked = ip & ~wildcardMask;
            uint32_t testIpMasked = testIpInt & ~wildcardMask;

            if (ipMasked == testIpMasked)
            {
                return true; // Match found
            }
        }

        return false; // No matches found
    }

    double Eigrp::calculateMetric(int bandwidth, int load, int delay, int reliability, int hopCount)
    {
        if (bandwidth == 0) return std::numeric_limits<unsigned int>::max();

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

    void Eigrp::updateInterfaceList()
    {
        std::unique_lock<std::shared_mutex> lock(globalEigrpMutex);

        // Iterate through all interfaces
        for (const auto& outer : interfaceList)
        {
            for (const auto& interface : outer.second)
            {
                auto ipInfo = interface.second->Get();
                std::unique_lock<std::shared_mutex> ipLock(ipInfo->ipMutex);
                if (testAddress(interface.second->Get()->ipv4.ipAddress))
                {
                    int intID = ipInfo->id;
                    // Add interface to eigrp
                    if (interface.second->eigrpInterfaceList.find(asNumber) == interface.second->eigrpInterfaceList.end() && 
                        eigrpInterfaceList.find(intID) == eigrpInterfaceList.end())
                    {
                        ipLock.unlock();
                        std::shared_ptr<EigrpInterface> instance;
                        std::shared_ptr<EigrpInterfaceInstance> interfaceInstance;
                        if (!interface.second->eigrpInterfaceList[asNumber])
                        {
                            interfaceInstance = std::make_shared<EigrpInterfaceInstance>();
                            interface.second->eigrpInterfaceList[asNumber] = interfaceInstance;
                        }
                        if (getAddressFamily() == AddressFamily::IPv6 && !interfaceInstance->IPv6)
                        {
                            instance = std::make_shared<EigrpInterface>(*this, interface.second);
                            interfaceInstance->IPv6 = instance;
                        }
                        else if (getAddressFamily() == AddressFamily::IPv4 && interfaceInstance)
                        {
                            instance = std::make_shared<EigrpInterface>(*this, interface.second);
                            interfaceInstance->IPv4 = instance;
                        }
                        eigrpInterfaceList[intID] = instance;
                        ipLock.lock();

                        if (configs.autoSummarizationEnabled)
                        {
                            std::string networkIt = ipInfo->ipv4.ipAddress.toString();
                            auto majorNetwork = Functions::findClassfullNetwork(networkIt);
                            addSummaryRoute(majorNetwork, Functions::getDefaultMask(majorNetwork));
                        }
                    }
                }
                else
                {
                    // Check and remove interface from eigrp
                    if (interface.second->eigrpInterfaceList.count(asNumber) > 0)
                    {
                        eigrpInterfaceList[ipInfo->id]->stopHello();
                        eigrpInterfaceList.erase(ipInfo->id);
                        interface.second->eigrpInterfaceList.erase(asNumber);
                    }
                }
            }
        }
    }

    ByteString Eigrp::calculateParameters(int holdTime)
    {
        ByteString params;
        params += Functions::numToByte(configs.kvalue.k1_Bandwidth, 1);
        params += Functions::numToByte(configs.kvalue.k2_Load, 1);
        params += Functions::numToByte(configs.kvalue.k3_Delay, 1);
        params += Functions::numToByte(configs.kvalue.k4_Reliability, 1);
        params += Functions::numToByte(configs.kvalue.k5_MTU, 1);
        params += Functions::numToByte(configs.kvalue.k6_Power, 1);
        params += Functions::numToByte(holdTime, 2);
        return params;
    }

    void Eigrp::updateRoutingTableForConnected(std::shared_ptr<EigrpInterface> eigrpInterface)
    {

        std::lock_guard<std::shared_mutex> lock(eigrpMutex);
        RoutingTable &routingTable = RoutingTable::getInstance();
        std::vector<RoutingTable::Eigrp> updatedRoutes{};
        std::vector<RoutingTable::Eigrp> removedRoutes{};
        std::vector<ByteString> connectedNetworks{};
        auto currentAddressFamily = getAddressFamily();
        
        std::unordered_map<int, std::shared_ptr<EigrpInterface>> currentEigrpInterface;
        // get Eigrp Interface list
        if (eigrpInterface)
        {
            auto ipInfo = eigrpInterface->currentInterface.lock()->Get();
            std::shared_lock<std::shared_mutex> lock(ipInfo->ipMutex);
            currentEigrpInterface[ipInfo->id] = eigrpInterface;
        }

        for (const auto& [id, eigrpInterfacePtr] : eigrpInterface ? currentEigrpInterface : eigrpInterfaceList)
        {
            auto interfacePtr = eigrpInterfacePtr->currentInterface;
            auto interfaceInfo = interfacePtr.lock()->Get();
            std::shared_lock<std::shared_mutex> ipLock(interfaceInfo->ipMutex);
            int eigrpBw;
            eigrpBw = interfaceInfo->bandwidth;
            ByteString connectedNetwork = Functions::computeNetworkAddress((currentAddressFamily == AddressFamily::IPv4) ? interfaceInfo->ipv4.ipAddress.toString() : interfaceInfo->ipv6.ipAddress.toString(), (currentAddressFamily == AddressFamily::IPv4) ? interfaceInfo->ipv4.mask : interfaceInfo->ipv6.mask);
            int connectedMask = currentAddressFamily == AddressFamily::IPv4 ? interfaceInfo->ipv4.mask : interfaceInfo->ipv6.mask;

            if ((!interfacePtr.lock()->Get()->ipv4.ipAddress.empty() && currentAddressFamily == AddressFamily::IPv4) || (!interfaceInfo->ipv6.ipAddress.empty() && currentAddressFamily == AddressFamily::IPv6))
            {
                // Compute the connected network
                connectedNetworks.emplace_back(connectedNetwork);

                // Create EIGRP route entry
                RoutingTable::Eigrp connectedRoute;
                    connectedRoute.bandwidth = ( 10000000 / eigrpBw ) * 256;
                    connectedRoute.delay = (interfaceInfo->delay / 10) * 256;
                    connectedRoute.hopCount = 0;
                    connectedRoute.mtu = interfaceInfo->mtu;
                    connectedRoute.reliability = 255;
                    connectedRoute.load = configs.variance;
                    connectedRoute.network = connectedNetwork;
                    connectedRoute.mask = connectedMask;
                    connectedRoute.nextHop = ByteString(connectedNetwork.size(), '\x00'); // indicates directly connected
                    connectedRoute.metric = calculateMetric(eigrpBw, 0, interfaceInfo->delay, 255);
                    connectedRoute.routeType = "connected";

                auto existingRoute = RoutingTable::getInstance().getEigrpRoute(connectedNetwork, connectedRoute.mask, getAddressFamily());

                bool hasChanged = !existingRoute.has_value() ||
                                  existingRoute->feasibleDistance != connectedRoute.feasibleDistance ||
                                  existingRoute->mask != connectedRoute.mask;

                if (hasChanged)
                {
                    // Insert into Routing Table
                    routingTable.addEigrp(connectedRoute, currentAddressFamily);
                    
                    // Advertise the connected route to eigrp neighbors
                    updatedRoutes.emplace_back(connectedRoute);
                }
            }
            else if (!interfacePtr.expired())
            {
                // Remove the connected route from the routing table
                routingTable.removeEigrp(connectedNetwork, connectedMask, currentAddressFamily);

                if (eigrpInterfacePtr->isRouteAdvertised(connectedNetwork, connectedMask))
                {
                    // Notify neighbors of route removal
                    RoutingTable::Eigrp removedRoute;
                    removedRoute.network = connectedNetwork;
                    removedRoute.mask = connectedMask;
                    removedRoute.nextHop = ByteString(connectedNetwork.size(), '\xff');
                    removedRoute.routeType = "connected";

                    removedRoutes.emplace_back(removedRoute);
                }
            }
        }

        // Remove any routes that are no longer exist
        for (const auto& route : RoutingTable::getInstance().getAllEigrpRoutes(currentAddressFamily))
        {
            if (route.routeType == "connected")
            {
                auto networkIt = std::find(connectedNetworks.begin(), connectedNetworks.end(), route.network);
                if (networkIt == connectedNetworks.end())
                {
                    removedRoutes.emplace_back(route);
                    RoutingTable::getInstance().removeEigrp(route.network, route.mask, currentAddressFamily);
                }
            }
        }

        // Notify neighbors
        if (!updatedRoutes.empty())
        {
            notifyRoutingChange(updatedRoutes, /*isRemoval*/false);
        }
        if (!removedRoutes.empty())
        {
            notifyRoutingChange(removedRoutes, /*isRemoved*/true);
        }
    }

    void Eigrp::onInterfaceChange(Interface *interfacePtr, AddressFamily af)
    {
        auto interfaceInfo = interfacePtr->Get();
        std::shared_lock<std::shared_mutex> interfaceLock(interfaceInfo->ipMutex);
        std::shared_lock<std::shared_mutex> lock(eigrpMutex);
        RoutingTable &routingTable = RoutingTable::getInstance();


        // Compute the connected network
        int mask = af == AddressFamily::IPv4 ? interfaceInfo->ipv4.mask : interfaceInfo->ipv6.mask;
        ByteString connectedNetwork = Functions::computeNetworkAddress(af == AddressFamily::IPv4 ? interfaceInfo->ipv4.ipAddress.toString() : interfaceInfo->ipv6.ipAddress.toString(), mask);

        // Check if the route already exists
        bool routeExists = false;

        auto eigrpTable = routingTable.getAllEigrpRoutes(getAddressFamily());

        for (const auto &route : eigrpTable)
        {
            if (route.network == connectedNetwork && route.mask == mask)
            {
                routeExists = true;
                break;
            }
        }

        if (!interfacePtr->shutdownFlag)
        {
            if (!routeExists)
            {
                // Create and insert new connected route
                RoutingTable::Eigrp connectedRoute;
                    connectedRoute.bandwidth = ( 10000000 / interfaceInfo->bandwidth );
                    connectedRoute.delay = (interfaceInfo->delay / 10);
                    connectedRoute.hopCount = 1;
                    connectedRoute.mtu = interfaceInfo->mtu;
                    connectedRoute.reliability = 0;
                    connectedRoute.load = configs.variance;
                    connectedRoute.network = connectedNetwork;
                    connectedRoute.mask = mask;
                    connectedRoute.nextHop = ByteString(connectedNetwork.size(), '\x00'); // indicates directly connected
                    connectedRoute.metric = calculateMetric(interfaceInfo->bandwidth, 0, interfaceInfo->delay, 255);
                    connectedRoute.routeType = "connected";

                RoutingTable::getInstance().addEigrp(connectedRoute, getAddressFamily());

                // Advertise to neighbors
                std::vector<RoutingTable::Eigrp> updatedRoutes{connectedRoute};
                notifyRoutingChange(updatedRoutes, /*isRemoval*/ false);
            }
        }
        else
        {
            if (routeExists)
            {
                // Remove the connected route from the routing table
                routingTable.removeEigrp(connectedNetwork, mask, getAddressFamily());

                // Notify neighbors of route removal
                RoutingTable::Eigrp removedRoute;
                removedRoute.network = connectedNetwork;
                removedRoute.mask = mask;
                removedRoute.nextHop = ByteString(connectedNetwork.size(), '\x00');
                removedRoute.routeType = "connected";

                // Advertise to neighbors
                std::vector<RoutingTable::Eigrp> updatedRoutes{removedRoute};
                notifyRoutingChange(updatedRoutes, /*isRemoval*/ true);
            }
        }
    }

    void Eigrp::notifyRoutingChange(const std::vector<RoutingTable::Eigrp>& changedRoutes, bool isRemoval, bool init)
    {
        for (const auto& route : changedRoutes)
        {
            if (route.network == Functions::hexToByte("03000000"))
            {
                std::cout << std::endl;
            }
        }
        Logger::getInstance().info() << "Notifying all neighbors for route changes" << std::endl;
        std::scoped_lock lock(globalEigrpMutex);

        // Adjust summaries based on added/removed routes
        for (const auto &[_, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            std::vector<ByteString> unicastNeighbors;
            bool hasMulticast = false;
            if (!changedRoutes.empty())
            {
                // Check all neighbors for unicast and multicast
                std::unique_lock<std::shared_mutex> lock(eigrpInterfacePtr->neighborMutex);
                for (const auto& [address, neighbor] : eigrpInterfacePtr->neighbors)
                {
                    if (neighbor->mode == EigrpConfigs::CommunicationMode::UNICAST)
                    {
                        unicastNeighbors.emplace_back(address);
                    }
                    else if (neighbor->mode == EigrpConfigs::CommunicationMode::MULTICAST)
                    {
                        hasMulticast = true;
                    }
                }

                for (const auto& address : unicastNeighbors)
                {
                    if (init)
                    {
                        eigrpInterfacePtr->sendUpdateToNeighbor(address, changedRoutes, EigrpConfigs::UpdateType::PARTIAL);
                    }
                    else
                    {
                        EigrpConfigs::UpdateType updateType = isRemoval ? EigrpConfigs::UpdateType::WITHDRAW : EigrpConfigs::UpdateType::PARTIAL;
                        eigrpInterfacePtr->sendUpdateToNeighbor(address, changedRoutes, updateType);
                    }
                }
                if (hasMulticast)
                {
                    if (init)
                    {
                        eigrpInterfacePtr->sendUpdateToNeighbor("", changedRoutes, EigrpConfigs::UpdateType::PARTIAL);
                    }
                    else 
                    {
                        EigrpConfigs::UpdateType updateType = isRemoval ? EigrpConfigs::UpdateType::WITHDRAW : EigrpConfigs::UpdateType::PARTIAL;
                        eigrpInterfacePtr->sendUpdateToNeighbor("", changedRoutes, updateType);
                    }
                }
            }
        }
    }

    void Eigrp::shutdown()
    {
        std::lock_guard<std::shared_mutex> lock(eigrpMutex);
        for (const auto& [id, eigrpInterface] : eigrpInterfaceList)
        {
            // Send termination message
            eigrpInterface->sendHelloPacket(ByteString(4, 0xFF)); // Termination message
            eigrpInterface->stopHello();
        }
        eigrpInterfaceList.clear();
        topologyTable.reset(); // Clear the topology table
        Logger::getInstance().info() << "EIGRP shutdown complete." << std::endl;
    }

    void Eigrp::redistributeRoute(const ByteString &destination, int mask, const ByteString &protocol)
    {
        RoutingTable& routingTable = RoutingTable::getInstance();
        auto route = routingTable.getEigrpRoute(destination, mask, getAddressFamily());

        if (route.has_value())
        {
            // Convert route to external EIGRP and notify neighbors
            RoutingTable::Eigrp externalRoute = route.value();
            externalRoute.routeType = "external";
            externalRoute.metric += configs.redistributionMetricOffset;

            routingTable.addEigrp(externalRoute, getAddressFamily());
            notifyRoutingChange({externalRoute}, false);
        }
    }

    void Eigrp::addNetwork(const EigrpConfigs::Network& newNetwork)
    {
        configs.networks.emplace_back(newNetwork);

        if (configs.autoSummarizationEnabled)
        {
            std::string tempNetwork = newNetwork.ip.toString();
            ByteString network = Functions::findClassfullNetwork(tempNetwork);
            int networkMask = Functions::getDefaultMask(network.toString());

            // Check of the summary route allready exists
            if (!isRouteSummarized(network, networkMask) && network != ByteString(newNetwork.ip.size(), '\x00'))
            {
                addSummaryRoute(network, networkMask);
            }
        }

        // Update interfaces and routing table after adding the network
        updateInterfaceList();
        updateRoutingTableForConnected();
    }

    void Eigrp::addSummaryRoute(const ByteString& network, int mask, bool isAuto)
    {
        std::shared_lock<std::shared_mutex> lock(eigrpMutex);

        // Validate network and mask
        if (!Functions::compareNetworkWithMask(network.toString(), mask))
        {
            // Mask is not valid
            return;
        }

        // Check for overlapping summary routes
        if (isRouteSummarized(network, mask))
        {
            return; // Already summarized
        }
        // Add new summary route
        EigrpConfigs::SummaryRoute summaryRoute;
        summaryRoute.network = network;
        summaryRoute.mask = mask;
        summaryRoute.isAuto = isAuto;
        summaryRoutes.emplace_back(summaryRoute);

        // Inject the summary route into the routing table as an internal summary route
        RoutingTable::Eigrp internalSummaryRoute;
        internalSummaryRoute.network = network;
        internalSummaryRoute.mask = mask;
        internalSummaryRoute.nextHop = ByteString(network.size(), '\x00');
        internalSummaryRoute.metric = calculateMetric(0, 0, 0, 255);
        internalSummaryRoute.routeType = "summary";

        RoutingTable::getInstance().addEigrp(internalSummaryRoute, getAddressFamily());

        // Update interfaces to advertise the new summary route
        updateInterfacesWithSummaryRoute(summaryRoute);
    }

    void Eigrp::removeSummaryRoute(const ByteString& network, int mask)
    {
        std::shared_lock<std::shared_mutex> lock(eigrpMutex); // Esures thread safety
        
        auto it = std::remove_if(summaryRoutes.begin(), summaryRoutes.end(),
            [&](const EigrpConfigs::SummaryRoute& sr) {
                return sr.network == network && sr.mask == mask;
            });
        if (it != summaryRoutes.end())
        {
            summaryRoutes.erase(it);

            // Remove from routing table
            RoutingTable::getInstance().removeEigrp(network, mask, getAddressFamily());

            // Withdraw summary route from all neighbors
            updateInterfacesAfterRemovingSummaryRoute(network, mask);
        }
    }

    void Eigrp::updateInterfacesWithSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute)
    {
        for (auto& [id, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            eigrpInterfacePtr->advertiseSummaryRoute(summaryRoute);
        }
    }

    void Eigrp::updateInterfacesAfterRemovingSummaryRoute(const ByteString& network, int mask)
    {
        for (auto& [id, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            eigrpInterfacePtr->withdrawSummaryRoute(network, mask);
        }
    }
    
    bool Eigrp::isRouteSummarized(const ByteString& network, int mask)
    {
        for (const auto& sr : summaryRoutes)
        {
            if (Functions::isSubnetOf(network.toString(), mask, sr.network.toString(), sr.mask))
            {
                return true;
            }
        }
        return false;
    }

    void Eigrp::enableAutoSummary(bool enable)
    {
        std::shared_lock<std::shared_mutex> lock(eigrpMutex);

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
                std::string networkIt = network.ip.toString();
                ByteString majorNetwork = Functions::findClassfullNetwork(networkIt);
                int defaultMask = Functions::getDefaultMask(majorNetwork.toString());

                if (!isRouteSummarized(majorNetwork, defaultMask))
                {
                    addSummaryRoute(majorNetwork, defaultMask, true);
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
                    removeSummaryRoute(summaryRoute.network, summaryRoute.mask);
                }
            }
        }
    }
   
    void Eigrp::setStub(bool isStub, bool advertiseConnected, bool advertiseStatic, bool advertiseSummary, bool advertiseRedistributed)
    {
        std::lock_guard<std::shared_mutex> lock(eigrpMutex);
        configs.stubConfig.isStub = isStub;
        configs.stubConfig.advertiseConnected = advertiseConnected;
        configs.stubConfig.advertiseStatic = advertiseStatic;
        configs.stubConfig.advertiseSummary = advertiseSummary;
        configs.stubConfig.advertiseRedistributed = advertiseRedistributed;

        // Update stub routes across all interfaces
        for (auto & [id, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            eigrpInterfacePtr->handleStubRouteUpdates();
        }
    }

    int Eigrp::getLowestBandwidth()
    {
        unsigned int lowestBW = std::numeric_limits<unsigned int>::max();
        for (const auto& [id, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            std::shared_lock<std::shared_mutex> lock(eigrpInterfacePtr->currentInterfaceInfo.lock()->ipMutex);
            if (eigrpInterfacePtr->currentInterfaceInfo.lock()->bandwidth < lowestBW)
            {
                lowestBW = eigrpInterfacePtr->currentInterfaceInfo.lock()->bandwidth;
            }
        }
        return lowestBW;
    }

    void Eigrp::addDefaultRoute()
    {
        if (configs.advertiseDefault)
            return;

        unsigned int lowestBW = getLowestBandwidth();
        if (lowestBW == std::numeric_limits<unsigned int>::max()) {
            Logger::getInstance().error() << "No active interfaces available to advertise the default route." << std::endl;
            return;
        }

        std::shared_ptr<EigrpInterface> selectedInterface;
        for (const auto& [id, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            if (eigrpInterfacePtr->currentInterfaceInfo.lock()->bandwidth == lowestBW)
            {
                selectedInterface = eigrpInterfacePtr;
                break;
            }
        }

        if (!selectedInterface)
        {
            Logger::getInstance().error() << "Selected interface for default route not found." << std::endl;
            return;
        }
        // Calculate metric
        int bandwidth;
        {
            std::shared_lock<std::shared_mutex> interfaceLock(selectedInterface->currentInterfaceInfo.lock()->ipMutex);
        }
        double metric = calculateMetric(selectedInterface->currentInterfaceInfo.lock()->bandwidth, selectedInterface->getConfigs()->load, selectedInterface->currentInterfaceInfo.lock()->delay, 255, 0);

        RoutingTable::Eigrp defaultRoute;
        defaultRoute.network = configs.defaultNetwork;
        defaultRoute.nextHop = ByteString(defaultRoute.network.size(), '\x00');
        defaultRoute.mask = configs.defaultMask;
        defaultRoute.metric = metric;
        defaultRoute.routeType = "default";

        // Add to routing table
        RoutingTable::getInstance().addEigrp(defaultRoute, getAddressFamily());

        // Notify neighbors about the new default route
        notifyRoutingChange({defaultRoute}, /*isRemoval=*/false);

        Logger::getInstance().info() << "Default route (" << defaultRoute.network.toHex() << "/" << defaultRoute.mask << ") added with metric " << defaultRoute.metric << std::endl;
    }

    void Eigrp::removeDefaultRoute()
    {
        if (!configs.advertiseDefault)
            return;
        
        auto defaultRoute = RoutingTable::getInstance().getEigrpRoute(configs.defaultNetwork, configs.defaultMask, getAddressFamily());
        if (defaultRoute.has_value())
        {
            RoutingTable::getInstance().removeEigrp(configs.defaultNetwork, configs.defaultMask, getAddressFamily());
            notifyRoutingChange({defaultRoute.value()}, /*isRemoval=*/true);
        }
    }

    void Eigrp::setVariance(int var)
    {
        std::lock_guard<std::shared_mutex> lock(eigrpMutex);
        configs.variance = var;
    }

    void Eigrp::recalculateRoutes()
    {
        // Iterate through the topology table and update routing table based on new Variance
        for (auto& [destination, entry] : topologyTable->getTopologyEntries())
        {
            // Find all feasible successors within the Variance
            for (const auto& [neighbor, routeInfo] : entry->routesByNeighbor)
            {
                if (routeInfo.feasibleDistance <= entry->bestFD && routeInfo.feasibleDistance <= entry->bestFD * configs.variance)
                {
                    // Add or update route in the routing table
                    RoutingTable::Eigrp newRoute;
                    newRoute.network = destination;
                    newRoute.mask = entry->prefixLength;
                    newRoute.nextHop = neighbor;
                    newRoute.metric = routeInfo.feasibleDistance;
                    newRoute.routeType = "internal";

                    RoutingTable::getInstance().addEigrp(newRoute, getAddressFamily());
                }
            }
        }
    }

    void Eigrp::gracefulRestart()
    {
        std::shared_lock<std::shared_mutex> lock(eigrpMutex);

        Logger::getInstance().info() << "Initiating Graceful Restart" << std::endl;

        // Notify neighbors of restart
        for (auto& [id, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            std::shared_lock<std::shared_mutex> lock(eigrpInterfacePtr->neighborMutex);
            for (const auto& [address, neighborInfo] : eigrpInterfacePtr->neighbors)
            {
                eigrpInterfacePtr->sendHelloPacket(address); // Send restart flag
            }
        }

        Logger::getInstance().info() << "EIGRP Graceful Restart initialted." << std::endl;
    }

    void Eigrp::restart()
    {
        std::lock_guard<std::shared_mutex> lock(eigrpMutex);
    
        Logger::getInstance().info() << "Restarting EIGRP process..." << std::endl;

        // Step 1: Backup neighbors    
        neighborBackup.clear();
        for (const auto& [interfaceId, interfacePtr] : eigrpInterfaceList) 
        {
            auto interfaceInfo = interfacePtr->currentInterfaceInfo;
            std::shared_lock<std::shared_mutex> interfaceLock(interfaceInfo.lock()->ipMutex);

            ByteString ipAddress = getAddressFamily() == AddressFamily::IPv4 ? interfaceInfo.lock()->ipv4.ipAddress : interfaceInfo.lock()->ipv6.ipAddress;
            std::shared_lock<std::shared_mutex> lock(interfacePtr->neighborMutex);
            for (const auto& [address, neighbor] : interfacePtr->neighbors)
            {
                std::shared_lock<std::shared_mutex> dataLock(neighbor->neighborDataMutex);
                auto backup = std::make_shared<EigrpConfigs::NeighborInfo>();
                backup->ipAddress = neighbor->ipAddress;
                backup->authKey = neighbor->authKey;
                backup->authType = neighbor->authType;
                backup->authenticationEnabled = neighbor->authenticationEnabled;
                backup->holdTime = neighbor->holdTime;
                backup->mode = neighbor->mode;

                neighborBackup[ipAddress][neighbor->ipAddress] = backup; // Store neighbor in backup
            }
        }
        
        // Step 2: Shutdown current state
        shutdown();

        // Step 3: Reinitialize the EIGRP process
        initializeEigrp();

        Logger::getInstance().info() << "EIGRP process restarted successfully." << std::endl;
    }

    void Eigrp::cleanup()
    {
        std::lock_guard<std::shared_mutex> lock(eigrpMutex);
        for (auto& [id, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            eigrpInterfacePtr->stopHello();
            eigrpInterfacePtr->cancelStuckInActive();
        }
        eigrpInterfaceList.clear();
        configs.networks.clear();
        summaryRoutes.clear();
        Logger::getInstance().info() << "EIGRP Cleanup complete" << std::endl;
    }

    void Eigrp::periodicMaintenance()
    {
        topologyTable->pruneStaleRoutes();
    }

    void Eigrp::calculateRouterID()
    {
        unsigned int highestNum = 0;
        ByteString highestIP{};
        std::lock_guard<std::shared_mutex> lock(eigrpDataMutex);
        if (routerID.isStatic)
        {
            std::shared_lock<std::shared_mutex> interfaceLock(interfaceListMutex);
            for (const auto& [id, interface] : interfaceList[InterfaceType::LOOPBACK])
            {
                auto interfaceInfo = interface->Get();
                std::shared_lock<std::shared_mutex> ipLock(interfaceInfo->ipMutex);
                ByteString address = interfaceInfo->ipv4.ipAddress;
                unsigned int ipNum = Functions::byteToNum(address.toString());
                if (ipNum > highestNum)
                {
                    highestNum = ipNum;
                }
            }
            if (highestIP.empty())
            {
                for (const auto& [id, interface] : interfaceList[InterfaceType::ETHERNET])
                {
                    auto interfaceInfo = interface->Get();
                    std::shared_lock<std::shared_mutex> ipLock(interfaceInfo->ipMutex);
                    ByteString address = interfaceInfo->ipv4.ipAddress;
                    unsigned int ipNum = Functions::byteToNum(address.toString());
                    if (ipNum > highestNum)
                    {
                        highestNum = ipNum;
                    }
                }
                for (const auto& [id, interface] : interfaceList[InterfaceType::FAST_ETHERNET])
                {
                    auto interfaceInfo = interface->Get();
                    std::shared_lock<std::shared_mutex> ipLock(interfaceInfo->ipMutex);
                    ByteString address = interfaceInfo->ipv4.ipAddress;
                    unsigned int ipNum = Functions::byteToNum(address.toString());
                    if (ipNum > highestNum)
                    {
                        highestNum = ipNum;
                    }
                }
                for (const auto& [id, interface] : interfaceList[InterfaceType::GIGABIT_ETHERNET])
                {
                    auto interfaceInfo = interface->Get();
                    std::shared_lock<std::shared_mutex> ipLock(interfaceInfo->ipMutex);
                    ByteString address = interfaceInfo->ipv4.ipAddress;
                    unsigned int ipNum = Functions::byteToNum(address.toString());
                    if (ipNum > highestNum)
                    {
                        highestNum = ipNum;
                    }
                }
            }
            routerID.ID = highestIP;
        }
    }

#pragma endregion

#pragma region EigrpInterface

    EigrpInterface::EigrpInterface(Eigrp &eigrpSystem, std::shared_ptr<Interface> interface) : currentInterface(interface),
          eigrpProcess(&eigrpSystem),
          helloStartTime(std::chrono::steady_clock::now())
    {
        {
            currentInterfaceInfo = currentInterface.lock()->Get();
            std::lock_guard<std::shared_mutex> lock(currentInterfaceInfo.lock()->ipMutex);
            configs.interfaceAddress = (eigrpProcess->getAddressFamily() == AddressFamily::IPv4) ? currentInterfaceInfo.lock()->ipv4.ipAddress : currentInterfaceInfo.lock()->ipv6.ipAddress;
            configs.interfaceMask = (eigrpProcess->getAddressFamily() == AddressFamily::IPv4) ? currentInterfaceInfo.lock()->ipv4.mask : currentInterfaceInfo.lock()->ipv6.mask;
        }
        startHelloHelper();
        sendHelloPacket();
    }

    EigrpInterface::~EigrpInterface()
    {
        // Aquire lock to modify neighbors
        std::shared_lock<std::shared_mutex> lock(neighborMutex);

        for (auto& [ip, neighbor] : neighbors)
        {
            std::lock_guard<std::shared_mutex> dataLock(neighbor->neighborDataMutex);
            for (auto& [seq, pktInfo] : neighbor->reliablePackets)
            {
                if (pktInfo.timerId != 0)
                {
                    TimeManager::getInstance().cancelTimer(pktInfo.timerId);
                    pktInfo.timerId = 0;
                }
            }
        }

        Logger::getInstance().info() << "EigrpInterface destroyed and all timers canceled." << std::endl;
    }

    void EigrpInterface::processPacket(EigrpHeader *eigrpPacket, const ByteString neighborIp)
    {
        EigrpConfigs::NeighborState neighborState;
        // Validate packet version
        if (eigrpPacket->version != "\x02")
        {
            // Version not valid
            return;
        }

        // Check for valid neighbor 
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor = nullptr;
        if (eigrpPacket->opcode != Variable::Eigrp::Type::hello || eigrpPacket->ack != ByteString(4, '\x00'))
        {
            std::lock_guard<std::shared_mutex> lock(neighborMutex);
            auto neighborIt = neighbors.find(neighborIp);
            if (neighborIt != neighbors.end())
            {
                neighbor = neighbors[neighborIp];
            }
            else
            {
                return;
            }
            std::lock_guard<std::mutex> initLock(neighbor->initializationMutex);
            neighborState = neighbor->initialization;
        }

        if (neighbor)
        {
            if (neighborState == EigrpConfigs::NeighborState::EXSTART)
            {
                changeNeighborState(neighbor, EigrpConfigs::NeighborState::EXCHANGE);
            }
        }


        if (configs.isPassive)
        {
            Logger::getInstance().info() << "Interface is passive. Incoming EIGRP packet ignored." << std::endl;
            return;
        }

        if (eigrpPacket->opcode == Variable::Eigrp::Type::hello)
        {
            if (eigrpPacket->ack == ByteString(4, '\x00'))
            {
                processHello(eigrpPacket, neighborIp);
            }
            else
            {
                processAck(eigrpPacket->ack, neighborIp);
            }
        }
        else if (eigrpPacket->opcode == Variable::Eigrp::Type::update)
        {
            processUpdate(eigrpPacket, neighborIp);
        }
        else if (eigrpPacket->opcode == Variable::Eigrp::Type::reply)
        {
            processReply(eigrpPacket, neighborIp);
        }
        else if (eigrpPacket->opcode == Variable::Eigrp::Type::query)
        {
            processQuery(eigrpPacket, neighborIp);
        }
    }

    void EigrpInterface::changeNeighborState(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor, EigrpConfigs::NeighborState newState)
    {
        // Check for currect state in order to change state
        {
            std::lock_guard<std::mutex> lock(neighbor->initializationMutex);
            if (neighbor->initialization != EigrpConfigs::NeighborState::DOWN)
            {
                if (neighbor->initialization != static_cast<EigrpConfigs::NeighborState>(static_cast<int>(newState) - 1))
                {
                    return;
                }
            }
        }

        // Initialization mutex for thread
        ByteString neighborIp;
        ByteString routerID;
        int seq = 0;
        bool unicast;
        EigrpConfigs::NeighborState currentState;
        EigrpConfigs::InitRole role;
        
        {
            // Lock mutex to safely access shared state
            std::shared_lock<std::shared_mutex> lock(neighborMutex);
            std::shared_lock<std::shared_mutex> seqLock(seqMutex);
            seq = nextSequenceNumber;
            routerID = neighbor->routerID;
            neighborIp = neighbor->ipAddress;
            currentState = neighbor->initialization;
            role = neighbor->initRole;
            unicast = (neighbor->mode == EigrpConfigs::CommunicationMode::UNICAST);

            // If no state change is required, return early
                if (currentState == newState)
            {
                return;
            }
        }

        switch (newState)
        {
            case EigrpConfigs::NeighborState::INIT:
                Logger::getInstance().info(true) << "Neighbor in INIT state. Sending Hello." << std::endl;

                // Update the neighbors state
                {
                    std::lock_guard<std::mutex> lock(neighbor->initializationMutex);
                    neighbor->initialization = newState;
                }

                changeNeighborState(neighbor, EigrpConfigs::NeighborState::TWOWAY);
                break;

            case EigrpConfigs::NeighborState::TWOWAY:
                Logger::getInstance().info(true) << "Neighbor in TWOWAY state." << std::endl;
                sendHelloPacket(neighborIp);
                
                // Update the neighbors state
                {
                    std::lock_guard<std::mutex> lock(neighbor->initializationMutex);
                    neighbor->initialization = newState;
                }

                // Move to EXSTART to exchange sequence numbers
                changeNeighborState(neighbor, EigrpConfigs::NeighborState::EXSTART);
                break;

            case EigrpConfigs::NeighborState::EXSTART:
                Logger::getInstance().info(true) << "Neighbor in EXSTART state. Waiting for role" << std::endl;
                
                    // Send Null Update to initialize reliable communication
                    Logger::getInstance().info(true) << "Sending Null Update to neighbor." << std::endl;
                    if (!neighbor->nullSent)
                    {
                        sendUpdateToNeighbor(neighborIp, {}, EigrpConfigs::UpdateType::QUERY);
                    }

                    // Update the neighbors state
                    {
                        std::lock_guard<std::mutex> lock(neighbor->initializationMutex);
                        neighbor->initialization = newState;
                    }

                // Move to EXCHANGE to start topology exchange
                changeNeighborState(neighbor, EigrpConfigs::NeighborState::EXCHANGE);
                break;

            case EigrpConfigs::NeighborState::EXCHANGE:
                Logger::getInstance().info(true) << "Neighbor in EXCHANGE state. Sharing topology." << std::endl;
                
                if (neighbor->slaveInit || neighbor->masterInit)
                {
                    {std::lock_guard<std::mutex> lock(neighbor->initializationMutex); neighbor->initialization = newState;}
                    changeNeighborState(neighbor, EigrpConfigs::NeighborState::LOADING);
                }
                else if (neighbor->initRole == EigrpConfigs::InitRole::MASTER)
                {
                    if (!neighbor->nullSent && !neighbor->masterInit)
                    {
                        {std::lock_guard<std::mutex> lock(neighbor->initializationMutex); neighbor->initialization = EigrpConfigs::NeighborState::EXCHANGE; neighbor->masterInit = true;}
                        // Send Sequence Hello with the generated sequence number
                        Logger::getInstance().info(true) << "MASTER sending Sequence Hello with sequence number: " << seq << std::endl;
                        sendHelloPacket(neighborIp, /*unicast=*/false, /*update=*/true, seq);

                        // Send your topology
                        Logger::getInstance().info(true) << "MASTER sending full topology." << std::endl;
                        auto allRoutes = RoutingTable::getInstance().getAllEigrpRoutes(eigrpProcess->getAddressFamily());
                        {std::lock_guard<std::mutex> lock(neighbor->initializationMutex); neighbor->initialization = newState;}
                        changeNeighborState(neighbor, EigrpConfigs::NeighborState::LOADING);
                    }
                }
                else if (neighbor->initRole == EigrpConfigs::InitRole::SLAVE && neighbor->initUpdateReceived && !neighbor->slaveInit)
                {
                    // Send Null update to initialize reliable connections
                    Logger::getInstance().info(true) << "Sending Null Update to neighbor." << std::endl;
                    
                    // Send Sequence Hello with the generated sequence number
                    Logger::getInstance().info(true) << "Sending Sequence Hello with sequence number: " << seq << std::endl;
                    sendHelloPacket(neighborIp, /*unicast=*/false, /*update=*/true, seq);

                    // Send your topology
                    Logger::getInstance().info(true) << "Sending full topology." << std::endl;
                    sendUpdateToNeighbor(unicast ? neighborIp : "", RoutingTable::getInstance().getAllEigrpRoutes(eigrpProcess->getAddressFamily()), EigrpConfigs::UpdateType::FULL, false, true, {neighborIp});

                    // Send a hello immediately after sending routes
                    Logger::getInstance().info(true) << "Sending immediate Hello after full topology." << std::endl;
                    sendHelloPacket(neighborIp);

                    {std::lock_guard<std::mutex> lock(neighbor->initializationMutex); neighbor->initialization = EigrpConfigs::NeighborState::EXCHANGE; neighbor->slaveInit = true;}
                    changeNeighborState(neighbor, EigrpConfigs::NeighborState::LOADING);
                }
                else if (neighbor->initRole == EigrpConfigs::InitRole::SLAVE)
                {
                    // Slave waits for masters topology
                    Logger::getInstance().info(true) << "SLAVE waiting for Master's topology." << std::endl;
                    return;
                }

                break;

            case EigrpConfigs::NeighborState::LOADING:
                Logger::getInstance().info(true) << "Neighbor in LOADING state.";

                neighbor->processAcks = true;
 
                // Update the neighbors state

                {
                    std::lock_guard<std::mutex> lock(neighbor->initializationMutex);
                    neighbor->initialization = newState;
                }
                break;

            case EigrpConfigs::NeighborState::ESTABLISHED:
                Logger::getInstance().info(true) << "Neighbor in ESTABLISHED state. Adjacency fully formed." << std::endl;

                // Update the neighbors state
                {
                    std::lock_guard<std::mutex> lock(neighbor->initializationMutex);
                    neighbor->initialization = newState;
                }
                break;
                
            default:
                break;
        }
    }

    void EigrpInterface::processHello(const EigrpHeader *receivedHello, const ByteString neighborIp)
    {
        // Neighor values if needed
        EigrpConfigs::NeighborState neighborState;
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor;
        bool neighborAdded = false;
        
        // Checks if point-to-point is configured
        if (configs.interfaceMode == EigrpConfigs::Mode::POINT_TO_POINT)
        {
            // In point-to-point mode, ensure there's only one neighbor
            if (!neighbors.empty() && neighbors.find(neighborIp) == neighbors.end())
            {
                Logger::getInstance().warn() << "Point-to-point interface already has a neighbor: " << neighbors.begin()->first.toHex() << std::endl;
                return;
            }
        }

        // Validate Autonomous System Number
        if (Functions::byteToNum(receivedHello->autonomousSystem.toString()) != eigrpProcess->getAsNumber())
        {
            // Drop the packet - AS number mismatch
            return;
        }

        // Extract Hold Time from options
        int recievedHoldTime = configs.holdTime; // Default holdtime

        // Add or update neighbor
        {
            std::lock_guard<std::shared_mutex> initLock(neighborMutex);
            if (neighbors.find(neighborIp) == neighbors.end())
            {
                neighbors[neighborIp] = std::make_shared<EigrpConfigs::NeighborInfo>();
                neighborAdded = true;
                std::shared_lock<std::shared_mutex> interfacePtr(currentInterfaceInfo.lock()->ipMutex);
                ByteString ipAddress = getConfigs()->interfaceAddress;
                auto backupIt = eigrpProcess->neighborBackup[ipAddress].find(neighborIp);
                if (backupIt != eigrpProcess->neighborBackup[ipAddress].end())
                {
                    neighbors[neighborIp] = backupIt->second;
                    neighbors[neighborIp]->lastHeard = std::chrono::steady_clock::now();

                    Logger::getInstance().info() << "Restored neighbor from backup: " << neighborIp.toHex() << std::endl;

                    eigrpProcess->neighborBackup[ipAddress].erase(backupIt);
                }
            }
            neighbor = neighbors[neighborIp];

            // Validate hello TLVs
            for (const auto& opt : receivedHello->options)
            {
                if (opt.option == Variable::Eigrp::Option::parameter)
                {
                    ByteString parameters = eigrpProcess->calculateParameters(configs.holdTime);
                    if (opt.value.substr(0, 5) != parameters.substr(0, 5)) return;
                    // Extract Holdtime
                    recievedHoldTime = Functions::byteToNum(opt.value.substr(6, 2).toString());

                    // Check for Peer Termination
                    if (parameters.substr(0, 5) == ByteString(5, 0xFF))
                    {
                        handleNeighborDown(neighborIp);
                        return;
                    }
                }
                if (opt.option == Variable::Eigrp::Option::sequence)
                {
                    std::shared_lock<std::shared_mutex> interfaceLock(currentInterfaceInfo.lock()->ipMutex);
                    if (getConfigs()->interfaceAddress == opt.value.substr(1, Functions::byteToNum(opt.value.substr(0, 1).toString())).toString())
                    {

                    }
                    else
                    {
                        return;
                    }
                }
                if (opt.option == Variable::Eigrp::Option::multicastSequence)
                {
                    std::lock_guard<std::shared_mutex> dataLock(neighbor->neighborDataMutex);
                    neighbor->initSequence = Functions::byteToNum(opt.value.toString());
                }
            }

            bool isNewNeighbor = (neighbor->holdTimerId == 0 || !neighbor->hasMac);

            neighbor->ipAddress = neighborIp;
            neighbor->holdTime = recievedHoldTime;
            neighbor->lastHeard = std::chrono::steady_clock::now();

            // Update neighbor fields and start/renew hold timers
            neighbor->lastReceivedSequenceNumber = 1;
            neighbor->srtt = 1.0;
            neighbor->rttvar = 0.5;
            neighbor->rto = 1.5;

            // Cancel existing hold timer
            if (neighbor->holdTimerId != 0)
            {
                TimeManager::getInstance().cancelTimer(neighbor->holdTimerId);
                neighbor->holdTimerId = 0;
            }

            // Schedule a new Hold Timer
            startHoldTimer(neighborIp, neighbor->holdTime);

            std::vector<RoutingTable::Eigrp> eigrpTable = RoutingTable::getInstance().getAllEigrpRoutes(eigrpProcess->getAddressFamily());

            // Set the neighbor state
            std::lock_guard<std::mutex> initLock2(neighbor->initializationMutex);
            neighborState = neighbor->initialization;
        }
        if (neighborAdded)
        {
            //eigrpProcess->UpdateRoutingTableForConnected(shared_from_this());
        }
        if (neighborState == EigrpConfigs::NeighborState::DOWN)
        {
            changeNeighborState(neighbor, EigrpConfigs::NeighborState::INIT);
        }
        if (neighborState == EigrpConfigs::NeighborState::TWOWAY)
        {
            changeNeighborState(neighbor, EigrpConfigs::NeighborState::EXSTART);
        }
    }

    void EigrpInterface::processUpdate(EigrpHeader *receivedUpdate, const ByteString neighborIp)
    {
        if (neighborIp == getConfigs()->interfaceAddress)
        {
            Logger::getInstance().warn() << "Received update with current interface as neighbor. Dropping update." << std::endl;
            return;
        }

        // Neighor values if needed
        bool processNextUpdate = false;
        bool endInitialization = false;
        bool initReceived = false;
        int receivedSequenceNumber;
        EigrpConfigs::NeighborState neighborState;
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor;
        {
            auto neighborIt = neighbors.find(neighborIp);
            if (neighborIt == neighbors.end())
            {
                return;
            }
            neighbor = neighbors[neighborIp];

            {
                std::lock_guard<std::mutex> lock(neighbor->initializationMutex);
                neighborState = neighbor->initialization;
            }

            // Get sequence number
            receivedSequenceNumber = Functions::byteToNum(receivedUpdate->sequence.toString());

            // Update flags if needed
            if (receivedUpdate->flags.restart == "1") { handleNeighborRestart(neighborIp); return; } // CAUSING FUCKING PROBLEMS
            if (receivedUpdate->flags.init == "1") { neighbor->sequenceList[receivedSequenceNumber].init = true; neighbor->initUpdateReceived = true;}
            if (receivedUpdate->flags.conditionalRecieve == "1") { neighbor->sequenceList[receivedSequenceNumber].conditionalReceive = true; }
            if (receivedUpdate->flags.endOfTable == "1") {neighbor->sequenceList[receivedSequenceNumber].endOfTable = true;}

            // Validate sequence number
            {
                std::lock_guard<std::shared_mutex> lock(neighbor->neighborDataMutex);
                if (receivedSequenceNumber <= neighbor->lastReceivedSequenceNumber && !neighbor->initComplete)
                {
                    Logger::getInstance().debug() << "Duplicate or old Update received with sequence number " << receivedSequenceNumber << " from neighbor " << neighborIp.toHex() << std::endl;
                    EigrpConfigs::NeighborState neighborState;
                    if (!initReceived && neighborState != EigrpConfigs::NeighborState::EXSTART)
                    {
                        sendAckToNeighbor(neighborIp, neighbor->lastReceivedSequenceNumber);
                    }
                    return;
                }
                else if (receivedSequenceNumber > neighbor->lastReceivedSequenceNumber + 1)
                {
                    Logger::getInstance().debug() << "Sequence number: " << receivedSequenceNumber << " from neighbor: " << neighborIp.toHex() << " is too new, adding packet to buffer." << std::endl;
                    // Buffer out of order packet
                    neighbor->packetBuffer[receivedSequenceNumber] = EigrpConfigs::NeighborInfo::PacketBuffer{.neighborIp = neighborIp, .eigrp = *receivedUpdate};
                }

                // Validate init sequence
                if (neighbor->initSequence != 0)
                {
                    if (receivedSequenceNumber != neighbor->initSequence)
                    {
                        neighbor->initSequence = 0;
                        handleNeighborRestart(neighborIp);
                        return;
                    }
                    neighbor->initSequence = 0;
                }
                    
                neighbor->lastReceivedSequenceNumber = receivedSequenceNumber;
            }

            // send ACK to neighbor
            if (neighborState > EigrpConfigs::NeighborState::EXCHANGE)
            {
                sendAckToNeighbor(neighborIp, receivedSequenceNumber);
            }

            // Determine roles based on sequence numbers
            if (!neighbor->initComplete) {
                std::lock_guard<std::shared_mutex> lock(neighbor->neighborDataMutex);
                if (receivedSequenceNumber < nextSequenceNumber)
                {
                    neighbor->initRole = EigrpConfigs::InitRole::MASTER;
                    Logger::getInstance().info(true) << "MASTER role has been chosen with sequence number: " << nextSequenceNumber << " and neighbors: " << receivedSequenceNumber << "." << std::endl;
                }
                if (receivedSequenceNumber > nextSequenceNumber)
                {
                    neighbor->initRole = EigrpConfigs::InitRole::SLAVE;
                    Logger::getInstance().info(true) << "SLAVE role has been chosen with sequence number: " << nextSequenceNumber << " and neighbors: " << receivedSequenceNumber << "." << std::endl;
                }
                if (neighbor->lastReceivedSequenceNumber == nextSequenceNumber)
                {
                // Use router ID as a tie-breaker
                    Logger::getInstance().info(true) << "Sequence numbers equal. Using Router ID as tie-breaker." << std::endl;
                    if (Functions::byteToNum(eigrpProcess->getRouterID().toString()) > Functions::byteToNum(neighbor->routerID.toString()))
                    {
                        neighbor->initRole = EigrpConfigs::InitRole::MASTER;
                        Logger::getInstance().info(true) << "Tie-breaker determined: MASTER." << std::endl;
                    }
                    else
                    {
                        neighbor->initRole = EigrpConfigs::InitRole::SLAVE;
                        Logger::getInstance().info(true) << "Tie-breaker determined: SLAVE." << std::endl;
                    }
                }
                neighbor->initComplete = true;
                endInitialization = true;
            }

            // Process ack if necessary
            if (receivedUpdate->ack != ByteString(4, 0x00))
            {
                processAck(receivedUpdate->ack, neighborIp);
            }

            // Collect routes for batch processing
            int receivedUpdates = 0;
            for (const auto &option : receivedUpdate->options)
            {
                if (option.option == Variable::Eigrp::Option::internalRoute ||
                    option.option == Variable::Eigrp::Option::externalRoute)
                {
                    RoutingTable::Eigrp route;
                    
                    route = decodeRoute(option.value, (option.option == Variable::Eigrp::Option::externalRoute), false);
                    route.nextHop = neighborIp;

                    if (route.delay != 0xFFFFFFFF)
                    {
                        routeBuffer.emplace_back(route);
                    }
                    else
                    {
                        // REMOVE ROUTE
                    }
                    receivedUpdates++;
                }
            }

            if (!routeBuffer.empty())
            {
                RoutingTable& routingTable = RoutingTable::getInstance();

                for (const auto& route : routeBuffer)
                {
                    routingTable.addEigrp(route, eigrpProcess->getAddressFamily());
                }
                updateRoutingTable(routeBuffer, neighbor->sequenceList[receivedSequenceNumber].init, neighborIp);
                routeBuffer.clear();
            }
        }

        // Check and process buffered packets
        processBufferedPackets(neighbor);
        std::lock_guard<std::mutex> lock(initMutex);
        if (neighbor->initComplete && neighborState == EigrpConfigs::NeighborState::EXSTART && neighbor->initRole == EigrpConfigs::InitRole::SLAVE && !neighbor->slaveInit)
        {
            changeNeighborState(neighbor, EigrpConfigs::NeighborState::EXCHANGE);
        }
    }

    void EigrpInterface::processBufferedPackets(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor)
    {
        std::unique_lock<std::shared_mutex> lock(neighborMutex);
        int nextExpectedSequence = neighbor->lastReceivedSequenceNumber + 1;

        while (neighbor->packetBuffer.count(nextExpectedSequence) > 0 || isTimeoutForMissing(nextExpectedSequence, neighbor))
        {
            if (neighbor->packetBuffer.count(nextExpectedSequence) > 0)
            {
                auto bufferedPacket = neighbor->packetBuffer[nextExpectedSequence];
                neighbor->packetBuffer.erase(nextExpectedSequence);

                // Update sequence number and process the buffered packet
                neighbor->lastReceivedSequenceNumber = nextExpectedSequence;
                lock.unlock();
                processUpdate(&bufferedPacket.eigrp, bufferedPacket.neighborIp);
            }
            else
            {
                // Timeout reached for skipped packet, moving on
                Logger::getInstance().info()
                    << "Skipped missing packet with sequence numbers: "
                    << nextExpectedSequence << ". Moving forward." << std::endl;
                neighbor->lastReceivedSequenceNumber = nextExpectedSequence;
            }

            nextExpectedSequence++;
        }
    }

    void EigrpInterface::processAck(const ByteString sequenceNumber, const ByteString neighborIp)
    {
        int ackSequenceNumber = Functions::byteToNum(sequenceNumber.toString());

        std::shared_lock<std::shared_mutex> lock(neighborMutex);
        auto neighborIt = neighbors.find(neighborIp);
        if (neighborIt == neighbors.end())
        {
            Logger::getInstance().warn() << "Received ACK from unknown neighbor: " << neighborIp.toHex() << std::endl;
            return;
        }
        auto neighbor = neighborIt->second;
        
        // Remove the Acknowledged Packet from reliablePackets
        {
            std::lock_guard<std::shared_mutex> lock(neighbor->neighborDataMutex);
            auto pktIt = neighbor->reliablePackets.find(ackSequenceNumber);
            if (pktIt != neighbor->reliablePackets.end())
            {
                // Add or remove routes from advertised table
                for (const auto& route : pktIt->second.packet.updatedRoutes)
                {
                    ByteString key = route.network + "/" + std::to_string(route.mask);
                    auto advertIt = neighbor->advertisedRoutes.find(key);

                    if (pktIt->second.packet.remove)
                    {
                        for (const auto& route : pktIt->second.packet.updatedRoutes)
                        {
                            ByteString key = route.network + "/" + std::to_string(route.mask);
                            auto advertIt = neighbor->advertisedRoutes.find(key);
                            if (advertIt != neighbor->advertisedRoutes.end() && advertIt->second.removePending)
                            {
                                neighbor->advertisedRoutes.erase(advertIt); // Fully remove the route
                                Logger::getInstance().debug() << "Route " << key.toHex() << " fully removed after ACK from neighbor " << neighborIp.toHex() << "." << std::endl;
                            }
                        }
                    }
                    else
                    {
                        if (advertIt == neighbor->advertisedRoutes.end())
                        {
                            neighbor->advertisedRoutes[key] = {route, true, false, false};
                        }
                        else
                        {
                            advertIt->second.active = true;
                            advertIt->second.pendingUpdate = false;
                        }
                        Logger::getInstance().debug() << "Route " << key.toHex() << " added/updated for neighbor " << neighborIp.toHex() << "." << std::endl;
                    }
                }

                // Cancel the Retransmission Timer
                if (pktIt->second.timerId != 0)
                {
                    TimeManager::getInstance().cancelTimer(pktIt->second.timerId);
                }

                // Erase the Packet from reliablePackets
                neighbor->reliablePackets.erase(pktIt);

                // Update RTT and RTO Estimates if Necessary
                updateRTTEstimate(neighbor, ackSequenceNumber);

                Logger::getInstance().debug() << "ACK processed for neighbor " << neighborIp.toHex() << " sequence number " << ackSequenceNumber << std::endl;
            }
            else
            {
                Logger::getInstance().warn() << "Received ACK for unknown sequence number " << ackSequenceNumber << " from neighbor " << neighborIp.toHex() << std::endl;
            }
        }
    }

    void EigrpInterface::processQuery(const EigrpHeader *receivedQuery, const ByteString neighborIp)
    {
        auto eigrpProcess = this->eigrpProcess;
        
        // Stub routing query blocking
        if (eigrpProcess->isStub())
        {
            // Extract the queries network from the query options
            std::vector<RoutingTable::Eigrp> replyRoutes;

            for (const auto& option : receivedQuery->options)
            {
                if (option.option == Variable::Eigrp::Option::internalRoute)
                {
                    // Decode the queried route
                    RoutingTable::Eigrp queriedRoute = decodeRoute(option.value, /*external=*/false, /*summary=*/false);

                    // Check if the queried route exists
                    std::optional<RoutingTable::Eigrp> existingRoute = RoutingTable::getInstance().getEigrpRoute(queriedRoute.network, queriedRoute.mask, eigrpProcess->getAddressFamily());
                    if (existingRoute)
                    {
                        replyRoutes.emplace_back(existingRoute.value());
                    }
                    else
                    {
                        queriedRoute.metric = std::numeric_limits<unsigned int>::max();

                        // Mark the route as pending removal for all neighbors
                        for (auto &[neighborIp, neighborInfo] : neighbors)
                        {
                            ByteString key = queriedRoute.network + "/" + std::to_string(queriedRoute.mask);
                            auto advertIt = neighborInfo->advertisedRoutes.find(key);
                            if (advertIt != neighborInfo->advertisedRoutes.end())
                            {
                                advertIt->second.removePending = true;
                            }
                        }

                        replyRoutes.emplace_back(queriedRoute);
                    }
                }
            }
            
            // Send replies to the querying neighbor
            sendReplyToNeighbor(neighborIp, replyRoutes);
            return; // Do not propagate the query further;
        }

        std::vector<RoutingTable::Eigrp> queryRoutes;
        std::vector<RoutingTable::Eigrp> validQueryRoutes;
        int queryId = Functions::byteToNum(receivedQuery->sequence.toString());

        if (outstandingReplies.count(queryId) > 0)
        {
            Logger::getInstance().info() << "Duplicate query received from neighbor: " << neighborIp.toHex() << std::endl;
            return;
        }

        // Extract the destination network network and mask from the Query options
        for (const auto& opt : receivedQuery->options)
        {
            RoutingTable::Eigrp route;
            if (opt.option == Variable::Eigrp::Option::internalRoute)
            {
                route = decodeRoute(opt.value, /*external=*/false, /*summary=*/false);
            }
            else
            {
                continue;
            }

            std::optional<RoutingTable::Eigrp> routeIt = RoutingTable::getInstance().getEigrpRoute(route.network, route.mask, eigrpProcess->getAddressFamily());
            bool hasValidRoute = routeIt.has_value();
            if (hasValidRoute)
            {
                validQueryRoutes.emplace_back(routeIt.value());
            }
            else
            {
                queryRoutes.emplace_back(route);
            }
        }


        if (!validQueryRoutes.empty())
        {
            // Send a Reply to the quering neighbor with the route information
            sendReplyToNeighbor(neighborIp, validQueryRoutes);
        }

        if (!queryRoutes.empty())
        {
            // Propagate the Query to other neighbors except the originator
            outstandingReplies[queryId] = {neighborIp, static_cast<int>(neighbors.size() - 1)};

            for (const auto& [_, neighborEntry] : neighbors)
            {
                if (neighborEntry->ipAddress != neighborIp)
                {
                    sendQueryToNeighbors(queryRoutes, neighborIp);
                }
            }
        }
    }

    bool EigrpInterface::isTimeoutForMissing(int sequenceNumber, std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor)
    {
        auto now = std::chrono::steady_clock::now();
        if (neighbor->missingPacketTimestamps.count(sequenceNumber) == 0)
        {
            // First time seeing the missing packet
            neighbor->missingPacketTimestamps[sequenceNumber] = now;
            return false;
        }

        // Check if the timeout has been exceeded
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - neighbor->missingPacketTimestamps[sequenceNumber]).count();
        return elapsed > PACKET_TIMEOUT_MS;
    }

    void EigrpInterface::processReply(const EigrpHeader *recievedReply, const ByteString neighborIp)
    {
        int queryId = Functions::byteToNum(recievedReply->sequence.toString());
        std::vector<RoutingTable::Eigrp> receivedRoutes;

        // Extract the route information from the reply options
        for (const auto& opt : recievedReply->options)
        {
            if (opt.option == Variable::Eigrp::Option::internalRoute)
            {
                RoutingTable::Eigrp route = decodeRoute(opt.value, /*external*/false, /*summary=*/false);
                receivedRoutes.emplace_back(route);
            }
        }
        
        // send ACK to neighbor
        {
            std::lock_guard<std::shared_mutex> lock(neighborMutex);
            sendAckToNeighbor(neighborIp, queryId);
        }

        if (!receivedRoutes.empty())
        {
            updateRoutingTable(receivedRoutes, /*init*/false, neighborIp);
        }

        {
            auto it = outstandingReplies.find(queryId);
            if (it != outstandingReplies.end())
            {
                it->second.second--; // Decrement the count of pending replies

                if (it->second.second == 0) // All replies received
                {
                    ByteString queryingNeighborIp = it->second.first;
                    outstandingReplies.erase(it);

                    if (!queryingNeighborIp.empty())
                    {
                        sendReplyToNeighbor(queryingNeighborIp, receivedRoutes);
                    }
                }
            }
        }

        sendAckToNeighbor(neighborIp, queryId);
    }

    size_t EigrpInterface::calculateMaxRoutesPerPacket(AddressFamily af, bool isExternal)
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

    void EigrpInterface::sendAckToNeighbor(const ByteString neighborIp, int sequenceNumber)
    {
        std::lock_guard<std::mutex> lock(ackMutex);
        auto neighborIt = neighbors.find(neighborIp);
        if (neighborIt == neighbors.end())
        {
            Logger::getInstance().warn() << "Attempted to send Ack to unknown neighbor: " << neighborIp.toHex() << std::endl;
            return;
        }
        auto neighbor = neighborIt->second;
        auto eigrpProcess = this->eigrpProcess;
        
        auto ackIt = std::find(neighbor->pendingAcks.begin(), neighbor->pendingAcks.end(), sequenceNumber);
        if (ackIt == neighbor->pendingAcks.end())
        {
            neighbor->pendingAcks.emplace_back(sequenceNumber);
        }
        if (!neighbor->processAcks)
        {
            return;
        }

        for (auto seq : neighbor->pendingAcks)
        {

            // Create the EIGRP Ack packet
            PacketInfo eigrpAckPacketStructure;
            EigrpHeader eigrp;

            eigrpProcess->eigrpHello(eigrp, this, neighborIp, seq, /*ack=*/true);

            // Generate and append Authentication TLV if enabled for this neighbor
            if (neighbor->authenticationEnabled)
            {
                EigrpHeader::Option authTLV = generateAuthenticatedTLV(eigrp, neighbor);
                if (authTLV.option != ByteString(1, 0x00))
                {
                    eigrp.options.emplace_back(authTLV);
                }
            }

            // Assemble the packet
            eigrpAckPacketStructure.Layer3.push_back(eigrp);

            // Enqueue for transmission
            currentInterface.lock()->ipPacket->setIPHeader(eigrpAckPacketStructure, neighborIp, getConfigs()->DSCP, 2, Variable::IP::eigrp);

            // Remove ack from pending
            neighbor->pendingAcks.erase(std::remove(neighbor->pendingAcks.begin(), neighbor->pendingAcks.end(), seq), neighbor->pendingAcks.end());

            // Log the ACK sending
            Logger::getInstance().debug() << "ACK sent for sequence number " << seq << " to neighbor " << neighborIp.toHex() << std::endl;
        }
    }

    void EigrpInterface::sendUpdateToNeighbor(const ByteString neighborIp, const std::vector<RoutingTable::Eigrp> &routes, EigrpConfigs::UpdateType updateType, bool restart, bool conditional, std::vector<ByteString> conditionalNeighbors)
    {
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor = nullptr;
        if (!neighborIp.empty())
        {
            std::shared_lock<std::shared_mutex> lock(neighborMutex);
            auto neighborIt = neighbors.find(neighborIp);
            if (neighborIt == neighbors.end())
            {
                Logger::getInstance().warn() << "Attempted to send update to unknown neighbor: " << neighborIp.toHex() << std::endl;
                return;
            }

            neighbor = neighborIt->second;
        }
        auto eigrpProcess = this->eigrpProcess;

        // Determine target IP based on communication mode
        ByteString targetIp = neighborIp.empty() ? getMulticast() : neighbor->ipAddress;

        // Save sequence number for Null Update
        if (updateType == EigrpConfigs::UpdateType::QUERY)
        {
            // Check if there is a neighbor Ip to use
            if (!neighbor)
            {
                return;
            }
            std::lock_guard<std::shared_mutex> nullLock(neighbor->neighborDataMutex);
            neighbor->nullUpdateSequence = nextSequenceNumber;
            targetIp = neighborIp;
            neighbor->nullSent = true;
        }

        // Filter routes based on stub configuration
        std::vector<RoutingTable::Eigrp> filteredRoutes;
        for (const auto& route : routes)
        {
            Logger::getInstance().debug() << "Applying filter for route: " << route.network.toHex() << std::endl;
            bool stubTest = false;
            bool splitHorizonTest = true;
            bool needsUpdate = false;

            // Stub test
            if (eigrpProcess->isStub())
            {
                if ((route.routeType == "connected" && eigrpProcess->advertiseConnected()) ||
                    (route.routeType == "static" && eigrpProcess->advertiseStatic()) ||
                    (route.routeType == "summary" && eigrpProcess->advertiseSummary()) ||
                    (route.routeType == "external" && eigrpProcess->advertiseRedistributed()))
                {
                    stubTest = true;
                }
            }
            else
            {
                stubTest = true;
            }

            // Split horizon test
            if (getConfigs()->splitHorizon)
            {
                for (const auto& [address, neighbor] : neighbors)
                {
                    if (route.nextHop == address || Functions::compareNetworkWithIp(route.network.toString(), neighbor->ipAddress.toString(), route.mask))
                    {
                        splitHorizonTest = false;
                    }
                }
            }
            
            ByteString key = route.network + "/" + std::to_string(route.mask);

            // Check if route requires an update
            for (const auto& [address, neighbor] : neighbors)
            {
                auto advertIt = neighbor->advertisedRoutes.find(key);

                if (advertIt != neighbor->advertisedRoutes.end())
                {
                    if (advertIt->second.pendingUpdate || advertIt->second.removePending)
                    {
                        needsUpdate = true;
                        break;
                    }
                }
                else
                {
                    needsUpdate = true; // Route not yet advertised
                }
            }

            Logger::getInstance().debug() << "Route received from neighbor: " << route.nextHop.toString() << " Route being sent out of interface: " << getConfigs()->interfaceAddress.toHex() << std::endl;
            Logger::getInstance().debug() << (needsUpdate ? "Route Needs updating " : "Route does not need updating ") << " with a new metric of: " << route.metric << std::endl;

            if (splitHorizonTest && stubTest && needsUpdate)
            {
                filteredRoutes.emplace_back(route);
                Logger::getInstance().info() << "Route: " << route.network.toHex() << " sent to out of interface: " << getConfigs()->interfaceAddress.toHex() << " with a metric of: " << route.metric << std::endl;
            }
        }

        if ((updateType == EigrpConfigs::UpdateType::PARTIAL || updateType == EigrpConfigs::UpdateType::WITHDRAW) && filteredRoutes.empty())
        {
            return;
        }

        size_t maxRoutesPerPacket = calculateMaxRoutesPerPacket(eigrpProcess->getAddressFamily(), /*isExernal=*/false);
        size_t routeCount = 0;
        auto it = filteredRoutes.begin();

        // Set if this is a withdraw update
        bool removal = (updateType == EigrpConfigs::UpdateType::WITHDRAW);

        // Send in batches if there are too many routes
        bool endOfTable = false;
        bool isConditional = false;
        bool isInit = false;
        do
        {
            // Create the EIGRP Update packet structure
            PacketInfo eigrpPacket;
            EigrpHeader eigrp;

            // Get the next sequence number
            int sequenceNumber = getNextSequenceNumber();

            // Add routes to the packet
            for (; it != filteredRoutes.end() && routeCount < maxRoutesPerPacket; ++it, ++routeCount)
            {
                EigrpHeader::Option routeOptions;
                routeOptions.option = Variable::Eigrp::Option::internalRoute;

                if (it->routeType == "external")
                {
                    routeOptions.value = encodeExternalRouteOption(*it, removal);
                    routeOptions.option = (eigrpProcess->getAddressFamily() == AddressFamily::IPv4) ? Variable::Eigrp::Option::externalRoute : Variable::Eigrp::Option::externalRouteV6;
                }
                else
                {
                    routeOptions.value = encodeRouteOption(*it, removal);
                    routeOptions.option = (eigrpProcess->getAddressFamily() == AddressFamily::IPv4) ? Variable::Eigrp::Option::internalRoute : Variable::Eigrp::Option::internalRouteV6;
                }
                routeOptions.length = Functions::numToByte(routeOptions.value.size() + 4, 2);
                eigrp.options.emplace_back(routeOptions);
            }
            
            // Set flags based on update type and stage
            endOfTable = (it == filteredRoutes.end());

            // Null Update
            if (updateType == EigrpConfigs::UpdateType::QUERY)
            {
                isInit = true;
                isConditional = false;
                endOfTable = false;
            }

            if (conditional)
            {
                isConditional = true;
            }

            eigrpProcess->eigrpUpdate(eigrp, sequenceNumber, 
                                      /*init=*/isInit,
                                      /*conditional=*/isConditional, 
                                      /*restart=*/restart, 
                                      /*endOfTable*/((endOfTable/* && filteredRoutes.size() != 1*/)));

            // Sequence initiated
            isInit = false;

            // Handle Acknowledgments
            if (neighbor)
            {
                if (!neighbor->pendingAcks.empty())
                {
                    eigrp.ack = Functions::numToByte(neighbor->pendingAcks.front(), 4);
                    neighbor->pendingAcks.erase(neighbor->pendingAcks.begin());
                }
            }
            
            // Handle authentication
            if (neighbor)
            {

                // Add authentication TLV if enabled
                if (neighbor->authenticationEnabled)
                {
                    EigrpHeader::Option authTLV = generateAuthenticatedTLV(eigrp, neighbor);
                    if (authTLV.option != ByteString(1, 0x00))
                    {
                        eigrp.options.emplace_back(authTLV);
                    }
                }
            }

            // Assemble and send the packet
            eigrpPacket.Layer3.push_back(eigrp);

            currentInterface.lock()->ipPacket->setIPHeader(eigrpPacket, targetIp, getConfigs()->DSCP, 2, Variable::IP::eigrp);
            
            Logger::getInstance().info() << "Update sent to neighbor " << neighborIp.toHex() << " with sequence number " << sequenceNumber << std::endl;

            if (!neighbor || (neighbor && neighbor->processAcks))
            {
                if (conditional)
                {
                    eigrp.flags.conditionalRecieve = "0";
                    std::shared_lock<std::shared_mutex> lock(neighborMutex);
                    for (const auto& [address, neighborPtr] : neighbors)
                    {
                        std::vector<RoutingTable::Eigrp> neighborSpecificRoutes;

                        for (const auto& route : filteredRoutes)
                        {
                            std::lock_guard<std::shared_mutex> lock(neighborPtr->neighborDataMutex);
                            ByteString key = route.network + "/" + std::to_string(route.mask);
                            auto advertIt = neighborPtr->advertisedRoutes.find(key);
                            
                            if (advertIt == neighborPtr->advertisedRoutes.end() || advertIt->second.pendingUpdate || advertIt->second.removePending)
                            {
                                neighborSpecificRoutes.emplace_back(route);
                            }
                        }
                        if (!neighborSpecificRoutes.empty())
                        {
                            setupReliablePacket(neighborPtr, EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(eigrp, address, neighborSpecificRoutes, removal), sequenceNumber);
                        }
                    }
                }
                else
                {
                    for (const auto& [address, neighborPtr] : neighbors)
                    {
                        setupReliablePacket(neighborPtr, EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(eigrp, address, filteredRoutes, removal), sequenceNumber);
                    }
                }
            }
        }
        while (!endOfTable && updateType == EigrpConfigs::UpdateType::FULL);
    }

    void EigrpInterface::sendQueryToNeighbors(const std::vector<RoutingTable::Eigrp>& failedRoutes, const ByteString& originNeighborIp)
    {
        std::lock_guard<std::shared_mutex> lock(neighborMutex);
        auto eigrpProcess = this->eigrpProcess;

        if (eigrpProcess->isStub())
        {
            // Instead of propagating the query, respond with route unavailable
            for (const auto& route : failedRoutes)
            {
                eigrpProcess->notifyRoutingChange({route}, /*isRemoval=*/true, /*init=*/false);
            }
        }

        int queryId = getNextSequenceNumber(); // Generate query ID
        outstandingReplies[queryId] = {originNeighborIp, static_cast<int>(neighbors.size() - 1)};

        for (const auto& [_, neighborEntry] : neighbors)
        {
            if (neighborEntry->ipAddress != originNeighborIp)
            {
                sendQueryToNeighbor(neighborEntry->ipAddress, failedRoutes);
            }
        }
        // Start Active Timer for the failed destination
        for (const auto& failedRoute : failedRoutes)
        {
            startActiveTimer(failedRoute);
        }
    }

    void EigrpInterface::sendQueryToNeighbor(const ByteString neighborIp, const std::vector<RoutingTable::Eigrp>& failedRoutes)
    {
        auto neighborIt = neighbors.find(neighborIp);
        if (neighborIt == neighbors.end())
        {
            Logger::getInstance().warn() << "Attempted to send query to unknown neighbor: " << neighborIp.toHex() << std::endl;
            return;
        }
        auto neighbor = neighborIt->second;
        auto eigrpProcess = this->eigrpProcess;

        // Increment sequence number for this route/query
        int currentSeqNum = getNextSequenceNumber();

        // Create the EIGRP Query Packet
        PacketInfo eigrpQueryPacketStructure;
        EigrpHeader eigrp;

        // Construct the Query option
        for (const auto& route : failedRoutes)
        {
            EigrpHeader::Option queryOption;
            queryOption.option = Variable::Eigrp::Option::internalRoute;
            queryOption.value = encodeQueryOption(route);
            queryOption.length = Functions::numToByte(queryOption.value.size() + 4, 2);
            // Add the Query option to EIGRP header
            eigrp.options.emplace_back(queryOption);
        }

        // Set other EIGRP header feilds
        eigrp.version = ByteString(1, 0x02);
        eigrp.opcode = Variable::Eigrp::Type::query;
        eigrp.checksum = ByteString(2, 0x00); // Will be calculated later
        eigrp.flags.init = "0";
        eigrp.flags.conditionalRecieve = "0";
        eigrp.flags.restart = "0";
        eigrp.flags.endOfTable = "0";
        eigrp.sequence = Functions::numToByte(currentSeqNum, 4);
        eigrp.ack = ByteString(4, 0x00);
        eigrp.virtualRouterID = eigrpProcess->getVirtualRouterID();
        eigrp.autonomousSystem = Functions::numToByte(eigrpProcess->getAsNumber(), 2);

        // Generate and append Authentication TLV if enabled for this neighbor
        if (neighbor->authenticationEnabled)
        {
            EigrpHeader::Option authTLV = generateAuthenticatedTLV(eigrp, neighbor);
            if (authTLV.option != ByteString(1, 0x00))
            {
                eigrp.options.emplace_back(authTLV);
            }
        }

        // Assemble the packet
        eigrpQueryPacketStructure.Layer3.push_back(eigrp);

        // Convert to raw packet ByteString
        currentInterface.lock()->ipPacket->setIPHeader(eigrpQueryPacketStructure, neighbor->ipAddress, getConfigs()->DSCP, 2, Variable::IP::eigrp);

        // Store the packet for possible retransmission (relieable delivery)
        setupReliablePacket(neighbor, EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(eigrp, neighborIp, failedRoutes, true), currentSeqNum);

        Logger::getInstance().info() << "Sent query to neighbor " << neighborIp.toHex() << " with sequence number " << currentSeqNum << std::endl;
    }

    ByteString EigrpInterface::encodeQueryOption(RoutingTable::Eigrp route)
    {
        ByteString encoded;
        encoded += route.nextHop;
        encoded += ByteString(4, 0xFF);
        encoded += Functions::numToByte(route.bandwidth, 4);
        encoded += Functions::numToByte(route.mtu, 3);
        encoded += Functions::numToByte(route.hopCount, 1);
        encoded += Functions::numToByte(route.reliability, 1);
        encoded += Functions::numToByte(route.load, 1);
        encoded += Functions::numToByte(route.routeTag, 1);
        encoded += ByteString(1, 0x00);
        encoded += Functions::numToByte(route.mask, 1);
        encoded += Functions::compactNetworkAddress(route.network.toString(), route.mask);
        return encoded;
    }

    void EigrpInterface::sendReplyToNeighbor(const ByteString neighborIp, const std::vector<RoutingTable::Eigrp>& routes)
    {
        auto neighborIt = neighbors.find(neighborIp);
        if (neighborIt == neighbors.end())
        {
            Logger::getInstance().warn() << "Attempted to send reply to unknown neighbor: " << neighborIp.toHex() << std::endl;
            return;
        }
        auto neighbor = neighborIt->second;
        auto eigrpProcess = this->eigrpProcess;

        // Increment sequence number for this route/reply
        int currentSeqNum = getNextSequenceNumber();

        // Create the EIGRP Reply Packet
        PacketInfo eigrpReplyPacketStructure;
        EigrpHeader eigrp;

        // Construct the Reply option with the route information
        for (const auto& route : routes)
        {
            EigrpHeader::Option replyOption;
            replyOption.option = Variable::Eigrp::Option::internalRoute;
            replyOption.value = encodeRouteOption(route);
            replyOption.length = Functions::numToByte(replyOption.value.size() + 4, 2);
            eigrp.options.emplace_back(replyOption);
        }

        // Set other EIGRP header fields
        eigrp.version = ByteString(1, 0x02);
        eigrp.opcode = Variable::Eigrp::Type::reply;
        eigrp.checksum = ByteString(2, 0x00); // Will be calculated later
        eigrp.flags.init = "0";
        eigrp.flags.conditionalRecieve = "0";
        eigrp.flags.restart = "0";
        eigrp.flags.endOfTable = "1";
        eigrp.sequence = Functions::numToByte(currentSeqNum, 4);
        eigrp.ack = ByteString(4, 0x00);
        eigrp.virtualRouterID = eigrpProcess->getVirtualRouterID();
        eigrp.autonomousSystem = Functions::numToByte(eigrpProcess->getAsNumber(), 2);

        // Generate and append Authentication TLV if enabled for this neighbor
        if (neighbor->authenticationEnabled)
        {
            EigrpHeader::Option authTLV = generateAuthenticatedTLV(eigrp, neighbor);
            if (authTLV.option != ByteString(1, 0x00))
            {
                eigrp.options.emplace_back(authTLV);
            }
        }

        // Assemble the packet
        eigrpReplyPacketStructure.Layer3.push_back(eigrp);

        // Enqueue for transmission
        currentInterface.lock()->ipPacket->setIPHeader(eigrpReplyPacketStructure, neighborIp, getConfigs()->DSCP, 2, Variable::IP::eigrp);

        // Store the packet for possible retransmission (reliable delivery)
        setupReliablePacket(neighbor, EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(eigrp, neighborIp), currentSeqNum);

        Logger::getInstance().info() << "Sent reply to neighbor " << neighborIp.toHex() << " with sequence number " << currentSeqNum << std::endl;
    }

    ByteString EigrpInterface::encodeRouteOption(const RoutingTable::Eigrp& route, bool removed)
    {
        ByteString encoded;
        encoded += route.nextHop;
        encoded += (removed ? ByteString (4, '\xff') : Functions::numToByte(route.delay, 4));
        encoded += Functions::numToByte(route.bandwidth, 4);
        encoded += Functions::numToByte(route.mtu, 3);
        encoded += Functions::numToByte(route.hopCount, 1);
        encoded += Functions::numToByte(route.reliability, 1);
        encoded += Functions::numToByte(route.load, 1);
        encoded += Functions::numToByte(route.routeTag, 1);
        encoded += ByteString(1, 0x00);
        encoded += Functions::numToByte(route.mask, 1);
        encoded += Functions::compactNetworkAddress(route.network.toString(), route.mask);
        return encoded;
    }

    ByteString EigrpInterface::encodeExternalRouteOption(const RoutingTable::Eigrp& route, bool removed)
    {
        ByteString encoded;
        encoded += Functions::changeSize(route.originRouter.toString(), 4);
        encoded += Functions::numToByte(route.originAS, 4);
        encoded += Functions::numToByte(route.routeTag, 4);
        encoded += (removed ? ByteString(4, 0xff) : Functions::numToByte(route.delay, 4));
        encoded += Functions::numToByte(route.bandwidth, 4);
        encoded += Functions::numToByte(route.mtu, 3);
        encoded += Functions::numToByte(route.hopCount, 1);
        encoded += Functions::numToByte(route.reliability, 1);
        encoded += Functions::numToByte(route.load, 1);
        encoded += Functions::numToByte(route.mask, 1);
        encoded += Functions::compactNetworkAddress(route.network.toString(), route.mask);
        return encoded;
    }

    void EigrpInterface::advertiseSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute)
    {
        // Construct the route to advertise
        RoutingTable::Eigrp summaryEigrpRoute = encodeSummaryRoute(summaryRoute);
        
        std::vector<ByteString> neighborsToNotify;
        bool hasMulticast = false;

        std::shared_lock<std::shared_mutex> lock(neighborMutex);

        // Iterate through neighbors
        for (const auto& [address, neighbor] : neighbors)
        {
            if (neighbor->mode == EigrpConfigs::CommunicationMode::UNICAST)
            {
                neighborsToNotify.emplace_back(address);
            }
            else if (neighbor->mode == EigrpConfigs::CommunicationMode::MULTICAST)
            {
                hasMulticast = true;
            }
        }

        // Send unicast updates
        for (const auto& address : neighborsToNotify)
        {
            sendUpdateToNeighbor(address, {summaryEigrpRoute}, EigrpConfigs::UpdateType::PARTIAL);
        }
        if (hasMulticast)
        {
            sendUpdateToNeighbor("", {summaryEigrpRoute}, EigrpConfigs::UpdateType::PARTIAL);
        }
    }

    void EigrpInterface::withdrawSummaryRoute(const ByteString& network, int mask)
    {    
        RoutingTable::Eigrp withdrawRoute;
        withdrawRoute.network = network;
        withdrawRoute.mask = mask;
        withdrawRoute.metric = std::numeric_limits<unsigned int>::max(); // Indicate route is withdrawn

        std::vector<ByteString> unicastNeighbors;
        bool hasMulticast = false;

        std::shared_lock<std::shared_mutex> lock(neighborMutex);
        for (const auto &[neighborIp, neighborInfo] : neighbors) 
        {
            std::lock_guard<std::shared_mutex> lock(neighborInfo->neighborDataMutex);
            ByteString key = withdrawRoute.network + "/" + std::to_string(withdrawRoute.mask);
            auto advertIt = neighborInfo->advertisedRoutes.find(key);

            if (advertIt != neighborInfo->advertisedRoutes.end())
            {
                advertIt->second.removePending = true;
            }

            if (neighborInfo->mode == EigrpConfigs::CommunicationMode::UNICAST)
            {
                unicastNeighbors.emplace_back(neighborIp);
            }
            else if (neighborInfo->mode == EigrpConfigs::CommunicationMode::MULTICAST)
            {
                hasMulticast = true;
            }
        }

        // Send withdraw update to the neighbor
        for (const auto& address : unicastNeighbors)
        {
            sendUpdateToNeighbor(address, {withdrawRoute}, EigrpConfigs::UpdateType::WITHDRAW);
        }
        if (hasMulticast)
        {
            sendUpdateToNeighbor("", {withdrawRoute}, EigrpConfigs::UpdateType::WITHDRAW);
        }
    }

    void EigrpInterface::handleStubRouteUpdates()
    {
        auto eigrpProcess = this->eigrpProcess;

        // Identify routes that should no longer be advertised
        std::vector<RoutingTable::Eigrp> routesToWithdraw;
        std::shared_lock<std::shared_mutex> lock(neighborMutex);
        for (const auto& [address, neighbor] : neighbors)
        {
            std::shared_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
            for (const auto& [routeKey, advertisedRoute] : neighbor->advertisedRoutes)
            {
                bool found = false;
                for (auto route : routesToWithdraw)
                {
                    if (route.network == advertisedRoute.route.network && route.mask == advertisedRoute.route.mask)
                    {
                        found = true;
                    }
                }
                if (found)
                {
                    continue;
                }

                bool shouldAdvertise = false;

                if (eigrpProcess->isStub())
                {
                    if (advertisedRoute.route.routeType == "connected" && eigrpProcess->advertiseConnected())
                        shouldAdvertise = true;
                    if (advertisedRoute.route.routeType == "static" && eigrpProcess->advertiseStatic())
                        shouldAdvertise = true;
                    if (advertisedRoute.route.routeType == "summary" && eigrpProcess->advertiseSummary())
                        shouldAdvertise = true;
                    if (advertisedRoute.route.routeType == "external" &&  eigrpProcess->advertiseRedistributed())
                        shouldAdvertise = true;
                }
                else
                {
                    shouldAdvertise = true;
                }

                if (!shouldAdvertise)
                {
                    // Prepare to withdraw this route
                    RoutingTable::Eigrp withdrawRoute = advertisedRoute.route;
                    withdrawRoute.metric = std::numeric_limits<unsigned int>::max();
                    withdrawRoute.nextHop = ByteString(advertisedRoute.route.network.size(), '\xff');
                    withdrawRoute.routeType = "withdrawn";

                    routesToWithdraw.emplace_back(withdrawRoute);
                }
            }
        }
        
        if (!routesToWithdraw.empty())
        {
            // Withdraw routes to all neighbors
            bool hasMulticast = false;
            for (const auto& [neighborIp, neighborInfo] : neighbors)
            {
                if (neighborInfo->mode == EigrpConfigs::CommunicationMode::UNICAST)
                {
                    if (!neighborInfo->isInit) continue;

                    // Send withdraw updates
                    sendUpdateToNeighbor(neighborIp, routesToWithdraw, EigrpConfigs::UpdateType::WITHDRAW);
                }
                else if (neighborInfo->mode == EigrpConfigs::CommunicationMode::MULTICAST)
                {
                    hasMulticast = true;
                }
            }
            if (hasMulticast)
            {
                sendUpdateToNeighbor("", routesToWithdraw, EigrpConfigs::UpdateType::WITHDRAW);
            }
        }
    }

    void EigrpInterface::startHelloHelper()
    {
        if (helloTimerActive)
        {
            // Hello timer is already active
            return;
        }
        helloTimerActive = true;

        startHello();
    }

    void EigrpInterface::startHello()
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

        helloTimerId = TimeManager::getInstance().addTimer(nextExpiration, [this]()
        {
            try
            {
                sendHelloPacket();
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

            startHello(); // Reschedule
        });
        Logger::getInstance().info() << "Hello timer started with expiration at " << std::chrono::duration_cast<std::chrono::seconds>(nextExpiration.time_since_epoch()).count() << std::endl;
    }

    void EigrpInterface::sendHelloPacket(ByteString neighborIp, bool unicast, bool update, int sequenceNumber)
    {
        if (configs.isPassive)
        {
            Logger::getInstance().info() << "Interface is passive. Hello packet not sent." << std::endl;
            return;
        }

        AddressFamily af = eigrpProcess->getAddressFamily();
        ByteString targetIp;
        
        targetIp = (unicast && !neighborIp.empty()) ? neighborIp : getMulticast();

        PacketInfo eigrpHello;
        EigrpHeader eigrp;

        eigrpProcess->eigrpHello(eigrp, this, neighborIp, sequenceNumber, false, update);

        // Add stub flags
        if (eigrpProcess->isStub())
        {
            EigrpHeader::Option stubOption;
            stubOption.option = Variable::Eigrp::Option::stub;
            stubOption.value = encodeStubOption(eigrpProcess->getConfigs()->stubConfig);
            stubOption.length = Functions::numToByte(stubOption.value.size() + 4, 2);
            eigrp.options.emplace_back(stubOption);
        }

        eigrpHello.Layer3.push_back(eigrp);

        currentInterface.lock()->ipPacket->setIPHeader(eigrpHello, targetIp, getConfigs()->DSCP, 2, Variable::IP::eigrp);
    }

    void EigrpInterface::stopHello()
    {
        std::lock_guard<std::mutex> lock(helloTimerMutex);
        if (helloTimerId != 0)
        {
            TimeManager::getInstance().cancelTimer(helloTimerId); 
            helloTimerId = 0;
        }
        helloTimerActive = false;
    }

    void EigrpInterface::startHoldTimer(const ByteString neighborIp, int holdTime)
    {
        if (neighbors.find(neighborIp) == neighbors.end()) { return; }
        std::shared_ptr<EigrpConfigs::NeighborInfo> &neighbor = neighbors[neighborIp];
        if (neighbor->holdTimerId != 0)
        {
            TimeManager::getInstance().cancelTimer(neighbor->holdTimerId);
        }

        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(holdTime);
        neighbor->holdTimerId = TimeManager::getInstance().addTimer(expirationTime, [this, neighborIp]()
                                                                   { handleHoldTimeExpire(neighborIp); });
    }

    void EigrpInterface::handleHoldTimeExpire(const ByteString neighborIp)
    {
        bool found = false;
        {
            std::lock_guard<std::shared_mutex> lock(neighborMutex);
            auto it = neighbors.find(neighborIp);
            if (it != neighbors.end())
            {
                found = true;
            }
        }

        if (found)
        {
            handleNeighborDown(neighborIp);
        }
    }

    void EigrpInterface::startActiveTimer(const RoutingTable::Eigrp& route)
    {
        if (!eigrpProcess->getConfigs()->activeTimerEnabled) return;
        auto key = route.network + "/" + std::to_string(route.mask);
        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(eigrpProcess->getConfigs()->activeTime);

        // Schedule Active timer
        int timerId = TimeManager::getInstance().addTimer(expirationTime, [this, route]() {
            handleActiveTimeExpire(route);
        });

        {
            std::lock_guard<std::mutex> lock(activeTimerMutex);
            activeTimers[key] = timerId;
        }
    }

    void EigrpInterface::handleActiveTimeExpire(const RoutingTable::Eigrp& route)
    {
        ByteString key = route.network + "/" + std::to_string(route.mask);

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
        eigrpProcess->topologyTable->handleRouteFailure(route.network, route.nextHop);

        // Step 3: Remove the route from the routing table
        RoutingTable& routingTable = RoutingTable::getInstance();
        routingTable.removeEigrp(route.network, route.mask, eigrpProcess->getAddressFamily());

        // Step 4: Notify neighbors about the route removal
        RoutingTable::Eigrp removedRoute = route;
        removedRoute.metric = std::numeric_limits<unsigned int>::max();
        removedRoute.nextHop = ByteString(route.nextHop.size(), '\xff');
        removedRoute.routeType = "internal"; // Ensure routeType is set appropriately

        eigrpProcess->notifyRoutingChange({removedRoute}, /*isRemoval*/true);

        // Step 5: Trigger stuck-in-active
        startStuckInActive();
    }

    void EigrpInterface::cancelActiveTimer(const ByteString &destination, int mask)
    {
        std::lock_guard<std::mutex> lock(activeTimerMutex);
        ByteString key = destination + "/" + std::to_string(mask);

        auto it = activeTimers.find(key);
        if (it != activeTimers.end())
        {
            TimeManager::getInstance().cancelTimer(it->second);
            activeTimers.erase(it);
        }
    }

    void EigrpInterface::startStuckInActive()
    {
        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(eigrpProcess->getConfigs()->stuckInActiveTime);

        // Schedule Stuck In Active timer
        stuckInActiveTimerId = TimeManager::getInstance().addTimer(expirationTime, [this]()
        {
            handleStuckInActive();
        });
    }

    void EigrpInterface::handleStuckInActive()
    {
        std::lock_guard<std::mutex> lock(activeTimerMutex);

        for (const auto& [key, timerId] : activeTimers)
        {
            auto delimiter = key.toString().find('/');
            if (delimiter == std::string::npos) continue;

            ByteString network = key.substr(0, delimiter);
            int mask = std::stoi(key.substr(delimiter + 1).toString());   

            // Log stuck-in-active route
            Logger::getInstance().warn() << "Handling stuck-in-active for network: " << network.toHex() << "/" << mask << std::endl;

            // Cancel timer
            cancelActiveTimer(network, mask);

            // Try recalculating a new route
            auto routeInfo = eigrpProcess->topologyTable->findBestRoute(network, eigrpProcess->getConfigs()->variance);
            if (routeInfo.has_value())
            {
                updateRoutingTableForDestination(network);
            }
            else
            {
                // Log route removal
                Logger::getInstance().info() << "Removing stuck-in-active route: " << network.toHex() << "/" << mask << std::endl;

                // make sure route exists
                ByteString neighborIp;
                std::optional<RoutingTable::Eigrp> route = RoutingTable::getInstance().getEigrpRoute(network, mask, eigrpProcess->getAddressFamily());
                if (route.has_value())
                {
                    neighborIp = route.value().nextHop;
                }
                
                RoutingTable::Eigrp routeToRemove;
                {
                    std::shared_lock<std::shared_mutex> interfaceLock(currentInterfaceInfo.lock()->ipMutex);
                    routeToRemove.network = network;
                    routeToRemove.mask = mask;
                    routeToRemove.delay = std::numeric_limits<unsigned int>::max();
                    routeToRemove.bandwidth = (10000000 / currentInterfaceInfo.lock()->bandwidth) * 256;
                    routeToRemove.nextHop = ByteString(network.size(), '\xff');
                    routeToRemove.routeType = "internal";
                }

                // Mark as pending removal for all neighbors
                for (auto& [neighborIp, neighborInfo] : neighbors)
                {
                    ByteString key = routeToRemove.network + "/" + std::to_string(routeToRemove.mask);
                    auto advertIt = neighborInfo->advertisedRoutes.find(key);
                    if (advertIt != neighborInfo->advertisedRoutes.end())
                    {
                        advertIt->second.removePending = true;
                    }
                }

                eigrpProcess->notifyRoutingChange({routeToRemove}, true);
                eigrpProcess->topologyTable->handleRouteFailure(network, neighborIp);
                RoutingTable::getInstance().removeEigrp(network, mask, eigrpProcess->getAddressFamily());
            }
        }

        stuckInActiveTimerId = 0;
    }

    void EigrpInterface::cancelStuckInActive()
    {
        if (stuckInActiveTimerId != 0)
        {
            TimeManager::getInstance().cancelTimer(stuckInActiveTimerId);
            stuckInActiveTimerId = 0;
        }
    }

    int EigrpInterface::startRetransmissionTimer(const ByteString neighborIp, const int& sequenceNumber, double timeout)
    {
        if (timeout <= 0.0)
        {
            Logger::getInstance().error() << "Invalid timeout value for retransmission timer." << std::endl;
            return 0;
        }
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor;
        {
            auto it = neighbors.find(neighborIp);
            if (it == neighbors.end())
            {
                Logger::getInstance().warn() << "Cannot start retransmission for unknown neighbor: " << neighborIp.toHex() << std::endl;
                return 0;
            }
            neighbor = it->second;
        }
        
        {
            // Cancel any existing retransmission timers for this sequence number
            std::shared_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
            auto pktIt = neighbor->reliablePackets.find(sequenceNumber);
            if (pktIt != neighbor->reliablePackets.end())
            {
                // Cancel existing timer if any
                if (pktIt->second.timerId != 0)
                {
                    TimeManager::getInstance().cancelTimer(pktIt->second.timerId);
                }
            }
        }

        // Capture a weak_ptr to prevent dangling reference
        std::weak_ptr<EigrpInterface> weakSelf = shared_from_this();

        // Schedule a retransmission timer
        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(timeout * 1000));
        int timerId = TimeManager::getInstance().addTimer(expirationTime, [weakSelf, neighborIp, sequenceNumber]()
        {
            if (auto self = weakSelf.lock())
            {
                self->handleRetransmissionTimeout(neighborIp, sequenceNumber);
            }
            else
            {
                Logger::getInstance().warn() << "EigrpInterface object no longer exists. Cannot handle retransmission timeout." << std::endl;
            }
        });

        // Update the timer ID in ReliablePacketInfo
        {
            std::lock_guard<std::shared_mutex> lock(neighbor->neighborDataMutex);
            neighbor->reliablePackets[sequenceNumber].timerId = timerId;
        }

        Logger::getInstance().debug() << "Retransmission timer started for neighbor: " << neighborIp.toHex() << " with sequence number: " << sequenceNumber << " and timeout: " << timeout << " seconds" << std::endl;

        return timerId;
    }

    void EigrpInterface::handleRetransmissionTimeout(const ByteString neighborIp, const int& sequenceNumber)
    {
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor;
        {
            std::lock_guard<std::shared_mutex> lock(neighborMutex);
            auto it = neighbors.find(neighborIp);
            if (it == neighbors.end()) {
                Logger::getInstance().warn() << "Retransmission timeout for unknown neighbor: " << neighborIp.toHex() << std::endl;
                return;
            }
            neighbor = it->second;
        }

        EigrpConfigs::NeighborInfo::ReliablePacketInfo* pktInfo;
        {
            std::shared_lock<std::shared_mutex> dataLock(neighbor->neighborDataMutex);
            auto pktIt = neighbor->reliablePackets.find(sequenceNumber);
            if (pktIt == neighbor->reliablePackets.end())
            {
                Logger::getInstance().warn() << "No reliable packet found for sequence number: " << sequenceNumber << " with neighbor: " << neighborIp.toHex() << std::endl;
                return;
            }
            pktInfo = &pktIt->second;
        }

        // Check retransmission count
        if (pktInfo->retransmissionCount >= MAX_RETRANSMISSIONS)
        {
            Logger::getInstance().info() << "Max retransmission reached for neighbor " << neighborIp.toHex() << " sequence number " << sequenceNumber << ". marking neighbor as down." << std::endl;
            if (pktInfo->timerId != 0)
            {
                TimeManager::getInstance().cancelTimer(pktInfo->timerId);
            }
            handleNeighborDown(neighborIp);
            return;
        }

        // Resend the packet
        if (!neighbor->pendingAcks.empty())
        {
            pktInfo->packet.eigrp.ack = Functions::numToByte(neighbor->pendingAcks.front(), 4);
            neighbor->pendingAcks.erase(neighbor->pendingAcks.begin());
        }
        PacketInfo retransmissionPacket;
        retransmissionPacket.Layer3.push_back(pktInfo->packet.eigrp);
        currentInterface.lock()->ipPacket->setIPHeader(retransmissionPacket, pktInfo->packet.destination, getConfigs()->DSCP, 2, Variable::IP::eigrp);

        Logger::getInstance().info() << "Resent packet to neighbor " << neighborIp.toHex() << " for sequence number " << sequenceNumber << ". Retransmission count: " << pktInfo->retransmissionCount + 1 << "." << std::endl;

        // Increment retransmission count and update RTT estimates
        {
            std::lock_guard<std::shared_mutex> dataLock(neighbor->neighborDataMutex);
            neighbor->reliablePackets[sequenceNumber].retransmissionCount += 1;
            neighbor->rto = std::min(neighbor->rto * 2.0, 60.0); // Exponential backoff with a cap
        }

        // Restart the retransmission timer
        int newTimerId = startRetransmissionTimer(neighborIp, sequenceNumber, neighbor->rto);
        {
            std::lock_guard<std::shared_mutex> dataLock(neighbor->neighborDataMutex);
            neighbor->reliablePackets[sequenceNumber].timerId = newTimerId;
        }
        
        Logger::getInstance().debug() << "Retransmission timer restarted for neighbor: " << neighborIp.toHex() << " with sequence number: " << sequenceNumber << " and new timeout: " << neighbor->rto << " seconds" << std::endl;
    }
    RoutingTable::Eigrp EigrpInterface::decodeRoute(ByteString value, bool external, bool summary)
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
                route.delay = Functions::byteToNum(value.substr(start, 4).toString());
                start += 4;

                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Bandwidth.");
                route.bandwidth = Functions::byteToNum(value.substr(start, 4).toString());
                start += 4;

                if (value.size() < start + 3) throw std::runtime_error("Insufficient data for MTU.");
                route.mtu = Functions::byteToNum(value.substr(start, 3).toString());
                start += 3;
    
                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Hop Count.");
                route.hopCount = Functions::byteToNum(value.substr(start, 1).toString());
                start += 1;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Reliability.");
                route.reliability = Functions::byteToNum(value.substr(start, 1).toString());
                start += 1;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Load.");
                route.load = Functions::byteToNum(value.substr(start, 1).toString());
                start += 1;

                if (value.size() < start + 2) throw std::runtime_error("Insufficient data for Route Tag.");
                route.routeTag = Functions::byteToNum(value.substr(start, 1).toString());
                start += 2;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Mask.");
                route.mask = Functions::byteToNum(value.substr(start, 1).toString());
                start += 1;

                if (value.size() < start + (route.mask + 7) / 8) throw std::runtime_error("Insufficient data for Network Address.");
                route.network = value.substr(start);
                start += route.mask;

                route.routeType = summary ? "summary" : "internal";
            }
            else 
            {
                // External Route Parsing
                if (eigrpProcess->getAddressFamily() == AddressFamily::IPv4) 
                {
                    if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Origin Router (IPv4).");
                    route.originRouter = value.substr(start, 4);
                    start += 4;
                }
                else if (eigrpProcess->getAddressFamily() == AddressFamily::IPv6) 
                {
                    if (value.size() < start + 16) throw std::runtime_error("Insufficient data for Origin Router (IPv6).");
                    route.originRouter = value.substr(start, 16);
                    start += 16;
                }

                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Origin AS.");
                route.originAS = Functions::byteToNum(value.substr(start, 4).toString());
                start += 4;

                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Route Tag (External).");
                route.routeTag = Functions::byteToNum(value.substr(start, 4).toString());
                start += 4;

                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Extended Metric.");
                route.extendedMetric = Functions::byteToNum(value.substr(start, 4).toString());
                start += 4;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Extended ID.");
                route.extendedId = Functions::byteToNum(value.substr(start, 1).toString());
                start += 1;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Flags.");
                route.flags = value.substr(start, 1);
                start += 1;

                // Parsing additional fields if necessary...
                // Ensure all fields are parsed based on EIGRP specifications

                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Delay (External).");
                route.delay = Functions::byteToNum(value.substr(start, 4).toString());
                start += 4;

                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Bandwidth (External).");
                route.bandwidth = Functions::byteToNum(value.substr(start, 4).toString());
                start += 4;

                if (value.size() < start + 3) throw std::runtime_error("Insufficient data for MTU (External).");
                route.mtu = Functions::byteToNum(value.substr(start, 3).toString());
                start += 3;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Hop Count (External).");
                route.hopCount = Functions::byteToNum(value.substr(start, 1).toString());
                start += 1;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Reliability (External).");
                route.reliability = Functions::byteToNum(value.substr(start, 1).toString());
                start += 1;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Load (External).");
                route.load = Functions::byteToNum(value.substr(start, 1).toString());
                start += 1;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Mask (External).");
                route.mask = Functions::byteToNum(value.substr(start, 1).toString());
                start += 1;

                if (value.size() < start + (route.mask + 7) / 8) throw std::runtime_error("Insufficient data for Network Address (External).");
                route.network = value.substr(start);
                start += route.mask;

                route.routeType = "external";
            }

            // Pad network address to standard length
            size_t standardLength = (eigrpProcess->getAddressFamily() == AddressFamily::IPv4) ? 4 : 16;
            if (route.network.size() < standardLength) 
            {
                route.network += ByteString(standardLength - route.network.size(), '\x00');
            }

            // Calculate Feasible Distance and Composite Metric
            double neighborRD = eigrpProcess->calculateMetric(route.bandwidth, route.load, route.delay, route.reliability);
            route.reportedDistance = neighborRD;
    
            // Calculate FD = Local Link Cost + RD
            double localLinkCost = calculateLocalLinkCost();
            route.feasibleDistance = localLinkCost + route.reportedDistance;
    
            // Calculate the composite metric for internal use
            route.metric = localLinkCost;

            // Set the administrative distance
            route.adminDistance = (route.routeType == "internal" || route.routeType == "summary")
                ? eigrpProcess->getConfigs()->adminDistance
                : eigrpProcess->getConfigs()->externalAdminDistance;

            return route;
        }
        catch (const std::exception& e)
        {
            Logger::getInstance().error() << "DecodedRoute Error: " << e.what() << std::endl;
            throw;
        }
    }

    double EigrpInterface::calculateLocalLinkCost()
    {
        std::shared_lock<std::shared_mutex> lock(currentInterfaceInfo.lock()->ipMutex);
        double bandwidthMetric = (static_cast<double>(eigrpProcess->getConfigs()->wideMetric) / currentInterfaceInfo.lock()->bandwidth);
        double delayMetric = static_cast<double>(currentInterfaceInfo.lock()->delay) / 10.0;

        double loadMetric = 0.0;
        if (eigrpProcess->getConfigs()->kvalue.k2_Load != 0 && (256.0 - configs.load) != 0)
        {
            loadMetric = (static_cast<double>(eigrpProcess->getConfigs()->kvalue.k2_Load) * configs.load) / (256.0 - configs.load);
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

    void EigrpInterface::updateRoutingTable(const std::vector<RoutingTable::Eigrp> routes, bool init, const ByteString neighborIp) 
    {
        std::vector<RoutingTable::Eigrp> updatedRoutes;

        for (const auto& route : routes)
        {
            // Logger::getInstance().debug() << "Updating routing table for route: " << route.network.toHex() << std::endl;

            // Access the topology table and update it with new routes
            TopologyTable::RouteInfo routeInfo;
            routeInfo.feasibleDistance = route.feasibleDistance;
            routeInfo.reportedDistance = route.reportedDistance;
            routeInfo.nextHop = neighborIp;
            routeInfo.hopCount = route.hopCount;
            routeInfo.isSuccessor = false;
            routeInfo.isFeasibleSuccessor = false;

            // Add or update the route in the topology table
            eigrpProcess->topologyTable->addOrUpdateRoute(route.network, route.mask, routeInfo, neighborIp);

            // Fetch the updated topology table
            auto bestRouteEntry = eigrpProcess->topologyTable->getEntryForRoute(route.network);
            if (!bestRouteEntry)
            {
                Logger::getInstance().warn() << "Failed to retrieve topology entry for network: " << route.network.toHex() << std::endl;
                continue;
            }

            auto successorIt = std::find_if(
                bestRouteEntry->routesByNeighbor.begin(),
                bestRouteEntry->routesByNeighbor.end(),
                [](const auto& pair) {return pair.second.isSuccessor;});

            if (successorIt == bestRouteEntry->routesByNeighbor.end())
            {
                Logger::getInstance().debug() << "No successors found for route: " << route.network.toHex() << std::endl;
                continue;
            }

            // Prepare the administrative distance based on the route type
            RoutingTable::Eigrp newRoute = route;
            newRoute.nextHop = successorIt->second.nextHop;
            newRoute.feasibleDistance = successorIt->second.feasibleDistance;

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
                Logger::getInstance().error() << "Unknown route type: " << route.routeType << std::endl;
                continue; // Skip unknown route type
            }

            // Check for existing routes and determine if an update is needed
            auto existingRoute = RoutingTable::getInstance().getEigrpRoute(route.network, route.mask, eigrpProcess->getAddressFamily());
            bool routeChange = !existingRoute.has_value() || (existingRoute->feasibleDistance != newRoute.feasibleDistance);

            if (routeChange)
            {
                if (existingRoute.has_value())
                {
                    Logger::getInstance().debug() << "Route metric better with: " << newRoute.metric << " Replacing the old metric of: " << existingRoute->metric << std::endl;
                }
                else
                {
                    Logger::getInstance().debug() << "New route detected, route: " << newRoute.network.toHex() << " being added to the routing table" << std::endl;
                }
                
                // Update the global EIGRP table
                RoutingTable::getInstance().addEigrp(newRoute, eigrpProcess->getAddressFamily());

                // Notify neighbors about the route change
                updatedRoutes.emplace_back(newRoute);
            }
        }
        if (!updatedRoutes.empty())
        {
            // Notify neighbors about the updates routes
            eigrpProcess->notifyRoutingChange(updatedRoutes, /*isRemoval*/ false, /*init*/init);
        }
    }

    void EigrpInterface::handleNeighborDown(const ByteString neighborIp)
    {
        return;
        // Remove neighbor from the neighbor table
        if (neighbors.find(neighborIp) == neighbors.end())
        {
            Logger::getInstance().warn() << "Attempted to handle down state for non-existent neighbor: " << neighborIp.toHex() << std::endl;
            return;
        }
        std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor = neighbors[neighborIp];

        // Cancel any pending timers
        if (neighbor->holdTimerId != 0)
        {
            TimeManager::getInstance().cancelTimer(neighbor->holdTimerId);
            neighbor->holdTimerId = 0;
        }
        for (const auto &timerEntry : neighbor->retransmissionTimers)
        {
            TimeManager::getInstance().cancelTimer(timerEntry.second);
        }

        neighbor->retransmissionTimers.clear();
        neighbor->reliablePackets.clear();
        neighbor->sequenceList.clear();

        // Removed Routes
        std::vector<RoutingTable::Eigrp> removedRoutes;
        eigrpProcess->topologyTable->removeRoutesFromNeighbor(neighborIp);

        // Check if this is the last neighbor
        if (neighbors.size() == 1)
        {
            auto routeIt = RoutingTable::getInstance().getEigrpRoute(Functions::computeNetworkAddress(getConfigs()->interfaceAddress.toString(), getConfigs()->interfaceMask), getConfigs()->interfaceMask, eigrpProcess->getAddressFamily());
            if (routeIt.has_value())
            {
                removedRoutes.push_back(routeIt.value());
            }
        }
        // for each affected destination, re-run DUEL
        for (auto &[destination, entry] : eigrpProcess->topologyTable->getTopologyEntries())
        {
            // Check if the route was learned from the failed neighbor
            if (entry->routesByNeighbor.count(neighborIp))
            {
                // Remove the roite from the topology table
                entry->routesByNeighbor.erase(neighborIp);

                // If no other routes are available, remove the route from the routing table
                if (entry->routesByNeighbor.empty())
                {
                    // Remove the route from the routing table
                    eigrpProcess->topologyTable->removeRoutesFromNeighbor(destination);

                    // Notify neighbors of the trade
                    RoutingTable::Eigrp removedRoute;
                    removedRoute.network = destination;
                    removedRoute.mask = entry->prefixLength;
                    removedRoute.nextHop = ByteString(destination.size(), '\xff');
                    removedRoute.metric = std::numeric_limits<unsigned int>::max();
                    removedRoute.routeType = "internal";

                    // Mark the route as pending removal in advertisedRoutes for all neighbors
                    for (auto &[otherNeighborIp, otherNeighborInfo] : neighbors)
                    {
                        ByteString key = removedRoute.network + "/" + std::to_string(removedRoute.mask);
                        auto advertIt = otherNeighborInfo->advertisedRoutes.find(key);
                        if (advertIt != otherNeighborInfo->advertisedRoutes.end())
                        {
                            advertIt->second.removePending = true;
                        }
                    }

                    removedRoutes.emplace_back(removedRoute);
                    
                    Logger::getInstance().info() << "Removed destination " << destination.toHex() << " from topology table due to neighbor down." << std::endl;
                }
                else
                {
                    // Find new successor and update routing table
                    updateRoutingTableForDestination(destination);
                }
            }
        }

        if (!removedRoutes.empty())
        {
            eigrpProcess->notifyRoutingChange(removedRoutes, /*isRemoval=*/true);
        }

        // Remove neighbor from neighbor map
        neighbors.erase(neighborIp);

        Logger::getInstance().info() << "Neighbor " << neighborIp.toHex() << " marked as down. Topology and routing tables updated." << std::endl;
    }

    void EigrpInterface::handleNeighborRestart(const ByteString neighborIp)
    {
        auto neighborIt = neighbors.find(neighborIp);
        if (neighborIt == neighbors.end()) return;

        auto neighbor = neighborIt->second;

        // Reset sequence numbers and reliable packets
        {
            std::lock_guard<std::shared_mutex> dataLock(neighbor->neighborDataMutex);
            neighbor->reliablePackets.clear();
        }

        // Cancel existing timers
        if (neighbor->holdTimerId != 0)
        {
            TimeManager::getInstance().cancelTimer(neighbor->holdTimerId);
            neighbor->holdTimerId = 0;
        }
        for (const auto& [seqNum, pktInfo] : neighbor->reliablePackets)
        {
            if (pktInfo.timerId != 0)
            {
                TimeManager::getInstance().cancelTimer(pktInfo.timerId);
            }
        }

        neighbor->reliablePackets.clear();
        neighbor->sequenceList.clear();

        // Reinitialize neighbor state
        {
            std::lock_guard<std::mutex> lock(neighbor->initializationMutex);
            neighbor->initialization = EigrpConfigs::NeighborState::INIT;
        }
        Logger::getInstance().info() << "Neighbor: " << neighborIp.toHex() << " is now int the INIT state" << std::endl;

        // Send a hello packet to re-establish communication
        sendHelloPacket(neighborIp, /*unicast=*/true);

        // Restart the hold timer
        startHoldTimer(neighborIp, neighbor->holdTime);
    }

    void EigrpInterface::updateRoutingTableForDestination(const ByteString &destination)
    {
        auto entry = eigrpProcess->topologyTable->getEntryForRoute(destination);
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
            routingTable.addEigrp(newRoute, eigrpProcess->getAddressFamily());

            eigrpProcess->notifyRoutingChange({newRoute}, /*isRemoval*/ false);
        }
        else
        {
            RoutingTable &routingTable = RoutingTable::getInstance();
            routingTable.removeEigrp(destination, entry->prefixLength, eigrpProcess->getAddressFamily());
        }
    }

    double EigrpInterface::calculateRTT(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor, int sequenceNumber)
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

    void EigrpInterface::updateRTTEstimate(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor, int sequenceNumber)
    {
        double rttSample = calculateRTT(neighbor, sequenceNumber);

        // Update srtt and rttvar using standard algorithms
        double alpha = 1.0 / 8.0;
        double beta = 1.0 / 4.0;

        {
            neighbor->rttvar = (1.0 - beta) * neighbor->rttvar + beta * std::abs(neighbor->srtt - rttSample);
            neighbor->srtt = (1.0 - alpha) * neighbor->srtt + alpha * rttSample;
            neighbor->rto = neighbor->srtt + std::max(0.1, 4.0 * neighbor->rttvar);
            neighbor->rto = std::clamp(neighbor->rto, 1.0, 60.0); // Bounds: 1s to 60s
        }
    }

    int EigrpInterface::getNextSequenceNumber() {
        std::lock_guard<std::shared_mutex> lock(seqMutex);
        int currentSeq = nextSequenceNumber;
        nextSequenceNumber += 1;
        Logger::getInstance().debug() << "Assigned sequence number " << currentSeq << " to interface: " << getConfigs()->interfaceAddress.toHex() << std::endl;
        return currentSeq;
    }

    void EigrpInterface::setupReliablePacket(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor, const EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet packet, int sequenceNum)
    {
        if (!neighbor->processAcks) return;

        {
            std::shared_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
            if (neighbor->reliablePackets.find(sequenceNum) != neighbor->reliablePackets.end())
            {
                Logger::getInstance().warn() << "Packet with sequence number " << sequenceNum << " already exists for neighbor " << neighbor->ipAddress.toHex() << std::endl;
                return;
            }
        }

        // Create ReliablePacketInfo
        EigrpConfigs::NeighborInfo::ReliablePacketInfo pktInfo(packet);
        pktInfo.sendTime = std::chrono::steady_clock::now();
        pktInfo.retransmissionCount = 0;

        // Start retransmission timer
        pktInfo.timerId = startRetransmissionTimer(neighbor->ipAddress, sequenceNum, neighbor->rto);
        if (pktInfo.timerId == 0)
        {
            Logger::getInstance().error() << "Failed to start retransmission timer for neighbor " << neighbor->ipAddress.toHex() << " sequence number " << sequenceNum << std::endl;
            return;
        }

        // Store the packet
        {
            std::lock_guard<std::shared_mutex> lock(neighbor->neighborDataMutex);
            neighbor->reliablePackets[sequenceNum] = pktInfo;
        }

        Logger::getInstance().debug() << "Reliable packet setup for neighbor " << neighbor->ipAddress.toHex() << " with sequence number " << sequenceNum << std::endl;
    }

    ByteString EigrpInterface::findQueryNeighbor(int queryId)
    {
        auto it = outstandingReplies.find(queryId);
        if (it != outstandingReplies.end())
        {
            return it->second.first;
        }
        return "";
    }

    RoutingTable::Eigrp EigrpInterface::encodeSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute)
    {
        std::shared_lock<std::shared_mutex> lock(currentInterfaceInfo.lock()->ipMutex);
        RoutingTable::Eigrp route;
        route.nextHop = ByteString(summaryRoute.network.size(), '\x00');
        route.bandwidth = (10000000 / currentInterfaceInfo.lock()->bandwidth) * 256;
        route.delay = (currentInterfaceInfo.lock()->delay / 10) * 256;
        route.mtu = currentInterfaceInfo.lock()->mtu;
        route.hopCount = 0;
        route.reliability = 255;
        route.load = configs.load;
        route.routeTag = 0;
        route.mask = summaryRoute.mask;
        route.network = summaryRoute.network;
        route.routeType = "summary";

        return route;
    }

    ByteString EigrpInterface::encodeStubOption(const EigrpConfigs::StubConfig stub)
    {
        ByteString binStub = "0000000000";
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
        
        return Functions::binToByte(binStub.toString(), 2);
    }

    bool EigrpInterface::isNeighborAuthenticated(const ByteString neighborIp)
    {
        std::shared_lock<std::shared_mutex> lock(neighborMutex);
        auto neighborIt = neighbors.find(neighborIp);
        if (neighborIt != neighbors.end())
        {
            return neighborIt->second->authenticationEnabled;
        }
        return false;
    }

    void EigrpInterface::configureAuthentication(const ByteString neighborIp, int keyId, const ByteString& key, bool enable)
    {
        std::lock_guard<std::shared_mutex> lock(neighborMutex);
        
        auto neighborIt = neighbors.find(neighborIp);
        if (neighborIt != neighbors.end())
        {
            auto neighbor = neighborIt->second;
            neighbor->authKeyId = keyId;
            neighbor->authKey = key;
            neighbor->authenticationEnabled = enable;
        }
    }

    ByteString EigrpInterface::serializeEigrpHeader(const EigrpHeader& eigrp, bool exclusiveAuthTLV)
    {
        ByteString serialized;
        serialized += eigrp.version;
        serialized += eigrp.opcode;
        serialized += eigrp.checksum;
        serialized += Functions::binToByte("0000000000000000000000000000" + eigrp.flags.endOfTable.toString() + eigrp.flags.restart.toString() + eigrp.flags.conditionalRecieve.toString() + eigrp.flags.init.toString());
        serialized += eigrp.sequence;
        serialized += eigrp.ack;
        serialized += eigrp.virtualRouterID;
        serialized += eigrp.autonomousSystem;

        for (const auto& option : eigrp.options)
        {
            if (exclusiveAuthTLV && option.option == Variable::Eigrp::Option::authentication)
            {
                continue;
            }
            serialized += option.option;
            serialized += option.length;
            serialized += option.value;
        }
        return serialized;
    }

    EigrpHeader::Option EigrpInterface::generateAuthenticatedTLV(const EigrpHeader& eigrp, const std::shared_ptr<EigrpConfigs::NeighborInfo>& neighbor)
    {
        if (!neighbor->authenticationEnabled || neighbor->authKey.empty())
        {
            return EigrpHeader::Option{};
        }

        // Create Authentication TLV with HMAC set to zero
        EigrpHeader::Option authTLV;
        authTLV.option = Variable::Eigrp::Option::authentication;

        // Key ID (1 byte) + HMAC placeholder (16 bytes of zeros)
        int hmacLength = (neighbor->authType == EigrpConfigs::AuthType::MD5) ? MD5_DIGEST_LENGTH : SHA_DIGEST_LENGTH;
        authTLV.value = Functions::numToByte(neighbor->authKeyId, 1) + ByteString(MD5_DIGEST_LENGTH, 0x00).toString();
        authTLV.length = Functions::numToByte(authTLV.value.size() + 4, 2);

        // Temporarily add the zeroed Authentication TLV to the EIGRP header
        EigrpHeader tempEigrp = eigrp;
        tempEigrp.options.emplace_back(authTLV);

        // Serialize the entire EIGRP header including the zeroed Authentication TLV
        ByteString serializedHeader = serializeEigrpHeader(tempEigrp, /*exclusiveAuthTLV=*/false);

        // Compute HMAC-MD5 over the serialed header
        ByteString computedHMAC;
        if (neighbor->authType == EigrpConfigs::AuthType::MD5)
        {
            computedHMAC = Authentication::generateMD5(serializedHeader.toString(), neighbor->authKey.toString());
        }
        else if (neighbor->authType == EigrpConfigs::AuthType::SHA1)
        {
            computedHMAC = Authentication::generateHMAC(serializedHeader.toString(), neighbor->authKey.toString(), "SHA1");
        }

        // Replace the zeroed HMAC with the real computed HMAC
//         authTLV.value.replace(1, MD5_DIGEST_LENGTH, computedHMAC);

        return authTLV;
    }

    void EigrpInterface::setPassive(bool passive)
    {
        getConfigs()->isPassive = passive;
        if (passive)
        {
            stopHello();
        }
        else
        {
            startHelloHelper();
        }

        Logger::getInstance().info() << "Interface set to " << (passive ? "passive" : "active") << " modeo." << std::endl;
    }

    void EigrpInterface::addNeighbor(const ByteString& ipAddress, const ByteString& macAddress, EigrpConfigs::CommunicationMode mode)
    {
        std::lock_guard<std::shared_mutex> lock(neighborMutex);

        // Add neighbor only if it doesn't already exist
        if (neighbors.find(ipAddress) == neighbors.end())
        {
            auto neighbor = std::make_shared<EigrpConfigs::NeighborInfo>();
            std::lock_guard<std::shared_mutex> lock(neighbor->macMutex);
            neighbor->ipAddress = ipAddress;
            neighbor->macAddress = macAddress;
            neighbor->mode = mode;
            neighbors[ipAddress] = neighbor;
        }
    }

    std::optional<std::shared_ptr<EigrpConfigs::NeighborInfo>> EigrpInterface::getNeighborInfo(const ByteString neighborIp)
    {
        auto it = neighbors.find(neighborIp);
        if (it != neighbors.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    void EigrpInterface::resolveMacAddress(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor)
    {
        std::lock_guard<std::shared_mutex> lock(neighbor->macMutex);
        if (eigrpProcess->getAddressFamily() == AddressFamily::IPv6) 
        {
            // Get neighbor mac Address
            // NEEDS IMPLEMENTATION
        }
        if (eigrpProcess->getAddressFamily() == AddressFamily::IPv4)
        {
            // Get neighbor mac Address
            if (!neighbor->hasMac || neighbor->macAddress.empty())
            {
                auto mac = RoutingTable::getInstance().ArpLookup(neighbor->ipAddress.toString());
                if (mac.has_value())
                {
                    neighbor->macAddress = mac.value().mac;
                    neighbor->hasMac = true;
                }
                else
                {
                    currentInterface.lock()->arp->sendRequest(neighbor->ipAddress.toString());
                }
            }
        }
    }

    ByteString EigrpInterface::getMulticast()
    {
        return (eigrpProcess->getAddressFamily() == AddressFamily::IPv4) ? Variable::Multicast::Eigrp::address : Variable::Multicast::Eigrp::addressv6;
    }

    
    bool EigrpInterface::isRouteAdvertised(ByteString& network, int mask)
    {
        std::lock_guard<std::shared_mutex> lock(neighborMutex);
        for (const auto& [address, neighbor] : neighbors)
        {
            ByteString key = network + "/" + std::to_string(mask);
            auto routeIt = neighbor->advertisedRoutes.find(key);
            if (routeIt == neighbor->advertisedRoutes.end())
            {
                return false;
            }
        }
        return true;
    }

#pragma endregion

#pragma region ClassicEigrp

    

    void ClassicEigrp::initializeEigrp()
    {
        // Call base class initialziation
        Eigrp::initializeEigrp();

        // Classic-specific configuration
        configs.autoSummarizationEnabled = true;
        Logger::getInstance().info() << "Classic EIGRP-specific initialization complete" << std::endl;
    }

    void ClassicEigrp::shutdown()
    {
        Logger::getInstance().info() << "Shutting down Classic EIGRP." << std::endl;

        // Call base class shutdown
        Eigrp::shutdown();

        // Remove auto-summarized routes
        if (configs.autoSummarizationEnabled)
        {
            for (const auto& route : configs.summaryRoutes) {
                removeSummaryRoute(route.network, route.mask);
            }
            Logger::getInstance().info() << "Cleared auto-summarized routes." << std::endl;
        }

        // Clear network configurations
        configs.networks.clear();
        Logger::getInstance().info() << "Cleared all Classic-configured networks." << std::endl;
    }

#pragma endregion

#pragma region NamedEigrp

    NamedEigrp::NamedEigrp(int& as, AddressFamily af, const ByteString& name)
        : Eigrp(as, af), processName(name) {}

    void NamedEigrp::initializeEigrp() {
        // Call base class initialization
        Eigrp::initializeEigrp();

        // Named-soecific configuration
        configs.autoSummarizationEnabled = false;
        Logger::getInstance().info() << "Initialized Named EIGRP (" << processName << ")." << std::endl;
    }

    void NamedEigrp::shutdown() {
        Logger::getInstance().info() << "Shutting down Named EIGRP (" << processName << ")." << std::endl;

        // Call base class shutdown
        Eigrp::shutdown();

        // Clear interface configurations
        interfaceConfigs.clear();
        Logger::getInstance().info() << "Cleared interface-specific configurations." << std::endl;
    }

#pragma endregion

#pragma region Topology

    TopologyTable::TopologyTable(Eigrp* process)
    {
        eigrpProcess = std::shared_ptr<Eigrp>(process);
    }

    void TopologyTable::addOrUpdateRoute(const ByteString destination, int prefixLength, const RouteInfo &routeInfo, const ByteString neighborIp)
    {
        std::lock_guard<std::mutex> lock(tableMutex);

        // Create or update the topology table entry
        auto& entry = topologyEntries[destination];
        if (!entry)
        {
            entry = std::make_shared<TopologyEntry>();
            entry->destination = destination;
            entry->prefixLength = prefixLength;
        }

        entry->routesByNeighbor[neighborIp] = routeInfo;
        entry->routesByNeighbor[neighborIp].lastUpdate = std::chrono::steady_clock::now();

        // Recalculate successors and feasible successors
        updateSuccessorAndFeasibleSuccessors(entry);

        // Logger::getInstance().debug() << "Added/Updates route for destination: " << destination.toHex() << std::endl;
    }

    void TopologyTable::updateSuccessorAndFeasibleSuccessors(std::shared_ptr<TopologyEntry>& entry)
    {
        unsigned int bestFD = std::numeric_limits<unsigned int>::max();
        int bestAD = std::numeric_limits<int>::max();
        entry->successors.clear();
        entry->feasibleSuccessors.clear();

        // Step 1: Find the best feasibel distance (FD) and lowest administrative distance (AD)
        for (const auto& [neighbor, route] : entry->routesByNeighbor)
        {
            bestFD = std::min(bestFD, route.feasibleDistance);
            bestAD = std::min(bestAD, route.adminDistance);
        }

        // Step 2: Determin successors and feasible successors
        for (auto& [neighbor, route] : entry->routesByNeighbor)
        {
            // Feasibility Condition: Reported Distance <= Best Feasible Distance
            route.isFeasibleSuccessor = (route.reportedDistance <= bestFD);
            if (route.isFeasibleSuccessor)
            {
                entry->feasibleSuccessors.push_back(neighbor);
            }

            // Successor Condition: FD within variance and AD matches best AD
            bool withinVariance = (route.feasibleDistance <= bestFD * eigrpProcess->getConfigs()->variance);
            if (withinVariance && route.adminDistance == bestAD)
            {
                route.isSuccessor = true;
                entry->successors.push_back(neighbor);
            }
            else
            {
                route.isSuccessor = false;
            }
        }

        // Step 3: Apply Traffic-Share mode
        if (eigrpProcess->getConfigs()->trafficShareMode == EigrpConfigs::TrafficShareMode::Minimum)
        {
            // Only keep the route with the lowest FD
            if (!entry->successors.empty())
            {
                const ByteString& bestNeighbor = entry->successors.front();
                entry->successors = {bestNeighbor}; // Keep only the best
            }
        }

        // Logger::getInstance().debug() << "Updated successors and feasible successors for destination: " << entry->destination.toHex() << std::endl;
        // Logger::getInstance().debug() << "Successors: " << entry->successors.size() << ", Feasible Successors: " << entry->feasibleSuccessors.size() << std::endl;
        // Logger::getInstance().debug() << "With the best successor at: " << entry->successors.front().toHex() << " with a feasible distance of: " << entry->bestFD << std::endl;
    }

    void TopologyTable::removeRoutesFromNeighbor(const ByteString neighborIp)
    {
        std::lock_guard<std::mutex> lock(tableMutex);
        for (auto it = topologyEntries.begin(); it != topologyEntries.end();)
        {
            it->second->routesByNeighbor.erase(neighborIp);
            if (it->second->routesByNeighbor.empty())
            {
                it = topologyEntries.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    std::shared_ptr<TopologyTable::TopologyEntry> TopologyTable::getEntryForRoute(const ByteString& destination)
    {
        std::lock_guard<std::mutex> lock(tableMutex);

        auto it = topologyEntries.find(destination);
        if (it == topologyEntries.end())
        {
            Logger::getInstance().warn() << "No entry found for destination: " << destination.toHex() << std::endl;
            return nullptr;
        }

        return it->second;
    }

    std::optional<TopologyTable::RouteInfo> TopologyTable::findBestRoute(const ByteString &destination, int variance)
    {
        std::lock_guard<std::mutex> lock(tableMutex);

        auto it = topologyEntries.find(destination);
        if (it == topologyEntries.end())
        {
            Logger::getInstance().warn() << "No topology entry for destination: " << destination.toHex() << std::endl;
            return std::nullopt;
        }

        const auto& entry = it->second;

        if (entry->successors.empty())
        {
            Logger::getInstance().info() << "No successors available for destination: " << destination.toHex() << std::endl;
            return std::nullopt;
        }

        // Return the route information for the best successor
        const ByteString& bestNeighbor = entry->successors.front();
        return entry->routesByNeighbor.at(bestNeighbor);
    }

    void TopologyTable::handleRouteFailure(const ByteString &destination, const ByteString& failedNeighborIp)
    {
        std::lock_guard<std::mutex> lock(tableMutex);

        auto it = topologyEntries.find(destination);
        if (it == topologyEntries.end())
        {
            Logger::getInstance().warn() << "No topology entry found for failed route to: " << destination.toHex();
            return;
        }

        auto& entry = it->second;

        // Remove the failed neighbor's route
        entry->routesByNeighbor.erase(failedNeighborIp);

        // Recalculate successors and feasible successors
        updateSuccessorAndFeasibleSuccessors(entry);

        if (entry->routesByNeighbor.empty())
        {
            Logger::getInstance().info() << "No remaining routes for destination: " << destination.toHex() << std::endl;
            topologyEntries.erase(it);
        }
        else
        {
            Logger::getInstance().debug() << "Handled route failure for destination: " << destination.toHex() << std::endl;
        }
    }

    void TopologyTable::markRouteAsPassive(const ByteString &destination, EigrpInterface *eigrp)
    {
        std::lock_guard<std::mutex> lock(tableMutex);
        auto it = topologyEntries.find(destination);
        if (it != topologyEntries.end())            // Default constructor
        {
            auto entry = it->second;
            entry->isActive = false;

            // Cancel Active timer if running
            if (entry->activeTimerId != 0)
            {
                TimeManager::getInstance().cancelTimer(entry->activeTimerId);
                entry->activeTimerId = 0;
            }
        }
    }

    void TopologyTable::removeEntry(const ByteString &destination)
    {
        std::lock_guard<std::mutex> lock(tableMutex);
        topologyEntries.erase(destination);
    }

    void TopologyTable::pruneStaleRoutes()
    {
        std::lock_guard<std::mutex> lock(tableMutex);

        auto now = std::chrono::steady_clock::now();
        for (auto it = topologyEntries.begin(); it != topologyEntries.end();)
        {
            auto entry = it->second;

            // Iterate through all neighbors for this destination
            for (auto neighborIt = entry->routesByNeighbor.begin(); neighborIt != entry->routesByNeighbor.end();)
            {
                auto age = std::chrono::duration_cast<std::chrono::seconds>(now - neighborIt->second.lastUpdate).count();
                if (age > staleThreshold) // Check against the stale threshold
                {
                    Logger::getInstance().info() << "Pruning stale route to destination: " << entry->destination.toHex()
                                                 << " learned from neighbor: " << neighborIt->first.toHex()
                                                 << " (Age: " << age << " seconds)" << std::endl;
                    neighborIt = entry->routesByNeighbor.erase(neighborIt); // Remove stale route
                }
                else
                {
                    ++neighborIt;
                }
            }

            // Remove the entry if no neighbors remain
            if (entry->routesByNeighbor.empty())
            {
                Logger::getInstance().info() << "Removing topology entry for destination: " << entry->destination.toHex() << " (no valid routes)" << std::endl;
                it = topologyEntries.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    
    void TopologyTable::handleNeighborDown(const ByteString neighborIp)
    {
        std::lock_guard<std::mutex> lock(tableMutex);
    
        for (auto &entry : topologyEntries)
        {
            entry.second->routesByNeighbor.erase(neighborIp); // Remove routes from this neighbor
        }

        pruneStaleRoutes(); // Remove any empty destinations
    }
}

// Global function to update active EIGRP processes for current interface
void updateEigrpInterface(Interface *interface)
{
    std::lock_guard<std::shared_mutex> lock(globalEigrpMutex);
    for (auto& instance : eigrpList)
    {
        if (instance.second)
        {
            for (auto as : instance.second->autonomousSystems)
            {
                if (as.second->addressFamilies[AddressFamily::IPv4] && !as.second->addressFamilies[AddressFamily::IPv4]->getConfigs()->networks.empty() && !as.second->addressFamilies[AddressFamily::IPv4]->testAddress(interface->Get()->ipv4.ipAddress))
                {
                    interface->eigrpInterfaceList.erase(as.second->addressFamilies[AddressFamily::IPv4]->getAsNumber());
                    as.second->addressFamilies[AddressFamily::IPv4]->eigrpInterfaceList.erase(interface->Get()->id);
                }
            }
        }
    }
}

EigrpConfigs::CommunicationMode* currentCommunicationMode = new EigrpConfigs::CommunicationMode;
std::weak_ptr<Protocol::Eigrp> currentEigrp;
std::weak_ptr<Protocol::EigrpInstance> currentEigrpInstance;
std::map<ByteString, std::shared_ptr<Protocol::EigrpInstance>> eigrpList;
std::map<int, std::weak_ptr<Protocol::EigrpAutonomousSystems>> eigrpAutonomousSystems;


#pragma endregion
