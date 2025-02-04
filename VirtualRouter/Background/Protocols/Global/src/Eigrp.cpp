#include <Eigrp.h>
#include <Encapsulation.h>
#include <Interface.h>
#include <algorithm>

#pragma region Eigrp

// Global mutex for EIGRP operations
std::shared_mutex globalEigrpMutex;

namespace Protocol
{
    Eigrp::Eigrp(uint32_t& as, AddressFamily af) : addressFamily(af), asNumber(as)
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

        topologyTable = new TopologyTable(this);

        updateInterfaceList();
        updateRoutingTableForConnected();

        Logger::getInstance().info() << "EIGRP process initiated." << std::endl;
    }

    void Eigrp::eigrpHello(EigrpHeader& eigrp, EigrpInterface* eigrpInt, EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, uint32_t sequenceNumber,  bool ack, bool update)
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
            paramTLV.value = calculateParameters(eigrpInt->getConfigs().holdTime);
            paramTLV.length = Functions::numToByte(static_cast<uint32_t>(paramTLV.value.size()) + 4, 2);
            eigrp.options.emplace_back(paramTLV);

            // Version TLV
            EigrpHeader::Option versionTLV;
            versionTLV.option = Variable::Eigrp::Option::version;
            versionTLV.value = Variable::Eigrp::Version::release + Variable::Eigrp::Version::tls;
            versionTLV.length = Functions::numToByte(static_cast<uint32_t>(versionTLV.value.size()) + 4, 2);
            eigrp.options.emplace_back(versionTLV);

            // Sequence TLV
            if (update)
            {
                EigrpHeader::Option sequenceTLV;
                sequenceTLV.option = Variable::Eigrp::Option::sequence;
                sequenceTLV.value = Functions::numToByte(static_cast<uint32_t>(neighborIp.size()), 1) + neighborIp;
                sequenceTLV.length = Functions::numToByte(static_cast<uint32_t>(sequenceTLV.value.size()) + 4, 2);
                eigrp.options.emplace_back(sequenceTLV);

                EigrpHeader::Option multicastSeqTLV;
                multicastSeqTLV.option = Variable::Eigrp::Option::multicastSequence;
                multicastSeqTLV.value = Functions::numToByte(sequenceNumber, 4);
                multicastSeqTLV.length = Functions::numToByte(static_cast<uint32_t>(multicastSeqTLV.value.size()) + 4, 2);
                eigrp.options.emplace_back(multicastSeqTLV);
            }
        }

        // Create other TLVs if applicable.
        if (isStub())
        {
            EigrpHeader::Option stubTLV{
                .option = Variable::Eigrp::Option::stub,
                .length = ByteString("\x02", 1),
                .value = eigrpInt->encodeStubOption(configs.stubConfig)
            };
            if (stubTLV.option != ByteString(1, 0x00))
            {
                eigrp.options.emplace_back(stubTLV);
            }
        }
        if (neighbor)
        {
            if (neighbor->authenticationEnabled)
            {
                EigrpHeader::Option authTLV = eigrpInt->generateAuthenticatedTLV(eigrp, neighbor);
                if (authTLV.option != ByteString(1, 0x00))
                {
                    eigrp.options.emplace_back(authTLV);
                }
            }
        }
    }

    void Eigrp::eigrpUpdate(EigrpHeader &eigrp, uint32_t sequenceNum, bool init, bool conditional, bool restart, bool endoftable, bool query, bool reply)
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
        if (testIp.empty() || testIp.size() != 4)
        {
            return false;
        }

        for (const auto &network : configs.networks)
        {
            if (Functions::compareNetworkWithIp(network.ip, testIp, 32 - Functions::byteMaskToNum(network.mask)))
            {
                return true;
            }
        }

        return false; // No matches found
    }

    uint32_t Eigrp::calculateMetric(uint32_t bandwidth, uint8_t load, uint32_t delay, uint8_t reliability, uint8_t hopCount)
    {
        if (bandwidth == 0) return std::numeric_limits<uint32_t>::max();

        // Calculate individual components of the metric
        uint32_t bandwidthMetric = configs.wideMetric / bandwidth;
        uint32_t delayMetric = delay / 10;
        uint32_t loadMetric = (configs.kvalue.k2_Load * bandwidthMetric) / (256 - load);

        uint32_t reliabilityFactor = (reliability + configs.kvalue.k4_Reliability);
        uint32_t compositeMetric = (configs.kvalue.k1_Bandwidth * bandwidthMetric) +
                                 loadMetric +
                                 (configs.kvalue.k3_Delay * delayMetric);

        // Account for K5 (optional scaling)
        if (configs.kvalue.k5_MTU != 0 && reliabilityFactor > 0) {
            compositeMetric *= configs.kvalue.k5_MTU / reliabilityFactor;
        }

        // Final scaling for the metric
        return static_cast<uint32_t>(std::min(static_cast<double>(compositeMetric) * 256.0, 4294967295.0)); // Max metric value
    }

    std::optional<EigrpInterface*> Eigrp::addEigrpInterface(Interface* interface, bool unicast)
    {
        IpInfo* interfaceInfo = interface->Get();
        unsigned char intID;
        ByteString ipv6Address;
        ByteString ipv4Address;
        EigrpInterface* instance;
        EigrpInterfaceInstance* interfaceInstance;
        {
            std::shared_lock<std::shared_mutex> lock(interfaceInfo->ipMutex);
            intID = interfaceInfo->id;
            ipv4Address = interfaceInfo->ipv4.ipAddress;
            ipv6Address = interfaceInfo->ipv6.ipAddress;
        }

        if (interface->eigrpInterfaceList.find(asNumber) == interface->eigrpInterfaceList.end() || (!interface->eigrpInterfaceList.find(asNumber)->second))
        {
            interfaceInstance = new EigrpInterfaceInstance();
            interface->eigrpInterfaceList[asNumber] = interfaceInstance;
        }
        else
        {
            interfaceInstance = interface->eigrpInterfaceList[asNumber];
        }
        {
            if (getAddressFamily() == AddressFamily::IPv4 && !ipv4Address.empty())
            {
                instance = new EigrpInterface(*this, interface);
                if (unicast)
                {
                    instance->getConfigs().mode = EigrpConfigs::CommunicationMode::UNICAST;
                }
                interfaceInstance->IPv4 = instance;
                eigrpInterfaceList[intID] = instance;
                return instance;
            }
            else if (getAddressFamily() == AddressFamily::IPv6 && !ipv6Address.empty())
            {
                instance = new EigrpInterface(*this, interface);
                if (unicast)
                {
                    instance->getConfigs().mode = EigrpConfigs::CommunicationMode::UNICAST;
                }
                interfaceInstance->IPv6 = instance;
                eigrpInterfaceList[intID] = instance;
                return instance;
            }
        }
        return std::nullopt;
    }

    void Eigrp::updateInterfaceList()
    {
        std::unique_lock<std::shared_mutex> lock(globalEigrpMutex);

        // Validate existing interfaces
        for (auto& [id, eigrpInterface] : eigrpInterfaceList)
        {
            if (!eigrpInterface->currentInterface)
            {
                delete eigrpInterfaceList[id];
                eigrpInterfaceList[id] = nullptr;
                eigrpInterfaceList.erase(id);
            }
        }

        // Iterate through all interfaces
        if (getAddressFamily() != AddressFamily::IPv4) return;

        for (const auto& outer : interfaceList)
        {
            for (const auto& interface : outer.second)
            {
                // Add the interface as existing
                if (!interface.second || interface.second->Get())
                {
                    auto ipInfo = interface.second->Get();
                    std::unique_lock<std::shared_mutex> ipLock(ipInfo->ipMutex);
                    if (testAddress(ipInfo->ipv4.ipAddress))
                    {
                        uint8_t intID = ipInfo->id;
                        // Add interface to eigrp
                        if (interface.second->eigrpInterfaceList.find(asNumber) == interface.second->eigrpInterfaceList.end() || 
                            eigrpInterfaceList.find(intID) == eigrpInterfaceList.end())
                        {
                            ipLock.unlock();
                            if (!addEigrpInterface(interface.second).has_value())
                            {
                                ipLock.lock();
                                continue;
                            }
                            ipLock.lock();

                            if (configs.autoSummarizationEnabled)
                            {
                                auto majorNetwork = Functions::findClassfullNetwork(ipInfo->ipv4.ipAddress);
                                uint8_t defaultMask = Functions::getDefaultMask(majorNetwork);

                                // Only summarize if the interface is in a different major network
                                if (!isRouteSummarized(majorNetwork, defaultMask))
                                {
                                    addSummaryRoute(majorNetwork, defaultMask, true);

                                }
                            }
                        }
                    }
                    else
                    {
                        // Check and remove interface from eigrp
                        if (eigrpInterfaceList.find(ipInfo->id) != eigrpInterfaceList.end())
                        {
                            ipLock.unlock();
                            delete eigrpInterfaceList[ipInfo->id];
                            eigrpInterfaceList[ipInfo->id] = nullptr;
                            eigrpInterfaceList.erase(ipInfo->id);
                            ipLock.lock();
                        }
                    }
                }
                else
                {
                    // Remove shutdown interface
                    if (eigrpInterfaceList.find(static_cast<unsigned char>(interface.first)) != eigrpInterfaceList.end())
                    {
                        delete eigrpInterfaceList[static_cast<unsigned char>(interface.first)];
                        eigrpInterfaceList[interface.second->configs.id] = nullptr;
                        eigrpInterfaceList.erase(interface.second->configs.id);
                    }
                }
            }
        }
    }

    ByteString Eigrp::calculateParameters(uint16_t holdTime)
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

    void Eigrp::updateRoutingTableForConnected(EigrpInterface* eigrpInterface)
    {

        std::lock_guard<std::shared_mutex> lock(eigrpMutex);
        RoutingTable &routingTable = RoutingTable::getInstance();
        std::vector<RoutingTable::Eigrp*> updatedRoutes{};
        std::vector<RoutingTable::Eigrp*> removedRoutes{};
        std::vector<ByteString> connectedNetworks{};
        auto currentAddressFamily = getAddressFamily();
        
        std::unordered_map<uint8_t, EigrpInterface*> currentEigrpInterface;
        // get Eigrp Interface list
        if (eigrpInterface)
        {
            auto ipInfo = eigrpInterface->currentInterface->Get();
            if (ipInfo)
            {
                std::shared_lock<std::shared_mutex> ipLock(ipInfo->ipMutex);
                currentEigrpInterface[ipInfo->id] = eigrpInterface;
            }
        }

        for (const auto& [id, eigrpInterfacePtr] : eigrpInterface ? currentEigrpInterface : eigrpInterfaceList)
        {
            auto interfaceInfo = eigrpInterfacePtr->currentInterface->Get();

            if (interfaceInfo)
            {
                uint32_t eigrpBw;
                uint8_t connectedMask;
                uint32_t delay;
                ByteString connectedNetwork;

                {
                    std::shared_lock<std::shared_mutex> ipLock(interfaceInfo->ipMutex);
                    connectedNetwork = Functions::computeNetworkAddress(
                        (currentAddressFamily == AddressFamily::IPv4) ? interfaceInfo->ipv4.ipAddress : interfaceInfo->ipv6.ipAddress, 
                        (currentAddressFamily == AddressFamily::IPv4) ? interfaceInfo->ipv4.mask : interfaceInfo->ipv6.mask
                    );
                    eigrpBw = interfaceInfo->bandwidth;
                    delay = interfaceInfo->delay;
                    connectedMask = currentAddressFamily == AddressFamily::IPv4 ? interfaceInfo->ipv4.mask : interfaceInfo->ipv6.mask;
                }

                // Check if the connected route is covered by a summary
                bool isSummarized = false;
                for (const auto& summaryRoute : configs.summaryRoutes) {
                    if (Functions::isSubnetOf(connectedNetwork, connectedMask, summaryRoute.summary->network, summaryRoute.summary->mask)) {
                        isSummarized = true;
                        break;
                    }
                }

                if (isSummarized) {
                    auto summarizedRoute = RoutingTable::getInstance().getEigrpRoute(connectedNetwork, connectedMask, getAddressFamily(), asNumber);
                    if (summarizedRoute.has_value())
                    {
                        removedRoutes.emplace_back(summarizedRoute.value());
                        routingTable.removeEigrp(connectedNetwork, connectedMask, currentAddressFamily, asNumber);
                    }
                    continue;
                }

                // Compute the connected network
                connectedNetworks.emplace_back(connectedNetwork);

                // Create EIGRP route entry
                RoutingTable::Eigrp* connectedRoute = new RoutingTable::Eigrp();
                    connectedRoute->bandwidth = ( 10000000 / eigrpBw ) * 256;
                    connectedRoute->delay = (delay / 10) * 256;
                    connectedRoute->hopCount = 0;
                    connectedRoute->mtu = interfaceInfo->mtu;
                    connectedRoute->reliability = 255;
                    connectedRoute->load = configs.variance;
                    connectedRoute->network = connectedNetwork;
                    connectedRoute->mask = connectedMask;
                    connectedRoute->nextHop = ByteString(connectedNetwork.size(), '\x00'); // indicates directly connected
                    connectedRoute->metric = calculateMetric(eigrpBw, 0, delay, 255);
                    connectedRoute->routeType = "connected";

                auto existingRoute = RoutingTable::getInstance().getEigrpRoute(connectedNetwork, connectedRoute->mask, getAddressFamily(), asNumber);

                bool hasChanged = !existingRoute.has_value() ||
                                  existingRoute.value()->feasibleDistance != connectedRoute->feasibleDistance ||
                                  existingRoute.value()->mask != connectedRoute->mask;

                if (hasChanged)
                {
                    // Insert into Routing Table
                    routingTable.addEigrp(connectedRoute, currentAddressFamily, asNumber);
                    
                    // Advertise the connected route to eigrp neighbors
                    updatedRoutes.emplace_back(connectedRoute);
                }
            }
            else
            {
                std::shared_lock<std::shared_mutex> ipLock(eigrpInterfacePtr->currentInterface->configs.ipMutex);
                auto removalRoute = RoutingTable::getInstance().getEigrpRoute(
                    getAddressFamily() == AddressFamily::IPv4 ? eigrpInterfacePtr->currentInterface->configs.ipv4.ipAddress
                    : eigrpInterfacePtr->currentInterface->configs.ipv6.ipAddress, 
                    getAddressFamily() == AddressFamily::IPv6 ? eigrpInterfacePtr->currentInterface->configs.ipv4.mask
                    : eigrpInterfacePtr->currentInterface->configs.ipv6.mask, getAddressFamily(), asNumber);
                if (removalRoute.has_value())
                {
                    RoutingTable::getInstance().removeEigrp(
                        getAddressFamily() == AddressFamily::IPv4 ? eigrpInterfacePtr->currentInterface->configs.ipv4.ipAddress
                        : eigrpInterfacePtr->currentInterface->configs.ipv6.ipAddress, 
                        getAddressFamily() == AddressFamily::IPv6 ? eigrpInterfacePtr->currentInterface->configs.ipv4.mask
                        : eigrpInterfacePtr->currentInterface->configs.ipv6.mask, getAddressFamily(), asNumber);
                    removedRoutes.emplace_back(removalRoute.value());
                }
            }
        }

        // Remove any routes that are no longer exist
        for (const auto& route : RoutingTable::getInstance().getAllEigrpRoutes(currentAddressFamily, asNumber))
        {
            if (route->routeType == "connected")
            {
                auto networkIt = std::find(connectedNetworks.begin(), connectedNetworks.end(), route->network);
                if (networkIt == connectedNetworks.end())
                {
                    removedRoutes.emplace_back(route);
                    RoutingTable::getInstance().removeEigrp(route->network, route->mask, currentAddressFamily, asNumber);
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

    void Eigrp::notifyRoutingChange(const std::vector<RoutingTable::Eigrp*>& changedRoutes, bool isRemoval, bool init)
    {
        Logger::getInstance().info() << "Notifying all neighbors for route changes" << std::endl;
        std::scoped_lock lock(globalEigrpMutex);

        // Adjust summaries based on added/removed routes
        for (const auto &[_, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            std::vector<ByteString> unicastNeighbors;
            bool hasMulticast = false;
            if (!changedRoutes.empty())
            {
                {
                    // Check all neighbors for unicast and multicast
                    std::unique_lock<std::shared_mutex> neighborLock(eigrpInterfacePtr->neighborMutex);
                    for (const auto& [address, neighbor] : eigrpInterfacePtr->neighbors)
                    {
                        auto& mode = eigrpInterfacePtr->getConfigs().mode;
                        if (mode == EigrpConfigs::CommunicationMode::UNICAST)
                        {
                            unicastNeighbors.emplace_back(address);
                        }
                        else if (mode == EigrpConfigs::CommunicationMode::MULTICAST)
                        {
                            hasMulticast = true;
                        }
                    }
                }

                for (const auto& address : unicastNeighbors)
                {
                    if (init)
                    {
                        eigrpInterfacePtr->sendUpdateToNeighbor(eigrpInterfacePtr->neighbors[address], changedRoutes, EigrpConfigs::UpdateType::PARTIAL);
                    }
                    else
                    {
                        EigrpConfigs::UpdateType updateType = isRemoval ? EigrpConfigs::UpdateType::WITHDRAW : EigrpConfigs::UpdateType::PARTIAL;
                        eigrpInterfacePtr->sendUpdateToNeighbor(eigrpInterfacePtr->neighbors[address], changedRoutes, updateType);
                    }
                }
                if (hasMulticast)
                {
                    if (init)
                    {
                        eigrpInterfacePtr->sendUpdateToNeighbor(nullptr, changedRoutes, EigrpConfigs::UpdateType::PARTIAL);
                    }
                    else 
                    {
                        EigrpConfigs::UpdateType updateType = isRemoval ? EigrpConfigs::UpdateType::WITHDRAW : EigrpConfigs::UpdateType::PARTIAL;
                        eigrpInterfacePtr->sendUpdateToNeighbor(nullptr, changedRoutes, updateType);
                    }
                }
            }
        }
    }

    void Eigrp::shutdown()
    {
        for (auto it = eigrpInterfaceList.begin(); it != eigrpInterfaceList.end();)
        {
            // Send termination message
            delete it->second;
            it->second = nullptr;
            it = eigrpInterfaceList.erase(it);
        }
        eigrpInterfaceList.clear();
        if (topologyTable)
        {
            delete topologyTable; // Clear the topology table
            topologyTable = nullptr;
        }
        Logger::getInstance().info() << "EIGRP shutdown complete." << std::endl;
    }

    void Eigrp::redistributeRoute(const ByteString &destination, uint8_t mask, const ByteString &protocol)
    {
        RoutingTable& routingTable = RoutingTable::getInstance();
        auto route = routingTable.getEigrpRoute(destination, mask, getAddressFamily(), asNumber);

        if (route.has_value())
        {
            // Convert route to external EIGRP and notify neighbors
            RoutingTable::Eigrp* externalRoute = route.value();
            externalRoute->routeType = "external";
            externalRoute->metric += configs.redistributionMetricOffset;

            routingTable.addEigrp(externalRoute, getAddressFamily(), asNumber);
            notifyRoutingChange({externalRoute}, false);
        }
    }

    void Eigrp::addNetwork(const EigrpConfigs::Network& newNetwork)
    {
        if (getAddressFamily() != AddressFamily::IPv4) return;

        // Check for duplicate
        for (auto network : configs.networks)
        {
            if (network.ip == newNetwork.ip && network.mask == newNetwork.mask)
            {
                return; // Network already exists
            }
        }
        configs.networks.emplace_back(newNetwork);

        // Update interfaces and routing table after adding the network
        updateInterfaceList();
        updateRoutingTableForConnected();
    }

    void Eigrp::addSummaryRoute(const ByteString& network, uint8_t mask, uint8_t adminDistance, bool isAuto)
    {
        if (mask > network.size() * 8) return; // Mask invalid

        std::shared_lock<std::shared_mutex> lock(eigrpMutex);

        // Validate network and mask
        if (!Functions::compareNetworkWithMask(network, mask)) return;

        // Check for overlapping summary routes
        if (isRouteSummarized(network, mask)) return;

        // Inject the summary route into the routing table as an internal summary route
        RoutingTable::Eigrp* internalSummaryRoute = new RoutingTable::Eigrp();
        internalSummaryRoute->network = network;
        internalSummaryRoute->mask = mask;
        internalSummaryRoute->nextHop = ByteString(network.size(), '\x00');
        internalSummaryRoute->metric = configs.summaryMetric;
        internalSummaryRoute->adminDistance = adminDistance;
        internalSummaryRoute->routeType = "summary";

        // Add new summary route
        EigrpConfigs::SummaryRoute summaryRoute;
        summaryRoute.summary = internalSummaryRoute;
        summaryRoute.isAuto = isAuto;
        getConfigs()->summaryRoutes.emplace_back(summaryRoute);

        // Update interfaces to advertise the new summary route
        if (internalSummaryRoute->metric == 0)
        {
            updateSummaryRouteMetrics(internalSummaryRoute);
        }
        updateInterfacesWithSummaryRoute(summaryRoute);

        // Other routes will be removed in here
        RoutingTable::getInstance().addEigrp(internalSummaryRoute, getAddressFamily(), asNumber);
    }

    void Eigrp::removeSummaryRoute(const ByteString& network, uint8_t mask)
    {
        std::shared_lock<std::shared_mutex> lock(eigrpMutex); // Esures thread safety
        
        auto it = std::remove_if(configs.summaryRoutes.begin(), configs.summaryRoutes.end(),
            [&](const EigrpConfigs::SummaryRoute& sr) {
                return sr.summary->network == network && sr.summary->mask == mask;
            });
        if (it != configs.summaryRoutes.end())
        {
            configs.summaryRoutes.erase(it);

            // Remove from routing table
            RoutingTable::getInstance().removeEigrp(network, mask, getAddressFamily(), asNumber);

            // Withdraw summary route from all neighbors
            updateInterfacesAfterRemovingSummaryRoute(network, mask);
        }
    }

    void Eigrp::updateSummaryRouteMetrics(RoutingTable::Eigrp* summaryRoute)
    {
        auto updateSummaryRoute = [&](RoutingTable::Eigrp* sr)
        {
            uint32_t lowestMetric = std::numeric_limits<uint32_t>::max();
            for (auto* routeInfo : RoutingTable::getInstance().getAllEigrpRoutes(getAddressFamily(), asNumber))
            {
                if (routeInfo && routeInfo->metric != 0 && routeInfo->metric < lowestMetric)
                {
                    lowestMetric = routeInfo->metric;
                }
                sr->metric = lowestMetric;
            }
        };

        if (summaryRoute)
        {
            updateSummaryRoute(summaryRoute);
        }
        else
        {
            for (auto& summary : configs.summaryRoutes)
            {
                updateSummaryRoute(summary.summary);
            }
        }
    }

    void Eigrp::updateInterfacesWithSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute)
    {
        for (auto& [id, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            eigrpInterfacePtr->advertiseSummaryRoute(summaryRoute);
        }
    }

    void Eigrp::updateInterfacesAfterRemovingSummaryRoute(const ByteString& network, uint8_t mask)
    {
        for (auto& [id, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            eigrpInterfacePtr->withdrawSummaryRoute(network, mask);
        }
    }
    
    bool Eigrp::isRouteSummarized(const ByteString& network, uint8_t mask)
    {
        for (const auto& sr : configs.summaryRoutes)
        {
            if (Functions::isSubnetOf(network, mask, sr.summary->network, sr.summary->mask))
            {
                return true;
            }
        }
        return false;
    }

    void Eigrp::enableAutoSummary(bool enable)
    {
        if (addressFamily != AddressFamily::IPv4) return; // Only supported for IPv4

        std::shared_lock<std::shared_mutex> lock(eigrpMutex);

        if (enable == configs.autoSummarizationEnabled)
        {
            return; // No change
        }

        configs.autoSummarizationEnabled = enable;
        
        if (enable)
        {
            // Add summary routes for all calssfull networks
            std::vector<RoutingTable::Eigrp*> removedRoutes;
            std::unordered_map<ByteString, uint8_t> summaryCanidates; // Stores summarized canidates

            // Process all existing EIGRP routes and group by classical networks
            for (const auto& route : RoutingTable::getInstance().getAllConnectedEigrpRoutes(getAddressFamily(), asNumber))
            {
                ByteString majorNetwork = Functions::findClassfullNetwork(route->network);
                uint8_t defaultMask = Functions::getDefaultMask(majorNetwork);

                // Add the classfull summary route if multiple subnets exist in the range
                if (summaryCanidates.find(majorNetwork) == summaryCanidates.end())
                {
                    summaryCanidates[majorNetwork] = defaultMask;
                }
            }

            // Add the summarized routes to the routing table
            for (const auto& [summaryNet, mask] : summaryCanidates)
            {
                addSummaryRoute(summaryNet, mask, true);
            }
        }
        else 
        {
            // Remove all summary Routes
            for (auto it = configs.summaryRoutes.begin(); it != configs.summaryRoutes.end();)
            {
                if (it->isAuto)
                {
                    removeSummaryRoute(it->summary->network, it->summary->mask);
                }
                else
                {
                    ++it;
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

    uint32_t Eigrp::getLowestBandwidth()
    {
        uint32_t lowestBW = std::numeric_limits<uint32_t>::max();
        for (const auto& [id, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            std::shared_lock<std::shared_mutex> lock(eigrpInterfacePtr->currentInterfaceInfo->ipMutex);
            if (eigrpInterfacePtr->currentInterfaceInfo->bandwidth < lowestBW)
            {
                lowestBW = eigrpInterfacePtr->currentInterfaceInfo->bandwidth;
            }
        }
        return lowestBW;
    }

    void Eigrp::addDefaultRoute()
    {
        if (configs.advertiseDefault)
            return;

        uint32_t lowestBW = getLowestBandwidth();
        if (lowestBW == std::numeric_limits<uint32_t>::max()) {
            Logger::getInstance().error() << "No active interfaces available to advertise the default route." << std::endl;
            return;
        }

        EigrpInterface* selectedInterface;
        for (const auto& [id, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            if (eigrpInterfacePtr->currentInterfaceInfo->bandwidth == lowestBW)
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
        uint32_t metric = calculateMetric(selectedInterface->currentInterfaceInfo->bandwidth, selectedInterface->getConfigs().load, selectedInterface->currentInterfaceInfo->delay, 255, 0);

        RoutingTable::Eigrp* defaultRoute = new RoutingTable::Eigrp();
        defaultRoute->network = configs.defaultNetwork;
        defaultRoute->nextHop = ByteString(defaultRoute->network.size(), '\x00');
        defaultRoute->mask = configs.defaultMask;
        defaultRoute->metric = metric;
        defaultRoute->routeType = "default";

        // Add to routing table
        RoutingTable::getInstance().addEigrp(defaultRoute, getAddressFamily(), asNumber);

        // Notify neighbors about the new default route
        notifyRoutingChange({defaultRoute}, /*isRemoval=*/false);

        configs.advertiseDefault = true;

        Logger::getInstance().info() << "Default route (" << defaultRoute->network.toHex() << "/" << defaultRoute->mask << ") added with metric " << defaultRoute->metric << std::endl;
    }

    void Eigrp::removeDefaultRoute()
    {
        if (!configs.advertiseDefault)
            return;
        
        auto defaultRoute = RoutingTable::getInstance().getEigrpRoute(configs.defaultNetwork, configs.defaultMask, getAddressFamily(), asNumber);
        if (defaultRoute.has_value() && defaultRoute.value())
        {
            RoutingTable::getInstance().removeEigrp(configs.defaultNetwork, configs.defaultMask, getAddressFamily(), asNumber);
            notifyRoutingChange({defaultRoute.value()}, /*isRemoval=*/true);
        }
        configs.advertiseDefault = false;
    }

    void Eigrp::setVariance(uint8_t var)
    {
        if ( var == 0 ) return; // Invalid variance
        for (auto routeInfo : topologyTable->getTopologyEntries())
        {
            topologyTable->updateSuccessorAndFeasibleSuccessors(routeInfo.second);
        }
        std::lock_guard<std::shared_mutex> lock(eigrpMutex);
        configs.variance = var;
    }

    void Eigrp::recalculateRoutes()
    {
        if (configs.variance == 0) return; // Invalid variance
        // Iterate through the topology table and update routing table based on new Variance
        for (auto& [destination, entry] : topologyTable->getTopologyEntries())
        {
            // Find all feasible successors within the Variance
            for (const auto& [neighbor, routeInfo] : entry->routesByNeighbor)
            {
                if (routeInfo.feasibleDistance <= entry->bestFD && routeInfo.feasibleDistance <= entry->bestFD * configs.variance && routeInfo.reportedDistance < entry->bestFD);
                {
                    // Add or update route in the routing table
                    RoutingTable::Eigrp* newRoute = new RoutingTable::Eigrp();
                    newRoute->network = destination;
                    newRoute->mask = entry->prefixLength;
                    newRoute->nextHop = neighbor;
                    newRoute->metric = routeInfo.feasibleDistance;
                    newRoute->routeType = "internal";

                    RoutingTable::getInstance().addEigrp(newRoute, getAddressFamily(), asNumber);
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
            std::shared_lock<std::shared_mutex> eigprInterfaceLock(eigrpInterfacePtr->neighborMutex);
            for (const auto& [address, neighborInfo] : eigrpInterfacePtr->neighbors)
            {
                eigrpInterfacePtr->sendHelloPacket(neighborInfo); // Send restart flag
            }
        }

        Logger::getInstance().info() << "EIGRP Graceful Restart initialted." << std::endl;
    }

    void Eigrp::restart()
    {
        // Shutdown current state
        shutdown();

        // Reinitialize the EIGRP process
        initializeEigrp();

        Logger::getInstance().info() << "EIGRP process restarted successfully." << std::endl;
    }

    void Eigrp::cleanup()
    {
        std::lock_guard<std::shared_mutex> lock(eigrpMutex);
        for (auto& [id, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            eigrpInterfacePtr->stopHello();
        }
        eigrpInterfaceList.clear();
        configs.networks.clear();
        configs.summaryRoutes.clear();
        Logger::getInstance().info() << "EIGRP Cleanup complete" << std::endl;
    }

    void Eigrp::periodicMaintenance()
    {
        topologyTable->pruneStaleRoutes();
    }

    void Eigrp::calculateRouterID()
    {
        uint32_t highestNum = 0;
        ByteString highestIP{};
        std::lock_guard<std::shared_mutex> lock(eigrpDataMutex);
        if (!routerID.isStatic)
        {
            std::shared_lock<std::shared_mutex> interfaceLock(interfaceListMutex);
            for (const auto& [id, interface] : interfaceList[InterfaceType::LOOPBACK])
            {
                auto interfaceInfo = interface->Get();
                if (!interfaceInfo) continue;
                std::shared_lock<std::shared_mutex> ipLock(interfaceInfo->ipMutex);
                ByteString address = interfaceInfo->ipv4.ipAddress;
                if (address.empty()) continue;
                uint32_t ipNum = Functions::byteToNum(address);
                if (ipNum < highestNum) continue;
                highestNum = ipNum;
                highestIP = address;
            }
            if (highestIP.empty())
            {
                for (const auto& subInterfaceList : interfaceList)
                {
                    // Skip Loopbacks
                    if (subInterfaceList.first == InterfaceType::LOOPBACK) continue;

                    for (const auto& [id, interface] : subInterfaceList.second)
                    {
                        auto interfaceInfo = interface->Get();
                        if (!interfaceInfo) continue;
                        std::shared_lock<std::shared_mutex> ipLock(interfaceInfo->ipMutex);
                        ByteString address = interfaceInfo->ipv4.ipAddress;
                        uint32_t ipNum = Functions::byteToNum(address);
                        if (ipNum < highestNum) continue;
                        highestNum = ipNum;
                        highestIP = address;
                    }
                }
            }
            if (highestIP.empty())
            {
                // Look for lowest mac to make it fair
                highestNum = UINT32_MAX;
                for (const auto& subInterfaceList : interfaceList)
                {
                    for (const auto& [id, interface] : subInterfaceList.second)
                    {
                        auto interfaceInfo = interface->Get();
                        if (!interfaceInfo) continue;
                        std::shared_lock<std::shared_mutex> macLock(interfaceInfo->ipMutex);
                        if (interfaceInfo->macAddress.size() != 6) continue;
                        ByteString address = interfaceInfo->macAddress.substr(2, 4);
                        uint32_t macNum = Functions::byteToNum(address);
                        if (macNum > highestNum) continue;
                        highestNum = macNum;
                        highestIP = address;
                    }
                }
            }
            routerID.ID = highestIP;
        }
    }

#pragma endregion

#pragma region EigrpInterface

    EigrpInterface::EigrpInterface(Eigrp &eigrpSystem, Interface* interface)
        : eigrpProcess(&eigrpSystem),
          currentInterface(interface),
          helloStartTime(std::chrono::steady_clock::now())
    {
        try
        {
            if (currentInterface)
            {
                currentInterfaceInfo = currentInterface->Get();
                if (currentInterfaceInfo)
                {
                    std::lock_guard<std::shared_mutex> lock(currentInterfaceInfo->ipMutex);
                    configs.interfaceAddress = (eigrpProcess->getAddressFamily() == AddressFamily::IPv4) 
                        ? currentInterfaceInfo->ipv4.ipAddress 
                        : currentInterfaceInfo->ipv6.ipAddress;
                    configs.interfaceMask = (eigrpProcess->getAddressFamily() == AddressFamily::IPv4) 
                        ? currentInterfaceInfo->ipv4.mask 
                        : currentInterfaceInfo->ipv6.mask;
                }
            }
            startHelloHelper();
            sendHelloPacket();
        }
        catch (const std::exception &e)
        {
            Logger::getInstance().error() << "Error initializing EigrpInterface: " << e.what() << std::endl;
            throw;
        }
    }

    EigrpInterface::~EigrpInterface()
    {
        AddressFamily addressFamily = eigrpProcess->getAddressFamily();
        uint8_t mask = 32;
        ByteString network;
        {
            std::shared_lock<std::shared_mutex> ipLock(currentInterface->configs.ipMutex);
            {
                if (addressFamily == AddressFamily::IPv4)
                {
                    mask = currentInterface->configs.ipv4.mask;
                    network = Functions::computeNetworkAddress(currentInterface->configs.ipv4.ipAddress, mask);
                }
                else if (addressFamily == AddressFamily::IPv6)
                {
                    mask = currentInterface->configs.ipv6.mask;
                    network = Functions::computeNetworkAddress(currentInterface->configs.ipv6.ipAddress, mask);
                }
            }
        }

        auto globalRoute = RoutingTable::getInstance().getEigrpRoute(network, mask, addressFamily, eigrpProcess->getAsNumber());
        if (globalRoute.has_value())
        {
            if (globalRoute.value() && globalRoute.value()->routeType == "connected")
            {
                // Remove if valid and is a connected route
                RoutingTable::getInstance().removeEigrp(network, mask, addressFamily, eigrpProcess->getAsNumber());
            }
        }

        // Remove interface from routing table if able to
        sendHelloPacket(nullptr); // Termination message
        stopHello();

        // Remove all active timers
        {
            std::lock_guard<std::mutex> activeLock(activeTimerMutex);
            for (auto& [_, id] : activeTimers)
            {
                TimeManager::getInstance().cancelTimer(id);
            }
            for (auto& [_, id] : siaTimers)
            {
                TimeManager::getInstance().cancelTimer(id);
            }
        }
        
        // Aquire lock to modify neighbors
        std::shared_lock<std::shared_mutex> lock(neighborMutex);

        for (auto it = neighbors.begin(); it != neighbors.end();)
        {
            auto neighbor = it->second;
            it = neighbors.erase(it);
            delete neighbor;
        }

        // Remove interface from other tables
        uint8_t id;
        {
            std::shared_lock<std::shared_mutex> ipLock(currentInterface->configs.ipMutex);
            id = currentInterface->configs.id;
        }
        if (currentInterface->eigrpInterfaceList.find(id) != currentInterface->eigrpInterfaceList.end())
        {
            if (addressFamily == AddressFamily::IPv4)
            {
                currentInterface->eigrpInterfaceList[id]->IPv4 = nullptr;
            }
            else if (addressFamily == AddressFamily::IPv6)
            {
                currentInterface->eigrpInterfaceList[id]->IPv6 = nullptr;
            }
            if (!currentInterface->eigrpInterfaceList[id]->IPv4 && !currentInterface->eigrpInterfaceList[id]->IPv6)
            {
                currentInterface->eigrpInterfaceList.erase(id);
            }
        }

        //Logger::getInstance().info() << "EigrpInterface destroyed and all timers canceled." << std::endl;
    }

    void EigrpInterface::processPacket(const EigrpHeader& eigrpPacket, const ByteString neighborIp)
    {
        EigrpConfigs::NeighborState neighborState;
        // Check if passive
        if (configs.isPassive)
        {
            Logger::getInstance().info() << "Interface is passive. Incoming EIGRP packet ignored." << std::endl;
            return;
        }
        // Validate packet version
        if (eigrpPacket.version != "\x02")
        {
            // Version not valid
            return;
        }

        // Check for valid neighbor 
        EigrpConfigs::NeighborInfo* neighbor = nullptr;
        {
            // Neighbor mutex for save access
            std::shared_lock<std::shared_mutex> lock(neighborMutex);
            auto neighborIt = neighbors.find(neighborIp);
            if (neighborIt == neighbors.end())
            {
                return;
            }
            neighbor = neighborIt->second;
        }

        if (!neighbor)
        {
            Logger::getInstance().warn() << "Recieved packet from unknown neighbor: " << neighborIp.toHex() << std::endl;
            return; // No neighbor matches this ip
        }
        else if (eigrpPacket.opcode != Variable::Eigrp::Type::hello || eigrpPacket.ack != ByteString(4, '\x00'))
        {
            std::lock_guard<std::mutex> initLock(neighbor->initializationMutex);
            neighborState = neighbor->neighborState;
        }

        // Change neighbor state if needed
        if (neighbor)
        {
            if (neighborState == EigrpConfigs::NeighborState::EXSTART)
            {
                changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::EXCHANGE);
            }
        }

        if (eigrpPacket.opcode == Variable::Eigrp::Type::hello)
        {
            if (eigrpPacket.ack == ByteString(4, '\x00'))
            {
                processHello(neighbor, eigrpPacket, neighborIp);
            }
            else
            {
                processAck(neighbor, eigrpPacket.ack);
            }
        }
        else if (eigrpPacket.opcode == Variable::Eigrp::Type::update)
        {
            processUpdate(neighbor, eigrpPacket);
        }
        else if (eigrpPacket.opcode == Variable::Eigrp::Type::reply)
        {
            processReply(neighbor, neighborIp, eigrpPacket);
        }
        else if (eigrpPacket.opcode == Variable::Eigrp::Type::query)
        {
            processQuery(neighbor, eigrpPacket, neighborIp);
        }
    }

    void EigrpInterface::changeNeighborState(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, EigrpConfigs::NeighborState newState)
    {
        // Make sure neighbor exists
        if (!neighbor)
        {
            return; // Neighbor is null
        }

        // Check for currect state in order to change state
        {
            std::lock_guard<std::mutex> lock(neighbor->initializationMutex);
            if (neighbor->neighborState != EigrpConfigs::NeighborState::DOWN &&
                neighbor->neighborState != static_cast<EigrpConfigs::NeighborState>(static_cast<int>(newState) - 1))
            {
                return;
            }
        }

        // Initialization mutex for thread
        ByteString routerID;
        uint32_t seq = 0;
        bool unicast;
        EigrpConfigs::NeighborState currentState;
        
        {
            // Lock mutex to safely access shared state
            std::shared_lock<std::shared_mutex> lock(neighborMutex);
            std::shared_lock<std::shared_mutex> seqLock(seqMutex);
            seq = nextSequenceNumber;
            routerID = neighbor->routerID;
            currentState = neighbor->neighborState;
            unicast = (getConfigs().mode == EigrpConfigs::CommunicationMode::UNICAST);

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
                sendHelloPacket(neighbor);

                // Update INIT start time and start stuck detection thread
                {
                    std::lock_guard<std::mutex> initLock(neighbor->initMutex);
                    neighbor->initStartTime = std::chrono::steady_clock::now();
                    neighbor->stuckInInitCheckActive = true;
                }

                // Spawn a thread to check for "stuck in INIT"
                neighbor->stuckInInitTimerId = TimeManager::getInstance().addTimer(std::chrono::steady_clock::now() + std::chrono::seconds(neighbor->holdTime), [this, neighbor, neighborIp]()
                {
                    std::lock_guard<std::mutex> initLock(neighbor->initMutex);

                    // Check if the neighbor is still in Initializing
                    if (neighbor->neighborState < EigrpConfigs::NeighborState::LOADING)
                    {
                        handleNeighborDown(neighbor, neighborIp);
                    }
                    else
                    {
                        neighbor->stuckInInitCheckActive = false;
                    }
                });

                // Update the neighbors state
                {
                    std::lock_guard<std::mutex> lock(neighbor->initializationMutex);
                    neighbor->neighborState = newState;
                }

                changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::TWOWAY);
                break;

            case EigrpConfigs::NeighborState::TWOWAY:
                Logger::getInstance().info(true) << "Neighbor in TWOWAY state." << std::endl;
                
                // Update the neighbors state
                {
                    std::lock_guard<std::mutex> lock(neighbor->initializationMutex);
                    neighbor->neighborState = newState;
                }
                
                // Start a seperate thread to monitor the second hello
                neighbor->twoWayThread = std::thread([this, neighbor, neighborIp]()
                {
                    std::unique_lock<std::mutex> cvLock(neighbor->twoWayMutex);

                    // Wait for up to 300ms for a second Hello
                    neighbor->cv.wait_for(cvLock, std::chrono::milliseconds(300), [neighbor]() {return neighbor->secondHelloReceived; });

                    changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::EXSTART);
                });

                break;

            case EigrpConfigs::NeighborState::EXSTART:
                Logger::getInstance().info(true) << "Neighbor in EXSTART state. Waiting for role" << std::endl;
                
                    // Send Null Update to initialize reliable communication
                    Logger::getInstance().info(true) << "Sending Null Update to neighbor." << std::endl;
                    if (!neighbor->nullSent)
                    {
                        sendUpdateToNeighbor(neighbor, {}, EigrpConfigs::UpdateType::QUERY);
                    }

                    // Update the neighbors state
                    {
                        std::lock_guard<std::mutex> lock(neighbor->initializationMutex);
                        neighbor->neighborState = newState;
                    }

                // Move to EXCHANGE to start topology exchange
                changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::EXCHANGE);
                break;

            case EigrpConfigs::NeighborState::EXCHANGE:
                Logger::getInstance().info(true) << "Neighbor in EXCHANGE state. Sharing topology." << std::endl;
                
                if (neighbor->slaveInit || neighbor->masterInit)
                {
                    {std::lock_guard<std::mutex> lock(neighbor->initializationMutex); neighbor->neighborState = newState;}
                    changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::LOADING);
                }
                else if (neighbor->initRole == EigrpConfigs::InitRole::MASTER)
                {
                    if (!neighbor->nullSent && !neighbor->masterInit)
                    {
                        {std::lock_guard<std::mutex> lock(neighbor->initializationMutex); neighbor->neighborState = EigrpConfigs::NeighborState::EXCHANGE; neighbor->masterInit = true;}
                        // Send Sequence Hello with the generated sequence number
                        Logger::getInstance().info(true) << "MASTER sending Sequence Hello with sequence number: " << seq << std::endl;
                        sendHelloPacket(neighbor, /*unicast=*/false, /*update=*/true, seq);

                        // Set state to loading
                        Logger::getInstance().info(true) << "MASTER sending full topology." << std::endl;
                        {std::lock_guard<std::mutex> lock(neighbor->initializationMutex); neighbor->neighborState = newState;}
                        changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::LOADING);
                    }
                }
                else if (neighbor->initRole == EigrpConfigs::InitRole::SLAVE && neighbor->initUpdateReceived && !neighbor->slaveInit)
                {
                    // Send Null update to initialize reliable connections
                    Logger::getInstance().info(true) << "Sending Null Update to neighbor." << std::endl;
                    
                    // Send Sequence Hello with the generated sequence number
                    Logger::getInstance().info(true) << "Sending Sequence Hello with sequence number: " << seq << std::endl;
                    sendHelloPacket(neighbor, /*unicast=*/false, /*update=*/true, seq);

                    // Send your topology
                    Logger::getInstance().info(true) << "Sending full topology." << std::endl;
                    sendUpdateToNeighbor(neighbor, RoutingTable::getInstance().getAllEigrpRoutes(eigrpProcess->getAddressFamily(), eigrpProcess->getAsNumber()), EigrpConfigs::UpdateType::FULL, false, true, {neighborIp});

                    // Send a hello immediately after sending routes
                    Logger::getInstance().info(true) << "Sending immediate Hello after full topology." << std::endl;
                    sendHelloPacket(neighbor);

                    {std::lock_guard<std::mutex> lock(neighbor->initializationMutex); neighbor->neighborState = EigrpConfigs::NeighborState::EXCHANGE; neighbor->slaveInit = true;}
                    changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::LOADING);
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
                    neighbor->neighborState = newState;
                }
                break;

            case EigrpConfigs::NeighborState::ESTABLISHED:
                Logger::getInstance().info(true) << "Neighbor in ESTABLISHED state. Adjacency fully formed." << std::endl;

                // Update the neighbors state
                {
                    std::lock_guard<std::mutex> lock(neighbor->initializationMutex);
                    neighbor->neighborState = newState;
                }
                break;
                
            default:
                break;
        }
    }

    void EigrpInterface::processHello(EigrpConfigs::NeighborInfo* neighbor, const EigrpHeader& receivedHello, const ByteString& neighborIp)
    {
        // Neighor values if needed
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

        // Validate the Autonomous System Number (ASN)
        if (Functions::byteToNum(receivedHello.autonomousSystem) != eigrpProcess->getAsNumber())
        {
            // Drop the packet - AS number mismatch
            return;
        }

        // Extract Hold Time from options
        uint16_t recievedHoldTime = configs.holdTime; // Default holdtime

        // Safely access or create the neighbor
        {
            std::lock_guard<std::shared_mutex> initLock(neighborMutex);

            if (!neighbor)
            {
                neighbor = new EigrpConfigs::NeighborInfo();
                neighbors[neighborIp] = neighbor;
                neighborAdded = true;
            }
        }

        // Process TLVs
        for (const auto& opt : receivedHello.options)
        {
            if (opt.option == Variable::Eigrp::Option::parameter)
            {
                ByteString parameters = eigrpProcess->calculateParameters(configs.holdTime);
                if (opt.value.substr(0, 5) != parameters.substr(0, 5)) return; // Drop the packet if parameters mismatch

                // Extract Holdtime
                recievedHoldTime = static_cast<uint16_t>(Functions::byteToNum(opt.value.substr(6, 2)));

                // Check for Peer Termination
                if (parameters.substr(0, 5) == ByteString(5, 0xFF))
                {
                    handleNeighborDown(neighbor, neighborIp);
                    return;
                }
            }
            else if (opt.option == Variable::Eigrp::Option::sequence)
            {
                if (getConfigs().interfaceAddress != opt.value.substr(1, Functions::byteToNum(opt.value.substr(0, 1))))
                    return; // Drop packet if the sequence doesn't match
            }
            else if (opt.option == Variable::Eigrp::Option::multicastSequence)
            {
                neighbor->initSequence = Functions::byteToNum(opt.value);
            }
        }

        // Update neighbor fields and start/renew hold timers
        neighbor->ipAddress = neighborIp;
        neighbor->holdTime = recievedHoldTime;
        neighbor->lastHeard = std::chrono::steady_clock::now();
        neighbor->lastReceivedSequenceNumber = 1;
        neighbor->srtt = 1.0;
        neighbor->rttvar = 0.5;
        neighbor->rto = 1.5;

        // Cancel existing hold timer
        if (neighbor->holdTimerId != 0)
        {
            TimeManager::getInstance().cancelTimer(neighbor->holdTimerId);
        }
        startHoldTimer(neighbor, neighborIp, neighbor->holdTime);

        if (neighborAdded)
        {
            // Optionally handle a new neighbor
            // eigrpProcess->UpdateRoutingTableForConnected(shared_from_this());
        }

        // Safely extract the neighbor state
        EigrpConfigs::NeighborState neighborState;
        {
            std::lock_guard<std::mutex> initLock(neighbor->initializationMutex);
            neighborState = neighbor->neighborState;
        }

        if (neighborState == EigrpConfigs::NeighborState::DOWN)
        {
            changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::INIT);
        }
        else if (neighborState == EigrpConfigs::NeighborState::TWOWAY)
        {
            std::lock_guard<std::mutex> cvLock(neighbor->twoWayMutex);
            neighbor->secondHelloReceived = true;
            neighbor->cv.notify_one(); // Wake up the waiting thread immediately.
        }
    }

    void EigrpInterface::processUpdate(EigrpConfigs::NeighborInfo* neighbor, const EigrpHeader& receivedUpdate)
    {
        // Get neighbor Ip
        ByteString neighborIp;
        {
            std::shared_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
            neighborIp = neighbor->ipAddress;
        }

        if (neighborIp == getConfigs().interfaceAddress || neighborIp.empty())
        {
            return; // Neighbor IP invalid
        }

        // Extract sequence number
        uint32_t receivedSequenceNumber = Functions::byteToNum(receivedUpdate.sequence);

        // Update flags
        bool initReceived = false;
        if (receivedUpdate.flags.restart == "1") {}
        if (receivedUpdate.flags.init == "1") { neighbor->sequenceList[receivedSequenceNumber].init = true; neighbor->initUpdateReceived = true;}
        if (receivedUpdate.flags.conditionalRecieve == "1") { neighbor->sequenceList[receivedSequenceNumber].conditionalReceive = true; }
        if (receivedUpdate.flags.endOfTable == "1") {neighbor->sequenceList[receivedSequenceNumber].endOfTable = true;}

        // Validate sequence number
        {
            std::lock_guard<std::shared_mutex> lock(neighbor->neighborDataMutex);
            std::lock_guard<std::mutex> initLock(neighbor->initializationMutex);

            if (receivedSequenceNumber <= neighbor->lastReceivedSequenceNumber && !neighbor->initComplete)
            {
                Logger::getInstance().debug() << "Duplicate or old Update received with sequence number " << receivedSequenceNumber << " from neighbor " << neighborIp.toHex() << std::endl;
                if (!initReceived && neighbor->neighborState != EigrpConfigs::NeighborState::EXSTART)
                {
                    sendAckToNeighbor(neighbor, neighborIp, neighbor->lastReceivedSequenceNumber);
                }
                return;
            }
            else if (receivedSequenceNumber > neighbor->lastReceivedSequenceNumber + 1)
            {
                Logger::getInstance().debug() << "Sequence number: " << receivedSequenceNumber << " from neighbor: " << neighborIp.toHex() << " is too new, adding packet to buffer." << std::endl;
                // Buffer out of order packet
                neighbor->packetBuffer[receivedSequenceNumber] = EigrpConfigs::NeighborInfo::PacketBuffer{.neighborIp = neighborIp, .eigrp = receivedUpdate};
            }

            // Validate init sequence
            if (neighbor->initSequence != 0)
            {
                if (receivedSequenceNumber != neighbor->initSequence)
                {
                    neighbor->initSequence = 0;
                    handleNeighborRestart(neighbor, neighborIp);
                    return;
                }
                neighbor->initSequence = 0;
            }
                
            neighbor->lastReceivedSequenceNumber = receivedSequenceNumber;
        }

        // Acknowledge packet
        {
            std::lock_guard<std::mutex> lock(neighbor->initializationMutex);
            if (neighbor->neighborState > EigrpConfigs::NeighborState::EXCHANGE)
            {
                sendAckToNeighbor(neighbor, neighborIp, receivedSequenceNumber);
            }
        }


        // Determine roles based on sequence numbers
        if (!neighbor->initComplete) 
        {
            std::lock_guard<std::shared_mutex> lock(neighbor->neighborDataMutex);
            if (receivedSequenceNumber < nextSequenceNumber)
            {
                neighbor->initRole = EigrpConfigs::InitRole::MASTER;
                Logger::getInstance().info(true) << "MASTER role has been chosen with sequence number: " << nextSequenceNumber << " and neighbors: " << receivedSequenceNumber << "." << std::endl;
            }
            else if (receivedSequenceNumber > nextSequenceNumber)
            {
                neighbor->initRole = EigrpConfigs::InitRole::SLAVE;
                Logger::getInstance().info(true) << "SLAVE role has been chosen with sequence number: " << nextSequenceNumber << " and neighbors: " << receivedSequenceNumber << "." << std::endl;
            }
            else if (neighbor->lastReceivedSequenceNumber == nextSequenceNumber)
            {
                // Use router ID as a tie-breaker
                Logger::getInstance().info(true) << "Sequence numbers equal. Using Router ID as tie-breaker." << std::endl;
                if (Functions::byteToNum(eigrpProcess->getRouterID()) > Functions::byteToNum(neighbor->routerID))
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
        }

        // Process Ack if present
        if (receivedUpdate.ack != ByteString(4, '\x00'))
        {
            processAck(neighbor, receivedUpdate.ack);
        }

        // Collect routes for batch processing
        for (const auto &option : receivedUpdate.options)
        {
            if (option.option == Variable::Eigrp::Option::internalRoute ||
                option.option == Variable::Eigrp::Option::externalRoute)
            {
                RoutingTable::Eigrp* route = decodeRoute(option.value, (option.option == Variable::Eigrp::Option::externalRoute), false);
                route->nextHop = neighborIp;

                if (route->delay != 0xFFFFFFFF)
                {
                    routeBuffer.emplace_back(route); // Ignore invalid routes
                }
            }
        }

        if (!routeBuffer.empty())
        {
            RoutingTable& routingTable = RoutingTable::getInstance();
            for (const auto& route : routeBuffer)
            {
                routingTable.addEigrp(route, eigrpProcess->getAddressFamily(), eigrpProcess->getAsNumber());
            }
            updateRoutingTable(neighbor, neighborIp, routeBuffer, neighbor->sequenceList[receivedSequenceNumber].init);
            routeBuffer.clear();
        }

        // Check and process buffered packets
        processBufferedPackets(neighbor);

        // Safely access neighbor state
        std::unique_lock<std::mutex> lock(neighbor->initializationMutex);
        if (neighbor->initComplete && neighbor->neighborState == EigrpConfigs::NeighborState::EXSTART && neighbor->initRole == EigrpConfigs::InitRole::SLAVE && !neighbor->slaveInit)
        {
            lock.unlock(); // Unlock initialization lock to prevent deadlock
            changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::EXCHANGE);
        }
    }

    void EigrpInterface::processBufferedPackets(EigrpConfigs::NeighborInfo* neighbor)
    {
        if (!neighbor) return; // neighbor is invalid
        
        uint32_t nextExpectedSequence = neighbor->lastReceivedSequenceNumber + 1;

        while (true)
        {
            std::unique_lock<std::shared_mutex> bufferLock(neighbor->neighborDataMutex);
            auto packetIt = neighbor->packetBuffer.find(nextExpectedSequence);

            if (packetIt != neighbor->packetBuffer.end())
            {
                // Process buffered packet
                EigrpConfigs::NeighborInfo::PacketBuffer bufferedPacket = packetIt->second;
                neighbor->packetBuffer.erase(packetIt);
                neighbor->lastReceivedSequenceNumber = nextExpectedSequence;

                // Unlock before processing the packet
                bufferLock.unlock();
                processUpdate(neighbor, bufferedPacket.eigrp);
            }
            else if (isTimeoutForMissing(neighbor, nextExpectedSequence))
            {
                Logger::getInstance().info() << "Timeout for missing packet with sequence number: "
                                             << nextExpectedSequence << ". Moving forward." << std::endl;
                neighbor->lastReceivedSequenceNumber = nextExpectedSequence;
            }
            else
            {
                break;
            }
            
            // Move to the next sequence number
            nextExpectedSequence++;
        }
    }

    void EigrpInterface::processAck(EigrpConfigs::NeighborInfo* neighbor, const ByteString& sequenceNumber)
    {
        // Ensure the neighbor is valid
        if (!neighbor)
        {
            Logger::getInstance().warn() << "Invalid neighbor passed to processAck." << std::endl;
            return;
        }

        uint32_t ackSequenceNumber = Functions::byteToNum(sequenceNumber);

        // Locate the acknowledgement packet in reliablePacket
        std::unique_lock<std::shared_mutex> lock(neighbor->reliableMutex);
        auto pktIt = neighbor->reliablePackets.find(ackSequenceNumber);

        if (pktIt == neighbor->reliablePackets.end())
        {
            Logger::getInstance().warn() << "Received ACK for unknown sequence number " << ackSequenceNumber << " from neighbor " << neighbor->ipAddress.toHex() << std::endl;
            return;
        }

        // Copy data before unlocking
        EigrpConfigs::NeighborInfo::ReliablePacketInfo reliablePacketCopy = pktIt->second;

        // Erase the packet while locked
        std::erase_if(neighbor->reliablePackets, [&](const auto& entry) {
            return entry.first == ackSequenceNumber;
        });

        lock.unlock();

        // Handle routes associated with the acknowledged packet
        for (const auto& route : reliablePacketCopy.packet.updatedRoutes)
        {
            ByteString key = route->network + "/" + std::to_string(route->mask);

            std::unique_lock<std::shared_mutex> routeLock(neighbor->neighborDataMutex);
            auto advertIt = neighbor->advertisedRoutes.find(key);

            if (reliablePacketCopy.packet.remove)
            {
                // Fully remove routes marked for removal
                if (advertIt != neighbor->advertisedRoutes.end() && advertIt->second.removePending)
                {
                    neighbor->advertisedRoutes.erase(advertIt);
                    Logger::getInstance().debug() << "Route " << key.toHex() << " fully remove after ACK." << std::endl;
                }
            }
            else
            {
                // Update or add routes
                if (advertIt == neighbor->advertisedRoutes.end())
                {
                    neighbor->advertisedRoutes[key] = {route, true, false, false};
                    Logger::getInstance().debug() << "Route " << key.toHex() << " added/updated for neighbor " << neighbor->ipAddress.toHex() << "." << std::endl;
                }
                else
                {
                    advertIt->second.active = true;
                    advertIt->second.pendingUpdate = false;
                }
            }
        }

        // Cancel the Retransmission Timer
        if (pktIt->second.timerId != 0)
        {
            TimeManager::getInstance().cancelTimer(pktIt->second.timerId);
        }

        // Update RTT and RTO Estimates if Necessary
        updateRTTEstimate(neighbor, ackSequenceNumber);
    }

    void EigrpInterface::processQuery(EigrpConfigs::NeighborInfo* neighbor, const EigrpHeader& receivedQuery, const ByteString& neighborIp)
    {
        if (!neighbor) return; // Neighbor does not exist

        uint32_t receivedSequenceNumber = Functions::byteToNum(receivedQuery.sequence);

        {
            std::lock_guard<std::shared_mutex> lock(neighbor->neighborDataMutex);
            std::lock_guard<std::mutex> initLock(neighbor->initializationMutex);

            // Ensure correct last received sequence tracking
            if (receivedSequenceNumber <= neighbor->lastReceivedSequenceNumber)
            {
                return; // Ignoring duplicate sequence number;
            }

            // Set the last received sequence number
            neighbor->lastReceivedSequenceNumber = receivedSequenceNumber;
        }

        RoutingTable::Eigrp* queriedRoute = nullptr;
        for (const auto& option : receivedQuery.options)
        {
            if (option.option == Variable::Eigrp::Option::internalRoute)
            {
                queriedRoute = decodeRoute(option.value, false, false);
            }
        }

        ByteString queryKey = queriedRoute->network + "/" + std::to_string(queriedRoute->mask);

        // Check if the queried route exists
        auto existingRoute = RoutingTable::getInstance().getEigrpRoute(queriedRoute->network, queriedRoute->mask, eigrpProcess->getAddressFamily(), eigrpProcess->getAsNumber());

        if (existingRoute)
        {
            // Route is know, send a reply immediately
            sendReplyToNeighbor(neighbor, neighborIp, existingRoute.value());
            return;
        }

        // Otherwise we need to query all neighbors
        EigrpConfigs::ActiveRoute newQuery;
        newQuery.route = queriedRoute;
        newQuery.originNeighbor = neighborIp;

        for (auto& [_, interface] : eigrpProcess->eigrpInterfaceList)
        {
            for (const auto& [ip, otherNeighbor] : interface->neighbors)
            {
                if (ip != neighborIp)
                {
                    newQuery.pendingReplies.insert(ip);
                    sendQueryToNeighbor(otherNeighbor, ip, queriedRoute);
                }
            }
        }

        // Store this query in our global tracker
        eigrpProcess->outstandingReplies[queryKey] = newQuery;

        // Start the timers
        startActiveTimer(queriedRoute);
        startSIATimer(queriedRoute);
    }

    bool EigrpInterface::isTimeoutForMissing(EigrpConfigs::NeighborInfo* neighbor, uint32_t sequenceNumber)
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

    void EigrpInterface::processReply(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, const EigrpHeader& receivedReply)
    {
        if (!neighbor) return; // Neighbor invalid

        uint32_t receivedSequenceNumber = Functions::byteToNum(receivedReply.sequence);

        {
            std::lock_guard<std::shared_mutex> lock(neighbor->neighborDataMutex);
            std::lock_guard<std::mutex> initLock(neighbor->initializationMutex);

            if (receivedSequenceNumber <= neighbor->lastReceivedSequenceNumber)
            {
                Logger::getInstance().debug() << "Ignoring duplicate or old Reply with sequence number "
                                              << receivedSequenceNumber << " from neighbor " << neighborIp.toHex() << std::endl;
                return;
            }

            // ✅ Set last received sequence number
            neighbor->lastReceivedSequenceNumber = receivedSequenceNumber;
        }

        RoutingTable::Eigrp* receivedRoute;
        for (const auto& option : receivedReply.options)
        {
            if (option.option == Variable::Eigrp::Option::internalRoute)
            {
                receivedRoute = decodeRoute(option.value, false, false);
            }
        }

        if (!receivedRoute) return;

        ByteString queryKey = receivedRoute->network + "/" + std::to_string(receivedRoute->mask);

        // Send ack for the received reply
        sendAckToNeighbor(neighbor, neighborIp, Functions::byteToNum(receivedReply.sequence));
        
        if (eigrpProcess->outstandingReplies.find(queryKey) != eigrpProcess->outstandingReplies.end())
        {
            auto& queryInfo = eigrpProcess->outstandingReplies[queryKey];

            // Remove this neighbor from the pensing replies list
            queryInfo.pendingReplies.erase(neighborIp);

            // If all replies are in, send our own reply upstream
            if (queryInfo.pendingReplies.empty())
            {
                for (auto& [_, interface] : eigrpProcess->eigrpInterfaceList)
                {
                    if (interface->neighbors.find(queryInfo.originNeighbor) != interface->neighbors.end())
                    {
                        sendReplyToNeighbor(neighbors[queryInfo.originNeighbor], queryInfo.originNeighbor, receivedRoute);
                    }
                }

                // Remove thequery from tracking
                cancelActiveTimer(receivedRoute->network, receivedRoute->mask);
                cancelSIATimer(receivedRoute->network, receivedRoute->mask);
                eigrpProcess->outstandingReplies.erase(queryKey);
            }
        }

        // If the received route is not unreachable, update the routing table
        if (receivedRoute->metric != std::numeric_limits<uint32_t>::max())
        {
            updateRoutingTable(neighbor, neighborIp, {receivedRoute}, false);
        }
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

    void EigrpInterface::sendAckToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, uint32_t sequenceNumber)
    {
        // Validate neighbor
        if (!neighbor)
        {
            Logger::getInstance().warn() << "Invalid neighbor passed to processReply." << std::endl;
            return;
        }

        // Add the sequence number to pensing ACKs if not already present
        if (std::binary_search(neighbor->pendingAcks.begin(), neighbor->pendingAcks.end(), sequenceNumber))
        {
            neighbor->pendingAcks.emplace_back(sequenceNumber);
        }

        // if ACK processing is disabled, return early
        if (!neighbor->processAcks)
        {
            return;
        }

        // Process pending ACKs
        PacketInfo eigrpAckPacketStructure;
        for (auto seq : neighbor->pendingAcks)
        {
            // Create the EIGRP Ack packet
            EigrpHeader eigrp;
            eigrpProcess->eigrpHello(eigrp, this, neighbor, neighborIp, seq, /*ack=*/true);

            // Add Authentication TLV if enabled
            if (neighbor->authenticationEnabled)
            {
                EigrpHeader::Option authTLV = generateAuthenticatedTLV(eigrp, neighbor);
                if (authTLV.option != ByteString(1, 0x00))
                {
                    eigrp.options.emplace_back(authTLV);
                }
            }

            // Assemble the packet
            eigrpAckPacketStructure.Layer4.push_back(std::move(eigrp));

            // Log the ACK sending
            Logger::getInstance().debug() << "ACK sent for sequence number " << seq << " to neighbor " << neighborIp.toHex() << std::endl;
        }

        // Send the assembled ACK packet if it contains data
        if (!eigrpAckPacketStructure.Layer4.empty() && currentInterface)
        {
            currentInterface->ipPacket->setIPHeader(
                eigrpAckPacketStructure, neighborIp, getConfigs().DSCP, 2, Variable::IP::eigrp);
            Logger::getInstance().info() << "ACKs sent to neighbor " << neighborIp.toHex() << std::endl;
        }

        // Clear the pending ACKs processing
        neighbor->pendingAcks.clear();
    }

    void EigrpInterface::sendUpdateToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const std::vector<RoutingTable::Eigrp*> &routes, EigrpConfigs::UpdateType updateType, bool restart, bool conditional, std::vector<ByteString> conditionalNeighbors)
    {
        // Determine target IP based on communication mode
        ByteString targetIp;
        {
            if (neighbor)
            {
                targetIp = neighbor->ipAddress;
            }
            else 
            {
                // Make sure interface has neigbors
                if (neighbors.empty())
                {
                    return; // No neighbors to sent routes.
                }
                targetIp = getMulticast();
            }

            // Save sequence number for NULL Update
            if (neighbor && updateType == EigrpConfigs::UpdateType::QUERY)
            {
                std::lock_guard<std::shared_mutex> ipLock(neighbor->neighborDataMutex);
                neighbor->nullUpdateSequence = getNextSequenceNumber();
                neighbor->nullSent = true;
            }
        }

        // Filter routes based on stub configuration and split horizon
        std::vector<RoutingTable::Eigrp*> filteredRoutes;
        for (const auto& route : routes)
        {
            bool needsUpdate = false;

            Logger::getInstance().debug() << "Applying filter for route: " << route->network.toHex() << std::endl;

            // Stub Test
            // If the process is running in stub mode, only allow routes that are permitted.
            bool stubTest = true;
            if (eigrpProcess->isStub())
            {
                stubTest = ((route->routeType == "connected" && eigrpProcess->advertiseConnected()) ||
                            (route->routeType == "static" && eigrpProcess->advertiseStatic()) ||
                            (route->routeType == "summary" && eigrpProcess->advertiseSummary()) ||
                            (route->routeType == "external" && eigrpProcess->advertiseRedistributed()));
                
            }

            // Split Horizon Test
            bool splitHorizonTest = true;
            if (getConfigs().splitHorizon)
            {
                for (const auto& [address, splitHorizonNeighbor] : neighbors)
                {
                    if (route->nextHop == address || Functions::compareNetworkWithIp(route->network, splitHorizonNeighbor->ipAddress, route->mask))
                    {
                        splitHorizonTest = false;
                        break;
                    }
                }
            }
            
            // Check if route requires an update
            ByteString key = route->network + "/" + std::to_string(route->mask);
            for (const auto& [address, updateNeighbor] : neighbors)
            {
                auto advertIt = updateNeighbor->advertisedRoutes.find(key);
                if (advertIt != updateNeighbor->advertisedRoutes.end())
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
                    break;
                }
            }

            if (splitHorizonTest && stubTest && needsUpdate)
            {
                filteredRoutes.emplace_back(route);
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
        bool isConditional = false;
        bool endOfTable = false;
        bool isInit = false;

        do
        {
            // Create the EIGRP Update packet structure
            PacketInfo eigrpPacket;
            EigrpHeader eigrp;

            // Get the next sequence number
            uint32_t sequenceNumber = getNextSequenceNumber();

            // Add routes to the packet
            for (; it != filteredRoutes.end() && routeCount < maxRoutesPerPacket; ++it, ++routeCount)
            {
                EigrpHeader::Option routeOptions;
                if ((*it)->routeType == "external")
                {
                    routeOptions.value = encodeExternalRouteOption(*it, removal);
                    routeOptions.option = (eigrpProcess->getAddressFamily() == AddressFamily::IPv4) ? Variable::Eigrp::Option::externalRoute : Variable::Eigrp::Option::externalRouteV6;
                }
                else
                {
                    routeOptions.value = encodeRouteOption(*it, removal);
                    routeOptions.option = (eigrpProcess->getAddressFamily() == AddressFamily::IPv4) ? Variable::Eigrp::Option::internalRoute : Variable::Eigrp::Option::internalRouteV6;
                }
                routeOptions.length = Functions::numToByte(static_cast<uint32_t>(routeOptions.value.size()) + 4, 2);
                eigrp.options.emplace_back(routeOptions);
            }
            
            // Set flags based on update type and stage
            endOfTable = (it == filteredRoutes.end());
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

            eigrpProcess->eigrpUpdate(eigrp, sequenceNumber, /*init=*/isInit, /*conditional=*/isConditional, /*restart=*/restart, /*endOfTable*/((endOfTable/* && filteredRoutes.size() != 1*/)));
            isInit = false;
            
            // Handle Acks and Authentication
            if (neighbor)
            {
                if (!neighbor->pendingAcks.empty())
                {
                    eigrp.ack = Functions::numToByte(neighbor->pendingAcks.front(), 4);
                    neighbor->pendingAcks.erase(neighbor->pendingAcks.begin());
                }
                // Add stub option
                if (eigrpProcess->isStub())
                {
                    EigrpHeader::Option stubTLV{
                        .option = Variable::Eigrp::Option::stub,
                        .length = ByteString("\x02", 1),
                        .value = encodeStubOption(eigrpProcess->getConfigs()->stubConfig)
                    };
                    if (stubTLV.option != ByteString(1, 0x00))
                    {
                        eigrp.options.emplace_back(stubTLV);
                    }
                }
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
            eigrpPacket.Layer4.push_back(eigrp);
            if (currentInterface && !currentInterface->shutdownFlag)
            {
                currentInterface->ipPacket->setIPHeader(eigrpPacket, targetIp, getConfigs().DSCP, 2, Variable::IP::eigrp);
            }
            
            Logger::getInstance().info() << "Update sent to neighbor " << targetIp.toHex() << " with sequence number " << sequenceNumber << std::endl;

            if (!neighbor || (neighbor && neighbor->processAcks))
            {
                if (conditional)
                {
                    eigrp.flags.conditionalRecieve = "0";
                    std::shared_lock<std::shared_mutex> lock(neighborMutex);
                    for (const auto& [address, neighborPtr] : neighbors)
                    {
                        std::vector<RoutingTable::Eigrp*> neighborSpecificRoutes;
                        for (const auto& route : filteredRoutes)
                        {
                            std::lock_guard<std::shared_mutex> neighborLock(neighborPtr->neighborDataMutex);
                            ByteString key = route->network + "/" + std::to_string(route->mask);
                            auto advertIt = neighborPtr->advertisedRoutes.find(key);
                            
                            if (advertIt == neighborPtr->advertisedRoutes.end() || advertIt->second.pendingUpdate || advertIt->second.removePending)
                            {
                                neighborSpecificRoutes.emplace_back(route);
                            }
                        }
                        if (!neighborSpecificRoutes.empty())
                        {
                            setupReliablePacket(neighborPtr, address, EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(eigrp, address, neighborSpecificRoutes, removal), sequenceNumber);
                        }
                    }
                }
                else
                {
                    for (const auto& [address, neighborPtr] : neighbors)
                    {
                        setupReliablePacket(neighborPtr, address, EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(eigrp, address, filteredRoutes, removal), sequenceNumber);
                    }
                }
            }
        }
        while (!endOfTable && updateType == EigrpConfigs::UpdateType::FULL);
    }

    void EigrpInterface::sendQueryToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, RoutingTable::Eigrp* failedRoute)
    {
        // Validate neighbor
        if (!neighbor)
        {
            Logger::getInstance().warn() << "Invalid neighbor provided to sendUpdateToNeighbor." << std::endl;
            return;
        }

        // Increment sequence number for this route/query
        uint32_t currentSeqNum = getNextSequenceNumber();

        // Create the EIGRP Query Packet
        PacketInfo eigrpQueryPacketStructure;
        EigrpHeader eigrp;

        // Construct the Query option
        EigrpHeader::Option queryOption;
        queryOption.option = Variable::Eigrp::Option::internalRoute;
        queryOption.value = encodeQueryOption(failedRoute);
        queryOption.length = Functions::numToByte(static_cast<uint32_t>(queryOption.value.size()) + 4, 2);
        // Add the Query option to EIGRP header
        eigrp.options.emplace_back(queryOption);

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
        eigrpQueryPacketStructure.Layer4.push_back(eigrp);

        // Convert to raw packet ByteString
        if (currentInterface)
        {
            currentInterface->ipPacket->setIPHeader(eigrpQueryPacketStructure, neighbor->ipAddress, getConfigs().DSCP, 2, Variable::IP::eigrp);
        }

        // Store the packet for possible retransmission (relieable delivery)
        setupReliablePacket(neighbor, neighborIp, EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(eigrp, neighborIp, {failedRoute}, true), currentSeqNum);

        Logger::getInstance().info() << "Sent query to neighbor " << neighborIp.toHex() << " with sequence number " << currentSeqNum << std::endl;
    }

    ByteString EigrpInterface::encodeQueryOption(RoutingTable::Eigrp* route)
    {
        ByteString encoded;
        encoded += route->nextHop;
        encoded += ByteString(4, 0xFF);
        encoded += Functions::numToByte(route->bandwidth, 4);
        encoded += Functions::numToByte(route->mtu, 3);
        encoded += Functions::numToByte(route->hopCount, 1);
        encoded += Functions::numToByte(route->reliability, 1);
        encoded += Functions::numToByte(route->load, 1);
        encoded += Functions::numToByte(route->routeTag, 1);
        encoded += ByteString(1, 0x00);
        encoded += Functions::numToByte(route->mask, 1);
        encoded += Functions::compactNetworkAddress(route->network, route->mask);
        return encoded;
    }

    void EigrpInterface::sendReplyToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, RoutingTable::Eigrp* route)
    {
        // Validate neighbor
        if (!neighbor)
        {
            Logger::getInstance().warn() << "Invalid neighbor provided to sendUpdateToNeighbor." << std::endl;
            return;
        }

        // Increment sequence number for this route/reply
        uint32_t currentSeqNum = getNextSequenceNumber();

        // Create the EIGRP Reply Packet
        PacketInfo eigrpReplyPacketStructure;
        EigrpHeader eigrp;

        // Construct the Reply option with the route information
        EigrpHeader::Option replyOption;
        replyOption.option = Variable::Eigrp::Option::internalRoute;
        replyOption.value = encodeRouteOption(route);
        replyOption.length = Functions::numToByte(static_cast<uint32_t>(replyOption.value.size()) + 4, 2);
        eigrp.options.emplace_back(replyOption);

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
        eigrpReplyPacketStructure.Layer4.push_back(eigrp);

        // Enqueue for transmission
        if (currentInterface)
        {
            currentInterface->ipPacket->setIPHeader(eigrpReplyPacketStructure, neighborIp, getConfigs().DSCP, 2, Variable::IP::eigrp);
        }

        // Store the packet for possible retransmission (reliable delivery)
        setupReliablePacket(neighbor, neighborIp, EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(eigrp, neighborIp), currentSeqNum);

        Logger::getInstance().info() << "Sent reply to neighbor " << neighborIp.toHex() << " with sequence number " << currentSeqNum << std::endl;
    }

    ByteString EigrpInterface::encodeRouteOption(RoutingTable::Eigrp* route, bool removed)
    {
        ByteString encoded;
        encoded += route->nextHop;
        encoded += (removed ? ByteString (4, 0xff) : Functions::numToByte(route->delay, 4));
        encoded += Functions::numToByte(route->bandwidth, 4);
        encoded += Functions::numToByte(route->mtu, 3);
        encoded += Functions::numToByte(route->hopCount, 1);
        encoded += Functions::numToByte(route->reliability, 1);
        encoded += Functions::numToByte(route->load, 1);
        encoded += Functions::numToByte(route->routeTag, 1);
        encoded += ByteString(1, 0x00);
        encoded += Functions::numToByte(route->mask, 1);
        encoded += Functions::compactNetworkAddress(route->network, route->mask);
        return encoded;
    }

    ByteString EigrpInterface::encodeExternalRouteOption(RoutingTable::Eigrp* route, bool removed)
    {
        ByteString encoded;
        encoded += Functions::changeSize(route->originRouter, 4);
        encoded += Functions::numToByte(route->originAS, 4);
        encoded += Functions::numToByte(route->routeTag, 4);
        encoded += (removed ? ByteString(4, 0xff) : Functions::numToByte(route->delay, 4));
        encoded += Functions::numToByte(route->bandwidth, 4);
        encoded += Functions::numToByte(route->mtu, 3);
        encoded += Functions::numToByte(route->hopCount, 1);
        encoded += Functions::numToByte(route->reliability, 1);
        encoded += Functions::numToByte(route->load, 1);
        encoded += Functions::numToByte(route->mask, 1);
        encoded += Functions::compactNetworkAddress(route->network, route->mask);
        return encoded;
    }

    void EigrpInterface::advertiseSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute)
    {
        // Construct the route to advertise
        RoutingTable::Eigrp* summaryEigrpRoute = encodeSummaryRoute(summaryRoute);
        
        std::vector<ByteString> neighborsToNotify;
        bool hasMulticast = false;

        std::shared_lock<std::shared_mutex> lock(neighborMutex);

        // Iterate through neighbors
        if (getConfigs().mode == EigrpConfigs::CommunicationMode::UNICAST)
        {
            for (const auto& [address, neighbor] : neighbors)
            {
                neighborsToNotify.emplace_back(address);
            }
        }
        else
        {
            hasMulticast = true;
        }

        // Send unicast updates
        for (const auto& address : neighborsToNotify)
        {
            sendUpdateToNeighbor(neighbors[address], {summaryEigrpRoute}, EigrpConfigs::UpdateType::PARTIAL);
        }
        if (hasMulticast)
        {
            sendUpdateToNeighbor(nullptr, {summaryEigrpRoute}, EigrpConfigs::UpdateType::PARTIAL);
        }
    }

    void EigrpInterface::withdrawSummaryRoute(const ByteString& network, uint8_t mask)
    {    
        RoutingTable::Eigrp* withdrawRoute = new RoutingTable::Eigrp();
        withdrawRoute->network = network;
        withdrawRoute->mask = mask;
        withdrawRoute->metric = std::numeric_limits<uint32_t>::max(); // Indicate route is withdrawn

        std::vector<ByteString> unicastNeighbors;
        bool hasMulticast = false;

        std::shared_lock<std::shared_mutex> lock(neighborMutex);
        auto& mode = getConfigs().mode;
        for (const auto &[neighborIp, neighborInfo] : neighbors) 
        {
            std::lock_guard<std::shared_mutex> neighborInfoLock(neighborInfo->neighborDataMutex);
            ByteString key = withdrawRoute->network + "/" + std::to_string(withdrawRoute->mask);
            auto advertIt = neighborInfo->advertisedRoutes.find(key);

            if (advertIt != neighborInfo->advertisedRoutes.end())
            {
                advertIt->second.removePending = true;
            }

            if (mode == EigrpConfigs::CommunicationMode::UNICAST)
            {
                unicastNeighbors.emplace_back(neighborIp);
            }
            else if (mode == EigrpConfigs::CommunicationMode::MULTICAST)
            {
                hasMulticast = true;
            }
        }

        // Send withdraw update to the neighbor
        for (const auto& address : unicastNeighbors)
        {
            sendUpdateToNeighbor(neighbors[address], {withdrawRoute}, EigrpConfigs::UpdateType::WITHDRAW);
        }
        if (hasMulticast)
        {
            sendUpdateToNeighbor(nullptr, {withdrawRoute}, EigrpConfigs::UpdateType::WITHDRAW);
        }
    }

    void EigrpInterface::handleStubRouteUpdates()
    {
        // Identify routes that should no longer be advertised
        std::vector<RoutingTable::Eigrp*> routesToWithdraw;
        std::shared_lock<std::shared_mutex> lock(neighborMutex);
        for (const auto& [address, neighbor] : neighbors)
        {
            std::shared_lock<std::shared_mutex> neighborDataLock(neighbor->neighborDataMutex);
            for (const auto& [routeKey, advertisedRoute] : neighbor->advertisedRoutes)
            {
                bool found = false;
                for (auto route : routesToWithdraw)
                {
                    if (route->network == advertisedRoute.route->network && route->mask == advertisedRoute.route->mask)
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
                    if (advertisedRoute.route->routeType == "connected" && eigrpProcess->advertiseConnected())
                        shouldAdvertise = true;
                    if (advertisedRoute.route->routeType == "static" && eigrpProcess->advertiseStatic())
                        shouldAdvertise = true;
                    if (advertisedRoute.route->routeType == "summary" && eigrpProcess->advertiseSummary())
                        shouldAdvertise = true;
                    if (advertisedRoute.route->routeType == "external" &&  eigrpProcess->advertiseRedistributed())
                        shouldAdvertise = true;
                }
                else
                {
                    shouldAdvertise = true;
                }

                if (!shouldAdvertise)
                {
                    // Prepare to withdraw this route
                    RoutingTable::Eigrp* withdrawRoute = advertisedRoute.route;
                    withdrawRoute->metric = std::numeric_limits<uint32_t>::max();
                    withdrawRoute->nextHop = ByteString(advertisedRoute.route->network.size(), 0xff);
                    withdrawRoute->routeType = "withdrawn";

                    routesToWithdraw.emplace_back(withdrawRoute);
                }
            }
        }
        
        if (!routesToWithdraw.empty())
        {
            // Withdraw routes to all neighbors
            auto& mode = getConfigs().mode;
            if (mode == EigrpConfigs::CommunicationMode::UNICAST)
            {
                for (const auto& [neighborIp, neighborInfo] : neighbors)
                {
                    if (!neighborInfo->isInit) continue;

                    // Send withdraw updates
                    sendUpdateToNeighbor(neighborInfo, routesToWithdraw, EigrpConfigs::UpdateType::WITHDRAW);
                }
            }
            else if (mode == EigrpConfigs::CommunicationMode::MULTICAST)
            {
                sendUpdateToNeighbor(nullptr, routesToWithdraw, EigrpConfigs::UpdateType::WITHDRAW);
            }
        }
    }

    void EigrpInterface::startHelloHelper()
    {
        if (helloTimerActive.exchange(true))
        {
            // Hello timer is already active
            return;
        }

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

        std::weak_ptr<Protocol::EigrpInterface> weakSelf = weak_from_this();
        helloTimerId = TimeManager::getInstance().addTimer(nextExpiration, [weakSelf]()
        {
            try
            {
                if (auto self = weakSelf.lock())
                {
                    auto& mode = self->getConfigs().mode;
                    if (mode == EigrpConfigs::CommunicationMode::MULTICAST)
                    {
                        self->sendHelloPacket();
                    }
                    else if (mode == EigrpConfigs::CommunicationMode::UNICAST)
                    {
                        std::lock_guard<std::shared_mutex> neighborLock(self->neighborMutex);
                        for (const auto& [address, neighbor] : self->neighbors)
                        {
                            self->sendHelloPacket(neighbor, true);
                        }
                    }
                }
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
            if (auto self = weakSelf.lock())
            {
                {
                    std::lock_guard<std::mutex> lock(self->helloTimerMutex);
                    self->helloTimerId = 0; // Clear timer ID after packet is sent
                    self->helloStartTime = std::chrono::steady_clock::now();
                    self->helloTimerActive = false;
                }
                self->startHello(); // Reschedule
            }

        });
        Logger::getInstance().info() << "Hello timer started with expiration at " << std::chrono::duration_cast<std::chrono::seconds>(nextExpiration.time_since_epoch()).count() << std::endl;
    }

    void EigrpInterface::sendHelloPacket(EigrpConfigs::NeighborInfo* neighbor, bool unicast, bool update, uint32_t sequenceNumber)
    {
        if (configs.isPassive)
        {
            Logger::getInstance().info() << "Interface is passive. Hello packet not sent." << std::endl;
            return;
        }

        ByteString targetIp;
        
        if (neighbor && unicast)
        {
            std::shared_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
            targetIp = neighbor->ipAddress;
        }
        else
        {
            targetIp = getMulticast();
        }

        PacketInfo eigrpHello;
        EigrpHeader eigrp;

        eigrpProcess->eigrpHello(eigrp, this, neighbor, targetIp, sequenceNumber, false, update);

        // Add stub flags
        if (eigrpProcess->isStub())
        {
            EigrpHeader::Option stubOption;
            stubOption.option = Variable::Eigrp::Option::stub;
            stubOption.value = encodeStubOption(eigrpProcess->getConfigs()->stubConfig);
            stubOption.length = Functions::numToByte(static_cast<uint32_t>(stubOption.value.size()) + 4, 2);
            eigrp.options.emplace_back(stubOption);
        }

        eigrpHello.Layer4.push_back(eigrp);

        if (currentInterface)
        {
            currentInterface->ipPacket->setIPHeader(eigrpHello, targetIp, getConfigs().DSCP, 2, Variable::IP::eigrp);
        }
    }

    void EigrpInterface::stopHello()
    {
        std::lock_guard<std::mutex> lock(helloTimerMutex);
        if (helloTimerId != 0)
        {
            TimeManager::getInstance().cancelTimer(helloTimerId); 
            helloTimerId = 0;
        }
        std::lock_guard<std::shared_mutex> neighborLock(neighborMutex);
        for (auto& [_, neighbor] : neighbors)
        {
            std::unique_lock<std::shared_mutex> dataLock(neighbor->neighborDataMutex);
            if (neighbor->holdTimerId != 0)
            {
                TimeManager::getInstance().cancelTimer(neighbor->holdTimerId);
                neighbor->holdTimerId = 0;
            }
        }
        helloTimerActive = false;
    }

    void EigrpInterface::startHoldTimer(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, uint16_t holdTime)
    {
        std::unique_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
        if (neighbor->holdTimerId != 0)
        {
            TimeManager::getInstance().cancelTimer(neighbor->holdTimerId);
        }

        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(holdTime);
        neighbor->holdTimerId = TimeManager::getInstance().addTimer(expirationTime, [this, neighbor, neighborIp]()
                                                                   { handleHoldTimeExpire(neighbor, neighborIp); });
    }

    void EigrpInterface::handleHoldTimeExpire(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp)
    {
        if (neighbor)
        {
            handleNeighborDown(neighbor, neighborIp);
        }
    }

    void EigrpInterface::startActiveTimer(RoutingTable::Eigrp* route)
    {
        auto key = route->network + "/" + std::to_string(route->mask);
        auto retryTime = std::chrono::steady_clock::now() + std::chrono::seconds(eigrpProcess->getConfigs()->activeTime / 2);
        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(eigrpProcess->getConfigs()->activeTime);

        // Schedule Active timer
        uint32_t activeTimerId = TimeManager::getInstance().addTimer(expirationTime, [this, route]() {
            handleActiveTimeExpire(route);
        });

        {
            std::lock_guard<std::mutex> lock(activeTimerMutex);
            activeTimers[key] = activeTimerId;
        }
    }

    void EigrpInterface::startSIATimer(RoutingTable::Eigrp* route)
    {
        ByteString queryKey = route->network + "/" + std::to_string(route->mask);

        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(eigrpProcess->getConfigs()->stuckInActiveTime);

        // Schedule SIA-Query timer
        uint32_t siaTimerId = TimeManager::getInstance().addTimer(expirationTime, [this, route, queryKey]()
        {
            handleSIATimeout(route, queryKey);
        });

        {
            std::lock_guard<std::mutex> lock(activeTimerMutex);
            siaTimers[queryKey] = siaTimerId;
        }
    }

    void EigrpInterface::handleSIATimeout(RoutingTable::Eigrp* route, const ByteString& queryKey)
    {
        // Check if the query is still pending
        if (eigrpProcess->outstandingReplies.find(queryKey) == eigrpProcess->outstandingReplies.end()) return;

        EigrpConfigs::ActiveRoute& queryInfo = eigrpProcess->outstandingReplies[queryKey];

        // Send SIA query to unresponsive neighbors
        for (auto& [_, interface] : eigrpProcess->eigrpInterfaceList)
        {
            for (auto& neighborIp : queryInfo.pendingReplies)
            {
                if (interface->neighbors.find(neighborIp) != interface->neighbors.end())
                {
                    interface->sendQueryToNeighbor(interface->neighbors[neighborIp], neighborIp, route);
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(activeTimerMutex);
            auto timerId = siaTimers.find(queryKey);

            if (timerId != siaTimers.end())
            {
                siaTimers.erase(queryKey);
            }
        }
    }

    void EigrpInterface::handleActiveTimeExpire(RoutingTable::Eigrp* route)
    {
        ByteString queryKey = route->network + "/" + std::to_string(route->mask);

        // Check if we still have outstanding replies
        if (eigrpProcess->outstandingReplies.find(queryKey) != eigrpProcess->outstandingReplies.end())
        {
            EigrpConfigs::ActiveRoute& queryInfo = eigrpProcess->outstandingReplies[queryKey];

            // Remove unresponsive neighbors
            for (const auto& neighborIp : queryInfo.pendingReplies)
            {
                for (auto& [_, interface] : eigrpProcess->eigrpInterfaceList)
                {
                    if (interface->neighbors.find(neighborIp) != interface->neighbors.end())
                    {
                        Logger::getInstance().error() << "Neighbor " << neighborIp.toHex() << " did not respons. Removing from EIGRP." << std::endl;
                        handleNeighborDown(interface->neighbors[neighborIp], neighborIp);
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(activeTimerMutex);
                // Remove query from tracking
                eigrpProcess->outstandingReplies.erase(queryKey);

                auto timerId = activeTimers.find(queryKey);
                if (timerId != activeTimers.end())
                {
                    activeTimers.erase(queryKey);
                }
            }
        }
    }

    void EigrpInterface::cancelActiveTimer(const ByteString &destination, uint8_t mask)
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

    void EigrpInterface::cancelSIATimer(const ByteString& destination, uint8_t mask)
    {
        std::lock_guard<std::mutex> lock(activeTimerMutex);
        ByteString key = destination + "/" + std::to_string(mask);

        auto it = siaTimers.find(key);
        if (it != siaTimers.end())
        {
            TimeManager::getInstance().cancelTimer(it->second);
            siaTimers.erase(it);
        }
    }

    uint32_t EigrpInterface::startRetransmissionTimer(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, const uint32_t& sequenceNumber, double timeout)
    {
        // Validate neighbor and timeout
        if (timeout <= 0.0 || !neighbor)
        {
            Logger::getInstance().error() << "Invalid timeout value or neighbor for retransmission timer." << std::endl;
            return 0;
        }
        
        {
            // Cancel any existing retransmission timers for this sequence number
            std::shared_lock<std::shared_mutex> lock(neighbor->reliableMutex);
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
        //std::weak_ptr<EigrpInterface> weakSelf = shared_from_this();

        // Schedule a retransmission timer
        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(timeout * 1000));
        uint32_t timerId = TimeManager::getInstance().addTimer(expirationTime, [this, neighbor, neighborIp, sequenceNumber]()
        {
            if (true)
            {
                handleRetransmissionTimeout(neighbor, neighborIp, sequenceNumber);
            }
            else
            {
                Logger::getInstance().warn() << "EigrpInterface object no longer exists. Cannot handle retransmission timeout." << std::endl;
            }
        });

        // Update the timer ID in ReliablePacketInfo
        {
            std::unique_lock<std::shared_mutex> lock(neighbor->reliableMutex);
            neighbor->reliablePackets[sequenceNumber].timerId = timerId;
        }

        return timerId;
    }

    void EigrpInterface::handleRetransmissionTimeout(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, const uint32_t& sequenceNumber)
    {
        // Validate neighbor
        if (!neighbor) return;


        // Retrieve the packet information under a shared lock
        EigrpConfigs::NeighborInfo::ReliablePacketInfo pktInfoCopy;
        {
            std::unique_lock<std::shared_mutex> lock(neighbor->reliableMutex);
            auto pktIt = neighbor->reliablePackets.find(sequenceNumber);
            if (pktIt == neighbor->reliablePackets.end()) return;
            pktInfoCopy = pktIt->second; // Copy the data for safe access
        }

        // Handle retransmission limit
        if (pktInfoCopy.retransmissionCount >= MAX_RETRANSMISSIONS)
        {
            if (pktInfoCopy.timerId != 0)
            {
                TimeManager::getInstance().cancelTimer(pktInfoCopy.timerId);
            }
            handleNeighborDown(neighbor, neighborIp);
            return;
        }

        // Resend the packet
        PacketInfo retransmissionPacket;
        {
            // Add ACK if there are pensing acknowledgements
            if (!neighbor->pendingAcks.empty())
            {
                pktInfoCopy.packet.eigrp.ack = Functions::numToByte(neighbor->pendingAcks.front(), 4);
                neighbor->pendingAcks.erase(neighbor->pendingAcks.begin());
            }

            // Construct and send the retransmission packet
            retransmissionPacket.Layer4.push_back(pktInfoCopy.packet.eigrp);
            if (currentInterface)
            {
                currentInterface->ipPacket->setIPHeader(retransmissionPacket, pktInfoCopy.packet.destination, getConfigs().DSCP, 2, Variable::IP::eigrp);
            }
        }

        // Increment retransmission timer safely
        uint32_t newTimerId;
        {
            std::lock_guard<std::shared_mutex> dataLock(neighbor->reliableMutex);
            auto pktIt = neighbor->reliablePackets.find(sequenceNumber);
            if (pktIt != neighbor->reliablePackets.end())
            {
                pktIt->second.retransmissionCount += 1;
                pktIt->second.sendTime = std::chrono::steady_clock::now(); // Update the send time.
            }
        }

        // Restart the retransmission timer
        {
            std::unique_lock<std::shared_mutex> dataLock(neighbor->reliableMutex);
            neighbor->rto = std::min(neighbor->rto * 2.0, 60.0);
            auto pktIt = neighbor->reliablePackets.find(sequenceNumber);
            if (pktIt != neighbor->reliablePackets.end())
            {
                dataLock.unlock();
                newTimerId = startRetransmissionTimer(neighbor, neighborIp, sequenceNumber, neighbor->rto);
                dataLock.lock();
                pktIt->second.timerId = newTimerId;
            }
        }
    }

    RoutingTable::Eigrp* EigrpInterface::decodeRoute(const ByteString& value, bool external, bool summary)
    {
        RoutingTable::Eigrp* route = new RoutingTable::Eigrp();
        size_t start = 0;
        
        try
        {
            // Parse next hop based on address family
            if (eigrpProcess->getAddressFamily() == AddressFamily::IPv4)
            {
                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for IPv4 next Hop");
                route->nextHop = value.substr(start, 4);
                start += 4;
            }
            else if (eigrpProcess->getAddressFamily() == AddressFamily::IPv6)
            {
                if (value.size() < start + 16) throw std::runtime_error("Insufficient date for IPv6 next Hop");
                route->nextHop = value.substr(start, 16);
                start += 16;
            }
            else
            {
                throw std::runtime_error("Unsupported AddressFamily");
            }

            if (!external) {
                // Internal Route Parsing
                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Delay.");
                route->delay = Functions::byteToNum(value.substr(start, 4));
                start += 4;

                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Bandwidth.");
                route->bandwidth = Functions::byteToNum(value.substr(start, 4));
                start += 4;

                if (value.size() < start + 3) throw std::runtime_error("Insufficient data for MTU.");
                route->mtu = static_cast<uint16_t>(Functions::byteToNum(value.substr(start, 3)));
                start += 3;
    
                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Hop Count.");
                route->hopCount = static_cast<uint8_t>(Functions::byteToNum(value.substr(start, 1)));
                start += 1;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Reliability.");
                route->reliability = static_cast<uint8_t>(Functions::byteToNum(value.substr(start, 1)));
                start += 1;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Load.");
                route->load = static_cast<uint8_t>(Functions::byteToNum(value.substr(start, 1)));
                start += 1;

                if (value.size() < start + 2) throw std::runtime_error("Insufficient data for Route Tag.");
                route->routeTag = Functions::byteToNum(value.substr(start, 1));
                start += 2;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Mask.");
                route->mask = static_cast<uint8_t>(Functions::byteToNum(value.substr(start, 1)));
                start += 1;

                if (value.size() < start + (route->mask + 7) / 8) throw std::runtime_error("Insufficient data for Network Address.");
                route->network = value.substr(start);
                start += route->mask;
                route->routeType = summary ? "summary" : "internal";
            }
            else 
            {
                // External Route Parsing
                if (eigrpProcess->getAddressFamily() == AddressFamily::IPv4) 
                {
                    if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Origin Router (IPv4).");
                    route->originRouter = value.substr(start, 4);
                    start += 4;
                }
                else if (eigrpProcess->getAddressFamily() == AddressFamily::IPv6) 
                {
                    if (value.size() < start + 16) throw std::runtime_error("Insufficient data for Origin Router (IPv6).");
                    route->originRouter = value.substr(start, 16);
                    start += 16;
                }

                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Origin AS.");
                route->originAS = Functions::byteToNum(value.substr(start, 4));
                start += 4;

                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Route Tag (External).");
                route->routeTag = Functions::byteToNum(value.substr(start, 4));
                start += 4;

                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Extended Metric.");
                route->extendedMetric = Functions::byteToNum(value.substr(start, 4));
                start += 4;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Extended ID.");
                route->extendedId = Functions::byteToNum(value.substr(start, 1));
                start += 1;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Flags.");
                route->flags = value.substr(start, 1);
                start += 1;

                // Parsing additional fields if necessary...
                // Ensure all fields are parsed based on EIGRP specifications

                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Delay (External).");
                route->delay = Functions::byteToNum(value.substr(start, 4));
                start += 4;

                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Bandwidth (External).");
                route->bandwidth = Functions::byteToNum(value.substr(start, 4));
                start += 4;

                if (value.size() < start + 3) throw std::runtime_error("Insufficient data for MTU (External).");
                route->mtu = static_cast<uint16_t>(Functions::byteToNum(value.substr(start, 3)));
                start += 3;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Hop Count (External).");
                route->hopCount = static_cast<uint8_t>(Functions::byteToNum(value.substr(start, 1)));
                start += 1;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Reliability (External).");
                route->reliability = static_cast<uint8_t>(Functions::byteToNum(value.substr(start, 1)));
                start += 1;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Load (External).");
                route->load = static_cast<uint8_t>(Functions::byteToNum(value.substr(start, 1)));
                start += 1;

                if (value.size() < start + 1) throw std::runtime_error("Insufficient data for Mask (External).");
                route->mask = static_cast<uint8_t>(Functions::byteToNum(value.substr(start, 1)));
                start += 1;

                if (value.size() < start + (route->mask + 7) / 8) throw std::runtime_error("Insufficient data for Network Address (External).");
                route->network = value.substr(start);
                start += route->mask;

                route->routeType = "external";
            }

            // Pad network address to standard length
            size_t standardLength = (eigrpProcess->getAddressFamily() == AddressFamily::IPv4) ? 4 : 16;
            if (route->network.size() < standardLength) 
            {
                route->network += ByteString(standardLength - route->network.size(), '\x00');
            }

            // Calculate Feasible Distance and Composite Metric
            uint32_t neighborRD = eigrpProcess->calculateMetric(route->bandwidth, route->load, route->delay, route->reliability);
            route->reportedDistance = neighborRD;
    
            // Calculate FD = Local Link Cost + RD
            uint32_t localLinkCost = calculateLocalLinkCost();
            route->feasibleDistance = localLinkCost + route->reportedDistance;
    
            // Calculate the composite metric for internal use
            route->metric = localLinkCost;

            // Set the administrative distance
            route->adminDistance = (route->routeType == "internal" || route->routeType == "summary")
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

    uint32_t EigrpInterface::calculateLocalLinkCost()
    {
        std::shared_lock<std::shared_mutex> lock(currentInterfaceInfo->ipMutex);
        uint32_t bandwidthMetric = (eigrpProcess->getConfigs()->wideMetric) / currentInterfaceInfo->bandwidth;
        uint32_t delayMetric = currentInterfaceInfo->delay / 10;

        uint32_t loadMetric = 0;
        if (eigrpProcess->getConfigs()->kvalue.k2_Load != 0 && (256.0 - configs.load) != 0)
        {
            loadMetric = ((eigrpProcess->getConfigs()->kvalue.k2_Load) * configs.load) / (256 - configs.load);
        }

        // Calculate link cost using K-values
        uint32_t linkCost = (eigrpProcess->getConfigs()->kvalue.k1_Bandwidth * bandwidthMetric) +
                          loadMetric +
                          (eigrpProcess->getConfigs()->kvalue.k3_Delay * delayMetric);

        // Apply scaling factor and reliability
        uint32_t reliabilitySum = configs.reliability + eigrpProcess->getConfigs()->kvalue.k4_Reliability;
        if (reliabilitySum > 0 && eigrpProcess->getConfigs()->kvalue.k5_MTU != 0)
        {
            linkCost *= (eigrpProcess->getConfigs()->kvalue.k5_MTU) / reliabilitySum;
        }

        return linkCost;
    }

    void EigrpInterface::updateRoutingTable(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, const std::vector<RoutingTable::Eigrp*>& routes, bool init)
    {
        // Validate neighbor
        if (!neighbor)
        {
            return; // invalid neighbor
        }

        std::vector<RoutingTable::Eigrp*> updatedRoutes;

        for (const auto& route : routes)
        {
            // Access the topology table and update it with new routes
            TopologyTable::RouteInfo routeInfo;
            routeInfo.feasibleDistance = route->feasibleDistance;
            routeInfo.reportedDistance = route->reportedDistance;
            {
                std::shared_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
                routeInfo.nextHop = neighbor->ipAddress;
            }
            routeInfo.hopCount = route->hopCount;
            routeInfo.isSuccessor = false;
            routeInfo.isFeasibleSuccessor = false;

            // Add or update the route in the topology table
            eigrpProcess->topologyTable->addOrUpdateRoute(neighborIp, route->network, route->mask, routeInfo);

            // Fetch the updated topology table
            auto bestRouteEntry = eigrpProcess->topologyTable->getEntryForRoute(route->network);
            if (!bestRouteEntry)
            {
                Logger::getInstance().warn() << "Failed to retrieve topology entry for network: " << route->network.toHex() << std::endl;
                continue;
            }

            auto successorIt = std::find_if(
                bestRouteEntry->routesByNeighbor.begin(),
                bestRouteEntry->routesByNeighbor.end(),
                [](const auto& pair) {return pair.second.isSuccessor;});

            if (successorIt == bestRouteEntry->routesByNeighbor.end())
            {
                Logger::getInstance().debug() << "No successors found for route: " << route->network.toHex() << std::endl;
                continue;
            }

            // Prepare the administrative distance based on the route type
            RoutingTable::Eigrp* newRoute = route;
            newRoute->nextHop = successorIt->second.nextHop;
            newRoute->feasibleDistance = successorIt->second.feasibleDistance;

            // Set administrative distance  based on route type
            if (newRoute->routeType == "internal")
            {
                newRoute->adminDistance = eigrpProcess->getConfigs()->adminDistance;
            }
            else if (newRoute->routeType == "external")
            {
                newRoute->adminDistance = eigrpProcess->getConfigs()->externalAdminDistance;
            }
            else if  (newRoute->routeSource == "summary")
            {
                newRoute->adminDistance = eigrpProcess->getConfigs()->summaryAdminDistance;
            }
            else if (newRoute->routeSource == "default")
            {
                newRoute->adminDistance = eigrpProcess->getConfigs()->defaultAdminDistance;
            }
            else 
            {
                Logger::getInstance().error() << "Unknown route type: " << route->routeType << std::endl;
                continue; // Skip unknown route type
            }

            // Check for existing routes and determine if an update is needed
            auto existingRoute = RoutingTable::getInstance().getEigrpRoute(route->network, route->mask, eigrpProcess->getAddressFamily(), eigrpProcess->getAsNumber());
            bool routeChange = !existingRoute.has_value() || (existingRoute.value()->feasibleDistance != newRoute->feasibleDistance);

            if (routeChange)
            {
                if (existingRoute.has_value())
                {
                    Logger::getInstance().debug() << "Route metric better with: " << newRoute->metric << " Replacing the old metric of: " << existingRoute.value()->metric << std::endl;
                }
                else
                {
                    Logger::getInstance().debug() << "New route detected, route: " << newRoute->network.toHex() << " being added to the routing table" << std::endl;
                }
                
                // Update the global EIGRP table
                RoutingTable::getInstance().addEigrp(newRoute, eigrpProcess->getAddressFamily(), eigrpProcess->getAsNumber());

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

    void EigrpInterface::handleNeighborDown(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp)
    {
        if (!neighbor) return;
        
        // Cancel any pending timers
        if (neighbor->holdTimerId != 0)
        {
            TimeManager::getInstance().cancelTimer(neighbor->holdTimerId);
        }
        for (const auto &timerEntry : neighbor->retransmissionTimers)
        {
            TimeManager::getInstance().cancelTimer(timerEntry.second);
        }

        {
            std::unique_lock<std::shared_mutex> lock(neighbor->reliableMutex);
            neighbor->retransmissionTimers.clear();
            neighbor->reliablePackets.clear();
            neighbor->sequenceList.clear();
        }

        // Removed Routes
        std::vector<RoutingTable::Eigrp*> removedRoutes;
        eigrpProcess->topologyTable->removeRoutesFromNeighbor(neighborIp);

        // Check if this is the last neighbor
        if (neighbors.size() == 1)
        {
            auto routeIt = RoutingTable::getInstance().getEigrpRoute(Functions::computeNetworkAddress(getConfigs().interfaceAddress, getConfigs().interfaceMask), getConfigs().interfaceMask, eigrpProcess->getAddressFamily(), eigrpProcess->getAsNumber());
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
                    RoutingTable::Eigrp* removedRoute = new RoutingTable::Eigrp;
                    removedRoute->network = destination;
                    removedRoute->mask = entry->prefixLength;
                    removedRoute->nextHop = ByteString(destination.size(), 0xff);
                    removedRoute->metric = std::numeric_limits<uint32_t>::max();
                    removedRoute->routeType = "internal";

                    // Mark the route as pending removal in advertisedRoutes for all neighbors
                    for (auto &[otherNeighborIp, otherNeighborInfo] : neighbors)
                    {
                        ByteString key = removedRoute->network + "/" + std::to_string(removedRoute->mask);
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

    void EigrpInterface::handleNeighborRestart(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp)
    {
        // Validate neighbor
        if (!neighbor)
        {
            return; //neighbor does not exist
        }

        // Cancel existing timers
        neighbor->clearTimers();

        {
            std::unique_lock<std::shared_mutex> lock(neighbor->reliableMutex);
            neighbor->reliablePackets.clear();
            neighbor->sequenceList.clear();
            eigrpProcess->topologyTable->removeRoutesFromNeighbor(neighborIp);
        }

        // Reinitialize neighbor state
        {
            std::lock_guard<std::mutex> lock(neighbor->initializationMutex);
            neighbor->neighborState = EigrpConfigs::NeighborState::DOWN;
            neighbor->initComplete = false;
        }
        changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::INIT);
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
            RoutingTable::Eigrp* newRoute = new RoutingTable::Eigrp;
            newRoute->network = destination;
            newRoute->mask = entry->prefixLength;
            newRoute->nextHop = successorIt->second.nextHop;
            newRoute->metric = successorIt->second.feasibleDistance;

            // Update the global routing table
            RoutingTable::getInstance().addEigrp(newRoute, eigrpProcess->getAddressFamily(), eigrpProcess->getAsNumber());

            eigrpProcess->notifyRoutingChange({newRoute}, /*isRemoval*/ false);
        }
        else
        {
            RoutingTable::getInstance().removeEigrp(destination, entry->prefixLength, eigrpProcess->getAddressFamily(), eigrpProcess->getAsNumber());
        }
    }

    double EigrpInterface::calculateRTT(EigrpConfigs::NeighborInfo* neighbor, uint32_t sequenceNumber)
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

    void EigrpInterface::updateRTTEstimate(EigrpConfigs::NeighborInfo* neighbor, uint32_t sequenceNumber)
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

    uint32_t EigrpInterface::getNextSequenceNumber() 
    {
        return nextSequenceNumber.fetch_add(1, std::memory_order_relaxed);
    }

    void EigrpInterface::setupReliablePacket(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, const EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet& packet, uint32_t sequenceNum)
    {
        if (!neighbor->processAcks) return;

        {
            std::shared_lock<std::shared_mutex> lock(neighbor->reliableMutex);
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
        pktInfo.timerId = startRetransmissionTimer(neighbor, neighborIp, sequenceNum, neighbor->rto);

        // Store the packet
        {
            std::unique_lock<std::shared_mutex> lock(neighbor->reliableMutex);
            neighbor->reliablePackets[sequenceNum] = pktInfo;
        }

        Logger::getInstance().debug() << "Reliable packet setup for neighbor " << neighbor->ipAddress.toHex() << " with sequence number " << sequenceNum << std::endl;
    }

    RoutingTable::Eigrp* EigrpInterface::encodeSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute)
    {
        std::shared_lock<std::shared_mutex> lock(currentInterfaceInfo->ipMutex);
        RoutingTable::Eigrp* route = new RoutingTable::Eigrp();
        route->nextHop = ByteString(summaryRoute.summary->network.size(), '\x00');
        route->bandwidth = (10000000 / currentInterfaceInfo->bandwidth) * 256;
        route->delay = (currentInterfaceInfo->delay / 10) * 256;
        route->mtu = currentInterfaceInfo->mtu;
        route->hopCount = 0;
        route->reliability = 255;
        route->load = configs.load;
        route->routeTag = 0;
        route->mask = summaryRoute.summary->mask;
        route->network = summaryRoute.summary->network;
        route->routeType = "summary";

        return route;
    }

    ByteString EigrpInterface::encodeStubOption(const EigrpConfigs::StubConfig& stub)
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
        
        return Functions::binToByte(binStub, 2);
    }

    bool EigrpInterface::isNeighborAuthenticated(EigrpConfigs::NeighborInfo* neighbor)
    {
        return neighbor && neighbor->authenticationEnabled;
    }

    void EigrpInterface::configureAuthentication(EigrpConfigs::NeighborInfo* neighbor, uint8_t keyId, const ByteString& key, bool enable)
    {
        // Validate neighbor
        if (!neighbor)
        {
            return; // Neighbor does not exist
        }
        
        neighbor->authKeyId = keyId;
        neighbor->authKey = key;
        neighbor->authenticationEnabled = enable;
    }

    ByteString EigrpInterface::serializeEigrpHeader(const EigrpHeader& eigrp, bool exclusiveAuthTLV)
    {
        ByteString serialized;
        serialized += eigrp.version;
        serialized += eigrp.opcode;
        serialized += eigrp.checksum;
        serialized += Functions::binToByte(ByteString("0000000000000000000000000000") + eigrp.flags.endOfTable + eigrp.flags.restart + eigrp.flags.conditionalRecieve + eigrp.flags.init);
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

    EigrpHeader::Option EigrpInterface::generateAuthenticatedTLV(const EigrpHeader& eigrp, EigrpConfigs::NeighborInfo* neighbor)
    {
        EigrpHeader::Option authTLV;
        if (!neighbor->authenticationEnabled || neighbor->authKey.empty())
        {
            return authTLV;
        }

        authTLV.option = Variable::Eigrp::Option::authentication;

        // Key ID (1 byte) + HMAC placeholder (16 bytes of zeros)
        const size_t hmacLength = (neighbor->authType == EigrpConfigs::AuthType::MD5) ? MD5_DIGEST_LENGTH : SHA_DIGEST_LENGTH;
        authTLV.value = Functions::numToByte(neighbor->authKeyId, 1) + ByteString(hmacLength, 0x00);
        authTLV.length = Functions::numToByte(static_cast<uint32_t>(authTLV.value.size()) + 4, 2);

        // Temporarily add the zeroed Authentication TLV to the EIGRP header
        EigrpHeader tempEigrp = eigrp;
        tempEigrp.options.emplace_back(authTLV);
        ByteString serializedHeader = serializeEigrpHeader(tempEigrp, /*exclusiveAuthTLV=*/false);

        // Compute HMAC-MD5 over the serialed header
        ByteString computedHMAC;
        if (neighbor->authType == EigrpConfigs::AuthType::MD5)
        {
            computedHMAC = Authentication::generateMD5(serializedHeader, neighbor->authKey);
        }
        else if (neighbor->authType == EigrpConfigs::AuthType::SHA1)
        {
            computedHMAC = Authentication::generateHMAC(serializedHeader, neighbor->authKey, "SHA1");
        }

        // Replace the zeroed HMAC with the real computed HMAC
        authTLV.value.replace(1, MD5_DIGEST_LENGTH, computedHMAC);

        return authTLV;
    }

    void EigrpInterface::setPassive(bool passive)
    {
        getConfigs().isPassive = passive;
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

    void EigrpInterface::addNeighbor(const ByteString& ipAddress, const ByteString& macAddress)
    {
        // Add neighbor only if it doesn't already exist
        std::shared_lock<std::shared_mutex> readLock(neighborMutex);

        auto it = neighbors.find(ipAddress);
        if (it == neighbors.end()) // Double check
        {
            auto* neighbor = new EigrpConfigs::NeighborInfo();
            neighbor->ipAddress = ipAddress;
            neighbor->macAddress = macAddress;
            neighbors[ipAddress] = neighbor;
        }
    }

    std::optional<EigrpConfigs::NeighborInfo*> EigrpInterface::getNeighborInfo(const ByteString& neighborIp)
    {
        auto it = neighbors.find(neighborIp);
        if (it != neighbors.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    void EigrpInterface::resolveMacAddress(EigrpConfigs::NeighborInfo* neighbor)
    {
        std::lock_guard<std::shared_mutex> lock(neighbor->macMutex);
        if (eigrpProcess->getAddressFamily() == AddressFamily::IPv6 && neighbor->ipAddress.size() == 16)
        {
            // Get neighbor mac Address
            // TODO Implement IPv6 neighbormac resolution
            // NEEDS IMPLEMENTATION
        }
        if (eigrpProcess->getAddressFamily() == AddressFamily::IPv4 && neighbor->ipAddress.size() == 4)
        {
            // Get neighbor mac Address
            if (!neighbor->hasMac || neighbor->macAddress.empty())
            {
                auto mac = RoutingTable::getInstance().ArpLookup(neighbor->ipAddress);
                if (mac.has_value())
                {
                    neighbor->macAddress = mac.value().mac;
                    neighbor->hasMac = true;
                }
                else
                {
                    currentInterface->arp->sendRequest(neighbor->ipAddress);
                }
            }
        }
    }

    ByteString EigrpInterface::getMulticast()
    {
        return (eigrpProcess->getAddressFamily() == AddressFamily::IPv4) ? Variable::Multicast::Eigrp::address : Variable::Multicast::Eigrp::addressv6;
    }

    
    bool EigrpInterface::isRouteAdvertised(ByteString& network, uint8_t mask)
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
                removeSummaryRoute(route.summary->network, route.summary->mask);
            }
            Logger::getInstance().info() << "Cleared auto-summarized routes." << std::endl;
        }

        // Clear network configurations
        configs.networks.clear();
        Logger::getInstance().info() << "Cleared all Classic-configured networks." << std::endl;
    }

#pragma endregion

#pragma region NamedEigrp

    NamedEigrp::NamedEigrp(uint32_t& as, AddressFamily af, const ByteString& name, bool multicast)
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
        eigrpProcess = process;
    }

    TopologyTable::~TopologyTable() {}

    std::vector<RoutingTable::Eigrp*> TopologyTable::getSuccessorsForRoutingTable()
    {
        std::lock_guard<std::mutex> lock(tableMutex);

        std::vector<RoutingTable::Eigrp*> bestRoutes;
        for (const auto& [destination, entry] : topologyEntries)
        {
            if (!entry->successors.empty())
            {
                const ByteString& bestNeighbor = entry->successors.front(); // Get the best successor
                const auto& bestRoute = entry->routesByNeighbor.at(bestNeighbor);

                // Construct a routing table entry
                RoutingTable::Eigrp* routingEntry = new RoutingTable::Eigrp();
                routingEntry->network = entry->destination;
                routingEntry->mask = entry->prefixLength;
                routingEntry->nextHop = bestNeighbor;
                routingEntry->metric = bestRoute.feasibleDistance;
                routingEntry->routeType = "internal";

                bestRoutes.push_back(routingEntry);
            }
        }

        return bestRoutes;
    }

                void TopologyTable::addOrUpdateRoute(const ByteString& neighborIp, const ByteString& destination, uint8_t prefixLength, const RouteInfo &routeInfo)
    {
        std::lock_guard<std::mutex> lock(tableMutex);

        // Create or update the topology table entry
        auto& entry = topologyEntries[destination];
        if (!entry)
        {
            entry = new TopologyEntry();
            entry->destination = destination;
            entry->prefixLength = prefixLength;
        }

        {
            entry->routesByNeighbor[neighborIp] = routeInfo;
            entry->routesByNeighbor[neighborIp].lastUpdate = std::chrono::steady_clock::now();
        }

        // Recalculate successors and feasible successors
        updateSuccessorAndFeasibleSuccessors(entry);
    }

    void TopologyTable::updateSuccessorAndFeasibleSuccessors(TopologyEntry* entry)
    {
        // Initialize the best feasible distance (FD) and the best administrative distance
        uint32_t bestFD = std::numeric_limits<uint32_t>::max();
        uint8_t bestAD = std::numeric_limits<uint8_t>::max();

        // Clear current successors and feasible successor list
        entry->successors.clear();
        entry->feasibleSuccessors.clear();

        // Step 1: Find the best feasible distance (FD) and lowest administrative distance (AD)
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

            // If the route is a feasible successor, add it to the feasible successors list
            if (route.isFeasibleSuccessor)
            {
                entry->feasibleSuccessors.push_back(neighbor);
            }

            // Check if the route meets the Successor Condition for being a successor
            bool withinVariance = (route.feasibleDistance <= bestFD * eigrpProcess->getConfigs()->variance);

            // Route must have FD within the variance threshold and AD equal to best AD to be a successor
            if (withinVariance && route.adminDistance == bestAD)
            {
                route.isSuccessor = true; // Mark as a successor
                entry->successors.push_back(neighbor); // Add to successor list
            }
            else
            {
                route.isSuccessor = false; // Mark false if not a successor
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

    void TopologyTable::removeRoutesFromNeighbor(const ByteString& neighborIp)
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

    TopologyTable::TopologyEntry* TopologyTable::getEntryForRoute(const ByteString& destination)
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

    std::optional<TopologyTable::RouteInfo> TopologyTable::findBestRoute(const ByteString &destination, uint8_t variance)
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
                // Prune if the age exceeds the stale threshold
                if (age < staleThreshold) // Check against the stale threshold
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
    
    void TopologyTable::handleNeighborDown(const ByteString& neighborIp)
    {
        {
            std::lock_guard<std::mutex> lock(tableMutex);

            for (auto &entry : topologyEntries)
            {
                entry.second->routesByNeighbor.erase(neighborIp); // Remove routes from this neighbor
            }
        }

        pruneStaleRoutes(); // Remove any empty destinations
    }
}

Protocol::Eigrp* currentEigrp;
Protocol::EigrpNamed* currentEigrpNamed;
std::map<uint32_t, Protocol::EigrpAutonomousSystem*> eigrpList;
std::map<std::string, Protocol::EigrpNamed*> namedEigrpList;


#pragma endregion
