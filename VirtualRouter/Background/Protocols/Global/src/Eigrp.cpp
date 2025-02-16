#include <Eigrp.h>
#include <Encapsulation.h>
#include <Interface.h>
#include <algorithm>
#include <Global.h>
#include <IPPacket.h>

// TODO Receive Queries

#pragma region Eigrp

namespace Protocol
{
    Eigrp::Eigrp(uint32_t& as, AddressFamily af, VirtualRouter* vrf) : routingInstance(vrf), addressFamily(af), asNumber(as)
    {
        initializeEigrp();
    }

    Eigrp::~Eigrp()
    {
        shutdown();
    }

    void Eigrp::initializeEigrp()
    {
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

        uint16_t holdTime = eigrpInt->getConfigs().holdTime.load(std::memory_order_relaxed);

        // Construct TLVs
        if (!ack && eigrpInt)
        {
            // Parameter TLV (K-values and Hold Time)
            EigrpHeader::Option paramTLV;
            paramTLV.option = Variable::Eigrp::Option::parameter;
            paramTLV.value = calculateParameters(holdTime);
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
        if (isStub() && eigrpInt)
        {
            std::shared_lock<std::shared_mutex> configLock(configs.configsMutex);
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
        if (neighbor && eigrpInt)
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

        {
            std::shared_lock<std::shared_mutex> configMutex(configs.configsMutex);
            for (const auto& network : configs.networks)
            {
                if (Functions::compareNetworkWithIp(network.ip, testIp, 32 - Functions::byteMaskToNum(network.mask)))
                {
                    return true;
                }
            }
        }

        return false; // No matches found
    }

    uint32_t Eigrp::calculateMetric(uint32_t bandwidth, uint8_t load, uint32_t delay, uint8_t reliability, uint8_t hopCount)
    {
        if (bandwidth == 0) return std::numeric_limits<uint32_t>::max();

        EigrpConfigs::KValue kvalue;
        {
            std::shared_lock<std::shared_mutex> configMutex(configs.configsMutex);
            kvalue = configs.kvalue;
        }

        // Calculate individual components of the metric
        uint32_t bandwidthMetric = configs.wideMetric.load(std::memory_order_relaxed) / bandwidth;
        uint32_t delayMetric = delay;
        uint32_t loadMetric = (kvalue.k2_Load * bandwidthMetric) / (256 - load);

        uint32_t compositeMetric = (kvalue.k1_Bandwidth * bandwidthMetric) +
                                 loadMetric +
                                 (kvalue.k3_Delay * delayMetric);

        // Account for K5 (optional scaling)
        if (kvalue.k5_MTU != 0 && (reliability + kvalue.k4_Reliability) > 0) {
            compositeMetric *= kvalue.k5_MTU / (reliability + kvalue.k4_Reliability);
        }

        // Final scaling for the metric
        compositeMetric = std::min(compositeMetric, 16777215U);
        return compositeMetric * 256; // Max metric value
    }

    uint32_t Eigrp::calculateLocalLinkCost(uint8_t load, uint32_t delay, uint8_t reliability)
    {
        uint32_t bandwidth;
        EigrpConfigs::KValue kvalue;
        {
            std::shared_lock<std::shared_mutex> lock(configs.configsMutex);
            kvalue = configs.kvalue;
        }
        bandwidth = configs.lowestBandwidth.load(std::memory_order_relaxed);

        uint32_t bandwidthMetric = (configs.wideMetric.load(std::memory_order_relaxed)) / bandwidth;
        uint32_t delayMetric = delay / 10;

        uint32_t loadMetric = 0;
        if (kvalue.k2_Load != 0 && (256.0 - load) != 0)
        {
            loadMetric = (kvalue.k2_Load * load) / (256 - load);
        }

        // Calculate link cost using K-values
        uint32_t linkCost = (kvalue.k1_Bandwidth * bandwidthMetric) +
                          loadMetric +
                          (kvalue.k3_Delay * delayMetric);

        // Apply scaling factor and reliability
        uint32_t reliabilitySum = reliability + kvalue.k4_Reliability;
        if (reliabilitySum > 0 && kvalue.k5_MTU != 0)
        {
            linkCost *= (kvalue.k5_MTU) / reliabilitySum;
        }

        return linkCost * 256;
    }

    EigrpInterface* Eigrp::addEigrpInterface(Interface* interface)
    {
        if (interface)
        {
            // Add the interface to eigrp even if its down
            IpInfo& interfaceInfo = interface->configs;
            float intID = interfaceInfo.id.load(std::memory_order_relaxed);
            ByteString ipv6Address;
            ByteString ipv4Address;
            EigrpInterface* instance;
            EigrpInterfaceInstance* interfaceInstance;
            InterfaceType type = interfaceInfo.interfaceType.load(std::memory_order_relaxed);
            
            {
                std::shared_lock<std::shared_mutex> lock(interfaceInfo.ipMutex);
                ipv4Address = interfaceInfo.ipv4.ipAddress;
                ipv6Address = interfaceInfo.ipv6.ipAddress;
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

            if (addressFamily == AddressFamily::IPv4 && !ipv4Address.empty())
            {
                instance = new EigrpInterface(*this, interface);
                interfaceInstance->IPv4 = instance;
                eigrpInterfaceList[type][intID] = instance;
                return instance;
            }
            else if (addressFamily == AddressFamily::IPv6 && !ipv6Address.empty())
            {
                instance = new EigrpInterface(*this, interface);
                interfaceInstance->IPv6 = instance;
                eigrpInterfaceList[type][intID] = instance;
                return instance;
            }
        }
        return nullptr;
    }

    void Eigrp::updateInterfaceList()
    {
        {
            std::unique_lock<std::shared_mutex> lock(interfaceMutex);
            // Validate existing interfaces
            forEachInterface([&](InterfaceType type, float id, EigrpInterface* eigrpInterface)
            {
                if (eigrpInterface && !eigrpInterface->currentInterface)
                {
                    delete eigrpInterfaceList[type][id];
                    eigrpInterfaceList[type][id] = nullptr;
                    eigrpInterfaceList[type].erase(id);
                }
            });
            
            //if (addressFamily != AddressFamily::IPv4 || !routingInstance) return;

            // Iterate through all interfaces
            routingInstance->forEachInterface([&](InterfaceType type, uint8_t index, Interface* interface) 
            {
                // Add the interface as existing
                if (interface && interface->Get())
                {
                    ByteString ipAddress;
                    bool ipv6Contained = false;

                    {
                        auto ipInfo = interface->Get();
                        std::shared_lock<std::shared_mutex> ipLock(ipInfo->ipMutex);
                        ipAddress = ipInfo->ipv4.ipAddress;
                        ipv6Contained = (addressFamily == AddressFamily::IPv6 && ipInfo->eigrp.ipv6AutonomousSystems.find(asNumber) != ipInfo->eigrp.ipv6AutonomousSystems.end());
                    }

                    // Test the address and add the interface if approved
                    if (testAddress(ipAddress) || ipv6Contained)
                    {
                        // Add interface to eigrp
                        auto eigrpInterfaceIt = interface->eigrpInterfaceList.find(asNumber);
                        if (eigrpInterfaceIt == interface->eigrpInterfaceList.end() || 
                            eigrpInterfaceList[type].find(index) == eigrpInterfaceList[type].end())
                        {
                            auto* newInterface = addEigrpInterface(interface);
                            if (newInterface)
                            {
                                return;
                            }

                            if (configs.autoSummarizationEnabled.load(std::memory_order_relaxed) && addressFamily == AddressFamily::IPv6)
                            {
                                auto majorNetwork = Functions::findClassfullNetwork(ipAddress);
                                uint8_t defaultMask = Functions::getDefaultMask(majorNetwork);

                                // Only summarize if the interface is in a different major network
                                if (!newInterface->isRouteSummarized(majorNetwork, defaultMask))
                                {
                                    newInterface->addSummaryRoute(majorNetwork, defaultMask, true);
                                }
                            }
                        }
                    }
                    else
                    {
                        // Check and remove interface from eigrp if no eigrp neig
                        auto intIt = eigrpInterfaceList[type].find(index);
                        
                        if (intIt != eigrpInterfaceList[type].end())
                        {
                            // Delete interface if no static neighbors are found.
                            delete eigrpInterfaceList[type][index];
                            eigrpInterfaceList[type][index] = nullptr;
                            eigrpInterfaceList[type].erase(index);
                        }
                    }
                }
                else
                {
                    // Remove shutdown interface
                    if (eigrpInterfaceList[type].find(index) != eigrpInterfaceList[type].end())
                    {
                        delete eigrpInterfaceList[type][index];
                        eigrpInterfaceList[type][index] = nullptr;
                        eigrpInterfaceList[type].erase(index);
                    }
                }
            });
        }
    }

    ByteString Eigrp::calculateParameters(uint16_t holdTime)
    {
        EigrpConfigs::KValue kvalue;
        {
            std::shared_lock<std::shared_mutex> lock(configs.configsMutex);
            kvalue = configs.kvalue;
        }

        ByteString params;
        params += Functions::numToByte(kvalue.k1_Bandwidth, 1);
        params += Functions::numToByte(kvalue.k2_Load, 1);
        params += Functions::numToByte(kvalue.k3_Delay, 1);
        params += Functions::numToByte(kvalue.k4_Reliability, 1);
        params += Functions::numToByte(kvalue.k5_MTU, 1);
        params += Functions::numToByte(kvalue.k6_Power, 1);
        params += Functions::numToByte(holdTime, 2);

        return params;
    }

    void Eigrp::updateRoutingTableForConnected(EigrpInterface* eigrpInterface)
    {
        std::vector<RoutingTable::Eigrp*> updatedRoutes{};
        std::vector<RoutingTable::Eigrp*> removedRoutes{};
        {
            if (!routingInstance) return;

            RoutingTable& routingTable = routingInstance->routingTable;
            std::vector<ByteString> connectedNetworks{};
            
            std::unordered_map<float, EigrpInterface*> currentEigrpInterface;
            // get Eigrp Interface list
            if (eigrpInterface)
            {
                auto ipInfo = eigrpInterface->currentInterface->Get();
                if (ipInfo)
                {
                    currentEigrpInterface[ipInfo->id.load(std::memory_order_relaxed)] = eigrpInterface;
                }
            }

            {
                auto updateRoutesForInterface([&](InterfaceType, float, EigrpInterface* eigrpInterfacePtr)
                {
                    if (!eigrpInterfacePtr || !eigrpInterfacePtr->currentInterface || !routingInstance) return;
                    auto interfaceInfo = eigrpInterfacePtr->currentInterface->Get();

                    if (interfaceInfo)
                    {
                        uint32_t eigrpBw = configs.lowestBandwidth.load(std::memory_order_relaxed);
                        uint16_t mtu = interfaceInfo->mtu.load(std::memory_order_relaxed);
                        uint8_t connectedMask;
                        ByteString connectedNetwork;

                        {
                            std::shared_lock<std::shared_mutex> ipLock(interfaceInfo->ipMutex);
                            if (addressFamily == AddressFamily::IPv4)
                            {
                                connectedNetwork = Functions::computeNetworkAddress(interfaceInfo->ipv4.ipAddress, interfaceInfo->ipv4.mask);
                                connectedMask = interfaceInfo->ipv4.mask;
                            }
                            else if (addressFamily == AddressFamily::IPv6)
                            {
                                if (interfaceInfo->ipv6.globalIpAddress.empty()) return;
                                connectedNetwork = Functions::computeNetworkAddress(interfaceInfo->ipv6.globalIpAddress, interfaceInfo->ipv6.mask);
                                connectedMask = interfaceInfo->ipv6.mask;
                            }
                            else return;
                        }

                        // Compute the connected network
                        connectedNetworks.emplace_back(connectedNetwork);

                        // Create EIGRP route entry
                        RoutingTable::Eigrp* connectedRoute = new RoutingTable::Eigrp();
                            connectedRoute->bandwidth = ( 10000000 / eigrpBw ) * 256;
                            connectedRoute->delay = 0;
                            connectedRoute->hopCount = 0;
                            connectedRoute->mtu = mtu;
                            connectedRoute->reliability = 255;
                            connectedRoute->load = configs.variance.load(std::memory_order_relaxed);
                            connectedRoute->network = connectedNetwork;
                            connectedRoute->mask = connectedMask;
                            connectedRoute->nextHop = (eigrpInterface && eigrpInterface->getConfigs().nextHopSelf.load(std::memory_order_relaxed)) ? eigrpInterface->getInterfaceIp() : ByteString(connectedNetwork.size(), '\x00'); // indicates directly connected
                            connectedRoute->routeType = "connected";

                        auto existingRoute = routingInstance->routingTable.getEigrpRoute(connectedNetwork, connectedRoute->mask, addressFamily, asNumber);

                        bool hasChanged = !existingRoute ||
                                        existingRoute->feasibleDistance != connectedRoute->feasibleDistance ||
                                        existingRoute->mask != connectedRoute->mask;

                        if (hasChanged)
                        {
                            // Insert into Routing Table
                            routingTable.addEigrp(connectedRoute, addressFamily, asNumber);
                            
                            // Advertise the connected route to eigrp neighbors
                            updatedRoutes.emplace_back(connectedRoute);
                        }
                        else
                        {
                            delete connectedRoute;
                        }
                    }
                    else
                    {

                        std::shared_lock<std::shared_mutex> ipLock(eigrpInterfacePtr->currentInterface->configs.ipMutex);
                        auto removalRoute = routingInstance->routingTable.getEigrpRoute(
                            addressFamily == AddressFamily::IPv4 ? eigrpInterfacePtr->currentInterface->configs.ipv4.ipAddress
                            : eigrpInterfacePtr->currentInterface->configs.ipv6.ipAddress, 
                            addressFamily == AddressFamily::IPv6 ? eigrpInterfacePtr->currentInterface->configs.ipv4.mask
                            : eigrpInterfacePtr->currentInterface->configs.ipv6.mask, addressFamily, asNumber);
                        if (removalRoute)
                        {
                            routingInstance->routingTable.removeEigrp(
                                addressFamily == AddressFamily::IPv4 ? eigrpInterfacePtr->currentInterface->configs.ipv4.ipAddress
                                : eigrpInterfacePtr->currentInterface->configs.ipv6.ipAddress, 
                                addressFamily == AddressFamily::IPv6 ? eigrpInterfacePtr->currentInterface->configs.ipv4.mask
                                : eigrpInterfacePtr->currentInterface->configs.ipv6.mask, addressFamily, asNumber);
                            removedRoutes.emplace_back(removalRoute);
                        }
                    }
                });

                {
                    std::shared_lock<std::shared_mutex> lock(interfaceMutex);
                    if (eigrpInterface)
                    {
                        updateRoutesForInterface(InterfaceType::UNDEFINED, 0.0, eigrpInterface);
                    }
                    forEachInterface(updateRoutesForInterface);
                }
            }

            // Remove any routes that are no longer exist
            for (const auto& route : routingInstance->routingTable.getAllEigrpRoutes(addressFamily, asNumber))
            {
                if (route->routeType == "connected")
                {
                    auto networkIt = std::find(connectedNetworks.begin(), connectedNetworks.end(), route->network);
                    if (networkIt == connectedNetworks.end())
                    {
                        removedRoutes.emplace_back(route);
                    }
                }
            }
        }

        // Notify neighbors
        if (!removedRoutes.empty())
        {
            notifyRoutingChange(removedRoutes, /*isRemoved*/true);

            // Remove routes from table after neighbor.
            for (const auto& route : removedRoutes)
            {
                routingInstance->routingTable.removeEigrp(route->network, route->mask, addressFamily, asNumber);
            }
        }
        if (!updatedRoutes.empty())
        {
            notifyRoutingChange(updatedRoutes, /*isRemoval*/false);
        }

    }

    void Eigrp::notifyRoutingChange(const std::vector<RoutingTable::Eigrp*>& changedRoutes, bool isRemoval)
    {
        Logger::getInstance().info() << "Notifying all neighbors for route changes" << std::endl;

        {
            std::shared_lock<std::shared_mutex> lock(interfaceMutex);

            // Adjust summaries based on added/removed routes
            forEachInterface([&](InterfaceType, float, EigrpInterface* eigrpInterfacePtr)
            {
                if (eigrpInterfacePtr->currentInterface->shutdownFlag.load(std::memory_order_relaxed)) return;
                std::vector<EigrpConfigs::NeighborInfo*> unicastNeighbors;
                bool hasMulticast = false;
                if (!changedRoutes.empty())
                {
                    {
                        // Check all neighbors for unicast and multicast
                        std::shared_lock<std::shared_mutex> neighborLock(eigrpInterfacePtr->neighborMutex);
                        for (const auto& [address, neighbor] : eigrpInterfacePtr->neighbors)
                        {
                            if (neighbor->unicast)
                            {
                                unicastNeighbors.emplace_back(neighbor);
                            }
                            else
                            {
                                hasMulticast = true;
                            }
                        }
                    }

                    for (const auto& neighbor : unicastNeighbors)
                    {
                        EigrpConfigs::UpdateType updateType = isRemoval ? EigrpConfigs::UpdateType::WITHDRAW : EigrpConfigs::UpdateType::PARTIAL;
                        eigrpInterfacePtr->sendUpdateToNeighbor(neighbor, changedRoutes, updateType);
                    }
                    if (hasMulticast && eigrpInterfacePtr->getConfigs().multicastEnabled.load(std::memory_order_relaxed))
                    {
                        EigrpConfigs::UpdateType updateType = isRemoval ? EigrpConfigs::UpdateType::WITHDRAW : EigrpConfigs::UpdateType::PARTIAL;
                        eigrpInterfacePtr->sendUpdateToNeighbor(nullptr, changedRoutes, updateType);
                    }
                }
            });
        }
    }

    void Eigrp::shutdown()
    {
        std::shared_lock<std::shared_mutex> lock(interfaceMutex);
        for (auto& [_, list] : eigrpInterfaceList)
        {
            for (auto it = list.begin(); it != list.end();)
            {
                // Send termination message
                delete it->second;
                it->second = nullptr;
                it = list.erase(it);
            }
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
        RoutingTable& routingTable = routingInstance->routingTable;
        auto route = routingTable.getEigrpRoute(destination, mask, addressFamily, asNumber);

        if (route)
        {
            // Convert route to external EIGRP and notify neighbors
            RoutingTable::Eigrp* externalRoute = route;
            externalRoute->routeType = "external";
            externalRoute->metric += configs.redistributionMetricOffset.load(std::memory_order_release);

            routingTable.addEigrp(externalRoute, addressFamily, asNumber);
            notifyRoutingChange({externalRoute}, false);
        }
    }

    void Eigrp::addNetwork(const EigrpConfigs::Network& newNetwork)
    {
        if (addressFamily != AddressFamily::IPv4) return;

        // Check for duplicate
        {
            std::unique_lock<std::shared_mutex> configsLock(configs.configsMutex);
            for (auto network : configs.networks)
            {
                if (network.ip == newNetwork.ip && network.mask == newNetwork.mask)
                {
                    return; // Network already exists
                }
            }
            configs.networks.emplace_back(newNetwork);
        }

        // Update interfaces and routing table after adding the network
        updateInterfaceList();
        updateRoutingTableForConnected();
    }

    void Eigrp::enableAutoSummary(bool enable)
    {
        if (addressFamily != AddressFamily::IPv4) return; // Only supported for IPv4

        if (enable == configs.autoSummarizationEnabled.load(std::memory_order_relaxed))
        {
            return; // No change
        }

        configs.autoSummarizationEnabled.store(enable, std::memory_order_release);
        
        if (enable)
        {
            // Add summary routes for all calssfull networks
            std::vector<RoutingTable::Eigrp*> removedRoutes;
            std::unordered_map<ByteString, uint8_t> summaryCanidates; // Stores summarized canidates

            // Process all existing EIGRP routes and group by classical networks
            for (const auto& route : routingInstance->routingTable.getAllConnectedEigrpRoutes(addressFamily, asNumber))
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
            forEachInterface([&](InterfaceType, float, EigrpInterface* eigrpInterfacePtr)
            {
                for (const auto& [summaryNet, mask] : summaryCanidates)
                {
                    eigrpInterfacePtr->addSummaryRoute(summaryNet, mask, true);
                }
            });
        }
        else 
        {
            // Remove all summary Routes
            std::unique_lock<std::shared_mutex> configsLock(configs.configsMutex);
            forEachInterface([&](InterfaceType, float, EigrpInterface* eigrpInterfacePtr)
            {
                for (auto it = eigrpInterfacePtr->getConfigs().summaryRoutes.begin(); it != eigrpInterfacePtr->getConfigs().summaryRoutes.end();)
                {
                    if (it->isAuto)
                    {
                        eigrpInterfacePtr->removeSummaryRoute(it->summary->network, it->summary->mask);
                    }
                    else
                    {
                        ++it;
                    }
                }
            });
        }
    }

    void Eigrp::setStub(bool isStub, bool advertiseConnected, bool advertiseLeakMap, bool advertiseStatic, bool advertiseSummary, bool advertiseRedistributed)
    {
        {
            std::unique_lock<std::shared_mutex> configsLock(configs.configsMutex);
            configs.stubConfig.isStub = isStub;
            configs.stubConfig.advertiseConnected = advertiseConnected;
            configs.stubConfig.advertiseLeakMap = advertiseLeakMap;
            configs.stubConfig.advertiseStatic = advertiseStatic;
            configs.stubConfig.advertiseSummary = advertiseSummary;
            configs.stubConfig.advertiseRedistributed = advertiseRedistributed;
        }

        std::shared_lock<std::shared_mutex> lock(interfaceMutex);
        // Update stub routes across all interfaces
        forEachInterface([&](InterfaceType, float, EigrpInterface* eigrpInterfacePtr)
        {
            eigrpInterfacePtr->handleStubRouteUpdates();
        });
    }

    uint32_t Eigrp::getLowestBandwidth()
    {
        uint32_t lowestBW = std::numeric_limits<uint32_t>::max();
        std::shared_lock<std::shared_mutex> lock(interfaceMutex);
        forEachInterface([&](InterfaceType, float, EigrpInterface* eigrpInterfacePtr)
        {
            if (!eigrpInterfacePtr->currentInterface->shutdownFlag.load(std::memory_order_relaxed) && eigrpInterfacePtr->currentInterfaceInfo->bandwidth < lowestBW)
            {
                lowestBW = eigrpInterfacePtr->currentInterfaceInfo->bandwidth.load(std::memory_order_relaxed);
            }
        });
        return lowestBW;
    }

    void Eigrp::addPassiveInterface(InterfaceType type, float id, bool add)
    {
        {
            std::shared_lock<std::shared_mutex> lock(configs.configsMutex);
            if (add)
            {
                configs.passiveInterfaces[type].insert(id);
            }
            else
            {
                configs.passiveInterfaces[type].erase(id);
            }
        }

        // Make the interface passive if it already exists
        auto intIt = eigrpInterfaceList[type].find(id);
        if (intIt != eigrpInterfaceList[type].end())
        {
            intIt->second->setPassive(add);
        }
    }

    void Eigrp::setVariance(uint8_t var)
    {
        if ( var == 0 ) return; // Invalid variance
        for (auto routeInfo : topologyTable->getTopologyEntries())
        {
            topologyTable->updateSuccessorAndFeasibleSuccessors(routeInfo.second);
        }
        std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
        configs.variance = var;
    }

    void Eigrp::recalculateRoutes()
    {
        if (configs.variance.load(std::memory_order_relaxed) == 0) return; // Invalid variance

        // Iterate through the topology table and recalculate all routes.
        for (auto& [destination, entry] : topologyTable->getTopologyEntries())
        {
            // Find all feasible successors within the Variance
            for (const auto& [neighbor, routeInfo] : entry->routesByNeighbor)
            {
                std::shared_lock<std::shared_mutex> lock(configs.configsMutex);
                if (routeInfo.feasibleDistance <= entry->bestFD && routeInfo.feasibleDistance <= entry->bestFD * configs.variance && routeInfo.reportedDistance < entry->bestFD)
                {
                    lock.unlock();
                    // Add or update route in the routing table
                    RoutingTable::Eigrp* newRoute = new RoutingTable::Eigrp();
                    newRoute->network = destination;
                    newRoute->mask = entry->prefixLength;
                    newRoute->nextHop = neighbor;
                    newRoute->metric = routeInfo.feasibleDistance;
                    newRoute->routeType = "internal";

                    routingInstance->routingTable.addEigrp(newRoute, addressFamily, asNumber);
                }
            }
        }
    }

    void Eigrp::recalculateRouteMetrics()
    {
        auto eigrpRoutes = routingInstance->routingTable.getAllEigrpRoutes(addressFamily, asNumber);
        for (auto* route : eigrpRoutes)
        {
            
        }
    }

    void Eigrp::addRouteMetric(uint32_t localCost, RoutingTable::Eigrp* route)
    {
        // Calculate Feasible Distance and Composite Metric
        uint32_t neighborRD = calculateMetric(route->bandwidth, route->load, route->delay, route->reliability);
        route->reportedDistance = neighborRD;

        // Calculate FD = Local Link Cost + RD
        route->feasibleDistance = localCost + route->reportedDistance;

        // Calculate the composite metric for internal use
        route->metric = localCost;

        // Set the administrative distance
        route->adminDistance = (route->routeType == "internal" || route->routeType == "summary")
            ? configs.adminDistance.load(std::memory_order_relaxed)
            : configs.externalAdminDistance.load(std::memory_order_relaxed);
    }

    void Eigrp::enableUnicastNeighbor(const ByteString& neighborIp, InterfaceType interfaceType, float id)
    {
        // Add unicast neighbor to the unicast neighbor list
        {
            std::shared_lock<std::shared_mutex> lock(configs.configsMutex);
            configs.unicastNeighbors[interfaceType][id].insert(neighborIp);
        }

        // Find the interface to add the neighbor
        {
            std::shared_lock<std::shared_mutex> intLock(interfaceMutex);
            auto intIt = eigrpInterfaceList[interfaceType].find(id);
            if (intIt != eigrpInterfaceList[interfaceType].end())
            {
                intIt->second->addUnicastNeighbor(neighborIp);
            }
        }
    }

    void Eigrp::disableUnicastNeighbor(const ByteString& neighborIp, InterfaceType interfaceType, float id)
    {
        // Remove unicast neighbor from the unicast neighbor list
        {
            std::shared_lock<std::shared_mutex> lock(configs.configsMutex);
            configs.unicastNeighbors[interfaceType][id].erase(neighborIp);
        }

        // Find the interface to remove the neighbor from
        {
            std::shared_lock<std::shared_mutex> intLock(interfaceMutex);
            auto intIt = eigrpInterfaceList[interfaceType].find(id);
            if (intIt != eigrpInterfaceList[interfaceType].end())
            {
                intIt->second->removeUnicastNeighbor(neighborIp);
            }
        }
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
        {
            std::unique_lock<std::shared_mutex> lock(interfaceMutex);
            forEachInterface([&](InterfaceType, float, EigrpInterface* eigrpInterfacePtr)
            {
                eigrpInterfacePtr->stopHello();
            });
            eigrpInterfaceList.clear();
        }

        {
            std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
            configs.networks.clear();
        }
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
        if (!routerID.isStatic)
        {
            routingInstance->forEachInterface([&](InterfaceType, uint8_t, Interface* interface)
            {
                auto interfaceInfo = interface->Get();
                if (!interfaceInfo) return;
                ByteString address;
                {
                    std::shared_lock<std::shared_mutex> ipLock(interfaceInfo->ipMutex);
                    address = interfaceInfo->ipv4.ipAddress;
                }
                if (address.empty()) return;
                uint32_t ipNum = Functions::byteToNum(address);
                if (ipNum < highestNum) return;
                highestNum = ipNum;
                highestIP = address;
            }, {InterfaceType::LOOPBACK}, true);
            if (highestIP.empty())
            {
                routingInstance->forEachInterface([&](InterfaceType, uint8_t, Interface* interface)
                {
                    auto interfaceInfo = interface->Get();
                    if (!interfaceInfo) return;
                    ByteString address;
                    {
                        std::shared_lock<std::shared_mutex> ipLock(interfaceInfo->ipMutex);
                        address = interfaceInfo->ipv4.ipAddress;
                    }
                    uint32_t ipNum = Functions::byteToNum(address);
                    if (ipNum < highestNum) return;
                    highestNum = ipNum;
                    highestIP = address;
                }, {InterfaceType::LOOPBACK}, false);
            }
            if (highestIP.empty())
            {
                // Look for lowest mac to make it fair
                highestNum = UINT32_MAX;
                routingInstance->forEachInterface([&](InterfaceType, uint8_t, Interface* interface)
                {
                    auto interfaceInfo = interface->Get();
                    if (!interfaceInfo) return;
                    ByteString mac;
                    {
                        std::shared_lock<std::shared_mutex> maclock(interfaceInfo->ipMutex);
                        if (interfaceInfo->macAddress.size() != 6) return;
                        mac = interfaceInfo->macAddress.substr(2, 4);
                    }
                    uint32_t macNum = Functions::byteToNum(mac);
                    if (macNum > highestNum) return;
                    highestNum = macNum;
                    highestIP = mac;
                });
            }
            routerID.ID = highestIP;
        }
    }

#pragma endregion

#pragma region EigrpInterface

    EigrpInterface::EigrpInterface(Eigrp &eigrpSystem, Interface* interface)
        : eigrpProcess(&eigrpSystem),
          currentInterface(interface),
          currentInterfaceInfo(interface->Get())
    {
        {
            if (!currentInterface->shutdownFlag.load(std::memory_order_relaxed))
            {
                // Gather locked values for local metric calculation
                uint32_t delay = currentInterfaceInfo->delay.load(std::memory_order_relaxed);
                uint32_t bandwidth = currentInterfaceInfo->bandwidth.load(std::memory_order_relaxed);
                uint8_t load = eigrpProcess->getConfigs()->variance.load(std::memory_order_relaxed);
                InterfaceType type = currentInterfaceInfo->interfaceType.load(std::memory_order_relaxed);
                float ID = currentInterfaceInfo->id.load(std::memory_order_relaxed);
                uint8_t reliability = 255;

                // Check if this interface is passive
                bool passive = false;
                {
                    std::shared_lock<std::shared_mutex> lock(eigrpProcess->getConfigs()->configsMutex);
                    if (eigrpProcess->getConfigs()->passiveInterfaces[type].find(ID) != eigrpProcess->getConfigs()->passiveInterfaces[type].end())
                    {
                        passive = true;
                    }
                }
                if (passive)
                {
                    setPassive(true);
                }

                // TODO add dampening for recalculation
                // Recalculate metrics if new lowest bandwidth is found
                if (eigrpProcess->getConfigs()->lowestBandwidth.load(std::memory_order_relaxed) > bandwidth)
                {
                    eigrpProcess->getConfigs()->lowestBandwidth.store(bandwidth, std::memory_order_release);
                    // Recalculate local metrics on all interfaces
                    {
                        eigrpProcess->forEachInterface([&](InterfaceType, float, EigrpInterface* interfacePtr)
                        {
                            if (interfacePtr->currentInterface->shutdownFlag.load(std::memory_order_relaxed)) return;

                            uint32_t delay = interfacePtr->currentInterfaceInfo->delay.load(std::memory_order_relaxed);
                            uint8_t load = eigrpProcess->getConfigs()->variance.load(std::memory_order_relaxed);
                            interfacePtr->getConfigs().localMetric = interfacePtr->eigrpProcess->calculateLocalLinkCost(load, delay, reliability);
                        });
                    }
                }
                else
                {
                    std::shared_lock<std::shared_mutex> metricLock(configs.configsMutex);
                    configs.localMetric = eigrpProcess->calculateLocalLinkCost(load, delay, reliability);
                }

                // Handle unciast neighbors
                std::vector<ByteString> unicastNeighbors;
                {
                    std::lock_guard<std::shared_mutex> lock(eigrpProcess->getConfigs()->configsMutex);
                    if (!eigrpProcess->getConfigs()->unicastNeighbors[type][ID].empty())
                    {
                        // Disable multicast if unicast neighbors are present
                        configs.multicastEnabled.store(false, std::memory_order_release);
                        
                        // Store neighbors to add
                        for (auto neighbor : eigrpProcess->getConfigs()->unicastNeighbors[type][ID])
                        {
                            unicastNeighbors.push_back(neighbor);
                        }
                    }
                }
                //Add unicast neighbors if any are present
                {
                    for (auto neighbor : unicastNeighbors)
                    {
                        addUnicastNeighbor(neighbor);
                    }
                }

            



                // Start hello for interface if interface is not passive
                if (!passive)
                {
                    helloStartTime = std::chrono::steady_clock::now();
                    startHelloHelper();
                    sendHelloPacket();
                }
            }
        }
    }

    EigrpInterface::~EigrpInterface()
    {
        AddressFamily addressFamily = eigrpProcess->addressFamily;
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

        auto globalRoute = eigrpProcess->routingInstance->routingTable.getEigrpRoute(network, mask, addressFamily, eigrpProcess->asNumber);
        if (globalRoute)
        {
            if (globalRoute && globalRoute->routeType == "connected")
            {
                // Remove if valid and is a connected route
                eigrpProcess->routingInstance->routingTable.removeEigrp(network, mask, addressFamily, eigrpProcess->asNumber);
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
        std::unique_lock<std::shared_mutex> lock(neighborMutex);

        for (auto it = neighbors.begin(); it != neighbors.end();)
        {
            auto nextIt = std::next(it);
            handleNeighborDown(it->second, it->first);

            if (neighbors.empty())
            {
                break;
            }

            it = nextIt;
        }

        // Remove interface from other tables
        uint32_t id = eigrpProcess->asNumber;
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

    void EigrpInterface::processPacket(const EigrpHeader& eigrpPacket, const ByteString neighborIp, bool multicast)
    {
        EigrpConfigs::NeighborState neighborState;
        // Check if passive

        {
            if (configs.isPassive.load(std::memory_order_relaxed))
            {
                Logger::getInstance().info() << "Interface is passive. Incoming EIGRP packet ignored." << std::endl;
                return;
            }
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
            if (neighborIt != neighbors.end())
            {
                neighbor = neighborIt->second;
            }
        }

        if (neighbor && (eigrpPacket.opcode != Variable::Eigrp::Type::hello || eigrpPacket.ack != ByteString(4, '\x00')))
        {
            neighborState = neighbor->neighborState.load(std::memory_order_relaxed);
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
                processHello(neighbor, eigrpPacket, neighborIp, !multicast);
            }
            else if (neighbor)
            {
                processAck(neighbor, eigrpPacket.ack);
            }
        }
        else if (neighbor)
        {
            if (eigrpPacket.opcode == Variable::Eigrp::Type::update)
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
    }

    void EigrpInterface::changeNeighborState(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, EigrpConfigs::NeighborState newState)
    {
        // Make sure neighbor exists
        if (!neighbor) return; // Neighbor is null

        // Check for currect state in order to change state
        auto currentState = neighbor->neighborState.load(std::memory_order_relaxed);
        uint32_t seq = nextSequenceNumber.load(std::memory_order_relaxed);
        ByteString routerID = eigrpProcess->getRouterID();
        bool unicast = neighbor->unicast;

        if (neighbor->neighborState != EigrpConfigs::NeighborState::DOWN &&
            neighbor->neighborState != static_cast<EigrpConfigs::NeighborState>(static_cast<int>(newState) - 1)) return;

        if (currentState == newState) return;

        switch (newState)
        {
            case EigrpConfigs::NeighborState::INIT:
                Logger::getInstance().info(true) << "Neighbor in INIT state. Sending Hello." << std::endl;
                sendHelloPacket(neighbor, unicast);

                // Update INIT start time and start stuck detection thread
                neighbor->initStartTime = std::chrono::steady_clock::now();
                neighbor->stuckInInitCheckActive.store(true, std::memory_order_release);

                // Spawn a thread to check for "stuck in INIT"
                neighbor->stuckInInitTimerId = TimeManager::getInstance().addTimer(std::chrono::steady_clock::now() + std::chrono::seconds(neighbor->holdTime), [this, neighbor, neighborIp]()
                {
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
                neighbor->neighborState.store(newState, std::memory_order_release);

                changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::TWOWAY);
                break;

            case EigrpConfigs::NeighborState::TWOWAY:
                Logger::getInstance().info(true) << "Neighbor in TWOWAY state." << std::endl;
                
                // Update the neighbors state
                neighbor->neighborState.store(newState, std::memory_order_release);
                
                // Start a seperate thread to monitor the second hello
                neighbor->twoWayThread = std::thread([this, neighbor, neighborIp]()
                {
                    {
                        std::unique_lock<std::mutex> cvLock(neighbor->twoWayMutex);

                        // Wait for up to 300ms for a second Hello
                        neighbor->cv.wait_for(cvLock, std::chrono::milliseconds(300), [neighbor]() {return neighbor->secondHelloReceived.load(std::memory_order_release); });
                    }

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
                neighbor->neighborState.store(newState, std::memory_order_release);

                // Move to EXCHANGE to start topology exchange
                changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::EXCHANGE);
                break;

            case EigrpConfigs::NeighborState::EXCHANGE:
                Logger::getInstance().info(true) << "Neighbor in EXCHANGE state. Sharing topology." << std::endl;
                
                if (neighbor->slaveInit || neighbor->masterInit)
                {
                    neighbor->neighborState.store(newState, std::memory_order_release);
                    changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::LOADING);
                }
                else if (neighbor->initRole == EigrpConfigs::InitRole::MASTER)
                {
                    if (!neighbor->nullSent && !neighbor->masterInit)
                    {
                        neighbor->masterInit.store(true, std::memory_order_release);
                        
                        // Send Sequence Hello with the generated sequence number
                        Logger::getInstance().info(true) << "MASTER sending Sequence Hello with sequence number: " << seq << std::endl;
                        sendHelloPacket(neighbor, unicast, /*update=*/true, seq);

                        // Set state to loading
                        Logger::getInstance().info(true) << "MASTER sending full topology." << std::endl;
                        neighbor->neighborState.store(newState, std::memory_order_release);
                        changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::LOADING);
                    }
                }
                else if (neighbor->initRole == EigrpConfigs::InitRole::SLAVE && neighbor->initUpdateReceived && !neighbor->slaveInit)
                {
                    // Send Null update to initialize reliable connections
                    Logger::getInstance().info(true) << "Sending Null Update to neighbor." << std::endl;
                    
                    // Send Sequence Hello with the generated sequence number
                    Logger::getInstance().info(true) << "Sending Sequence Hello with sequence number: " << seq << std::endl;
                    sendHelloPacket(neighbor, /*unicast=*/unicast, /*update=*/true, seq);

                    // Send your topology
                    Logger::getInstance().info(true) << "Sending full topology." << std::endl;
                    sendUpdateToNeighbor(neighbor, eigrpProcess->routingInstance->routingTable.getAllEigrpRoutes(eigrpProcess->addressFamily, eigrpProcess->asNumber), EigrpConfigs::UpdateType::FULL, false, true, {neighborIp});

                    // Send a hello immediately after sending routes
                    Logger::getInstance().info(true) << "Sending immediate Hello after full topology." << std::endl;
                    sendHelloPacket(neighbor);

                    neighbor->neighborState.store(newState, std::memory_order_release);
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
                neighbor->neighborState.store(newState, std::memory_order_release);
                break;

            case EigrpConfigs::NeighborState::ESTABLISHED:
                Logger::getInstance().info(true) << "Neighbor in ESTABLISHED state. Adjacency fully formed." << std::endl;

                // Update the neighbors state
                neighbor->neighborState.store(newState, std::memory_order_release);
                break;
                
            default:
                break;
        }
    }

    void EigrpInterface::processHello(EigrpConfigs::NeighborInfo* neighbor, const EigrpHeader& receivedHello, const ByteString& neighborIp, bool unicast)
    {
        // Neighor values if needed
        bool neighborAdded = false;
        
        // Checks if point-to-point is configured
        {
            if (configs.interfaceMode.load(std::memory_order_relaxed) == EigrpConfigs::Mode::POINT_TO_POINT)
            {
                // In point-to-point mode, ensure there's only one neighbor
                if (!neighbors.empty() && neighbors.find(neighborIp) == neighbors.end())
                {
                    Logger::getInstance().warn() << "Point-to-point interface already has a neighbor: " << neighbors.begin()->first.toHex() << std::endl;
                    return;
                }
            }
        }

        // Validate the Autonomous System Number (ASN)
        if (Functions::byteToNum(receivedHello.autonomousSystem) != eigrpProcess->asNumber)
        {
            // Drop the packet - AS number mismatch
            return;
        }

        // Extract Hold Time
        uint16_t recievedHoldTime = configs.holdTime.load(std::memory_order_relaxed); // Default holdtime.

        // Safely access or create the neighbor
        if (!neighbor && !unicast)
        {
            neighbor = new EigrpConfigs::NeighborInfo(neighborIp);
            neighbors[neighborIp] = neighbor;
            neighborAdded = true;
        }
        else if (!neighbor && unicast)
        {
            return;
        }
        else if (neighbor && neighbor->holdTimerId.load(std::memory_order_relaxed) == 0) // If hold timer is not running
        {
            neighborAdded = true;
        }

        // Process TLVs
        for (const auto& opt : receivedHello.options)
        {
            if (opt.option == Variable::Eigrp::Option::parameter)
            {
                ByteString parameters = eigrpProcess->calculateParameters(recievedHoldTime);
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
                if (neighborIp != opt.value.substr(1, Functions::byteToNum(opt.value.substr(0, 1))))
                    return; // Drop packet if the sequence doesn't match
            }
            else if (opt.option == Variable::Eigrp::Option::multicastSequence)
            {
                neighbor->initSequence = Functions::byteToNum(opt.value);
            }
        }

        // Update neighbor fields and start/renew hold timers
        if (neighborAdded)
        {
            neighbor->holdTime = recievedHoldTime;
            neighbor->lastHeard = std::chrono::steady_clock::now();
            neighbor->lastReceivedSequenceNumber = 1;
            neighbor->srtt = 1.0;
            neighbor->rttvar = 0.5;
            neighbor->rto = 1.5;
        }

        // Cancel existing hold timer
        if (neighbor && neighbor->holdTimerId.load(std::memory_order_relaxed) != 0)
        {
            TimeManager::getInstance().cancelTimer(neighbor->holdTimerId);
        }
        startHoldTimer(neighbor, neighborIp, neighbor->holdTime);

        // Safely extract the neighbor state
        EigrpConfigs::NeighborState neighborState = neighbor->neighborState.load(std::memory_order_relaxed);

        if (neighborState == EigrpConfigs::NeighborState::DOWN)
        {
            changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::INIT);
        }
        else if (neighborState == EigrpConfigs::NeighborState::TWOWAY)
        {
            std::lock_guard<std::mutex> cvLock(neighbor->twoWayMutex);
            neighbor->secondHelloReceived.store(true, std::memory_order_release);
            neighbor->cv.notify_one(); // Wake up the waiting thread immediately.
        }
    }

    void EigrpInterface::processUpdate(EigrpConfigs::NeighborInfo* neighbor, const EigrpHeader& receivedUpdate)
    {
        // Get neighbor Ip
        ByteString neighborIp;
        ByteString interfaceIp;
        bool initComplete = neighbor->initComplete.load(std::memory_order_relaxed);
        if (currentInterface->Get())
        {
            interfaceIp = getInterfaceIp();
        }
        {
            std::shared_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
            neighborIp = neighbor->ipAddress;
        }
        
        if (neighborIp == interfaceIp || neighborIp.empty())
        {
            return; // Neighbor IP invalid
        }

        // Extract sequence number
        uint32_t receivedSequenceNumber = Functions::byteToNum(receivedUpdate.sequence);

        // Handle non-stop-forwarding
        if (neighbor->isGracfullyRestarting.load(std::memory_order_relaxed) && neighbor->lastReceivedSequenceNumber + 1 == receivedSequenceNumber)
        {
            TimeManager::getInstance().cancelTimer(neighbor->gracefulRestartTimerId.load(std::memory_order_relaxed));
            neighbor->gracefulRestartTimerId.store(0, std::memory_order_release);
        }

        // Update flags
        bool initReceived = false;
        bool endOfTable = false;
        if (receivedUpdate.flags.restart == "1") {}
        if (receivedUpdate.flags.init == "1") { neighbor->sequenceList[receivedSequenceNumber].init = true; neighbor->initUpdateReceived = true; neighbor->initSequence.store(receivedSequenceNumber); initReceived = true; }
        if (receivedUpdate.flags.conditionalRecieve == "1") { neighbor->sequenceList[receivedSequenceNumber].conditionalReceive = true; }
        if (receivedUpdate.flags.endOfTable == "1") {neighbor->sequenceList[receivedSequenceNumber].endOfTable = true; endOfTable = true; }

        // Validate sequence number
        {
            uint32_t lastReceivedSequenceNumber = neighbor->lastReceivedSequenceNumber.load(std::memory_order_relaxed);

            if (receivedSequenceNumber <= lastReceivedSequenceNumber && !initComplete)
            {
                Logger::getInstance().debug() << "Duplicate or old Update received with sequence number " << receivedSequenceNumber << " from neighbor " << neighborIp.toHex() << std::endl;
                sendAckToNeighbor(neighbor, neighborIp, neighbor->lastReceivedSequenceNumber);
            }
            else if (receivedSequenceNumber > lastReceivedSequenceNumber + 1)
            {
                Logger::getInstance().debug() << "Sequence number: " << receivedSequenceNumber << " from neighbor: " << neighborIp.toHex() << " is too new, adding packet to buffer." << std::endl;
                // Buffer out of order packet
                std::unique_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
                neighbor->packetBuffer[receivedSequenceNumber] = EigrpConfigs::NeighborInfo::PacketBuffer{.neighborIp = neighborIp, .eigrp = receivedUpdate};
            }

            // Validate init sequence
            if (initReceived)
            {
                neighbor->initSequence = 0;
                handleNeighborRestart(neighbor, neighborIp);
            }
                
            neighbor->lastReceivedSequenceNumber = receivedSequenceNumber;
        }

        // Acknowledge packet
        sendAckToNeighbor(neighbor, neighborIp, receivedSequenceNumber);


        // Determine roles based on sequence numbers
        if (!initComplete)
        {
            if (receivedSequenceNumber < nextSequenceNumber)
            {
                neighbor->initRole.store(EigrpConfigs::InitRole::MASTER, std::memory_order_release);
                Logger::getInstance().info(true) << "MASTER role has been chosen with sequence number: " << nextSequenceNumber << " and neighbors: " << receivedSequenceNumber << "." << std::endl;
            }
            else if (receivedSequenceNumber > nextSequenceNumber)
            {
                neighbor->initRole.store(EigrpConfigs::InitRole::SLAVE, std::memory_order_release);
                Logger::getInstance().info(true) << "SLAVE role has been chosen with sequence number: " << nextSequenceNumber << " and neighbors: " << receivedSequenceNumber << "." << std::endl;
            }
            else if (neighbor->lastReceivedSequenceNumber == nextSequenceNumber)
            {
                // Use router ID as a tie-breaker
                Logger::getInstance().info(true) << "Sequence numbers equal. Using Router ID as tie-breaker." << std::endl;
                if (Functions::byteToNum(eigrpProcess->getRouterID()) > Functions::byteToNum(neighbor->routerID))
                {
                    neighbor->initRole.store(EigrpConfigs::InitRole::MASTER, std::memory_order_release);
                    Logger::getInstance().info(true) << "Tie-breaker determined: MASTER." << std::endl;
                }
                else
                {
                    neighbor->initRole.store(EigrpConfigs::InitRole::SLAVE, std::memory_order_release);
                    Logger::getInstance().info(true) << "Tie-breaker determined: SLAVE." << std::endl;
                }
            }
            neighbor->initComplete.store(true, std::memory_order_release);
            initComplete = true;
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
                    std::lock_guard<std::mutex> lock(bufferMutex);
                    routeBuffer.emplace_back(route); // Ignore invalid routes
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            if (!routeBuffer.empty())
            {
                updateRoutingTable(neighbor, neighborIp, routeBuffer);
                routeBuffer.clear();
            }
        }

        // Check and process buffered packets
        processBufferedPackets(neighbor);

        // Safely access neighbor state
        if (initComplete && neighbor->neighborState == EigrpConfigs::NeighborState::EXSTART && neighbor->initRole == EigrpConfigs::InitRole::SLAVE && !neighbor->slaveInit)
        {
            changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::EXCHANGE);
        }
    }

    void EigrpInterface::processBufferedPackets(EigrpConfigs::NeighborInfo* neighbor)
    {
        if (!neighbor) return; // neighbor is invalid
        
        uint32_t nextExpectedSequence = neighbor->lastReceivedSequenceNumber + 1;

        while (true)
        {
            auto packetIt = neighbor->packetBuffer.find(nextExpectedSequence);

            if (packetIt != neighbor->packetBuffer.end())
            {
                // Process buffered packet
                EigrpConfigs::NeighborInfo::PacketBuffer bufferedPacket = packetIt->second;
                neighbor->packetBuffer.erase(packetIt);
                neighbor->lastReceivedSequenceNumber = nextExpectedSequence;

                // Unlock before processing the packet
                processUpdate(neighbor, bufferedPacket.eigrp);
            }
            else if (isTimeoutForMissing(neighbor, nextExpectedSequence))
            {
                Logger::getInstance().info() << "Timeout for missing packet with sequence number: "
                                             << nextExpectedSequence << ". Moving forward." << std::endl;
                neighbor->lastReceivedSequenceNumber.store(nextExpectedSequence, std::memory_order_release);
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
        EigrpConfigs::NeighborInfo::ReliablePacketInfo reliablePacketCopy;
        {
            // Locate the acknowledgement packet in reliablePacket
            std::lock_guard<std::mutex> lock(neighbor->reliableMutex);
            auto pktIt = neighbor->reliablePackets.find(ackSequenceNumber);

            if (pktIt == neighbor->reliablePackets.end())
            {
                Logger::getInstance().warn() << "Received ACK for unknown sequence number " << ackSequenceNumber << " from neighbor " << neighbor->ipAddress.toHex() << std::endl;
                return;
            }

            // Cancel the Retransmission Timer
            if (pktIt->second.timerId != 0)
            {
                TimeManager::getInstance().cancelTimer(pktIt->second.timerId);
            }

            // Copy data before unlocking
            reliablePacketCopy = pktIt->second;

            // Erase the packet while locked
            std::erase_if(neighbor->reliablePackets, [&](const auto& entry) {
                return entry.first == ackSequenceNumber;
            });
        }
        

        // Handle routes associated with the acknowledged packet
        for (const auto& route : reliablePacketCopy.packet.updatedRoutes)
        {
            ByteString key = route->network + "/" + std::to_string(route->mask);

            {
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
        }

        // Update RTT and RTO Estimates if Necessary
        updateRTTEstimate(neighbor, ackSequenceNumber);
    }

    void EigrpInterface::processQuery(EigrpConfigs::NeighborInfo* neighbor, const EigrpHeader& receivedQuery, const ByteString& neighborIp)
    {
        if (!neighbor) return; // Neighbor does not exist

        uint32_t receivedSequenceNumber = Functions::byteToNum(receivedQuery.sequence);

        // Ensure correct last received sequence tracking
        if (receivedSequenceNumber < neighbor->lastReceivedSequenceNumber.load(std::memory_order_relaxed))
        {
            return; // Ignoring duplicate sequence number;
        }

        // Set the last received sequence number
        neighbor->lastReceivedSequenceNumber.store(receivedSequenceNumber, std::memory_order_release);

        std::vector<RoutingTable::Eigrp*> queriedRoutes;
        for (const auto& option : receivedQuery.options)
        {
            if (option.option == Variable::Eigrp::Option::internalRoute)
            {
                queriedRoutes.push_back(decodeRoute(option.value, false, false));
            }
        }
        
        sendAckToNeighbor(neighbor, neighborIp, Functions::byteToNum(receivedQuery.sequence));

        std::vector<RoutingTable::Eigrp*> knownQueryRoutes;
        std::vector<RoutingTable::Eigrp*> knownRoutes;
        std::vector<RoutingTable::Eigrp*> unknownQueryRoutes;

        for (const auto& queriedRoute : queriedRoutes)
        {
            // Check if the queried route exists
            auto existingRoute = eigrpProcess->routingInstance->routingTable.getEigrpRoute(queriedRoute->network, queriedRoute->mask, eigrpProcess->addressFamily, eigrpProcess->asNumber);
            // Check summary routes
            {
                std::shared_lock<std::shared_mutex> lock(configs.configsMutex);
                for (const auto& summaryRoute : configs.summaryRoutes) {
                    if (Functions::isSubnetOf(queriedRoute->network, queriedRoute->mask, summaryRoute.summary->network, summaryRoute.summary->mask))
                    {
                        existingRoute = summaryRoute.summary;
                    }
                }
            }

            if (existingRoute)
            {
                knownQueryRoutes.push_back(queriedRoute);
                knownRoutes.push_back(existingRoute);
            }
            else
            {
                unknownQueryRoutes.push_back(queriedRoute);
            }
        }


        if (!knownQueryRoutes.empty() && !knownRoutes.empty())
        {
            // Route is know, send a reply immediately
            sendReplyToNeighbor(neighbor, neighborIp, knownQueryRoutes, knownRoutes, receivedSequenceNumber);
            for (auto route : queriedRoutes)
            {
                delete route;
            }
            queriedRoutes.clear();
            return;
        }

        // Otherwise we need to query all neighbors
        std::vector<EigrpConfigs::ActiveRoute> activeRoutes;
        for (const auto& queriedRoute : unknownQueryRoutes)
        {
            EigrpConfigs::ActiveRoute newQuery;
            newQuery.route = queriedRoute;
            newQuery.originNeighbor = neighborIp;
            newQuery.sequenceNumber = receivedSequenceNumber;
        }

        {
            std::shared_lock<std::shared_mutex> intLock(eigrpProcess->interfaceMutex);
            eigrpProcess->forEachInterface([&](InterfaceType, float, EigrpInterface* interface)
            {
                for (const auto& [ip, otherNeighbor] : interface->neighbors)
                {
                    if (ip != neighborIp)
                    {
                        for (auto& query : activeRoutes)
                        {
                            query.pendingReplies.insert(ip);
                        }
                        sendQueryToNeighbor(otherNeighbor, ip, {unknownQueryRoutes});
                    }
                }
            });
        }

        // Store this query in our global tracker
        for (const auto& query : activeRoutes)
        {
            ByteString queryKey = query.route->network + "/" + std::to_string(query.route->mask);

            eigrpProcess->outstandingReplies[queryKey] = query;
            // Start the timers
            if (!eigrpProcess->getConfigs()->activeDisabled)
            {
                startActiveTimer(query.route);
                startSIATimer(query.route);
            }
        }
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

        if (receivedSequenceNumber < neighbor->lastReceivedSequenceNumber.load(std::memory_order_relaxed))
        {
            Logger::getInstance().debug() << "Ignoring duplicate or old Reply with sequence number "
                                          << receivedSequenceNumber << " from neighbor " << neighborIp.toHex() << std::endl;
            return;
        }

        // ✅ Set last received sequence number
        neighbor->lastReceivedSequenceNumber.store(receivedSequenceNumber, std::memory_order_release);

        std::vector<RoutingTable::Eigrp*> receivedRoutes;
        for (const auto& option : receivedReply.options)
        {
            if (option.option == Variable::Eigrp::Option::internalRoute)
            {
                receivedRoutes.push_back(decodeRoute(option.value, false, false));
            }
        }

        if (receivedRoutes.empty()) return;

        // Send ack for the received reply
        sendAckToNeighbor(neighbor, neighborIp, Functions::byteToNum(receivedReply.sequence));

        std::vector<RoutingTable::Eigrp*> removedRoutes;
        std::vector<RoutingTable::Eigrp*> updatedRoutes;

        for (const auto& receivedRoute : receivedRoutes)
        {
            ByteString queryKey = receivedRoute->network + "/" + std::to_string(receivedRoute->mask);

            if (eigrpProcess->outstandingReplies.find(queryKey) != eigrpProcess->outstandingReplies.end())
            {
                auto& queryInfo = eigrpProcess->outstandingReplies[queryKey];

                // Remove this neighbor from the pensing replies list
                queryInfo.pendingReplies.erase(neighborIp);

                // If the received route is not unreachable, update the feasible route list
                if (receivedRoute->delay != std::numeric_limits<uint32_t>::max())
                {
                    queryInfo.feasibleRoutes.push_back(std::tuple(neighbor, neighborIp, receivedRoute));
                }

                // If all replies are in, send our own reply upstream
                if (queryInfo.pendingReplies.empty())
                {
                    std::cout << "route: " << queryInfo.route->network.toHex() << "stucked" << std::endl;
                    RoutingTable::Eigrp* bestRoute = nullptr;

                    if (!queryInfo.feasibleRoutes.empty())
                    {
                        // Find the best feasible route (lowest feasible distance)
                        std::sort(queryInfo.feasibleRoutes.begin(), queryInfo.feasibleRoutes.end(), [](const auto& a, const auto& b)
                        {
                            return std::get<2>(a)->feasibleDistance < std::get<2>(b)->feasibleDistance;
                        });

                        bestRoute = std::get<2>(queryInfo.feasibleRoutes.front()); // Best Route
                    }
                    else
                    {
                        eigrpProcess->routingInstance->routingTable.removeEigrp(queryInfo.route->network, queryInfo.route->mask, eigrpProcess->addressFamily, eigrpProcess->asNumber);
                    }

                    {
                        std::shared_lock<std::shared_mutex> intLock(eigrpProcess->interfaceMutex);
                        eigrpProcess->forEachInterface([&](InterfaceType, float, EigrpInterface* interface)
                        {
                            if (interface->neighbors.find(queryInfo.originNeighbor) != interface->neighbors.end())
                            {
                                if (bestRoute)
                                {
                                    // Reply with the best route
                                    sendReplyToNeighbor(interface->neighbors[queryInfo.originNeighbor], queryInfo.originNeighbor, {bestRoute}, {bestRoute}, queryInfo.sequenceNumber);
                                }
                                else
                                {
                                    // No valid route, reply with unreachable
                                    sendReplyToNeighbor(interface->neighbors[queryInfo.originNeighbor], queryInfo.originNeighbor, {queryInfo.route}, {queryInfo.route}, queryInfo.sequenceNumber);
                                    delete queryInfo.route;
                                }
                            }
                        });
                    }

                    // Add routes to the routing table;
                    for (const auto& route : queryInfo.feasibleRoutes)
                    {
                        updateRoutingTable(std::get<0>(route), std::get<1>(route), {std::get<2>(route)});
                    }

                    // Remove thequery from tracking
                    cancelActiveTimer(receivedRoute->network, receivedRoute->mask);
                    cancelSIATimer(receivedRoute->network, receivedRoute->mask);
                    eigrpProcess->outstandingReplies.erase(queryKey);

                    auto& topologyTable = eigrpProcess->topologyTable->getTopologyEntries();
                    for (auto& entry : topologyTable)
                    {
                        std::cout << "entry: " << entry.first.toHex() << std::endl;
                        for (auto& route : entry.second->routesByNeighbor)
                        {
                            std::cout << "     neighbor: " << route.first.toHex();
                        }
                    }
                }
            }
        }
    }

    size_t EigrpInterface::calculateMaxRoutesPerPacket(AddressFamily af, bool isExternal)
    {
        uint16_t maxPacketSize = currentInterfaceInfo->mtu.load(std::memory_order_relaxed);
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
        {
            std::unique_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
            neighbor->pendingAcks.insert(sequenceNumber);

            // if ACK processing is disabled, return early
            if (!neighbor->processAcks.load(std::memory_order_relaxed))
            {
                return;
            }

            // Process pending ACKs
            for (auto seq : neighbor->pendingAcks)
            {
                PacketInfo eigrpAckPacketStructure;

                // Create the EIGRP Ack packet
                EigrpHeader eigrp;
                eigrpProcess->eigrpHello(eigrp, this, neighbor, neighborIp, seq, /*ack=*/true);

                // Add Authentication TLV if enabled
                if (neighbor->authenticationEnabled.load(std::memory_order_relaxed))
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

                // Send the assembled ACK packet if it contains data
                if (!eigrpAckPacketStructure.Layer4.empty() && currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed))
                {
                    currentInterface->ipPacket->setIPHeader(
                        eigrpAckPacketStructure, neighborIp, nullptr, nullptr, getConfigs().DSCP, 255, Variable::IP::eigrp);
                    Logger::getInstance().info() << "ACKs sent to neighbor " << neighborIp.toHex() << std::endl;
                }
            }
            neighbor->pendingAcks.clear();
        }
    }

    void EigrpInterface::sendUpdateToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const std::vector<RoutingTable::Eigrp*> &routes, EigrpConfigs::UpdateType updateType, bool restart, bool conditional, std::vector<ByteString> conditionalNeighbors)
    {
        // Determine target IP based on communication mode
        ByteString interfaceIp = getInterfaceIp();
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
                neighbor->nullUpdateSequence.store(getNextSequenceNumber(), std::memory_order_release);
                neighbor->nullSent.store(true, std::memory_order_release);
            }
        }

        // Filter routes based on stub configuration and split horizon
        std::vector<RoutingTable::Eigrp*> filteredRoutes;
        for (const auto& route : routes)
        {
            if (route->delay != 0xFFFFFFFF)
            {
                bool isSummarized = std::any_of(configs.summaryRoutes.begin(), configs.summaryRoutes.end(),
                    [&](const auto& summary) { return Functions::isSubnetOf(route->network, route->mask, summary.summary->network, summary.summary->mask); });
                
                if (isSummarized) continue;

                // Stub Test
                // If the process is running in stub mode, only allow routes that are permitted.
                if (eigrpProcess->isStub() && !((route->routeType == "connected" && eigrpProcess->advertiseConnected()) ||
                                                (route->routeType == "static" && eigrpProcess->advertiseStatic()) ||
                                                (route->routeType == "summary" && eigrpProcess->advertiseSummary()) ||
                                                (route->routeType == "external" && eigrpProcess->advertiseRedistributed())))
                {
                    continue;
                }

                // Split Horizon Test
                if (getConfigs().splitHorizon.load(std::memory_order_relaxed) && false)
                {
                    std::shared_lock<std::shared_mutex> lock(neighborMutex);
                    auto it = neighbors.find(route->nextHop);
                    if (it != neighbors.end() && (route->nextHop == it->first || Functions::compareNetworkWithIp(route->network, it->second->ipAddress, route->mask)))
                    {
                        continue;
                    }
                    if (Functions::compareNetworkWithIp(route->network, interfaceIp, route->mask))
                    {
                        continue;
                    }

                }

                // Check if route requires an update
                bool needsUpdate = true;
                ByteString key = route->network + "/" + std::to_string(route->mask);
                for (const auto& [address, updateNeighbor] : neighbors)
                {
                    auto advertIt = updateNeighbor->advertisedRoutes.find(key);
                    if (advertIt != updateNeighbor->advertisedRoutes.end())
                    {
                        if (!advertIt->second.pendingUpdate && !advertIt->second.removePending)
                        {
                            needsUpdate = false;
                            break;
                        }
                    }
                    if (!needsUpdate) continue;
                }
            }

            filteredRoutes.emplace_back(route);
        }

        if ((updateType == EigrpConfigs::UpdateType::PARTIAL || updateType == EigrpConfigs::UpdateType::WITHDRAW) && filteredRoutes.empty())
        {
            return;
        }

        uint32_t bandwidthMetric = (10000000 / currentInterfaceInfo->bandwidth.load(std::memory_order_relaxed));
        uint32_t delay = currentInterfaceInfo->delay.load(std::memory_order_relaxed);
        size_t maxRoutesPerPacket = calculateMaxRoutesPerPacket(eigrpProcess->addressFamily, /*isExernal=*/false);
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
                    routeOptions.value = encodeExternalRouteOption(*it, bandwidthMetric, delay, removal);
                    routeOptions.option = (eigrpProcess->addressFamily == AddressFamily::IPv4) ? Variable::Eigrp::Option::externalRoute : Variable::Eigrp::Option::externalRouteV6;
                }
                else
                {
                    routeOptions.value = encodeRouteOption(*it, bandwidthMetric, delay, removal);
                    routeOptions.option = (eigrpProcess->addressFamily == AddressFamily::IPv4) ? Variable::Eigrp::Option::internalRoute : Variable::Eigrp::Option::internalRouteV6;
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
                {
                    std::unique_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
                    if (!neighbor->pendingAcks.empty())
                    {
                        eigrp.ack = Functions::numToByte(*neighbor->pendingAcks.begin(), 4);
                        neighbor->pendingAcks.erase(neighbor->pendingAcks.begin());
                    }
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
                if (neighbor->authenticationEnabled.load(std::memory_order_relaxed))
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
            if (currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed))
            {
                currentInterface->ipPacket->setIPHeader(eigrpPacket, targetIp, nullptr, nullptr, getConfigs().DSCP, 255, Variable::IP::eigrp);
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
                            ByteString key = route->network + "/" + std::to_string(route->mask);

                            {
                                std::unique_lock<std::shared_mutex> neighborLock(neighborPtr->neighborDataMutex);
                                auto advertIt = neighborPtr->advertisedRoutes.find(key);
                                
                                if (advertIt == neighborPtr->advertisedRoutes.end() || advertIt->second.pendingUpdate || advertIt->second.removePending)
                                {
                                    neighborSpecificRoutes.emplace_back(route);
                                }
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

    void EigrpInterface::sendQueryToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, std::vector<RoutingTable::Eigrp*> failedRoutes)
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
        for (const auto& failedRoute : failedRoutes)
        { 
            EigrpHeader::Option queryOption;
            queryOption.option = Variable::Eigrp::Option::internalRoute;
            queryOption.value = encodeQueryOption(failedRoute);
            queryOption.length = Functions::numToByte(static_cast<uint32_t>(queryOption.value.size()) + 4, 2);
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
        eigrp.autonomousSystem = Functions::numToByte(eigrpProcess->asNumber, 2);

        // Generate and append Authentication TLV if enabled for this neighbor
        if (neighbor->authenticationEnabled.load(std::memory_order_relaxed))
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
        if (currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed))
        {
            currentInterface->ipPacket->setIPHeader(eigrpQueryPacketStructure, neighborIp, nullptr, nullptr, getConfigs().DSCP.load(std::memory_order_relaxed), 255, Variable::IP::eigrp);
        }

        // Store the packet for possible retransmission (relieable delivery)
        eigrp.ack = ByteString(4, '\x00');
        setupReliablePacket(neighbor, neighborIp, EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(eigrp, neighborIp, failedRoutes, true), currentSeqNum);

        Logger::getInstance().info() << "Sent query to neighbor " << neighborIp.toHex() << " with sequence number " << currentSeqNum << std::endl;
    }

    void EigrpInterface::sendQueryToNeighbors(std::vector<RoutingTable::Eigrp*> failedRoutes)
    {
        if (failedRoutes.empty()) return;

        std::vector<EigrpConfigs::NeighborInfo*> neighborsToNotify;

        {
            std::shared_lock<std::shared_mutex> lock(neighborMutex);
            for (const auto& [neighborIp, neighbor] : neighbors)
            {
                // Add only fully established neighbors
                if (!neighbor->isInit.load(std::memory_order_relaxed))
                {
                    neighborsToNotify.push_back(neighbor);
                }
            }
        }

        for (auto& neighbor : neighborsToNotify)
        {
            sendQueryToNeighbor(neighbor, neighbor->ipAddress, failedRoutes);
        }
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

    void EigrpInterface::sendReplyToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, std::vector<RoutingTable::Eigrp*> queryRoutes, std::vector<RoutingTable::Eigrp*> existingRoutes, uint32_t querySequence)
    {
        // Validate neighbor
        if (!neighbor)
        {
            Logger::getInstance().warn() << "Invalid neighbor provided to sendUpdateToNeighbor." << std::endl;
            return;
        }

        // Validate routeList
        if (queryRoutes.empty() || queryRoutes.size() != existingRoutes.size())
        {
            return;
        }

        uint32_t sequenceNumber = getNextSequenceNumber();

        // Get values for metric recalculateion
        uint32_t bandwidthMetric = (10000000 / currentInterfaceInfo->bandwidth.load(std::memory_order_relaxed));
        uint32_t delay = currentInterfaceInfo->delay.load(std::memory_order_relaxed);

        // Create the EIGRP Reply Packet
        PacketInfo eigrpReplyPacketStructure;
        EigrpHeader eigrp;

        for (size_t i = 0; i < queryRoutes.size(); i++)
        {
            RoutingTable::Eigrp combinedRoute = *existingRoutes[i];
            combinedRoute.network = queryRoutes[i]->network;
            combinedRoute.mask = queryRoutes[i]->mask;
            combinedRoute.nextHop = configs.nextHopSelf.load(std::memory_order_relaxed) ? getInterfaceIp() : ByteString(combinedRoute.network.size(), '\x00'); // indicates directly connected

            // Construct the Reply option with the route information
            EigrpHeader::Option replyOption;
            replyOption.option = Variable::Eigrp::Option::internalRoute;
            replyOption.value = encodeRouteOption(&combinedRoute, bandwidthMetric, delay);
            replyOption.length = Functions::numToByte(static_cast<uint32_t>(replyOption.value.size()) + 4, 2);
            eigrp.options.emplace_back(replyOption);
        }

        // Indicate if its the last route
        bool lastReply = true;
        {
            for (const auto& [_, reply] : eigrpProcess->outstandingReplies)
            {
                if (reply.sequenceNumber == querySequence)
                {
                    lastReply = false;
                    break;
                }
            }
        }

        // Set other EIGRP header fields
        eigrp.version = ByteString(1, 0x02);
        eigrp.opcode = Variable::Eigrp::Type::reply;
        eigrp.checksum = ByteString(2, 0x00); // Will be calculated later
        eigrp.flags.init = "0";
        eigrp.flags.conditionalRecieve = "0";
        eigrp.flags.restart = "0";
        eigrp.flags.endOfTable = lastReply ? "1" : "0";
        eigrp.sequence = Functions::numToByte(sequenceNumber, 4);
        eigrp.ack = Functions::numToByte(querySequence, 4);
        eigrp.virtualRouterID = eigrpProcess->getVirtualRouterID();
        eigrp.autonomousSystem = Functions::numToByte(eigrpProcess->asNumber, 2);

        // Generate and append Authentication TLV if enabled for this neighbor
        if (neighbor->authenticationEnabled.load(std::memory_order_relaxed))
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
        if (currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed))
        {
            currentInterface->ipPacket->setIPHeader(eigrpReplyPacketStructure, neighborIp, nullptr, nullptr, getConfigs().DSCP.load(std::memory_order_relaxed), 255, Variable::IP::eigrp);
        }

        // Store the packet for possible retransmission (reliable delivery)
        setupReliablePacket(neighbor, neighborIp, EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(eigrp, neighborIp), querySequence);

        Logger::getInstance().info() << "Sent reply to neighbor " << neighborIp.toHex() << " with sequence number " << querySequence << std::endl;
    }

    ByteString EigrpInterface::encodeRouteOption(RoutingTable::Eigrp* route, uint32_t currentBandwidthMetric, uint32_t currentDelay, bool removed)
    {
        ByteString encoded;

        // Include next hop as usual
        encoded += route->nextHop;

        // Update delay: add local interface delay
        uint32_t newDelay = route->delay + ((currentDelay / 10) * 256);

        // Update Bandwidth: Take the higher bandwidth metric
        uint32_t newBandwidthMetric = std::max(route->bandwidth, currentBandwidthMetric);

        encoded += (removed ? ByteString (4, 0xff) : Functions::numToByte(newDelay, 4));
        encoded += Functions::numToByte(newBandwidthMetric, 4);
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

    ByteString EigrpInterface::encodeExternalRouteOption(RoutingTable::Eigrp* route, uint32_t currentBandwidthMetric, uint32_t currentDelay, bool removed)
    {
        ByteString encoded;

        encoded += Functions::changeSize(route->originRouter, 4);
        encoded += Functions::numToByte(route->originAS, 4);
        encoded += Functions::numToByte(route->routeTag, 4);

        // Update delay: add local interface delay
        uint32_t newDelay = route->delay + ((currentDelay / 10) * 256);

        // Update Bandwidth: Take the higher bandwidth metric
        uint32_t newBandwidthMetric = std::max(route->bandwidth, currentBandwidthMetric);

        encoded += (removed ? ByteString(4, 0xff) : Functions::numToByte(newDelay, 4));
        encoded += Functions::numToByte(newBandwidthMetric, 4);
        encoded += Functions::numToByte(route->mtu, 3);
        encoded += Functions::numToByte(route->hopCount, 1);
        encoded += Functions::numToByte(route->reliability, 1);
        encoded += Functions::numToByte(route->load, 1);
        encoded += Functions::numToByte(route->mask, 1);
        encoded += Functions::compactNetworkAddress(route->network, route->mask);
        return encoded;
    }

    void EigrpInterface::addSummaryRoute(const ByteString& network, uint8_t mask, bool isAuto)
    {
        if (mask > network.size() * 8) return; // Mask invalid

        // Validate network and mask
        if (!Functions::compareNetworkWithMask(network, mask)) return;

        // Check for overlapping summary routes
        if (isRouteSummarized(network, mask)) return;

        uint8_t adminDistance = eigrpProcess->getConfigs()->adminDistance.load(std::memory_order_relaxed);
        uint32_t eigrpBw = currentInterfaceInfo->bandwidth.load(std::memory_order_relaxed);
        uint32_t delay = currentInterfaceInfo->delay.load(std::memory_order_relaxed);
        uint16_t mtu = currentInterfaceInfo->mtu.load(std::memory_order_relaxed);

        // Inject the summary route into the routing table as an internal summary route
        RoutingTable::Eigrp* internalSummaryRoute = new RoutingTable::Eigrp();
        internalSummaryRoute->bandwidth = ( 10000000 / eigrpBw) * 256;
        internalSummaryRoute->delay = 0;
        internalSummaryRoute->hopCount = 0;
        internalSummaryRoute->mtu = mtu;
        internalSummaryRoute->reliability = 255;
        internalSummaryRoute->load = eigrpProcess->getConfigs()->variance.load(std::memory_order_relaxed);
        internalSummaryRoute->network = network;
        internalSummaryRoute->mask = mask;
        internalSummaryRoute->nextHop = configs.nextHopSelf.load(std::memory_order_relaxed) ? getInterfaceIp() : ByteString(internalSummaryRoute->network.size(), '\x00'); // indicates directly connected
        internalSummaryRoute->adminDistance = adminDistance;
        internalSummaryRoute->routeType = "summary";

        // Add new summary route
        EigrpConfigs::SummaryRoute summaryRoute;
        summaryRoute.summary = internalSummaryRoute;
        summaryRoute.isAuto = isAuto;
        
        {
            std::unique_lock<std::shared_mutex> configsLock(configs.configsMutex);
            configs.summaryRoutes.emplace_back(summaryRoute);
        }

        // Update interface to advertise the new summary route
        advertiseSummaryRoute(summaryRoute);
    }

    void EigrpInterface::removeSummaryRoute(const ByteString& network, uint8_t mask)
    {
        {
            std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
            auto summaryRoutes = configs.summaryRoutes;
            auto it = std::remove_if(summaryRoutes.begin(), summaryRoutes.end(),
                [&](const EigrpConfigs::SummaryRoute& sr) {
                    return sr.summary->network == network && sr.summary->mask == mask;
                });
            if (it != summaryRoutes.end())
            {
                configs.summaryRoutes.erase(it);
            }

            withdrawSummaryRoute(network, mask);
        }
    }

    void EigrpInterface::restoreSummaryRoutes(const ByteString& summaryNetwork, uint8_t summaryMask)
    {
        
    }


    bool EigrpInterface::isRouteSummarized(const ByteString& network, uint8_t mask)
    {
        std::shared_lock<std::shared_mutex> configsLock(configs.configsMutex);
        for (const auto& sr : configs.summaryRoutes)
        {
            if (Functions::isSubnetOf(network, mask, sr.summary->network, sr.summary->mask))
            {
                return true;
            }
        }
        return false;
    }

    void EigrpInterface::advertiseSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute)
    {
        // Construct the route to advertise
        RoutingTable::Eigrp* summaryEigrpRoute = summaryRoute.summary;
        
        std::vector<EigrpConfigs::NeighborInfo*> neighborsToNotify;
        bool hasMulticast = false;


        // Iterate through neighbors
        {
            std::shared_lock<std::shared_mutex> lock(neighborMutex);
            for (const auto& [address, neighbor] : neighbors)
            {
                if (neighbor->unicast)
                {
                    neighborsToNotify.emplace_back(neighbor);
                }
                else
                {
                    hasMulticast = true;
                }
            }
        }

        // Send unicast updates
        for (const auto& neighborIt : neighborsToNotify)
        {
            sendUpdateToNeighbor(neighborIt, {summaryEigrpRoute}, EigrpConfigs::UpdateType::PARTIAL);
        }
        if (hasMulticast && configs.multicastEnabled.load(std::memory_order_relaxed))
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

        std::vector<EigrpConfigs::NeighborInfo*> unicastNeighbors;
        bool hasMulticast = false;

        // Iterate through neighbors
        {
            std::shared_lock<std::shared_mutex> lock(neighborMutex);
            for (const auto &[neighborIp, neighborInfo] : neighbors) 
            {
                ByteString key = withdrawRoute->network + "/" + std::to_string(withdrawRoute->mask);

                {
                    std::unique_lock<std::shared_mutex> neighborInfoLock(neighborInfo->neighborDataMutex);
                    auto advertIt = neighborInfo->advertisedRoutes.find(key);

                    if (advertIt != neighborInfo->advertisedRoutes.end())
                    {
                        advertIt->second.removePending = true;
                    }
                }

                if (neighborInfo->unicast)
                {
                    unicastNeighbors.emplace_back(neighborInfo);
                }
                else
                {
                    hasMulticast = true;
                }
            }
        }

        // Send withdraw update to the neighbor
        for (const auto& neighborIt : unicastNeighbors)
        {
            sendUpdateToNeighbor(neighborIt, {withdrawRoute}, EigrpConfigs::UpdateType::WITHDRAW);
        }
        if (hasMulticast && configs.multicastEnabled.load(std::memory_order_relaxed))
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
            bool multicast = false;
            std::vector<EigrpConfigs::NeighborInfo*> unicastNeighbors;
            {
                std::shared_lock<std::shared_mutex> neighborLock(neighborMutex);
                for (const auto& [neighborIp, neighborInfo] : neighbors)
                {
                    if (neighborInfo->unicast)
                    {
                        if (!neighborInfo->isInit) continue;

                        // Send withdraw updates
                        unicastNeighbors.push_back(neighborInfo);
                    }
                    else
                    {
                        multicast = true;
                    }
                }
            }

            for (const auto& neighbor : unicastNeighbors)
            {
                sendUpdateToNeighbor(neighbor, routesToWithdraw, EigrpConfigs::UpdateType::WITHDRAW);
            }
            if (multicast && configs.multicastEnabled.load(std::memory_order_relaxed))
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

        if (helloTimerId.load(std::memory_order_relaxed) != 0)
        {
            return; // Timer already active
        }
        
        {
            std::lock_guard<std::mutex> lock(helloTimerMutex);
            if (helloStartTime.time_since_epoch().count() == 0)
            {
                helloStartTime = std::chrono::steady_clock::now();
            }

            auto nextExpiration = helloStartTime + std::chrono::seconds(configs.helloTime);

            uint32_t helloId = TimeManager::getInstance().addTimer(nextExpiration, [&]()
            {
                try
                {
                    {
                        std::vector<EigrpConfigs::NeighborInfo*> unicastNeighbors;
                        {
                            std::shared_lock<std::shared_mutex> lock(neighborMutex);
                            for (const auto& [_, neighbor] : neighbors)
                            {
                                if (neighbor->unicast)
                                {
                                    unicastNeighbors.push_back(neighbor);
                                }
                            }
                        }

                        for (const auto& neighbor : unicastNeighbors)
                        {
                            sendHelloPacket(neighbor, true);
                        }
                        if (configs.multicastEnabled.load(std::memory_order_relaxed))
                        {
                            sendHelloPacket();
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
                {
                    std::lock_guard<std::mutex> lock(helloTimerMutex);
                    helloTimerId = 0; // Clear timer ID after packet is sent
                    helloStartTime = std::chrono::steady_clock::now();
                    helloTimerActive = false;
                }
                startHello(); // Reschedule

            });
            helloTimerId.store(helloId, std::memory_order_release);
        }
    }

    void EigrpInterface::sendHelloPacket(EigrpConfigs::NeighborInfo* neighbor, bool unicast, bool update, uint32_t sequenceNumber)
    {
        if (configs.isPassive.load(std::memory_order_relaxed))
        {
            Logger::getInstance().info() << "Interface is passive. Hello packet not sent." << std::endl;
            return;
        }

        ByteString targetIp;
        
        {
            if (neighbor && unicast)
            {
                std::shared_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
                targetIp = neighbor->ipAddress;
            }
            else if (configs.multicastEnabled.load(std::memory_order_relaxed))
            {
                targetIp = getMulticast();
            }
        }

        PacketInfo eigrpHello;
        EigrpHeader eigrp;

        eigrpProcess->eigrpHello(eigrp, this, neighbor, targetIp, sequenceNumber, false, update);

        // Add stub flags
        if (eigrpProcess->isStub())
        {
            EigrpHeader::Option stubOption;
            stubOption.option = Variable::Eigrp::Option::stub;
            {
                std::shared_lock<std::shared_mutex> lock(eigrpProcess->getConfigs()->configsMutex);
                stubOption.value = encodeStubOption(eigrpProcess->getConfigs()->stubConfig);
            }
            stubOption.length = Functions::numToByte(static_cast<uint32_t>(stubOption.value.size()) + 4, 2);
            eigrp.options.emplace_back(stubOption);
        }

        eigrpHello.Layer4.push_back(eigrp);

        if (currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed))
        {
            currentInterface->ipPacket->setIPHeader(eigrpHello, targetIp, nullptr, nullptr, getConfigs().DSCP.load(std::memory_order_relaxed), 255, Variable::IP::eigrp);
        }
    }

    void EigrpInterface::stopHello()
    {
        if (helloTimerId != 0)
        {
            TimeManager::getInstance().cancelTimer(helloTimerId); 
            helloTimerId = 0;
        }
        {
            std::shared_lock<std::shared_mutex> neighborLock(neighborMutex);
            for (auto& [_, neighbor] : neighbors)
            {
                uint32_t holdTimerId = neighbor->holdTimerId.load(std::memory_order_relaxed);
                if (holdTimerId != 0)
                {
                    TimeManager::getInstance().cancelTimer(holdTimerId);
                    neighbor->holdTimerId.store(0, std::memory_order_release);
                }
            }
        }
        helloTimerActive.store(false, std::memory_order_release);
    }

    void EigrpInterface::startHoldTimer(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, uint16_t holdTime)
    {
        uint32_t holdTimerId = neighbor->holdTimerId.load(std::memory_order_relaxed);
        if (holdTimerId != 0)
        {
            TimeManager::getInstance().cancelTimer(holdTimerId);
        }

        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(holdTime);
        neighbor->holdTimerId.store(TimeManager::getInstance().addTimer(expirationTime, [this, neighbor, neighborIp]()
                                                                   { handleHoldTimeExpire(neighbor, neighborIp); }), std::memory_order_release);
    }

    void EigrpInterface::handleHoldTimeExpire(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp)
    {
        if (neighbor)
        {
            if (eigrpProcess->getConfigs()->nonStopForwarding.load(std::memory_order_relaxed))
            {
                gracefulRestart(neighbor, neighborIp);
            }
            else
            {
                handleNeighborDown(neighbor, neighborIp);
            }
        }
    }

    void EigrpInterface::startActiveTimer(RoutingTable::Eigrp* route)
    {
        auto key = route->network + "/" + std::to_string(route->mask);
        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(eigrpProcess->getConfigs()->activeTime);
        // TODO Add SRTT into timeout

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
        {
            std::shared_lock<std::shared_mutex> lock(eigrpProcess->interfaceMutex);
            eigrpProcess->forEachInterface([&](InterfaceType, float, EigrpInterface* interface)
            {
                for (auto& neighborIp : queryInfo.pendingReplies)
                {
                    if (interface->neighbors.find(neighborIp) != interface->neighbors.end())
                    {
                        interface->sendQueryToNeighbor(interface->neighbors[neighborIp], neighborIp, {route});
                    }
                }
            });
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

            std::vector<std::pair<const ByteString, EigrpConfigs::NeighborInfo*>> neighborList;
            
            // Remove unresponsive neighbors
            for (const auto& neighborIp : queryInfo.pendingReplies)
            {
                std::shared_lock<std::shared_mutex> lock(eigrpProcess->interfaceMutex);
                eigrpProcess->forEachInterface([&](InterfaceType, float, EigrpInterface* interface)
                {
                    std::shared_lock<std::shared_mutex> neighborLock(neighborMutex);
                    auto it = interface->neighbors.find(neighborIp);
                    if (it != interface->neighbors.end())
                    {
                        Logger::getInstance().error() << "Neighbor " << neighborIp.toHex() << " did not respons. Removing from EIGRP." << std::endl;
                        neighborList.push_back(std::pair(it->first, it->second));
                    }
                });
            }

            // Handle neighbor down for each
            for (const auto& [address, neighbor] : neighborList)
            {
                handleNeighborDown(neighbor, address);
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
        ByteString key = destination + "/" + std::to_string(mask);

        {
            std::lock_guard<std::mutex> lock(activeTimerMutex);
            auto it = activeTimers.find(key);
            if (it != activeTimers.end())
            {
                TimeManager::getInstance().cancelTimer(it->second);
                activeTimers.erase(it);
            }
        }
    }

    void EigrpInterface::cancelSIATimer(const ByteString& destination, uint8_t mask)
    {
        ByteString key = destination + "/" + std::to_string(mask);

        {
            std::lock_guard<std::mutex> lock(activeTimerMutex);
            auto it = siaTimers.find(key);
            if (it != siaTimers.end())
            {
                TimeManager::getInstance().cancelTimer(it->second);
                siaTimers.erase(it);
            }
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
            std::lock_guard<std::mutex> lock(neighbor->reliableMutex);
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

        // Schedule a retransmission timer
        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(timeout * 1000));
        uint32_t timerId = TimeManager::getInstance().addTimer(expirationTime, [this, neighbor, neighborIp, sequenceNumber]()
        {
            handleRetransmissionTimeout(neighbor, neighborIp, sequenceNumber);
        });

        // Update the timer ID in ReliablePacketInfo
        {
            std::lock_guard<std::mutex> lock(neighbor->reliableMutex);
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
            std::lock_guard<std::mutex> lock(neighbor->reliableMutex);
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
            // Construct and send the retransmission packet
            retransmissionPacket.Layer4.push_back(pktInfoCopy.packet.eigrp);
            if (currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed))
            {
                currentInterface->ipPacket->setIPHeader(retransmissionPacket, pktInfoCopy.packet.destination, nullptr, nullptr, getConfigs().DSCP.load(std::memory_order_relaxed), 255, Variable::IP::eigrp);
            }
        }

        // Increment retransmission timer safely
        uint32_t newTimerId;
        {
            std::lock_guard<std::mutex> dataLock(neighbor->reliableMutex);
            auto pktIt = neighbor->reliablePackets.find(sequenceNumber);
            if (pktIt != neighbor->reliablePackets.end())
            {
                pktIt->second.retransmissionCount += 1;
                pktIt->second.sendTime = std::chrono::steady_clock::now(); // Update the send time.
            }
        }

        // Restart the retransmission timer
        {
            std::unique_lock<std::mutex> dataLock(neighbor->reliableMutex);
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
            if (eigrpProcess->addressFamily == AddressFamily::IPv4)
            {
                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for IPv4 next Hop");
                route->nextHop = value.substr(start, 4);
                start += 4;
            }
            else if (eigrpProcess->addressFamily == AddressFamily::IPv6)
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
                if (eigrpProcess->addressFamily == AddressFamily::IPv4) 
                {
                    if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Origin Router (IPv4).");
                    route->originRouter = value.substr(start, 4);
                    start += 4;
                }
                else if (eigrpProcess->addressFamily == AddressFamily::IPv6) 
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
            size_t standardLength = (eigrpProcess->addressFamily == AddressFamily::IPv4) ? 4 : 16;
            if (route->network.size() < standardLength)
            {
                route->network += ByteString(standardLength - route->network.size(), '\x00');
            }
            
            eigrpProcess->addRouteMetric(configs.localMetric, route);
            route->hopCount++;

            return route;
        }
        catch (const std::exception& e)
        {
            Logger::getInstance().error() << "DecodedRoute Error: " << e.what() << std::endl;
            throw;
        }
    }

    void EigrpInterface::updateRoutingTable(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, const std::vector<RoutingTable::Eigrp*>& routes)
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
            routeInfo.eigrpInterface = this;
            routeInfo.bandwidthMetric = route->bandwidth;
            routeInfo.delayMetric = route->delay;
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
                newRoute->adminDistance = eigrpProcess->getConfigs()->adminDistance;
            }
            else 
            {
                Logger::getInstance().error() << "Unknown route type: " << route->routeType << std::endl;
                continue; // Skip unknown route type
            }

            // Check for existing routes and determine if an update is needed
            auto existingRoute = eigrpProcess->routingInstance->routingTable.getEigrpRoute(route->network, route->mask, eigrpProcess->addressFamily, eigrpProcess->asNumber);
            bool routeChange = !existingRoute || (existingRoute->feasibleDistance != newRoute->feasibleDistance);

            if (routeChange)
            {
                // Update the global EIGRP table
                eigrpProcess->routingInstance->routingTable.updateEigrp(newRoute, eigrpProcess->addressFamily, eigrpProcess->asNumber);

                // Notify neighbors about the route change
                updatedRoutes.emplace_back(newRoute);
            }
        }
        if (!updatedRoutes.empty())
        {
            // Notify neighbors about the updates routes
            eigrpProcess->notifyRoutingChange(updatedRoutes, /*isRemoval*/ false);
        }
        for (const auto& route : routes)
        {
            eigrpProcess->routingInstance->routingTable.updateEigrp(route, eigrpProcess->addressFamily, eigrpProcess->asNumber);
        }
    }

    void EigrpInterface::handleNeighborDown(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp)
    {
        if (!neighbor) return;
        
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

        // Clear reliable packets and sequence tracking
        {
            std::lock_guard<std::mutex> lock(neighbor->reliableMutex);
            neighbor->retransmissionTimers.clear();
            neighbor->reliablePackets.clear();
            neighbor->sequenceList.clear();
        }

        // Process all effected routes
        std::vector<std::pair<ByteString, uint8_t>> affectedRoutes;
        {
            for (auto& [destination, entry] : eigrpProcess->topologyTable->getTopologyEntries())
            {
                // If this neighbor was advertising the route
                if (entry->routesByNeighbor.find(neighborIp) != entry->routesByNeighbor.end())
                {
                    affectedRoutes.push_back(std::pair(destination, entry->prefixLength));
                }
            }
        }

        // Remove the neighbor rotues from topology
        eigrpProcess->topologyTable->handleNeighborDown(neighborIp);

        // Check affected rotues and handle failuresS
        std::vector<RoutingTable::Eigrp*> activeRoutes;
        for (const auto& [destination, prefixLength] : affectedRoutes)
        {

            // Find the best remaining successor
            auto bestRoute = eigrpProcess->topologyTable->findBestRoute(destination, 0);
            
            if (bestRoute.has_value())
            {
                updateRoutingTableForDestination(destination, prefixLength);
            }
            else
            {
                // Look up the existing EIGRP route from the routing table
                auto existingRoute = eigrpProcess->routingInstance->routingTable.getEigrpRoute(destination, prefixLength, eigrpProcess->addressFamily, eigrpProcess->asNumber);

                if (existingRoute)
                {
                    // Mark the route as active
                    existingRoute->stuckInActive = true;
                    startActiveTimer(existingRoute);
                    startSIATimer(existingRoute);

                    // Send query to all neighbors
                    activeRoutes.push_back(existingRoute);
                }
            }
        }

        // Remove neighbor from neighbor list
        {
            std::unique_lock<std::shared_mutex> lock(neighborMutex);
            neighbors.erase(neighborIp);
        }

        if (!neighbor->unicast)
        {
            delete neighbor;
            neighbor = nullptr;
        }

        // Create query objects
        for (const auto& route : activeRoutes)
        {
            ByteString key = route->network + "/" + std::to_string(route->mask);
            std::shared_lock<std::shared_mutex> lock(eigrpProcess->interfaceMutex);
            auto& queryInfo = eigrpProcess->outstandingReplies[key];
            route->delay = std::numeric_limits<uint32_t>::max();
            queryInfo.route = route;
            eigrpProcess->forEachInterface([&](InterfaceType, float, EigrpInterface* eigrpPtr)
            {
                std::shared_lock<std::shared_mutex> neighborLock(eigrpPtr->neighborMutex);
                for (const auto& [address, _] : neighbors)
                {
                    queryInfo.pendingReplies.insert(address);
                }
            });
        }

        if (!activeRoutes.empty())
        {
            std::shared_lock<std::shared_mutex> lock(eigrpProcess->interfaceMutex);
            eigrpProcess->forEachInterface([&](InterfaceType, float, EigrpInterface* eigrpPtr)
            {
                eigrpPtr->sendQueryToNeighbors(activeRoutes);
            });
        }
    }
    
    void EigrpInterface::gracefulRestart(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp)
    {
        auto expireTime = std::chrono::steady_clock::now() + std::chrono::seconds(eigrpProcess->getConfigs()->purgeTime);
        uint32_t gracefulTimerId = TimeManager::getInstance().addTimer(expireTime, [&]() {
            handleNeighborRestart(neighbor, neighborIp);
        });
        neighbor->gracefulRestartTimerId.store(gracefulTimerId, std::memory_order_release);
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
            std::lock_guard<std::mutex> lock(neighbor->reliableMutex);
            neighbor->reliablePackets.clear();
            neighbor->sequenceList.clear();
            eigrpProcess->topologyTable->removeRoutesFromNeighbor(neighborIp);
        }

        // Reinitialize neighbor state
        {
            neighbor->neighborState.store(EigrpConfigs::NeighborState::DOWN, std::memory_order_release);
            neighbor->initComplete.store(false, std::memory_order_release);
        }
        changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::DOWN);
    }

    void EigrpInterface::updateRoutingTableForDestination(const ByteString &destination, uint8_t prefixLength)
    {
        auto entry = eigrpProcess->topologyTable->getEntryForRoute(destination);
        if (!entry)
        {
            eigrpProcess->routingInstance->routingTable.removeEigrp(destination, prefixLength, eigrpProcess->addressFamily, eigrpProcess->asNumber);
            return;
        }
        
        // Find the successor route
        auto successorIt = std::find_if(entry->routesByNeighbor.begin(), entry->routesByNeighbor.end(),
                                        [](const auto &pair) { return pair.second.isSuccessor; });

        if (successorIt != entry->routesByNeighbor.end())
        {
            // Update the routing table accordingly
            RoutingTable::Eigrp* newRoute = new RoutingTable::Eigrp;
            newRoute->bandwidth = successorIt->second.bandwidthMetric;
            newRoute->delay = successorIt->second.delayMetric;
            newRoute->hopCount = successorIt->second.hopCount;
            newRoute->mtu = currentInterfaceInfo->mtu.load(std::memory_order_relaxed);
            newRoute->reliability = 255;
            newRoute->load = eigrpProcess->getConfigs()->variance.load(std::memory_order_relaxed);
            newRoute->network = destination;
            newRoute->mask = entry->prefixLength;
            newRoute->nextHop = successorIt->second.nextHop;
            newRoute->metric = successorIt->second.feasibleDistance;

            // Update the global routing table
            if (eigrpProcess->routingInstance->routingTable.addEigrp(newRoute, eigrpProcess->addressFamily, eigrpProcess->asNumber))
            {
                eigrpProcess->notifyRoutingChange({newRoute}, /*isRemoval*/ false);
            }
        }
        else
        {
            eigrpProcess->routingInstance->routingTable.removeEigrp(destination, prefixLength, eigrpProcess->addressFamily, eigrpProcess->asNumber);
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
            std::unique_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
            neighbor->rttvar = (1.0 - beta) * neighbor->rttvar + beta * std::abs(neighbor->srtt - rttSample);
            neighbor->srtt = (1.0 - alpha) * neighbor->srtt + alpha * rttSample;
            neighbor->rto = neighbor->srtt + std::max(0.1, 4.0 * neighbor->rttvar);
            neighbor->rto = std::clamp(neighbor->rto, 1.0, 60.0); // Bounds: 1s to 60s
        }
    }

    uint32_t EigrpInterface::getNextSequenceNumber() 
    {
        return nextSequenceNumber.fetch_add(1, std::memory_order_acquire);
    }

    void EigrpInterface::setupReliablePacket(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, const EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet& packet, uint32_t sequenceNum)
    {
        if (!neighbor->processAcks.load(std::memory_order_relaxed)) return;

        {
            std::lock_guard<std::mutex> lock(neighbor->reliableMutex);
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
            std::lock_guard<std::mutex> lock(neighbor->reliableMutex);
            neighbor->reliablePackets[sequenceNum] = pktInfo;
        }

        Logger::getInstance().debug() << "Reliable packet setup for neighbor " << neighbor->ipAddress.toHex() << " with sequence number " << sequenceNum << std::endl;
    }

    RoutingTable::Eigrp* EigrpInterface::encodeSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute)
    {
        uint32_t bandwidth = eigrpProcess->getConfigs()->lowestBandwidth.load(std::memory_order_relaxed);
        uint32_t delay = currentInterfaceInfo->delay.load(std::memory_order_relaxed);

        RoutingTable::Eigrp* route = new RoutingTable::Eigrp();
        route->nextHop = configs.nextHopSelf.load(std::memory_order_relaxed) ? getInterfaceIp() : ByteString(summaryRoute.summary->network.size(), '\x00');
        route->bandwidth = (10000000 / bandwidth) * 256;
        route->delay = (delay / 10) * 256;
        route->mtu = currentInterfaceInfo->mtu.load(std::memory_order_relaxed);
        route->hopCount = 0;
        route->reliability = 255;
        route->load = eigrpProcess->getConfigs()->variance.load(std::memory_order_relaxed);
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
        getConfigs().isPassive.store(passive, std::memory_order_release);
        if (passive)
        {
            stopHello();
        }
        else
        {
            helloStartTime = std::chrono::steady_clock::now();
            startHelloHelper();
            sendHelloPacket();
        }

        Logger::getInstance().info() << "Interface set to " << (passive ? "passive" : "active") << " modeo." << std::endl;
    }

    void EigrpInterface::enableMulticast()
    {
        if (!configs.multicastEnabled.load(std::memory_order_relaxed))
        {
            configs.multicastEnabled.store(true, std::memory_order_release);
        }
    }

    void EigrpInterface::disableMulticast()
    {
        // Check if multicast is already disabled
        if (!configs.multicastEnabled.load(std::memory_order_relaxed)) return;

        configs.multicastEnabled.store(false, std::memory_order_release);
        for (auto it = neighbors.begin(); it != neighbors.end();)
        {
            if (!it->second->unicast)
            {
                handleNeighborDown(it->second, it->first);
            }
            else
            {
                it++;
            }
        }
    }

    void EigrpInterface::addNeighbor(const ByteString& ipAddress, const ByteString& macAddress, bool unicast)
    {
        // Add neighbor only if it doesn't already exist
        std::shared_lock<std::shared_mutex> readLock(neighborMutex);

        auto it = neighbors.find(ipAddress);
        if (it == neighbors.end()) // Double check
        {
            auto* neighbor = new EigrpConfigs::NeighborInfo(ipAddress, unicast);
            neighbor->macAddress = macAddress;
            neighbors[ipAddress] = neighbor;
        }
    }

    void EigrpInterface::addUnicastNeighbor(const ByteString& neighborIp)
    {
        {
            std::unique_lock<std::shared_mutex> neighborLock(neighborMutex);

            // Check if neighbor already exists
            if (neighbors.find(neighborIp) != neighbors.end())
            {
                // Delete neighbor and replace it with a unicast neighbor if neighbor exists
                delete neighbors[neighborIp];
                neighbors[neighborIp] = nullptr;
                neighbors.erase(neighborIp);
                neighbors[neighborIp] = new EigrpConfigs::NeighborInfo(neighborIp, true);
            }
            else
            {
                // Add the neighbor normally if not present
                neighbors[neighborIp] = new EigrpConfigs::NeighborInfo(neighborIp, true);
            }
        }

        // Disable mutlicast if enabled
        if (configs.multicastEnabled.load(std::memory_order_relaxed))
        {
            disableMulticast();
        }
    }

    void EigrpInterface::removeUnicastNeighbor(const ByteString& neighborIp)
    {
        // Find the neighbor and remove it if present
        EigrpConfigs::NeighborInfo* neighbor = nullptr;
        {
            std::unique_lock<std::shared_mutex> lock(neighborMutex);
            auto neighborIt = neighbors.find(neighborIp);
            if (neighborIt != neighbors.end() && neighborIt->second->unicast)
            {
                neighbor = neighborIt->second;
            }
        }

        // Remove the neighbor if found and is in unciast
        if (neighbor)
        {
            handleNeighborDown(neighbor, neighborIp);
        }

        // Enable multicast if neighbor are empty
        {
            std::shared_lock<std::shared_mutex> lock(neighborMutex);
            if (neighbors.empty())
            {
                enableMulticast();
            }
        }
    }

    EigrpConfigs::NeighborInfo* EigrpInterface::getNeighborInfo(const ByteString& neighborIp)
    {
        auto it = neighbors.find(neighborIp);
        if (it != neighbors.end())
        {
            return it->second;
        }
        return nullptr;
    }

    ByteString EigrpInterface::getMulticast()
    {
        return (eigrpProcess->addressFamily == AddressFamily::IPv4) ? Variable::Multicast::Eigrp::address : Variable::Multicast::Eigrp::addressv6;
    }

    
    bool EigrpInterface::isRouteAdvertised(ByteString& network, uint8_t mask)
    {
        std::shared_lock<std::shared_mutex> lock(neighborMutex);
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

    inline ByteString EigrpInterface::getInterfaceIp()
    {
        if (currentInterfaceInfo)
        {
            std::shared_lock<std::shared_mutex> lock(currentInterfaceInfo->ipMutex);
            return (eigrpProcess->addressFamily == AddressFamily::IPv4) ? currentInterfaceInfo->ipv4.ipAddress : currentInterfaceInfo->ipv6.ipAddress;
        }
        return "";
    }

#pragma endregion

#pragma region ClassicEigrp

    

    void ClassicEigrp::initializeEigrp()
    {
        // Call base class initialziation
        Eigrp::initializeEigrp();

        // Classic-specific configuration
        configs.autoSummarizationEnabled.store(true, std::memory_order_release);
        Logger::getInstance().info() << "Classic EIGRP-specific initialization complete" << std::endl;
    }

    void ClassicEigrp::shutdown()
    {
        Logger::getInstance().info() << "Shutting down Classic EIGRP." << std::endl;

        // Call base class shutdown
        Eigrp::shutdown();

        // Clear network configurations
        configs.networks.clear();
        Logger::getInstance().info() << "Cleared all Classic-configured networks." << std::endl;
    }

#pragma endregion

#pragma region NamedEigrp

    NamedEigrp::NamedEigrp(uint32_t& as, AddressFamily af, const ByteString& name, VirtualRouter* vrf, bool multicast)
        : Eigrp(as, af, vrf), processName(name) {}

    void NamedEigrp::initializeEigrp() {
        // Call base class initialization
        Eigrp::initializeEigrp();

        // Named-soecific configuration
        configs.autoSummarizationEnabled.store(false, std::memory_order_release);
        Logger::getInstance().info() << "Initialized Named EIGRP (" << processName << ")." << std::endl;
    }

    void NamedEigrp::shutdown() {
        Logger::getInstance().info() << "Shutting down Named EIGRP (" << processName << ")." << std::endl;

        // Call base class shutdown
        Eigrp::shutdown();

        // Clear interface configurations
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
        std::vector<RoutingTable::Eigrp*> bestRoutes;

        std::lock_guard<std::mutex> lock(tableMutex);
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

        // Step 2: make sure feasible routes are still present
        bool feasibleFound = false;
        for (auto& [neighbor, route] : entry->routesByNeighbor)
        {
            if (!route.notFeasible)
            {
                feasibleFound = true;
                break; // Found a feasible route
            }
        }
        if (!feasibleFound)
        {
            return; // No new feasible routes
        }

        // Step 3: Determin successors and feasible successors
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

            if (!route.isSuccessor && !route.isFeasibleSuccessor)
            {
                route.notFeasible = true;
            }
            else
            {
                route.notFeasible = false;
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

        // If no remaining neighbors, remove route completely
        if (entry->routesByNeighbor.empty())
        {
            Logger::getInstance().info() << "No remaining routes for destination: " << destination.toHex() << std::endl;
            topologyEntries.erase(it);
            return;
        }

        // Recalculate successors and feasible successors
        updateSuccessorAndFeasibleSuccessors(entry);
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
Protocol::EigrpInterface* currentEigrpInterface;


#pragma endregion
