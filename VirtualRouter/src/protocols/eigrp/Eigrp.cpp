#include <Eigrp.h>
#include <Encapsulation.h>
#include <VirtualRouter.h>
#include <Interface.h>
#include <algorithm>
#include <Global.h>
#include <IPPacket.h>

#pragma region Eigrp

namespace Protocol
{
    Eigrp::Eigrp(uint32_t as, AddressFamily af, VirtualRouter* vrf, bool named) : routingInstance(vrf), addressFamily(af), asNumber(as), namedMode(named)
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

        Logger::getInstance().info() << "EIGRP process initiated." << std::endl;
    }

    void Eigrp::addCommonTlvs(EigrpHeader& hdr, EigrpInterface& cfg, bool isUpdate, bool isAck, const ByteString neighborIp, uint32_t sequenceNumber)
    {
        if (!isAck)
        {
            // Parameter TLV (K-values and Hold Time)
            ByteString parameters = calculateParameters(cfg.configs->holdTime.load(std::memory_order_relaxed));
            hdr.options.emplace_back(
                Variable::Eigrp::Option::parameter,
                Functions::numToByte(parameters.size() + 4, 2),
                std::move(parameters)
            );

            // Version TLV
            ByteString version = Variable::Eigrp::Version::release + Variable::Eigrp::Version::tls;
            hdr.options.emplace_back(
                Variable::Eigrp::Option::version,
                Functions::numToByte(version.size() + 4, 2),
                std::move(version)
            );

            // Sequence TLV
            if (isUpdate)
            {
                ByteString sequence = Functions::numToByte(neighborIp.size(), 1) + neighborIp;
                hdr.options.emplace_back(
                    Variable::Eigrp::Option::sequence,
                    Functions::numToByte(sequence.size() + 4, 2),
                    std::move(sequence)
                );

                ByteString multicast = Functions::numToByte(sequenceNumber, 4);
                hdr.options.emplace_back(
                    Variable::Eigrp::Option::multicastSequence,
                    Functions::numToByte(multicast.size() + 4, 2),
                    std::move(multicast)
                );
            }
        }

        // Create other TLVs if applicable.

        // Stub TLV
        if (isStub())
        {
            std::shared_lock<std::shared_mutex> configLock(configs.configsMutex);
            ByteString stubValue = cfg.encodeStubOption(configs.stubConfig);
            hdr.options.emplace_back(
                Variable::Eigrp::Option::stub,
                Functions::numToByte(stubValue.size() + 4, 2),
                std::move(stubValue)
            );
        }

        // Authentication TLV
        if (cfg.configs->authKey.fullyEnabled.load(std::memory_order_relaxed))
        {
            EigrpHeader::Option authTLV = cfg.generateAuthenticatedTLV(hdr);
            if (authTLV.option != ByteString(1, 0x00))
            {
                hdr.options.push_back(std::move(authTLV));
            }
        }
    }

    void Eigrp::eigrpHello(EigrpHeader& eigrp, EigrpInterface& eigrpInt, const ByteString& neighborIp, uint32_t sequenceNumber,  bool ack, bool update)
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
        addCommonTlvs(eigrp, eigrpInt, update, ack, neighborIp, sequenceNumber);
    }

    void Eigrp::eigrpUpdate(EigrpHeader &eigrp, uint32_t sequenceNum, bool init, bool conditional, bool restart, bool endoftable, bool query, bool reply)
    {
        // Create an initiated eigrp update header
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

    uint64_t Eigrp::calculateMetric(uint32_t bandwidth, uint8_t load, uint32_t delay, uint8_t reliability, uint8_t hopCount)
    {
        if (bandwidth == 0) return std::numeric_limits<uint32_t>::max();

        EigrpConfigs::KValue kvalue;
        {
            std::shared_lock<std::shared_mutex> configMutex(configs.configsMutex);
            kvalue = configs.kvalue;
        }

        // Calculate individual components of the metric
        uint64_t bandwidthMetric = configs.wideMetric.load(std::memory_order_relaxed) / bandwidth;
        uint64_t delayMetric = delay;
        uint64_t loadMetric = (kvalue.k2_Load * bandwidthMetric) / (256 - load);

        uint64_t compositeMetric = (kvalue.k1_Bandwidth * bandwidthMetric) +
                                 loadMetric +
                                 (kvalue.k3_Delay * delayMetric);

        // Account for K5 (optional scaling)
        if (kvalue.k5_MTU != 0 && (reliability + kvalue.k4_Reliability) > 0) {
            compositeMetric *= kvalue.k5_MTU / (reliability + kvalue.k4_Reliability);
        }

        // Final scaling for the metric
        compositeMetric = std::min(compositeMetric, 16777215UL);
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
            InterfaceConfigs& interfaceInfo = interface->configs;
            float intID = interfaceInfo.id;
            ByteString ipv6Address = interfaceInfo.ipv4.getAddress();
            ByteString ipv4Address = interfaceInfo.ipv6.getGlobalUnicast();
            EigrpInterface* instance = nullptr;
            EigrpInterfaceInstance* interfaceInstance = nullptr;
            InterfaceType type = interfaceInfo.interfaceType;

            if (interface->eigrpInterfaceList.find(asNumber) == interface->eigrpInterfaceList.end() || (!interface->eigrpInterfaceList.find(asNumber)->second))
            {
                interfaceInstance = new EigrpInterfaceInstance();
                interface->eigrpInterfaceList[asNumber] = interfaceInstance;
            }
            else
            {
                interfaceInstance = interface->eigrpInterfaceList[asNumber];
            }

            EigrpConfigs::InterfaceConfigs* intConfig;
            auto pairIt = eigrpInterfaceConfigList.find({type, intID});
            if (pairIt != eigrpInterfaceConfigList.end())
            {
                intConfig = pairIt->second;
            }
            else
            {
                // INITIALIZE EIGRP CONFIGURATIONS
                EigrpConfigs::InterfaceConfigs* intConfig = nullptr;
                if (namedMode)
                {
                    intConfig = new EigrpConfigs::InterfaceConfigs(type, intID);
                }
                else
                {
                    intConfig = interface->getEigrpConfig(asNumber, addressFamily, false);
                }
                eigrpInterfaceConfigList[{type, intID}] = intConfig;
            }

            if (addressFamily == AddressFamily::IPv4 && !ipv4Address.empty())
            {
                instance = new EigrpInterface(*this, eigrpInterfaceConfigList[{type, intID}], interface);
                interfaceInstance->IPv4 = instance;
                eigrpInterfaceList[{type, intID}] = instance;
                interface->eigrpInterfaceList[asNumber] = interfaceInstance;
                return instance;
            }
            else if (addressFamily == AddressFamily::IPv6 && !ipv6Address.empty())
            {
                instance = new EigrpInterface(*this, intConfig, interface);
                interfaceInstance->IPv6 = instance;
                eigrpInterfaceList[{type, intID}] = instance;
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
            for (auto it = eigrpInterfaceList.begin(); it != eigrpInterfaceList.end();)
            {
                if (it->second && it->second->currentInterface && it->second->currentInterface->shutdownFlag.load(std::memory_order_relaxed))
                {
                    delete it->second;
                    it = eigrpInterfaceList.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        // Iterate through all interfaces
        {
            std::unique_lock<std::shared_mutex> lock(routingInstance->interfaceMutex);
            for (const auto& [id, interface] : routingInstance->interfaceList)
            {
                // Add the interface as existing
                if (interface && !interface->shutdownFlag.load(std::memory_order_relaxed))
                {
                    ByteString ipAddress;
                    bool ipv6Contained = false;
                    auto& ipInfo = interface->configs;

                    if (namedMode)
                    {
                        std::shared_lock<std::shared_mutex> lock(interfaceMutex);
                        ipv6Contained = eigrpInterfaceConfigList.contains({ipInfo.interfaceType, ipInfo.id}) && !eigrpInterfaceConfigList[{ipInfo.interfaceType, ipInfo.id}]->shutdown;
                    }

                    ipAddress = ipInfo.ipv4.getAddress();
                    if (!ipv6Contained)
                    {
                        ipv6Contained = ipInfo.eigrp.ipv6AutonomousSystems.contains(asNumber) &&
                            interface->routingInstance == routingInstance;
                    }

                    // Test the address and add the interface if approved
                    if (testAddress(ipAddress) || ipv6Contained)
                    {
                        // Add interface to eigrp
                        std::unique_lock<std::shared_mutex> interfaceLock(interfaceMutex);
                        auto eigrpInterfaceIt = interface->eigrpInterfaceList.find(asNumber);
                        if (eigrpInterfaceIt == interface->eigrpInterfaceList.end() || 
                            eigrpInterfaceList.find(id) == eigrpInterfaceList.end())
                        {
                            auto* newInterface = addEigrpInterface(interface);
                            if (!newInterface)
                            {
                                continue;
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
                        std::shared_lock<std::shared_mutex> interfaceLost(interfaceMutex);
                        auto intIt = eigrpInterfaceList.find(id);
                        
                        if (intIt != eigrpInterfaceList.end())
                        {
                            // Delete interface if no static neighbors are found.
                            delete eigrpInterfaceList[id];
                            eigrpInterfaceList[id] = nullptr;
                            eigrpInterfaceList.erase(id);
                        }
                    }
                }
                else
                {
                    // Remove shutdown interface
                    if (eigrpInterfaceList.find(id) != eigrpInterfaceList.end())
                    {
                        delete eigrpInterfaceList[id];
                        eigrpInterfaceList[id] = nullptr;
                        eigrpInterfaceList.erase(id);
                    }
                }
            };
        }
        updateRoutingTableForConnected();
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
        std::vector<EigrpConfigs::RoutingUpdate> updatedRoutes{};
        {
            if (!routingInstance) return;

            RoutingTable& routingTable = routingInstance->routingTable;
            std::vector<ByteString> connectedNetworks{};
            
            std::unordered_map<float, EigrpInterface*> currentEigrpInterface;
            // get Eigrp Interface list
            if (eigrpInterface)
            {
                if (!eigrpInterface->currentInterface->shutdownFlag.load(std::memory_order_relaxed))
                {
                    auto& ipInfo = eigrpInterface->currentInterface->configs;
                    currentEigrpInterface[ipInfo.id] = eigrpInterface;
                }
            }

            {
                auto updateRoutesForInterface([&](InterfaceType, float, EigrpInterface* eigrpInterfacePtr)
                {
                    if (!eigrpInterfacePtr || !eigrpInterfacePtr->currentInterface || !routingInstance) return;

                    if (!eigrpInterfacePtr->currentInterface->shutdownFlag.load(std::memory_order_relaxed))
                    {
                        auto& interfaceInfo = eigrpInterfacePtr->currentInterface->configs;
                        uint32_t eigrpBw = configs.lowestBandwidth.load(std::memory_order_relaxed);
                        uint16_t mtu = addressFamily == AddressFamily::IPv4 
                            ? interfaceInfo.ipv4.mtu.load(std::memory_order_relaxed)
                            : interfaceInfo.ipv6.mtu.load(std::memory_order_relaxed);
                        uint8_t connectedMask;
                        ByteString connectedNetwork;

                        {
                            if (addressFamily == AddressFamily::IPv4)
                            {
                                uint8_t mask = interfaceInfo.ipv4.getMask();
                                connectedNetwork = Functions::computeNetworkAddress(interfaceInfo.ipv4.getAddress(), mask);
                                connectedMask = mask;
                            }
                            else if (addressFamily == AddressFamily::IPv6)
                            {
                                auto address = interfaceInfo.ipv6.getGlobalUnicastPair();
                                if (address.first.empty()) return;
                                connectedNetwork = Functions::computeNetworkAddress(address.first, address.second);
                                connectedMask = address.second;
                            }
                            else return;
                        }

                        // Compute the connected network
                        connectedNetworks.push_back(connectedNetwork);

                        // Create EIGRP route entry
                        RoutingTable::Eigrp* connectedRoute = new RoutingTable::Eigrp(eigrpInterfacePtr->interfaceKey);
                            connectedRoute->bandwidth = ( 10000000 / eigrpBw ) * 256;
                            connectedRoute->delay = 0;
                            connectedRoute->hopCount = 0;
                            connectedRoute->mtu = mtu;
                            connectedRoute->reliability = 255;
                            connectedRoute->load = configs.variance.load(std::memory_order_relaxed);
                            connectedRoute->network = connectedNetwork;
                            connectedRoute->mask = connectedMask;
                            connectedRoute->nextHop = (eigrpInterface && eigrpInterface->configs->nextHopSelf.load(std::memory_order_relaxed)) ? eigrpInterface->getInterfaceIp() : ByteString(connectedNetwork.size(), '\x00'); // indicates directly connected
                            connectedRoute->routeType = RoutingTable::Eigrp::RouteType::CONNECTED;

                        auto existingRoute = routingInstance->routingTable.getEigrpRoute(connectedNetwork, connectedRoute->mask, addressFamily, asNumber);

                        bool hasChanged = !existingRoute ||
                                        existingRoute->feasibleDistance != connectedRoute->feasibleDistance ||
                                        existingRoute->mask != connectedRoute->mask;

                        if (hasChanged)
                        {
                            // Insert into Routing Table
                            routingTable.addEigrp(connectedRoute, addressFamily, asNumber);
                            
                            // Advertise the connected route to eigrp neighbors
                            updatedRoutes.emplace_back(std::move(connectedRoute), false);
                        }
                        else
                        {
                            delete connectedRoute;
                            connectedRoute = nullptr;
                        }
                    }
                    else
                    {
                        auto removalRoute = routingInstance->routingTable.getEigrpRoute(
                            addressFamily == AddressFamily::IPv4 ? eigrpInterfacePtr->currentInterface->configs.ipv4.getAddress()
                            : eigrpInterfacePtr->currentInterface->configs.ipv6.getGlobalUnicast(),
                            addressFamily == AddressFamily::IPv6 ? eigrpInterfacePtr->currentInterface->configs.ipv4.getMask()
                            : eigrpInterfacePtr->currentInterface->configs.ipv6.getGlobalUnicastMask(), addressFamily, asNumber);
                        if (removalRoute)
                        {
                            routingInstance->routingTable.removeEigrp(
                                addressFamily == AddressFamily::IPv4 ? eigrpInterfacePtr->currentInterface->configs.ipv4.getAddress()
                                : eigrpInterfacePtr->currentInterface->configs.ipv6.getGlobalUnicast(),
                                addressFamily == AddressFamily::IPv6 ? eigrpInterfacePtr->currentInterface->configs.ipv4.getMask()
                                : eigrpInterfacePtr->currentInterface->configs.ipv6.getGlobalUnicastMask(), addressFamily, asNumber);
                            updatedRoutes.emplace_back(std::move(removalRoute), true);
                        }
                    }
                });

                {
                    if (eigrpInterface)
                    {
                        updateRoutesForInterface(InterfaceType::UNDEFINED, 0.0, eigrpInterface);
                    }
                    else
                    {
                        std::shared_lock<std::shared_mutex> lock(interfaceMutex);
                        for (const auto& [type, eigrpInterfacePtr] : eigrpInterfaceList)
                        {
                            updateRoutesForInterface(type.first, type.second, eigrpInterfacePtr);
                        }
                    }
                }
            }

            // Remove any routes that are no longer exist
            for (const auto& route : routingInstance->routingTable.getAllEigrpRoutes(addressFamily, asNumber))
            {
                if (route->routeType == RoutingTable::Eigrp::RouteType::CONNECTED)
                {
                    auto networkIt = std::find(connectedNetworks.begin(), connectedNetworks.end(), route->network);
                    if (networkIt == connectedNetworks.end())
                    {
                        updatedRoutes.emplace_back(route, true);
                    }
                }
            }
        }

        // Notify neighbors
        notifyRoutingChange(updatedRoutes);

        // Remove routes from table after neighbor.
        for (const auto& route : updatedRoutes)
        {
            if (route.withdraw)
            {
                routingInstance->routingTable.removeEigrp(route.route->network, route.route->mask, addressFamily, asNumber);
            }
        }

    }

    void Eigrp::notifyRoutingChange(const std::vector<EigrpConfigs::RoutingUpdate>& changedRoutes)
    {
        Logger::getInstance().info() << "Notifying all neighbors for route changes" << std::endl;

        {
            std::shared_lock<std::shared_mutex> lock(interfaceMutex);

            // Adjust summaries based on added/removed routes
            for (const auto& [_, eigrpInterfacePtr] : eigrpInterfaceList)
            {
                if (!eigrpInterfacePtr || !eigrpInterfacePtr->currentInterface) continue;
                if (eigrpInterfacePtr->currentInterface->shutdownFlag.load(std::memory_order_relaxed)) continue;
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
                                unicastNeighbors.push_back(neighbor);
                            }
                            else
                            {
                                hasMulticast = true;
                            }
                        }
                    }

                    for (const auto& neighbor : unicastNeighbors)
                    {
                        eigrpInterfacePtr->sendUpdateToNeighbor(neighbor, changedRoutes, EigrpConfigs::UpdateType::PARTIAL);
                    }
                    if (hasMulticast && eigrpInterfacePtr->configs->multicastEnabled.load(std::memory_order_relaxed))
                    {
                        eigrpInterfacePtr->sendUpdateToNeighbor(nullptr, changedRoutes, EigrpConfigs::UpdateType::PARTIAL);
                    }
                }
            }
        }
    }

    void Eigrp::shutdown()
    {
        std::shared_lock<std::shared_mutex> lock(interfaceMutex);
        for (auto it = eigrpInterfaceList.begin(); it != eigrpInterfaceList.end();)
        {
            // Send termination message
            delete it->second;
            it->second = nullptr;
            it = eigrpInterfaceList.erase(it);
        }
        eigrpInterfaceList.clear();
        if (namedMode)
        {
            for (auto it = eigrpInterfaceConfigList.begin(); it != eigrpInterfaceConfigList.end();)
            {
                // Delete configs
                delete it->second;
                it->second = nullptr;
                it = eigrpInterfaceConfigList.erase(it);
            }
            eigrpInterfaceConfigList.clear();
        }
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
            externalRoute->routeType = RoutingTable::Eigrp::RouteType::EXTERNAL;
            externalRoute->metric += configs.redistributionMetricOffset.load(std::memory_order_relaxed);

            routingTable.addEigrp(externalRoute, addressFamily, asNumber);
            notifyRoutingChange({{externalRoute, true}});
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
            configs.networks.push_back(std::move(newNetwork));
        }

        // Update interfaces and routing table after adding the network
        updateInterfaceList();
    }

    void Eigrp::enableAutoSummary(bool enable)
    {
        if (addressFamily != AddressFamily::IPv4) return; // Only supported for IPv4

        bool current = configs.autoSummarizationEnabled.load(std::memory_order_relaxed);
        if (enable == current) return; // No change

        configs.autoSummarizationEnabled.store(enable, std::memory_order_release);

        // Lock interface for duration
        std::shared_lock<std::shared_mutex> lock(interfaceMutex);
        
        if (enable)
        {
            std::unordered_map<ByteString, std::vector<RoutingTable::Eigrp*>> classfulGroups;

            // Process all existing EIGRP routes and group by classical networks
            for (RoutingTable::Eigrp* route : routingInstance->routingTable.getAllEigrpRoutes(addressFamily, asNumber))
            {
                if (route->delay == 0xFFFFFFFF || route->routeType == RoutingTable::Eigrp::RouteType::SUMMARY)
                    continue;
                ByteString major = Functions::findClassfullNetwork(route->network);
                classfulGroups[major].push_back(route);
            }

            // Only summarize when there are 2+ subnets in a classful group
            for (const auto& [majorNet, routes] : classfulGroups)
            {
                if (routes.size() < 2) continue;

                uint8_t defaultMask = Functions::getDefaultMask(majorNet);

                // For each interface, check if summary is missing
                for (const auto& [_, iface] : eigrpInterfaceList)
                {
                    if (!iface->isRouteSummarized(majorNet, defaultMask))
                    {
                        iface->addSummaryRoute(majorNet, defaultMask, true);
                    }
                }
            }
        }
        else 
        {
            // Disable auto-summarization on all interfaces
            for (const auto& [_, iface] : eigrpInterfaceList)
            {
                iface->removeAllAutoSummaries();
            }
        }
    }

    void Eigrp::recomputeAutoSummaries()
    {
        if (!configs.autoSummarizationEnabled.load(std::memory_order_relaxed) || addressFamily != AddressFamily::IPv4)
            return;

        // Step 1: Group all connected EIGRP routes by classful major network
        std::unordered_map<ByteString, std::vector<RoutingTable::Eigrp*>> grouped;
        for (RoutingTable::Eigrp* route : routingInstance->routingTable.getAllEigrpRoutes(addressFamily, asNumber))
        {
            if (route->delay == 0xFFFFFFFF || route->routeType == RoutingTable::Eigrp::RouteType::SUMMARY)
                continue;
            ByteString major = Functions::findClassfullNetwork(route->network);
            grouped[major].push_back(route);
        }

        // Step 2: Loop through all known major networks
        std::shared_lock<std::shared_mutex> ifaceLock(interfaceMutex);

        for (const auto& [majorNet, routes] : grouped)
        {
            uint8_t defaultMask = Functions::getDefaultMask(majorNet);

            // Compute best metric among components
            uint32_t minBandwidth = std::numeric_limits<uint32_t>::max();
            uint32_t minDelay = std::numeric_limits<uint32_t>::max();

            for (const auto* r : routes)
            {
                if (r->bandwidth < minBandwidth)
                    minBandwidth = r->bandwidth;
                if (r->delay < minDelay)
                    minDelay = r->delay;
            }

            // If less than 2, treat as no summary opportunity
            if (routes.size() < 2)
            {
                for (const auto& [_, iface] : eigrpInterfaceList)
                {
                    if (iface->isRouteSummarized(majorNet, defaultMask))
                        iface->removeSummaryRoute(majorNet, defaultMask);
                }
                continue;
            }

            // Step 3: Ensure summary exists on each interface and has correct metric
            for (const auto& [_, iface] : eigrpInterfaceList)
            {
                // Check if summary is already present
                bool found = false;
                {
                    std::shared_lock<std::shared_mutex> lock(iface->configs->configsMutex);
                    for (const auto& sr : iface->configs->summaryRoutes)
                    {
                        if (sr.isAuto &&
                            sr.summary->network == majorNet &&
                            sr.summary->mask == defaultMask)
                        {
                            found = true;

                            // Check if the metric needs updating
                            if (sr.summary->bandwidth != minBandwidth || sr.summary->delay != minDelay)
                            {
                                iface->removeSummaryRoute(majorNet, defaultMask);
                                iface->addSummaryRoute(majorNet, defaultMask, true);
                            }

                            break;
                        }
                    }
                }

                // Not found? Add new summary
                if (!found)
                    iface->addSummaryRoute(majorNet, defaultMask, true);
            }
        }

        // Step 4: Clean up any summaries that no longer match anything
        for (const auto& [_, iface] : eigrpInterfaceList)
        {
            std::unique_lock<std::shared_mutex> lock(iface->configs->configsMutex);
            for (auto it = iface->configs->summaryRoutes.begin(); it != iface->configs->summaryRoutes.end(); )
            {
                if (!it->isAuto)
                {
                    ++it;
                    continue;
                }

                ByteString major = it->summary->network;
                uint8_t mask = it->summary->mask;

                // Does this still match 2+ connected routes?
                int matchCount = 0;
                for (const auto* route : routingInstance->routingTable.getAllConnectedEigrpRoutes(addressFamily, asNumber))
                {
                    if (Functions::isSubnetOf(route->network, route->mask, major, mask))
                    {
                        matchCount++;
                        if (matchCount >= 2)
                            break;
                    }
                }

                if (matchCount < 2)
                {
                    iface->removeSummaryRoute(major, mask);
                    it = iface->configs->summaryRoutes.erase(it); // erase here because we’re in the loop
                }
                else
                {
                    ++it;
                }
            }
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
        for (const auto& [_, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            eigrpInterfacePtr->handleStubRouteUpdates();
        };
    }

    uint32_t Eigrp::getLowestBandwidth()
    {
        uint32_t lowestBW = std::numeric_limits<uint32_t>::max();
        std::shared_lock<std::shared_mutex> lock(interfaceMutex);
        for (const auto& [_, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            if (!eigrpInterfacePtr->currentInterface->shutdownFlag.load(std::memory_order_relaxed) && eigrpInterfacePtr->currentInterfaceInfo->bandwidth < lowestBW)
            {
                lowestBW = eigrpInterfacePtr->currentInterfaceInfo->bandwidth.load(std::memory_order_relaxed);
            }
        };
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
        auto intIt = eigrpInterfaceList.find({type, id});
        if (intIt != eigrpInterfaceList.end())
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
                    RoutingTable::Eigrp* newRoute = new RoutingTable::Eigrp(routeInfo.eigrpInterface->interfaceKey);
                    newRoute->network = destination;
                    newRoute->mask = entry->prefixLength;
                    newRoute->nextHop = neighbor;
                    newRoute->metric = routeInfo.feasibleDistance;
                    newRoute->routeType = RoutingTable::Eigrp::RouteType::INTERNAL;

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
        route->adminDistance = (route->routeType == RoutingTable::Eigrp::RouteType::INTERNAL || route->routeType == RoutingTable::Eigrp::RouteType::SUMMARY)
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
            auto intIt = eigrpInterfaceList.find({interfaceType, id});
            if (intIt != eigrpInterfaceList.end())
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
            auto intIt = eigrpInterfaceList.find({interfaceType, id});
            if (intIt != eigrpInterfaceList.end())
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
            for (const auto& [_, eigrpInterfacePtr] : eigrpInterfaceList)
            {
                eigrpInterfacePtr->stopHello();
                delete eigrpInterfacePtr;
            };
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
            auto processID = [&](Interface* interface)
            {
                if (interface->shutdownFlag.load(std::memory_order_relaxed)) return;
                auto& interfaceInfo = interface->configs;
                ByteString address = interfaceInfo.ipv4.getAddress();
                if (address.empty()) return;
                uint32_t ipNum = Functions::byteToNum(address);
                if (ipNum < highestNum) return;
                highestNum = ipNum;
                highestIP = address;
            };
            
            {
                std::shared_lock<std::shared_mutex> lock(routingInstance->interfaceMutex);
                for (const auto& [id, interface] : routingInstance->interfaceList)
                {
                    if (id.first != InterfaceType::LOOPBACK) continue;
                    processID(interface);
                }
                if (highestIP.empty())
                {
                    for (const auto& [id, interface] : routingInstance->interfaceList)
                    {
                        processID(interface);
                    }
                }
            }
            routerID.ID = highestIP;
        }
    }

#pragma endregion

#pragma region EigrpInterface

    EigrpInterface::EigrpInterface(Eigrp &eigrpSystem, EigrpConfigs::InterfaceConfigs* intConfigs, Interface* interface)
        : eigrpProcess(eigrpSystem),
          configs(intConfigs),
          currentInterface(interface),
          currentInterfaceInfo(&interface->configs)
    {
        {
            // Start router

            // Store initial IP of interface
            if (eigrpSystem.addressFamily == AddressFamily::IPv4)
            {
                interfaceKey = currentInterfaceInfo->ipv4.getAddress() + "/" + std::to_string(currentInterfaceInfo->ipv4.getMask());
            }
            else
            {
                interfaceKey = currentInterfaceInfo->ipv6.getLocalAddress();
            }

            // Add pending summary routes if needed
            for (const auto& [network, mask] : configs->pendingSummaryRoutes)
            {
                addSummaryRoute(network, mask);
            }
            configs->pendingSummaryRoutes.clear();

            // Gather locked values for local metric calculation
            uint32_t delay = currentInterfaceInfo->delay.load(std::memory_order_relaxed);
            uint32_t bandwidth = currentInterfaceInfo->bandwidth.load(std::memory_order_relaxed);
            uint8_t load = eigrpProcess.configs.variance.load(std::memory_order_relaxed);
            InterfaceType type = currentInterfaceInfo->interfaceType;
            float ID = currentInterfaceInfo->id;
            uint8_t reliability = 255;

            // Check if this interface is passive
            bool passive = false;
            {
                std::shared_lock<std::shared_mutex> lock(eigrpProcess.configs.configsMutex);
                if (eigrpProcess.configs.passiveInterfaces[type].find(ID) != eigrpProcess.configs.passiveInterfaces[type].end())
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
            if (eigrpProcess.configs.lowestBandwidth.load(std::memory_order_relaxed) > bandwidth)
            {
                eigrpProcess.configs.lowestBandwidth.store(bandwidth, std::memory_order_release);
                // Recalculate local metrics on all interfaces
                {
                    //std::shared_lock<std::shared_mutex> lock(eigrpProcess.interfaceMutex);
                    for (const auto& [id, interfacePtr] : eigrpProcess.eigrpInterfaceList)
                    {
                        if (interfacePtr->currentInterface->shutdownFlag.load(std::memory_order_relaxed)) return;

                        uint32_t intDelay = interfacePtr->currentInterfaceInfo->delay.load(std::memory_order_relaxed);
                        uint8_t intLoad = eigrpProcess.configs.variance.load(std::memory_order_relaxed);
                        interfacePtr->configs->localMetric = interfacePtr->eigrpProcess.calculateLocalLinkCost(intLoad, intDelay, reliability);
                    };
                }
            }
            else
            {
                std::shared_lock<std::shared_mutex> metricLock(configs->configsMutex);
                configs->localMetric = eigrpProcess.calculateLocalLinkCost(load, delay, reliability);
            }

            // Handle unciast neighbors
            std::vector<ByteString> unicastNeighbors;
            {
                std::shared_lock<std::shared_mutex> lock(eigrpProcess.configs.configsMutex);
                if (!eigrpProcess.configs.unicastNeighbors[type][ID].empty())
                {
                    // Disable multicast if unicast neighbors are present
                    configs->multicastEnabled.store(false, std::memory_order_release);
                    
                    // Store neighbors to add
                    for (auto neighbor : eigrpProcess.configs.unicastNeighbors[type][ID])
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

            if (!currentInterface->shutdownFlag.load(std::memory_order_relaxed))
            {
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
        destroy.store(true, std::memory_order_seq_cst);
        stopHello();

        // Wait for any in-progress hello packets
        while (!helloDone.load(std::memory_order_seq_cst))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        AddressFamily addressFamily = eigrpProcess.addressFamily;
        uint8_t mask = 32;
        ByteString network;
        {
            if (addressFamily == AddressFamily::IPv4)
            {
                mask = currentInterface->configs.ipv4.getMask();
                network = Functions::computeNetworkAddress(currentInterface->configs.ipv4.getAddress(), mask);
            }
            else if (addressFamily == AddressFamily::IPv6)
            {
                auto ip = currentInterface->configs.ipv6.getGlobalUnicastPair();
                if (!ip.first.empty())
                {
                    network = Functions::computeNetworkAddress(ip.first, ip.second);
                }
            }
        }

        auto globalRoute = eigrpProcess.routingInstance->routingTable.getEigrpRoute(network, mask, addressFamily, eigrpProcess.asNumber);
        if (globalRoute)
        {
            if (globalRoute && globalRoute->routeType == RoutingTable::Eigrp::RouteType::CONNECTED)
            {
                // Remove if valid and is a connected route
                eigrpProcess.routingInstance->routingTable.removeEigrp(network, mask, addressFamily, eigrpProcess.asNumber);
            }
        }

        // Remove interface from routing table if able to
        sendHelloPacket(nullptr); // Termination message

        auto interface = currentInterface;
        currentInterface = nullptr;

        // Remove all active timers
        {
            std::lock_guard<std::mutex> activeLock(activeTimerMutex);
            for (auto& [_, id] : activeTimers)
            {
                eigrpProcess.routingInstance->global.timeManager.cancelTimer(id);
            }
            for (auto& [_, query] : eigrpProcess.outstandingReplies)
            {
                for (auto& [_, id] : query.pendingQueries)
                {
                    eigrpProcess.routingInstance->global.timeManager.cancelTimer(id.siaTimerId);
                }
            }
        }

        // Remove query timers
        for (const auto& [key, query] : eigrpProcess.outstandingReplies)
        {
            for (const auto& query : query.pendingQueries)
            {
                eigrpProcess.routingInstance->global.timeManager.cancelTimer(query.second.siaTimerId);
            }
        }
        for (const auto& [key, activeId] : activeTimers)
        {
            eigrpProcess.routingInstance->global.timeManager.cancelTimer(activeId);
        }
        
        // Aquire lock to modify neighbors
        std::unordered_map<ByteString, EigrpConfigs::NeighborInfo*> neighborsCopy;
        {
            std::unique_lock<std::shared_mutex> lock(neighborMutex);
            neighborsCopy = neighbors;
        }

        for (auto& [ip, neighbor] : neighborsCopy)
        {
            handleNeighborDown(neighbor, ip);
        }

        {
            std::shared_lock<std::shared_mutex> lock(configs->configsMutex);
            for (const auto& sr : configs->summaryRoutes)
            {
                if (!sr.isAuto)
                    configs->pendingSummaryRoutes.emplace_back(sr.summary->network, sr.summary->mask);
            }
            configs->summaryRoutes.clear();
        }

        // Remove interface from other tables
        uint32_t id = eigrpProcess.asNumber;
        if (interface->eigrpInterfaceList.find(id) != interface->eigrpInterfaceList.end())
        {
            if (addressFamily == AddressFamily::IPv4)
            {
                interface->eigrpInterfaceList[id]->IPv4 = nullptr;
            }
            else if (addressFamily == AddressFamily::IPv6)
            {
                interface->eigrpInterfaceList[id]->IPv6 = nullptr;
            }
            if (!interface->eigrpInterfaceList[id]->IPv4 && !interface->eigrpInterfaceList[id]->IPv6)
            {
                interface->eigrpInterfaceList.erase(id);
            }
        }

        // Remove all routes/summary routes
        eigrpProcess.routingInstance->routingTable.removeEigrpWithOutInterface(eigrpProcess.addressFamily, eigrpProcess.asNumber, interfaceKey);

        //Logger::getInstance().info() << "EigrpInterface destroyed and all timers canceled." << std::endl;
    }

    void EigrpInterface::processPacket(const EigrpHeader& eigrpPacket, const ByteString neighborIp, bool multicast)
    {
        EigrpConfigs::NeighborState neighborState;
        // Check if passive

        {
            if (configs->isPassive.load(std::memory_order_relaxed))
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
            else if (eigrpPacket.opcode == Variable::Eigrp::Type::siaReply)
            {
                processSIAReply(neighbor, neighborIp, eigrpPacket);
            }
            else if (eigrpPacket.opcode == Variable::Eigrp::Type::query)
            {
                processQuery(neighbor, eigrpPacket, neighborIp);
            }
            else if (eigrpPacket.opcode == Variable::Eigrp::Type::siaQuery)
            {
                processSIAQuery(neighbor, eigrpPacket, neighborIp);
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
        ByteString routerID = eigrpProcess.getRouterID();
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
                neighbor->stuckInInitTimerId = eigrpProcess.routingInstance->global.timeManager.addTimer(std::chrono::steady_clock::now() + std::chrono::seconds(neighbor->holdTime.load(std::memory_order_relaxed)), [this, neighbor, neighborIp]()
                {
                    // Check if the neighbor is still in Initializing
                    if (destroy.load(std::memory_order_acquire)) return;
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
                neighbor->twoWayThreadID.store(eigrpProcess.routingInstance->global.timeManager.addTimer(
                    std::chrono::steady_clock::now() + std::chrono::milliseconds(300),
                    [this, neighbor, neighborIp]()
                    {
                        if (destroy.load(std::memory_order_acquire)) return;
                        changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::EXSTART);
                    }
                ), std::memory_order_release);
                break;

            case EigrpConfigs::NeighborState::EXSTART:
                Logger::getInstance().info(true) << "Neighbor in EXSTART state. Waiting for role" << std::endl;
                
                // Send Null Update to initialize reliable communication
                Logger::getInstance().info(true) << "Sending Null Update to neighbor." << std::endl;
                
                bool nullSent;
                {
                    std::lock_guard<std::mutex> initLock(neighbor->initFlagMutex);
                    nullSent = neighbor->initFlags.nullSent;
                }

                if (!nullSent)
                {
                    sendUpdateToNeighbor(neighbor, {}, EigrpConfigs::UpdateType::QUERY);
                }

                // Update the neighbors state
                neighbor->neighborState.store(newState, std::memory_order_release);

                // Move to EXCHANGE to start topology exchange
                changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::EXCHANGE);

                // Set the TWOWAY delay ID to 0 since the thread is done.
                neighbor->twoWayThreadID.store(0, std::memory_order_release);
                break;

            case EigrpConfigs::NeighborState::EXCHANGE:
                Logger::getInstance().info(true) << "Neighbor in EXCHANGE state. Sharing topology." << std::endl;

                bool slaveInit, masterInit, initUpdateReceived;
                EigrpConfigs::InitRole initRole;
                {
                    std::lock_guard<std::mutex> initLock(neighbor->initFlagMutex);
                    slaveInit = neighbor->initFlags.slaveInit;
                    masterInit = neighbor->initFlags.masterInit;
                    initUpdateReceived = neighbor->initFlags.initUpdateReceived;
                    initRole = neighbor->initFlags.initRole;
                }
                
                if (slaveInit || masterInit)
                {
                    neighbor->neighborState.store(newState, std::memory_order_release);
                    changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::LOADING);
                }
                else if (initRole == EigrpConfigs::InitRole::MASTER)
                {
                    if (!masterInit)
                    {
                        {
                            std::lock_guard<std::mutex> initLock(neighbor->initFlagMutex);
                            neighbor->initFlags.masterInit = true;
                        }
                        
                        // Send Sequence Hello with the generated sequence number
                        Logger::getInstance().info(true) << "MASTER sending Sequence Hello with sequence number: " << seq << std::endl;
                        sendHelloPacket(neighbor, unicast, /*update=*/true, seq);

                        // Set state to loading
                        Logger::getInstance().info(true) << "MASTER sending full topology." << std::endl;

                        // Get all routes
                        auto allRotes = eigrpProcess.routingInstance->routingTable.getAllEigrpRoutes(eigrpProcess.addressFamily, eigrpProcess.asNumber);
                        std::vector<EigrpConfigs::RoutingUpdate> fullUpdate;
                        for (auto& route : allRotes)
                        {
                            fullUpdate.emplace_back(route, false);
                        }
                        // Send a conditional update to the neighbor
                        sendUpdateToNeighbor(neighbor, fullUpdate, EigrpConfigs::UpdateType::FULL, false, true, {neighborIp});

                        neighbor->neighborState.store(newState, std::memory_order_release);
                        changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::LOADING);
                    }
                }
                else if (initRole == EigrpConfigs::InitRole::SLAVE && initUpdateReceived && !slaveInit)
                {
                    // Send Null update to initialize reliable connections
                    Logger::getInstance().info(true) << "Sending Null Update to neighbor." << std::endl;
                    
                    // Send Sequence Hello with the generated sequence number
                    Logger::getInstance().info(true) << "Sending Sequence Hello with sequence number: " << seq << std::endl;
                    sendHelloPacket(neighbor, /*unicast=*/unicast, /*update=*/true, seq);

                    // Send your topology
                    Logger::getInstance().info(true) << "Sending full topology." << std::endl;
                    auto allRotes = eigrpProcess.routingInstance->routingTable.getAllEigrpRoutes(eigrpProcess.addressFamily, eigrpProcess.asNumber);
                    std::vector<EigrpConfigs::RoutingUpdate> fullUpdate;
                    for (auto& route : allRotes)
                    {
                        fullUpdate.emplace_back(route, false);
                    }
                    // Send a conditional update to the neighbor
                    sendUpdateToNeighbor(neighbor, fullUpdate, EigrpConfigs::UpdateType::FULL, false, true, {neighborIp});

                    // Send a hello immediately after sending routes
                    Logger::getInstance().info(true) << "Sending immediate Hello after full topology." << std::endl;
                    sendHelloPacket(neighbor);

                    neighbor->neighborState.store(newState, std::memory_order_release);
                    changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::LOADING);
                }
                else if (initRole == EigrpConfigs::InitRole::SLAVE)
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
            if (configs->interfaceMode.load(std::memory_order_relaxed) == EigrpConfigs::Mode::POINT_TO_POINT)
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
        if (Functions::byteToNum(receivedHello.autonomousSystem) != eigrpProcess.asNumber)
        {
            // Drop the packet - AS number mismatch
            return;
        }

        // Extract Hold Time
        uint16_t recievedHoldTime = configs->holdTime.load(std::memory_order_relaxed); // Default holdtime.

        // Safely access or create the neighbor
        if (!neighbor && !unicast)
        {
            std::unique_lock<std::shared_mutex> intLock(neighborMutex);
            neighbor = new EigrpConfigs::NeighborInfo(eigrpProcess.routingInstance->global.timeManager, neighborIp);
            neighbors[neighborIp] = neighbor;
            neighborAdded = true;
            std::lock_guard<std::mutex> globalLock(eigrpProcess.neighborMutex);
            eigrpProcess.allNeighbors[neighborIp] = neighbor;
        }
        else if (!neighbor && unicast)
        {
            return;
        }
        else if (neighbor && neighbor->holdTimerId.load(std::memory_order_relaxed) == 0) // If hold timer is not running
        {
            neighborAdded = true;
        }

        std::optional<EigrpHeader::Option> authOpt = std::nullopt;

        // Process TLVs
        for (const auto& opt : receivedHello.options)
        {
            if (opt.option == Variable::Eigrp::Option::parameter)
            {
                ByteString parameters = eigrpProcess.calculateParameters(recievedHoldTime);
                if (opt.value.substr(0, 6) != parameters.substr(0, 6)) return; // Drop the packet if parameters mismatch

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
            else if (opt.option == Variable::Eigrp::Option::authentication)
            {
                authOpt = opt;
                break;
            }
        }

        // Validate Authentication
        if (neighbor && configs->authKey.fullyEnabled.load(std::memory_order_relaxed))
        {
            if (!authOpt.has_value()) return;
            EigrpHeader tempHeader = receivedHello;
            auto authTLV = generateAuthenticatedTLV(tempHeader);
            if (authTLV.value != authOpt->value) return;
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
            if (eigrpProcess.routingInstance->global.timeManager.cancelTimer(neighbor->holdTimerId))
            {
                changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::EXSTART);
            }
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
            neighbor->secondHelloReceived.store(true, std::memory_order_release);
            // Cancel existing Time if it exists and change states
            if (neighbor->twoWayThreadID.load(std::memory_order_relaxed) != 0)
            {
                eigrpProcess.routingInstance->global.timeManager.cancelTimer(neighbor->holdTimerId.load(std::memory_order_relaxed));
            }
        }
    }

    void EigrpInterface::processUpdate(EigrpConfigs::NeighborInfo* neighbor, const EigrpHeader& receivedUpdate)
    {
        // Get neighbor Ip
        ByteString neighborIp;
        ByteString interfaceIp;
        bool initComplete = neighbor->initComplete.load(std::memory_order_relaxed);
        bool processAcks = neighbor->processAcks.load(std::memory_order_relaxed);
        if (currentInterface->shutdownFlag.load(std::memory_order_relaxed))
            interfaceIp = getInterfaceIp();

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
            eigrpProcess.routingInstance->global.timeManager.cancelTimer(neighbor->gracefulRestartTimerId.load(std::memory_order_relaxed));
            neighbor->gracefulRestartTimerId.store(0, std::memory_order_release);
        }

        // Update flags
        bool initReceived = false;
        bool endOfTable = false;
        bool conditionalReceive = false;
        if (receivedUpdate.flags.restart == "1") {}
        if (receivedUpdate.flags.init == "1") { neighbor->initSequence.store(receivedSequenceNumber); initReceived = true; }
        if (receivedUpdate.flags.conditionalRecieve == "1") { conditionalReceive = true; }
        if (receivedUpdate.flags.endOfTable == "1") { endOfTable = true; }

        // Validate sequence number
        {
            uint32_t lastReceivedSequenceNumber = neighbor->lastReceivedSequenceNumber.load(std::memory_order_relaxed);

            if (receivedSequenceNumber <= lastReceivedSequenceNumber && !processAcks)
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
                neighbor->initSequence = receivedSequenceNumber;
            }
                
            neighbor->lastReceivedSequenceNumber = receivedSequenceNumber;
        }

        // Acknowledge packet
        sendAckToNeighbor(neighbor, neighborIp, receivedSequenceNumber);


        // Determine roles based on sequence numbers
        if (!initComplete)
        {
            std::lock_guard<std::mutex> initLock(neighbor->initFlagMutex);
            if (receivedSequenceNumber < nextSequenceNumber)
            {
                neighbor->initFlags.initRole = EigrpConfigs::InitRole::MASTER;
                Logger::getInstance().info(true) << "MASTER role has been chosen with sequence number: " << nextSequenceNumber << " and neighbors: " << receivedSequenceNumber << "." << std::endl;
            }
            else if (receivedSequenceNumber > nextSequenceNumber)
            {
                neighbor->initFlags.initRole = EigrpConfigs::InitRole::SLAVE;
                Logger::getInstance().info(true) << "SLAVE role has been chosen with sequence number: " << nextSequenceNumber << " and neighbors: " << receivedSequenceNumber << "." << std::endl;
            }
            else if (neighbor->lastReceivedSequenceNumber == nextSequenceNumber)
            {
                // Use router ID as a tie-breaker
                Logger::getInstance().info(true) << "Sequence numbers equal. Using Router ID as tie-breaker." << std::endl;
                if (Functions::byteToNum(eigrpProcess.getRouterID()) > Functions::byteToNum(neighbor->routerID))
                {
                    neighbor->initFlags.initRole = EigrpConfigs::InitRole::MASTER;
                    Logger::getInstance().info(true) << "Tie-breaker determined: MASTER." << std::endl;
                }
                else
                {
                    neighbor->initFlags.initRole = EigrpConfigs::InitRole::SLAVE;
                    Logger::getInstance().info(true) << "Tie-breaker determined: SLAVE." << std::endl;
                }
            }
            neighbor->initComplete.store(true, std::memory_order_release);
            initComplete = true;
        }

        // Process Ack if present
        if (receivedUpdate.ack != ByteString(4, '\x00') && processAcks)
        {
            processAck(neighbor, receivedUpdate.ack);
        }

        // Collect routes for batch processing
        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            for (const auto &option : receivedUpdate.options)
            {
                if (option.option == Variable::Eigrp::Option::internalRoute ||
                    option.option == Variable::Eigrp::Option::internalRouteV6 ||
                    option.option == Variable::Eigrp::Option::externalRoute ||
                    option.option == Variable::Eigrp::Option::externalRouteV6)
                {
                    RoutingTable::Eigrp* route = decodeRoute(option.value, (option.option == Variable::Eigrp::Option::externalRoute), false);
                    route->nextHop = neighborIp;

                    routeBuffer.emplace_back(route, route->delay == 0xFFFFFFFF);
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            if (!routeBuffer.empty())
            {
                recordRouteChange();
                updateRoutingTable(neighbor, neighborIp, routeBuffer);
                size_t neighborAmount;
                {
                    std::lock_guard<std::mutex> globalLock(eigrpProcess.neighborMutex);
                    neighborAmount = eigrpProcess.allNeighbors.size();
                }
                if (neighborAmount == 1 && !neighbor->isInit && !routeBuffer.empty())
                {
                    // Reject routes if this is the only neighbor
                    for (auto& route : routeBuffer)
                    {
                        route.withdraw = true;
                    }
                    sendUpdateToNeighbor(neighbor, routeBuffer, EigrpConfigs::UpdateType::PARTIAL);
                }

                routeBuffer.clear();
            }
        }

        // Check and process buffered packets
        processBufferedPackets(neighbor);

        bool slaveInit;
        EigrpConfigs::InitRole initRole;
        {
            std::lock_guard<std::mutex> lock(neighbor->initFlagMutex);
            slaveInit = neighbor->initFlags.slaveInit;
            initRole = neighbor->initFlags.initRole;
        }

        // Safely access neighbor state
        if (initComplete && neighbor->neighborState == EigrpConfigs::NeighborState::EXSTART && initRole == EigrpConfigs::InitRole::SLAVE && !slaveInit)
        {
            changeNeighborState(neighbor, neighborIp, EigrpConfigs::NeighborState::EXCHANGE);
        }
    }

    void EigrpInterface::processBufferedPackets(EigrpConfigs::NeighborInfo* neighbor)
    {
        if (!neighbor) return; // neighbor is invalid
        
        uint32_t nextExpectedSequence = neighbor->lastReceivedSequenceNumber + 1;

        {
            std::lock_guard<std::mutex> lock(neighbor->bufferMutex);
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
    }

    void EigrpInterface::processAck(EigrpConfigs::NeighborInfo* neighbor, const ByteString& sequenceNumber)
    {
        // Ensure the neighbor is valid
        if (!neighbor && !neighbor->processAcks.load(std::memory_order_relaxed))
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
                eigrpProcess.routingInstance->global.timeManager.cancelTimer(pktIt->second.timerId);
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
            ByteString key = route.route->network + "/" + std::to_string(route.route->mask);

            {
                std::unique_lock<std::shared_mutex> routeLock(neighbor->neighborDataMutex);
                auto advertIt = neighbor->advertisedRoutes.find(key);

                if (route.withdraw)
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
                        neighbor->advertisedRoutes[key] = {route.route, true, false, false};
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
            return; // Ignoring duplicate sequence number;

        std::vector<RoutingTable::Eigrp*> queriedRoutes;
        for (const auto& option : receivedQuery.options)
        {
            if (option.option == Variable::Eigrp::Option::internalRoute ||
                option.option == Variable::Eigrp::Option::internalRouteV6)
                queriedRoutes.push_back(decodeRoute(option.value, false, false));
            else if (option.option == Variable::Eigrp::Option::externalRoute ||
                option.option == Variable::Eigrp::Option::externalRouteV6)
                queriedRoutes.push_back(decodeRoute(option.value, true, false));
        }
        
        sendAckToNeighbor(neighbor, neighborIp, Functions::byteToNum(receivedQuery.sequence));

        std::vector<RoutingTable::Eigrp*> knownQueryRoutes;
        std::vector<RoutingTable::Eigrp*> knownRoutes;
        std::vector<RoutingTable::Eigrp*> unknownQueryRoutes;

        for (const auto& queriedRoute : queriedRoutes)
        {
            // Check if the queried route exists
            auto existingRoute = eigrpProcess.routingInstance->routingTable.getEigrpRoute(queriedRoute->network, queriedRoute->mask, eigrpProcess.addressFamily, eigrpProcess.asNumber);
            // Check summary routes
            {
                std::shared_lock<std::shared_mutex> lock(configs->configsMutex);
                for (const auto& summaryRoute : configs->summaryRoutes) {
                    if (Functions::isSubnetOf(queriedRoute->network, queriedRoute->mask, summaryRoute.summary->network, summaryRoute.summary->mask))
                        existingRoute = summaryRoute.summary;
                }
            }

            if (existingRoute && existingRoute->nextHop != neighborIp)
            {
                knownQueryRoutes.push_back(queriedRoute);
                knownRoutes.push_back(existingRoute);
            }
            else
            {
                unknownQueryRoutes.push_back(queriedRoute);
            }
        }

        if (!knownQueryRoutes.empty() && !knownRoutes.empty() && neighborIp != ByteString(knownQueryRoutes.front()->network.size(), '\x00'))
        {
            // Route is know, send a reply immediately
            sendReplyToNeighbor(neighbor, neighborIp, knownQueryRoutes, knownRoutes, receivedSequenceNumber);
            for (auto route : queriedRoutes) { delete route; }
            return;
        }

        // Otherwise we need to query all neighbors
        std::vector<EigrpConfigs::ActiveRoute> activeRoutes;
        for (const auto& queriedRoute : unknownQueryRoutes)
        {
            EigrpConfigs::ActiveRoute newQuery;
            newQuery.route = new RoutingTable::Eigrp(*queriedRoute);
            newQuery.originNeighbor = neighborIp;
            newQuery.sequenceNumber = receivedSequenceNumber;
            activeRoutes.push_back(std::move(newQuery));
        }

        // Indicates no neighbors are available
        bool noNeighbors = true;
        {
            std::shared_lock<std::shared_mutex> intLock(eigrpProcess.interfaceMutex);
            for (const auto& [_, interface] : eigrpProcess.eigrpInterfaceList)
            {
                for (const auto& [ip, otherNeighbor] : interface->neighbors)
                {
                    if (ip == neighborIp) continue;

                    noNeighbors = false;

                    for (auto& query : activeRoutes)
                    {
                        EigrpConfigs::OutgoingQuery outgoing;
                        outgoing.sequenceNumber = nextSequenceNumber.load(std::memory_order_relaxed);
                        outgoing.lastSIARefreshTime = std::chrono::steady_clock::now();
                        startSIATimer(query.route, ip, outgoing);
                        query.pendingQueries[ip] = std::move(outgoing);
                    }
                    interface->sendQueryToNeighbor(otherNeighbor, ip, {unknownQueryRoutes});
                }
            };
        }

        // If no neighbors are available, send an empty reply
        if (noNeighbors)
        {
            for (auto* route : queriedRoutes) { route->delay = std::numeric_limits<uint32_t>::max(); }
            sendReplyToNeighbor(neighbor, neighborIp, queriedRoutes, queriedRoutes, receivedSequenceNumber);
            for (auto* route : queriedRoutes) { 
                delete route; 
            }
            return;
        }

        // Store this query in our global tracker
        for (auto query : activeRoutes)
        {
            ByteString queryKey = query.route->network + "/" + std::to_string(query.route->mask);

            eigrpProcess.outstandingReplies[queryKey] = std::move(query);
            if (!eigrpProcess.configs.activeDisabled)
            {
                startActiveTimer(query.route);
            }
        }
        
        // Delete remaining queried routes
        for (auto route : queriedRoutes) { 
            delete route; 
        }
        queriedRoutes.clear();
    }

    void EigrpInterface::processSIAQuery(EigrpConfigs::NeighborInfo* neighbor, const EigrpHeader& receivedQuery, const ByteString& neighborIp)
    {
        if (!neighbor) return; // Neighbor does not exist

        uint32_t receivedSequenceNumber = Functions::byteToNum(receivedQuery.sequence);

        if (receivedSequenceNumber < neighbor->lastReceivedSequenceNumber.load(std::memory_order_relaxed))
            return;

        sendAckToNeighbor(neighbor, neighborIp, Functions::byteToNum(receivedQuery.sequence));

        sendSIAReplyToNeighbor(neighbor, neighborIp, receivedSequenceNumber);
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
            return;

        // Send ack for the received reply
        sendAckToNeighbor(neighbor, neighborIp, receivedSequenceNumber);

        std::vector<RoutingTable::Eigrp*> receivedRoutes;
        for (const auto& option : receivedReply.options)
        {
            if (option.option == Variable::Eigrp::Option::internalRoute ||
                option.option == Variable::Eigrp::Option::internalRouteV6)
            {
                receivedRoutes.push_back(decodeRoute(option.value, false, false));
            }
            else if (option.option == Variable::Eigrp::Option::externalRoute ||
                option.option == Variable::Eigrp::Option::externalRouteV6)
            {
                receivedRoutes.push_back(decodeRoute(option.value, true, true));
            }
        }

        if (receivedRoutes.empty()) return;

        for (auto it = eigrpProcess.outstandingReplies.begin(); it != eigrpProcess.outstandingReplies.end();)
        {
            auto& queryInfo = it->second;

            if (queryInfo.pendingQueries.count(neighborIp))
            {
                auto& outgoing = queryInfo.pendingQueries[neighborIp];
                if (outgoing.sequenceNumber != Functions::byteToNum(receivedReply.ack))
                {
                    ++it;
                    continue; // Not the patching query
                }

                eigrpProcess.routingInstance->global.timeManager.cancelTimer(outgoing.siaTimerId);
                queryInfo.pendingQueries.erase(neighborIp);

                RoutingTable::Eigrp* currentRoute = eigrpProcess.routingInstance->routingTable.getEigrpRoute(
                    queryInfo.route->network, queryInfo.route->mask,
                    eigrpProcess.addressFamily, eigrpProcess.asNumber);

                if (receivedRoutes.front()->delay != std::numeric_limits<uint32_t>::max() &&
                    isFeasibleSuccessor(receivedRoutes.front(), currentRoute))
                {
                    queryInfo.feasibleRoutes.push_back(std::tuple(neighbor, neighborIp, receivedRoutes.front(), false));
                }
                else
                {
                    // Poisened route
                    queryInfo.route->delay = std::numeric_limits<uint32_t>::max();
                    queryInfo.feasibleRoutes.push_back(std::tuple(neighbor, neighborIp, queryInfo.route, true));
                }

                if (queryInfo.pendingQueries.empty())
                {
                    RoutingTable::Eigrp* bestRoute = nullptr;
                    bool remove = false;

                    if (!queryInfo.feasibleRoutes.empty())
                    {
                        std::sort(queryInfo.feasibleRoutes.begin(), queryInfo.feasibleRoutes.end(), 
                            [](const auto& a, const auto& b) 
                            {
                                return std::get<2>(a)->feasibleDistance < std::get<2>(b)->feasibleDistance;
                            });
                        bestRoute = std::get<2>(queryInfo.feasibleRoutes.front());
                    }
                    else
                    {
                        remove = true;
                    }

                    if (queryInfo.originNeighbor != ByteString(queryInfo.route->network.size(), '\x00'))
                    {
                        std::shared_lock<std::shared_mutex> intLock(eigrpProcess.interfaceMutex);
                        for (const auto& [_, interface] : eigrpProcess.eigrpInterfaceList)
                        {
                            if (interface->neighbors.count(queryInfo.originNeighbor))
                            {
                                if (bestRoute)
                                    interface->sendReplyToNeighbor(interface->neighbors[queryInfo.originNeighbor], queryInfo.originNeighbor, { bestRoute }, { bestRoute }, queryInfo.sequenceNumber);
                                else
                                    interface->sendReplyToNeighbor(interface->neighbors[queryInfo.originNeighbor], queryInfo.originNeighbor, { queryInfo.route }, { queryInfo.route }, queryInfo.sequenceNumber);
                            }
                        }
                    }

                    if (remove)
                        eigrpProcess.routingInstance->routingTable.removeEigrp(queryInfo.route->network, queryInfo.route->mask, eigrpProcess.addressFamily, eigrpProcess.asNumber);
                    for (const auto& route : queryInfo.feasibleRoutes)
                    {
                        updateRoutingTable(std::get<0>(route), std::get<1>(route), {{std::get<2>(route), std::get<3>(route)}});
                    }

                    cancelActiveTimer(queryInfo.route->network, queryInfo.route->mask);
                    for (const auto& outgoing : queryInfo.pendingQueries)
                    {
                        eigrpProcess.routingInstance->global.timeManager.cancelTimer(outgoing.second.siaTimerId);
                    }
                    it = eigrpProcess.outstandingReplies.erase(it);
                    continue;
                }
            }

            ++it;
        }
    }
    
    void EigrpInterface::processSIAReply(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, const EigrpHeader& receivedReply)
    {
        if (!neighbor) return; // Neighbor invalid

        uint32_t receivedSequenceNumber = Functions::byteToNum(receivedReply.sequence);

        if (receivedSequenceNumber < neighbor->lastReceivedSequenceNumber.load(std::memory_order_relaxed))
            return;

        // Send ack for the received reply
        sendAckToNeighbor(neighbor, neighborIp, receivedSequenceNumber);

        auto now = std::chrono::steady_clock::now();

        for (auto& [key, queryInfo] : eigrpProcess.outstandingReplies)
        {
            if (queryInfo.pendingQueries.count(neighborIp))
            {
                auto& outgoing = queryInfo.pendingQueries[neighborIp];

                if (outgoing.sequenceNumber != Functions::byteToNum(receivedReply.ack))
                    continue; // Not matching sequence number

                eigrpProcess.routingInstance->global.timeManager.cancelTimer(outgoing.siaTimerId);
                startSIATimer(queryInfo.route, neighborIp, outgoing);

                outgoing.lastSIARefreshTime = now;
            }
        }
    }

    size_t EigrpInterface::calculateMaxRoutesPerPacket(AddressFamily af, bool isExternal)
    {
        uint16_t maxPacketSize = eigrpProcess.addressFamily == AddressFamily::IPv4 
            ? currentInterfaceInfo->ipv4.mtu.load(std::memory_order_relaxed)
            : currentInterfaceInfo->ipv6.mtu.load(std::memory_order_relaxed);
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
        if (!neighbor && neighbor->processAcks.load(std::memory_order_release))
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
                eigrpProcess.eigrpHello(eigrp, *this, neighborIp, seq, /*ack=*/true);

                // Add Authentication TLV if enabled
                if (configs->authKey.fullyEnabled.load(std::memory_order_relaxed))
                {
                    EigrpHeader::Option authTLV = generateAuthenticatedTLV(eigrp);
                    if (authTLV.option != ByteString(1, 0x00))
                    {
                        eigrp.options.push_back(std::move(authTLV));
                    }
                }

                // Assemble the packet
                eigrpAckPacketStructure.Layer4.push_back(std::move(eigrp));

                // Send the assembled ACK packet if it contains data
                if (!eigrpAckPacketStructure.Layer4.empty() && currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed) && !destroy.load(std::memory_order_relaxed))
                {
                    ByteString ip = getInterfaceIp();
                    IPPacket::buildIp(currentInterface, eigrpAckPacketStructure, neighborIp, &ip, nullptr, configs->DSCP.load(std::memory_order_relaxed), 255, Variable::IP::eigrp);
                }
            }
            neighbor->pendingAcks.clear();
        }
    }

    void EigrpInterface::sendUpdateToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const std::vector<EigrpConfigs::RoutingUpdate> &routes, EigrpConfigs::UpdateType updateType, bool restart, bool conditional, std::vector<ByteString> conditionalNeighbors)
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
                std::lock_guard<std::mutex> lock(neighbor->initFlagMutex);
                neighbor->initFlags.nullSent = true;
            }
        }

        // Filter routes based on stub configuration and split horizon
        std::vector<EigrpConfigs::RoutingUpdate> filteredRoutes;
        for (const auto& routeUpdate : routes)
        {
            auto* route = routeUpdate.route;
            if (route->delay == 0xFFFFFFFF)
                continue;

            bool isSummarized = std::any_of(configs->summaryRoutes.begin(), configs->summaryRoutes.end(),
                [&](const auto& summary) { return Functions::isSubnetOf(route->network, route->mask, summary.summary->network, summary.summary->mask); });
            
            if (isSummarized) continue;

            // Stub Test
            // If the process is running in stub mode, only allow routes that are permitted.
            if (eigrpProcess.isStub() && 
                !((route->routeType == RoutingTable::Eigrp::RouteType::CONNECTED && eigrpProcess.advertiseConnected()) ||
                (route->routeType == RoutingTable::Eigrp::RouteType::STATIC && eigrpProcess.advertiseStatic()) ||
                (route->routeType == RoutingTable::Eigrp::RouteType::SUMMARY && eigrpProcess.advertiseSummary()) ||
                (route->routeType == RoutingTable::Eigrp::RouteType::EXTERNAL && eigrpProcess.advertiseRedistributed())))
                continue;

            // Split Horizon Test
            if (configs->splitHorizon.load(std::memory_order_relaxed))
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
            ByteString key = route->network + "/" + std::to_string(route->mask);
            bool needsUpdate = true;

            for (const auto& [address, updateNeighbor] : neighbors)
            {
                auto advertIt = updateNeighbor->advertisedRoutes.find(key);
                if (advertIt != updateNeighbor->advertisedRoutes.end() &&
                    !advertIt->second.pendingUpdate &&
                    !advertIt->second.removePending)
                {
                    needsUpdate = false;
                    break;
                }
            }

            if (!needsUpdate)
                continue;

            // Passed all filters, include in output
            filteredRoutes.push_back(routeUpdate);
        }

        if ((updateType == EigrpConfigs::UpdateType::PARTIAL) && filteredRoutes.empty())
        {
            return;
        }

        uint32_t bandwidthMetric = (10000000 / currentInterfaceInfo->bandwidth.load(std::memory_order_relaxed));
        uint32_t delay = currentInterfaceInfo->delay.load(std::memory_order_relaxed);
        size_t maxRoutesPerPacket = calculateMaxRoutesPerPacket(eigrpProcess.addressFamily, /*isExernal=*/false);
        size_t routeCount = 0;
        auto it = filteredRoutes.begin();

        // Set if this is a withdraw update
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
                if (it->route->routeType == RoutingTable::Eigrp::RouteType::EXTERNAL)
                {
                    routeOptions.value = encodeExternalRouteOption(it->route, bandwidthMetric, delay, it->withdraw);
                    routeOptions.option = (eigrpProcess.addressFamily == AddressFamily::IPv4) ? Variable::Eigrp::Option::externalRoute : Variable::Eigrp::Option::externalRouteV6;
                }
                else
                {
                    routeOptions.value = encodeRouteOption(it->route, bandwidthMetric, delay, it->withdraw);
                    routeOptions.option = (eigrpProcess.addressFamily == AddressFamily::IPv4) ? Variable::Eigrp::Option::internalRoute : Variable::Eigrp::Option::internalRouteV6;
                }
                routeOptions.length = Functions::numToByte(static_cast<uint32_t>(routeOptions.value.size()) + 4, 2);
                eigrp.options.push_back(std::move(routeOptions));
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

            eigrpProcess.eigrpUpdate(eigrp, sequenceNumber, /*init=*/isInit, /*conditional=*/isConditional, /*restart=*/restart, /*endOfTable*/endOfTable/* && filteredRoutes.size() != 1*/);
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
                if (eigrpProcess.isStub())
                {
                    ByteString stubValue = encodeStubOption(eigrpProcess.configs.stubConfig);
                    eigrp.options.emplace_back(
                        Variable::Eigrp::Option::stub,
                        Functions::numToByte(stubValue.size() + 4, 2),
                        std::move(stubValue)
                    );
                }
                // Add authentication TLV if enabled
                if (configs->authKey.fullyEnabled.load(std::memory_order_relaxed))
                {
                    EigrpHeader::Option authTLV = generateAuthenticatedTLV(eigrp);
                    if (authTLV.option != ByteString(1, 0x00))
                    {
                        eigrp.options.push_back(std::move(authTLV));
                    }
                }
            }

            // Assemble and send the packet
            eigrpPacket.Layer4.push_back(eigrp);
            if (currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed) && !destroy.load(std::memory_order_relaxed))
            {
                ByteString ip = getInterfaceIp();
                IPPacket::buildIp(currentInterface, eigrpPacket, targetIp, &ip, nullptr, configs->DSCP.load(std::memory_order_relaxed), 255, Variable::IP::eigrp);
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
                        std::vector<EigrpConfigs::RoutingUpdate> neighborSpecificRoutes;
                        for (const auto& route : filteredRoutes)
                        {
                            ByteString key = route.route->network + "/" + std::to_string(route.route->mask);

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
                            setupReliablePacket(neighborPtr, address, EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(eigrp, address, neighborSpecificRoutes), sequenceNumber);
                        }
                    }
                }
                else
                {
                    for (const auto& [address, neighborPtr] : neighbors)
                    {
                        setupReliablePacket(neighborPtr, address, EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(eigrp, address, filteredRoutes), sequenceNumber);
                    }
                }
            }
        }
        while (!endOfTable && updateType == EigrpConfigs::UpdateType::FULL);
    }

    uint32_t EigrpInterface::sendQueryToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, std::vector<RoutingTable::Eigrp*> failedRoutes)
    {
        // Increment sequence number for this route/query
        uint32_t sequenceNumber = getNextSequenceNumber();

        // Create the EIGRP Query Packet
        PacketInfo eigrpQueryPacketStructure;
        EigrpHeader eigrp;

        // Construct the Query option
        for (const auto& failedRoute : failedRoutes)
        { 
            ByteString query;
            ByteString type;

            if (failedRoute->routeType == RoutingTable::Eigrp::RouteType::EXTERNAL)
            {
                query = encodeExternalRouteOption(failedRoute, (10000000 / currentInterfaceInfo->bandwidth.load(std::memory_order_relaxed)) * 256, currentInterfaceInfo->delay.load(std::memory_order_relaxed));
                type = (eigrpProcess.addressFamily == AddressFamily::IPv4)
                  ? Variable::Eigrp::Option::externalRoute
                  : Variable::Eigrp::Option::externalRouteV6;
            }
            else
            {
                query = encodeRouteOption(failedRoute, (10000000 / currentInterfaceInfo->bandwidth.load(std::memory_order_relaxed)), currentInterfaceInfo->delay.load(std::memory_order_relaxed));
                type = (eigrpProcess.addressFamily == AddressFamily::IPv4)
                  ? Variable::Eigrp::Option::internalRoute
                  : Variable::Eigrp::Option::internalRouteV6;
            }
            eigrp.options.emplace_back(
                type,
                Functions::numToByte(query.size() + 4, 2),
                std::move(query)
            );
        }

        // Set other EIGRP header feilds
        eigrp.version = ByteString(1, 0x02);
        eigrp.opcode = Variable::Eigrp::Type::query;
        eigrp.checksum = ByteString(2, 0x00); // Will be calculated later
        eigrp.flags.init = "0";
        eigrp.flags.conditionalRecieve = "0";
        eigrp.flags.restart = "0";
        eigrp.flags.endOfTable = "0";
        eigrp.sequence = Functions::numToByte(sequenceNumber, 4);
        eigrp.ack = ByteString(4, 0x00);
        eigrp.virtualRouterID = eigrpProcess.getVirtualRouterID();
        eigrp.autonomousSystem = Functions::numToByte(eigrpProcess.asNumber, 2);

        // Generate and append Authentication TLV if enabled for this neighbor
        if (configs->authKey.fullyEnabled.load(std::memory_order_relaxed))
        {
            EigrpHeader::Option authTLV = generateAuthenticatedTLV(eigrp);
            if (authTLV.option != ByteString(1, 0x00))
                eigrp.options.push_back(std::move(authTLV));
        }

        // Assemble the packet
        eigrpQueryPacketStructure.Layer4.push_back(eigrp);

        // Convert to raw packet ByteString
        if (currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed) && !destroy.load(std::memory_order_relaxed))
        {
            ByteString ip = getInterfaceIp();
            IPPacket::buildIp(currentInterface, eigrpQueryPacketStructure, neighborIp, &ip, nullptr, configs->DSCP.load(std::memory_order_relaxed), 255, Variable::IP::eigrp);
        }

        // Store the packet for possible retransmission (relieable delivery)
        eigrp.ack = ByteString(4, '\x00');
        std::vector<EigrpConfigs::RoutingUpdate> formattedRoutes;
        for (const auto& route : failedRoutes)
        {
            formattedRoutes.emplace_back(route, true);
        }
        setupReliablePacket(neighbor, neighborIp, EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(eigrp, neighborIp, formattedRoutes), sequenceNumber);

        Logger::getInstance().info() << "Sent query to neighbor " << neighborIp.toHex() << " with sequence number " << sequenceNumber << std::endl;

        return sequenceNumber;
    }

    void EigrpInterface::sendSIAQueryToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp)
    {
        //TODO FINISH THIS
        sendQueryToNeighbor(neighbor, neighborIp, {});
    }

    void EigrpInterface::sendQueryToNeighbors(std::vector<RoutingTable::Eigrp*> failedRoutes)
    {
        if (failedRoutes.empty()) return;

        for (const auto& [ip, neighbor] : neighbors)
        {
            uint32_t seqNum = sendQueryToNeighbor(neighbor, ip, failedRoutes);
            if (seqNum == 0) return;

            for (RoutingTable::Eigrp* route : failedRoutes)
            {
                ByteString key = route->network + "/" + std::to_string(route->mask);

                if (eigrpProcess.outstandingReplies.find(key) == eigrpProcess.outstandingReplies.end())
                {
                    EigrpConfigs::ActiveRoute& active = eigrpProcess.outstandingReplies[key];
                    active.route = route;
                    active.originNeighbor = ByteString(route->network.size(), '\x00');
                }

                EigrpConfigs::ActiveRoute& active = eigrpProcess.outstandingReplies[key];

                EigrpConfigs::OutgoingQuery outgoing;
                outgoing.sequenceNumber = seqNum;
                outgoing.lastSIARefreshTime = std::chrono::steady_clock::now();
                startSIATimer(route, ip, outgoing);

                active.pendingQueries[ip] = std::move(outgoing);
            }
        }
    }

    void EigrpInterface::sendReplyToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, std::vector<RoutingTable::Eigrp*> queryRoutes, std::vector<RoutingTable::Eigrp*> existingRoutes, uint32_t sequenceNumber)
    {
        // Validate neighbor
        if (!neighbor)
            return;

        uint32_t currentSeqNum = getNextSequenceNumber();

        // Create the EIGRP Query Packet
        PacketInfo eigrpQueryPacketStructure;
        EigrpHeader eigrp;

        // Construct the Query option
        for (size_t i = 0; i < queryRoutes.size(); ++i)
        { 
            RoutingTable::Eigrp combinedRoute = *existingRoutes[i];
            combinedRoute.network = queryRoutes[i]->network;
            combinedRoute.mask = queryRoutes[i]->mask;
            combinedRoute.nextHop = configs->nextHopSelf.load(std::memory_order_relaxed) ? getInterfaceIp() : ByteString(combinedRoute.network.size(), '\x00');

            ByteString relayValue;
            ByteString type;

            if (combinedRoute.routeType == RoutingTable::Eigrp::RouteType::EXTERNAL)
            {
                relayValue = encodeExternalRouteOption(&combinedRoute, (10000000 / currentInterfaceInfo->bandwidth.load(std::memory_order_relaxed)) * 256, currentInterfaceInfo->delay.load(std::memory_order_relaxed));
                type = (eigrpProcess.addressFamily == AddressFamily::IPv4)
                  ? Variable::Eigrp::Option::externalRoute
                  : Variable::Eigrp::Option::externalRouteV6;
            }
            else
            {
                relayValue = encodeRouteOption(&combinedRoute, (10000000 / currentInterfaceInfo->bandwidth.load(std::memory_order_relaxed)), currentInterfaceInfo->delay.load(std::memory_order_relaxed));
                type = (eigrpProcess.addressFamily == AddressFamily::IPv4)
                  ? Variable::Eigrp::Option::internalRoute
                  : Variable::Eigrp::Option::internalRouteV6;
            }

            eigrp.options.emplace_back(
                type,
                Functions::numToByte(relayValue.size() + 4, 2),
                std::move(relayValue)
            );
        }

        // Set other EIGRP header feilds
        eigrp.version = ByteString(1, 0x02);
        eigrp.opcode = Variable::Eigrp::Type::reply;
        eigrp.checksum = ByteString(2, 0x00); // Will be calculated later
        eigrp.flags.init = "0";
        eigrp.flags.conditionalRecieve = "0";
        eigrp.flags.restart = "0";
        eigrp.flags.endOfTable = "0";
        eigrp.sequence = Functions::numToByte(currentSeqNum, 4);
        eigrp.ack = Functions::numToByte(sequenceNumber, 4);
        eigrp.virtualRouterID = eigrpProcess.getVirtualRouterID();
        eigrp.autonomousSystem = Functions::numToByte(eigrpProcess.asNumber, 2);

        // Generate and append Authentication TLV if enabled for this neighbor
        if (configs->authKey.fullyEnabled.load(std::memory_order_relaxed))
        {
            EigrpHeader::Option authTLV = generateAuthenticatedTLV(eigrp);
            if (authTLV.option != ByteString(1, 0x00))
            {
                eigrp.options.push_back(std::move(authTLV));
            }
        }

        // Assemble the packet
        eigrpQueryPacketStructure.Layer4.push_back(eigrp);

        // Convert to raw packet ByteString
        if (currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed) && !destroy.load(std::memory_order_relaxed))
        {
            ByteString ip = getInterfaceIp();
            IPPacket::buildIp(currentInterface, eigrpQueryPacketStructure, neighborIp, &ip, nullptr, configs->DSCP.load(std::memory_order_relaxed), 255, Variable::IP::eigrp);
        }

        // Store the packet for possible retransmission (relieable delivery)
        eigrp.ack = ByteString(4, '\x00');
        setupReliablePacket(neighbor, neighborIp, EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(eigrp, neighborIp), currentSeqNum);

        Logger::getInstance().info() << "Sent query to neighbor " << neighborIp.toHex() << " with sequence number " << currentSeqNum << std::endl;
    }

    void EigrpInterface::sendSIAReplyToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, uint32_t querySequence)
    {
        // Validate neighbor
        if (!neighbor) return;

        uint32_t currentSeqNum = getNextSequenceNumber();

        PacketInfo eigrpPacket;
        EigrpHeader eigrp;

        eigrp.version = ByteString(1, 0x02);
        eigrp.opcode = Variable::Eigrp::Type::siaReply;
        eigrp.checksum = ByteString(2, 0x00); // Will be calculated later
        eigrp.flags.init = "0";
        eigrp.flags.conditionalRecieve = "0";
        eigrp.flags.restart = "0";
        eigrp.flags.endOfTable = "0";
        eigrp.sequence = Functions::numToByte(currentSeqNum, 4);
        eigrp.ack = Functions::numToByte(querySequence, 4);
        eigrp.virtualRouterID = eigrpProcess.getVirtualRouterID();
        eigrp.autonomousSystem = Functions::numToByte(eigrpProcess.asNumber, 2);

        // Generate and append Authentication TLV if enabled for this neighbor
        if (configs->authKey.fullyEnabled.load(std::memory_order_relaxed))
        {
            EigrpHeader::Option authTLV = generateAuthenticatedTLV(eigrp);
            if (authTLV.option != ByteString(1, 0x00))
            {
                eigrp.options.push_back(std::move(authTLV));
            }
        }

        // Assemble the packet
        eigrpPacket.Layer4.push_back(eigrp);

        if (currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed) && !destroy.load(std::memory_order_relaxed))
        {
            ByteString ip = getInterfaceIp();
            IPPacket::buildIp(currentInterface, eigrpPacket, neighborIp, &ip, nullptr, configs->DSCP.load(std::memory_order_relaxed), 255, Variable::IP::eigrp);
        }

        // Store the packet for possible retransmission (relieable delivery)
        eigrp.ack = ByteString(4, '\x00');
        setupReliablePacket(neighbor, neighborIp, EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(eigrp, neighborIp), currentSeqNum);

        Logger::getInstance().info() << "Sent SIA Reply to neighbor " << neighborIp.toHex() << " with sequence number " << currentSeqNum << std::endl;
    }

    ByteString EigrpInterface::encodeRouteOption(RoutingTable::Eigrp* route, uint32_t currentBandwidthMetric, uint32_t currentDelay, bool removed)
    {
        ByteString encoded;

        // Include next hop as usual
        encoded += route->nextHop;

        // Update delay: add local interface delay
        uint32_t newDelay = route->delay != std::numeric_limits<uint32_t>::max()
          ? route->delay + ((currentDelay / 10) *256)
          : route->delay;

        // Update Bandwidth: Take the lower bandwidth metric
        uint32_t newBandwidthMetric = std::min(route->bandwidth, currentBandwidthMetric * 256);

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

        // Update Bandwidth: Take the lower bandwidth metric
        uint32_t newBandwidthMetric = std::min(route->bandwidth, currentBandwidthMetric * 256);

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
        if (mask > network.size() * 8 || !Functions::compareNetworkWithMask(network, mask))
            return; // Mask invalid

        // Don't add if already summarized
        if (isRouteSummarized(network, mask)) 
            return;

        uint32_t minBandwidth = std::numeric_limits<uint32_t>::max();
        uint32_t minDelay = std::numeric_limits<uint32_t>::max();

        const auto& allRoutes = eigrpProcess.routingInstance->routingTable.getAllEigrpRoutes(
            eigrpProcess.addressFamily, eigrpProcess.asNumber
        );

        for (const auto* route : allRoutes)
        {
            if (route->delay == 0xFFFFFFFF || route->routeType == RoutingTable::Eigrp::RouteType::SUMMARY)
                continue;
            if (isRouteSummarized(route->network, route->mask))
            {
                if (route->bandwidth < minBandwidth)
                    minBandwidth = route->bandwidth;

                if (route->delay < minDelay)
                    minDelay = route->delay;
            }
        }

        // Create summary route
        RoutingTable::Eigrp* route = new RoutingTable::Eigrp(interfaceKey);
        route->network = network;
        route->mask = mask;
        route->routeType = RoutingTable::Eigrp::RouteType::SUMMARY;
        route->hopCount = 0;
        route->delay = 0;
        route->bandwidth = ( 10000000 / currentInterfaceInfo->bandwidth.load(std::memory_order_relaxed)) * 256;
        route->mtu = eigrpProcess.addressFamily == AddressFamily::IPv4
            ? currentInterfaceInfo->ipv4.mtu.load(std::memory_order_relaxed)
            : currentInterfaceInfo->ipv6.mtu.load(std::memory_order_relaxed);
        route->reliability = 255;
        route->load = eigrpProcess.configs.variance.load(std::memory_order_relaxed);
        route->adminDistance = eigrpProcess.configs.adminDistance.load(std::memory_order_relaxed);
        route->nextHop = configs->nextHopSelf.load(std::memory_order_relaxed)
          ? getInterfaceIp()
          : ByteString(network.size(), '\x00'); // indicates directly connected

        eigrpProcess.routingInstance->routingTable.addEigrp(route, eigrpProcess.addressFamily, eigrpProcess.asNumber);

        // Add new summary route
        {
            EigrpConfigs::SummaryRoute entry;
            std::unique_lock<std::shared_mutex> configsLock(configs->configsMutex);
            entry.summary = route;
            entry.isAuto = isAuto;
            configs->summaryRoutes.push_back(std::move(entry));
        }

        // Update interface to advertise the new summary route
        advertiseSummaryRoute(route);
    }

    void EigrpInterface::removeSummaryRoute(const ByteString& network, uint8_t mask)
    {
        std::unique_lock<std::shared_mutex> lock(configs->configsMutex);

        auto list = configs->summaryRoutes;
        for (auto it = list.begin(); it != list.end();)
        {
            RoutingTable::Eigrp* route = it->summary;
            if (route->network == network && route->mask == mask)
            {
                // Withdraw from neighbors
                withdrawSummaryRoute(route);

                // Remove from global routing table
                eigrpProcess.routingInstance->routingTable.removeEigrp(
                    route->network,
                    route->mask,
                    eigrpProcess.addressFamily,
                    eigrpProcess.asNumber
                );

                // TODO remove discard route

                // Free route object
                delete route;

                // Remove from list
                it = list.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void EigrpInterface::restoreSummaryRoutes(const ByteString& summaryNetwork, uint8_t summaryMask)
    {
        std::shared_lock<std::shared_mutex> lock(configs->configsMutex);

        for (const auto& sr : configs->summaryRoutes)
        {
            if (sr.summary->network == summaryNetwork && sr.summary->mask == summaryMask)
            {
                advertiseSummaryRoute(sr.summary);

                // Reinstall discard route //TODO
//                 RoutingTable::Discard;
//                 discard.network == summaryRoute;
//                 discard.mask == summaryMask;
//                 discard.interfaceId = currentInterface->id;
//                 discard.protocol = "eigrp";
//                 discard.name = "summary-discard";

                //TODO remove discard route
                break;
            }
        }
    }


    EigrpConfigs::SummaryRoute* EigrpInterface::isRouteSummarized(const ByteString& network, uint8_t mask)
    {
        std::shared_lock<std::shared_mutex> configsLock(configs->configsMutex);

        for (auto& sr : configs->summaryRoutes)
        {
            if (Functions::isSubnetOf(network, mask, sr.summary->network, sr.summary->mask))
            {
                return &sr;
            }
        }
        return nullptr;
    }

    void EigrpInterface::advertiseSummaryRoute(RoutingTable::Eigrp* summaryRoute)
    {
        // Construct the route to advertise
        if (!summaryRoute) return;
        
        std::vector<EigrpConfigs::NeighborInfo*> neighborsToNotify;
        bool hasMulticast = false;

        // Collect neigbors
        {
            std::shared_lock<std::shared_mutex> lock(neighborMutex);
            for (const auto& [address, neighbor] : neighbors)
            {
                if (neighbor->unicast)
                {
                    neighborsToNotify.push_back(neighbor);
                }
                else
                {
                    hasMulticast = true;
                }
            }
        }

        // Send unicast updates
        for (const auto& neighbor : neighborsToNotify)
        {
            sendUpdateToNeighbor(neighbor, {{summaryRoute, false}}, EigrpConfigs::UpdateType::PARTIAL);
        }

        // Send multicast update if enabled
        if (hasMulticast && configs->multicastEnabled.load(std::memory_order_relaxed))
        {
            sendUpdateToNeighbor(nullptr, {{summaryRoute, false}}, EigrpConfigs::UpdateType::PARTIAL);
        }
    }

    void EigrpInterface::withdrawSummaryRoute(RoutingTable::Eigrp* route)
    {
        if (!route) return;

        std::vector<EigrpConfigs::NeighborInfo*> unicastNeighbors;
        bool hasMulticast = false;

        // Iterate through neighbors
        {
            ByteString key = route->network + "/" + std::to_string(route->mask);

            std::shared_lock<std::shared_mutex> lock(neighborMutex);
            for (const auto& [_, neighbor] : neighbors) 
            {
                // Mark for removal in neighbor state
                {
                    std::unique_lock<std::shared_mutex> neighborInfoLock(neighbor->neighborDataMutex);
                    auto it = neighbor->advertisedRoutes.find(key);
                    if (it != neighbor->advertisedRoutes.end())
                    {
                        it->second.removePending = true;
                    }
                }

                if (neighbor->unicast)
                    unicastNeighbors.push_back(neighbor);
                else
                    hasMulticast = true;
            }
        }

        // Build withdrawal route (infinite metric)
        RoutingTable::Eigrp* withdrawal = new RoutingTable::Eigrp(interfaceKey);
        withdrawal->network = route->network;
        withdrawal->mask = route->mask;
        withdrawal->routeType = RoutingTable::Eigrp::RouteType::SUMMARY;
        withdrawal->metric = std::numeric_limits<uint32_t>::max();

        // Send withdraw update to the neighbor
        for (const auto& neighbor : unicastNeighbors)
        {
            sendUpdateToNeighbor(neighbor, {{withdrawal, true}}, EigrpConfigs::UpdateType::PARTIAL);
        }
        if (hasMulticast && configs->multicastEnabled.load(std::memory_order_relaxed))
        {
            sendUpdateToNeighbor(nullptr, {{withdrawal, true}}, EigrpConfigs::UpdateType::PARTIAL);
        }

        delete withdrawal;
    }

    void EigrpInterface::removeAllAutoSummaries()
    {
        std::unique_lock<std::shared_mutex> lock(configs->configsMutex);

        for (auto it = configs->summaryRoutes.begin(); it != configs->summaryRoutes.end();)
        {
            if (it->isAuto)
            {
                // Withdraw form neighbors
                withdrawSummaryRoute(it->summary);
                
                // Remove from global routing table
                eigrpProcess.routingInstance->routingTable.removeEigrp(
                    it->summary->network,
                    it->summary->mask,
                    eigrpProcess.addressFamily,
                    eigrpProcess.asNumber
                );

                //TODO remove null0 discard route

                // Free route
                delete it->summary;

                // Erase entry
                it = configs->summaryRoutes.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void EigrpInterface::handleStubRouteUpdates()
    {
        // Identify routes that should no longer be advertised
        std::vector<EigrpConfigs::RoutingUpdate> routesToWithdraw;
        std::shared_lock<std::shared_mutex> lock(neighborMutex);
        for (const auto& [address, neighbor] : neighbors)
        {
            std::shared_lock<std::shared_mutex> neighborDataLock(neighbor->neighborDataMutex);
            for (const auto& [routeKey, advertisedRoute] : neighbor->advertisedRoutes)
            {
                // Check if route has already been removed
                bool found = false;
                for (auto route : routesToWithdraw)
                {
                    if (route.route->network == advertisedRoute.route->network && route.route->mask == advertisedRoute.route->mask)
                    {
                        found = true;
                    }
                }
                if (found)
                {
                    continue;
                }

                // Check if route should be removed
                bool shouldAdvertise = false;

                if (eigrpProcess.isStub())
                {
                    if (advertisedRoute.route->routeType == RoutingTable::Eigrp::RouteType::CONNECTED && eigrpProcess.advertiseConnected())
                        shouldAdvertise = true;
                    if (advertisedRoute.route->routeType == RoutingTable::Eigrp::RouteType::STATIC && eigrpProcess.advertiseStatic())
                        shouldAdvertise = true;
                    if (advertisedRoute.route->routeType == RoutingTable::Eigrp::RouteType::SUMMARY && eigrpProcess.advertiseSummary())
                        shouldAdvertise = true;
                    if (advertisedRoute.route->routeType == RoutingTable::Eigrp::RouteType::EXTERNAL &&  eigrpProcess.advertiseRedistributed())
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
                    withdrawRoute->routeType = RoutingTable::Eigrp::RouteType::WITHDRAW;

                    routesToWithdraw.push_back({withdrawRoute, true});
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
                sendUpdateToNeighbor(neighbor, routesToWithdraw, EigrpConfigs::UpdateType::PARTIAL);
            }
            if (multicast && configs->multicastEnabled.load(std::memory_order_relaxed))
            {
                sendUpdateToNeighbor(nullptr, routesToWithdraw, EigrpConfigs::UpdateType::PARTIAL);
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
        helloTimerActive.store(true, std::memory_order_release);

        startHello();
    }

    void EigrpInterface::startHello()
    {
        // Mark hello as active

        if (!helloTimerActive.load(std::memory_order_relaxed) && helloTimerId.load(std::memory_order_relaxed) != 0)
        {
            return; // Timer already active
        }
        
        {
            std::lock_guard<std::mutex> lock(helloTimerMutex);
            if (helloStartTime.time_since_epoch().count() == 0)
            {
                helloStartTime = std::chrono::steady_clock::now();
            }

            auto nextExpiration = helloStartTime + std::chrono::seconds(configs->helloTime);

            uint32_t helloId = eigrpProcess.routingInstance->global.timeManager.addTimer(nextExpiration, [&, vrf = eigrpProcess.routingInstance->instanceName, as = eigrpProcess.asNumber, af = eigrpProcess.addressFamily]()
            {
                if (VirtualRouter* virtualRouter = eigrpProcess.routingInstance->global.getRoutingInstance(vrf))
                {
                    if (auto* eigrp = virtualRouter->getEigrpAutonomousSystem(as))
                    {
                        if (af == AddressFamily::IPv4 ? !eigrp->ipv4 : !eigrp->ipv6)
                        {
                            return;
                        }
                    }
                    else return;
                }
                else return;

                if (!helloTimerActive.load(std::memory_order_relaxed) || destroy.load(std::memory_order_relaxed)) return;
                helloDone.store(false, std::memory_order_release);
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
                        if (configs->multicastEnabled.load(std::memory_order_relaxed))
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
                }

                // Mark hello as done
                helloDone.store(true, std::memory_order_release);
                if (!destroy.load(std::memory_order_relaxed))
                {
                    startHello(); // Reschedule
                }
            });
            helloTimerId.store(helloId, std::memory_order_release);
        }
    }

    void EigrpInterface::sendHelloPacket(EigrpConfigs::NeighborInfo* neighbor, bool unicast, bool update, uint32_t sequenceNumber)
    {
        if (destroy.load(std::memory_order_seq_cst))
        {
            return;
        }


        if (configs->isPassive.load(std::memory_order_relaxed))
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
            else if (configs->multicastEnabled.load(std::memory_order_relaxed))
            {
                targetIp = getMulticast();
            }
        }

        PacketInfo eigrpHello;
        EigrpHeader eigrp;

        eigrpProcess.eigrpHello(eigrp, *this, targetIp, sequenceNumber, false, update);

        // Add stub flags
        if (eigrpProcess.isStub())
        {
            ByteString stubValue;
            {
                std::shared_lock<std::shared_mutex> lock(eigrpProcess.configs.configsMutex);
                stubValue = encodeStubOption(eigrpProcess.configs.stubConfig);
            }
            eigrp.options.emplace_back(
                Variable::Eigrp::Option::stub,
                Functions::numToByte(stubValue.size() + 4, 2),
                std::move(stubValue)
            );
        }

        eigrpHello.Layer4.push_back(std::move(eigrp));

        auto interface = currentInterface;
        if (!interface || interface->shutdownFlag.load(std::memory_order_acquire) || destroy.load(std::memory_order_acquire))
        {
            return;
        }

        ByteString ip = getInterfaceIp();
        IPPacket::buildIp(interface, eigrpHello, targetIp, &ip, nullptr, configs->DSCP.load(std::memory_order_relaxed), 255, Variable::IP::eigrp);
    }
    void EigrpInterface::stopHello()
    {
        helloTimerActive.store(false, std::memory_order_release);
        if (helloTimerId != 0)
        {
            eigrpProcess.routingInstance->global.timeManager.cancelTimer(helloTimerId);
            if (!helloDone.load(std::memory_order_relaxed))
            {
                // Wait until the hello timer is fully shut down
                while (!helloDone.load(std::memory_order_relaxed))
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            helloTimerId = 0;
        }
        {
            std::shared_lock<std::shared_mutex> neighborLock(neighborMutex);
            for (auto& [_, neighbor] : neighbors)
            {
                uint32_t holdTimerId = neighbor->holdTimerId.load(std::memory_order_relaxed);
                if (holdTimerId != 0)
                {
                    eigrpProcess.routingInstance->global.timeManager.cancelTimer(holdTimerId);
                    neighbor->holdTimerId.store(0, std::memory_order_release);
                }
            }
        }
    }

    void EigrpInterface::startHoldTimer(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, uint16_t holdTime)
    {
        uint32_t holdTimerId = neighbor->holdTimerId.load(std::memory_order_relaxed);
        if (holdTimerId != 0)
        {
            eigrpProcess.routingInstance->global.timeManager.cancelTimer(holdTimerId);
        }

        if (destroy.load(std::memory_order_relaxed)) return;
        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(holdTime);
        holdTimerId = eigrpProcess.routingInstance->global.timeManager.addTimer(expirationTime, [this, neighbor, neighborIp]()
                                                                   { handleHoldTimeExpire(neighbor, neighborIp); });
        neighbor->holdTimerId.store(holdTimerId, std::memory_order_release);
    }

    void EigrpInterface::handleHoldTimeExpire(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp)
    {
        if (neighbor)
        {
            if (eigrpProcess.configs.nonStopForwarding.load(std::memory_order_relaxed))
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
        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(eigrpProcess.configs.activeTime);
        // TODO Add SRTT into timeout

        // Schedule Active timer
        uint32_t activeTimerId = eigrpProcess.routingInstance->global.timeManager.addTimer(expirationTime, [this, route, vrf = eigrpProcess.routingInstance->instanceName, as = eigrpProcess.asNumber, af = eigrpProcess.addressFamily]() {
            if (VirtualRouter* virtualRouter = eigrpProcess.routingInstance->global.getRoutingInstance(vrf))
            {
                if (auto* eigrp = virtualRouter->getEigrpAutonomousSystem(as))
                {
                    if (af == AddressFamily::IPv4 ? !eigrp->ipv4 : !eigrp->ipv6)
                    {
                        return;
                    }
                }
                else return;
            }
            else return;
            handleActiveTimeExpire(route);
        });

        {
            std::lock_guard<std::mutex> lock(activeTimerMutex);
            activeTimers[key] = activeTimerId;
        }
    }

    uint32_t EigrpInterface::startSIATimer(RoutingTable::Eigrp* route, const ByteString& neighborIp, EigrpConfigs::OutgoingQuery& outgoing)
    {
        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(eigrpProcess.configs.stuckInActiveTime);

        // Schedule SIA-Query timer
        uint32_t timerId = eigrpProcess.routingInstance->global.timeManager.addTimer(expirationTime, [this, route, neighborIp, vrf = eigrpProcess.routingInstance->instanceName, as = eigrpProcess.asNumber, af = eigrpProcess.addressFamily]()
        {
            if (VirtualRouter* virtualRouter = eigrpProcess.routingInstance->global.getRoutingInstance(vrf))
            {
                if (auto* eigrp = virtualRouter->getEigrpAutonomousSystem(as))
                {
                    if (af == AddressFamily::IPv4 ? !eigrp->ipv4 : !eigrp->ipv6)
                    {
                        return;
                    }
                }
                else return;
            }
            else return;
            handleSIATimeout(route, neighborIp);
        });

        outgoing.siaTimerId = timerId;
        outgoing.lastSIARefreshTime = std::chrono::steady_clock::now();

        return timerId;
    }

    void EigrpInterface::handleSIATimeout(RoutingTable::Eigrp* route, const ByteString& neighborIp)
    {
        ByteString queryKey = route->network + "/" + std::to_string(route->mask);

        auto it = eigrpProcess.outstandingReplies.find(queryKey);
        if (it == eigrpProcess.outstandingReplies.end()) return;

        if (it->second.originNeighbor == neighborIp) return;

        EigrpConfigs::ActiveRoute& queryInfo = it->second;

        auto neighborIt = queryInfo.pendingQueries.find(neighborIp);
        if (neighborIt == queryInfo.pendingQueries.end()) return;

        std::shared_lock<std::shared_mutex> lock(eigrpProcess.interfaceMutex);
        auto neighborEntry = neighbors.find(neighborIp);
        if (neighborEntry == neighbors.end()) return;

        EigrpConfigs::NeighborInfo* neighbor = neighborEntry->second;
        sendSIAQueryToNeighbor(neighbor, neighborIp);
        startSIATimer(route, neighborIp, neighborIt->second);
    }

    void EigrpInterface::handleActiveTimeExpire(RoutingTable::Eigrp* route)
    {
        ByteString queryKey = route->network + "/" + std::to_string(route->mask);

        auto it = eigrpProcess.outstandingReplies.find(queryKey);
        if (it == eigrpProcess.outstandingReplies.end()) return;

        EigrpConfigs::ActiveRoute& queryInfo = it->second;

        std::vector<ByteString> failedNeighbors;
        for (const auto& [neighborIp, outgoing] : queryInfo.pendingQueries)
            failedNeighbors.push_back(neighborIp);

        for (const auto& neighborIp : failedNeighbors)
        {
            std::shared_lock<std::shared_mutex> lock(eigrpProcess.interfaceMutex);
            for (const auto& [_, interface] : eigrpProcess.eigrpInterfaceList)
            {
                if (interface->neighbors.count(neighborIp))
                {
                    Logger::getInstance().error() << "Neighbor " << neighborIp.toHex() << " did not respond to active Query. Removing neighbor." << std::endl;
                    handleNeighborDown(interface->neighbors[neighborIp], neighborIp);
                }
            }
        }

        // Clean up
        {
            std::lock_guard<std::mutex> lock(activeTimerMutex);

            // Cancel all SIA timers for each neighbor
            for (auto& [neighborIp, outgoing] : queryInfo.pendingQueries)
            {
                eigrpProcess.routingInstance->global.timeManager.cancelTimer(outgoing.siaTimerId);
            }
            activeTimers.erase(queryKey);
        }

        eigrpProcess.outstandingReplies.erase(queryKey);
    }

    void EigrpInterface::cancelActiveTimer(const ByteString &destination, uint8_t mask)
    {
        ByteString key = destination + "/" + std::to_string(mask);

        {
            std::lock_guard<std::mutex> lock(activeTimerMutex);
            auto it = activeTimers.find(key);
            if (it != activeTimers.end())
            {
                eigrpProcess.routingInstance->global.timeManager.cancelTimer(it->second);
                activeTimers.erase(it);
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
                    eigrpProcess.routingInstance->global.timeManager.cancelTimer(pktIt->second.timerId);
                }
            }
        }

        // Schedule a retransmission timer
        auto expirationTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(timeout * 1000));
        uint32_t timerId = eigrpProcess.routingInstance->global.timeManager.addTimer(expirationTime, [this, neighbor, neighborIp, sequenceNumber]()
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
        if (!neighbor || destroy.load(std::memory_order_relaxed)) return;


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
                eigrpProcess.routingInstance->global.timeManager.cancelTimer(pktInfoCopy.timerId);
            }
            handleNeighborDown(neighbor, neighborIp);
            return;
        }

        // Resend the packet
        PacketInfo retransmissionPacket;
        {
            // Construct and send the retransmission packet
            retransmissionPacket.Layer4.push_back(pktInfoCopy.packet.eigrp);
            if (currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed) && !destroy.load(std::memory_order_relaxed))
            {
                ByteString ip = getInterfaceIp();
                IPPacket::buildIp(currentInterface, retransmissionPacket, pktInfoCopy.packet.destination, &ip, nullptr, configs->DSCP.load(std::memory_order_relaxed), 255, Variable::IP::eigrp);
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
        RoutingTable::Eigrp* route = new RoutingTable::Eigrp(interfaceKey);
        size_t start = 0;
        
        try
        {
            // Parse next hop based on address family
            if (eigrpProcess.addressFamily == AddressFamily::IPv4)
            {
                if (value.size() < start + 4) throw std::runtime_error("Insufficient data for IPv4 next Hop");
                route->nextHop = value.substr(start, 4);
                start += 4;
            }
            else if (eigrpProcess.addressFamily == AddressFamily::IPv6)
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
                route->routeType = summary ? RoutingTable::Eigrp::RouteType::SUMMARY : RoutingTable::Eigrp::RouteType::INTERNAL;
            }
            else 
            {
                // External Route Parsing
                if (eigrpProcess.addressFamily == AddressFamily::IPv4) 
                {
                    if (value.size() < start + 4) throw std::runtime_error("Insufficient data for Origin Router (IPv4).");
                    route->originRouter = value.substr(start, 4);
                    start += 4;
                }
                else if (eigrpProcess.addressFamily == AddressFamily::IPv6) 
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

                route->routeType = RoutingTable::Eigrp::RouteType::EXTERNAL;
            }

            // Pad network address to standard length
            size_t standardLength = (eigrpProcess.addressFamily == AddressFamily::IPv4) ? 4 : 16;
            if (route->network.size() < standardLength)
            {
                route->network += ByteString(standardLength - route->network.size(), '\x00');
            }
            
            eigrpProcess.addRouteMetric(configs->localMetric, route);
            route->hopCount++;

            return route;
        }
        catch (const std::exception& e)
        {
            Logger::getInstance().error() << "DecodedRoute Error: " << e.what() << std::endl;
            throw;
        }
    }

    void EigrpInterface::updateRoutingTable(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, const std::vector<EigrpConfigs::RoutingUpdate>& routes)
    {
        // Validate neighbor
        if (!neighbor) return; // invalid neighbor

        std::vector<EigrpConfigs::RoutingUpdate> updatedRoutes;
        std::vector<RoutingTable::Eigrp*> withdrawnRoutes;

        for (const auto& route : routes)
        {
            if (route.withdraw)
            {
                eigrpProcess.topologyTable->removeRoute(route.route->network, route.route->mask, neighborIp);
                auto* existingRoute = eigrpProcess.routingInstance->routingTable.getEigrpRoute(route.route->network, route.route->mask, eigrpProcess.addressFamily, eigrpProcess.asNumber);
                if (existingRoute && existingRoute->nextHop == neighborIp)
                {
                    withdrawnRoutes.push_back(existingRoute);
                }
            }
            else
            {
                if (route.route->reportedDistance > route.route->feasibleDistance) continue; // Violation

                if (auto* summary = isRouteSummarized(route.route->network, route.route->mask))
                {
                    if (route.route->bandwidth < summary->summary->bandwidth)
                    {
                        summary->summary->bandwidth = route.route->bandwidth;
                    }
                    if (route.route->delay < summary->summary->delay)
                    {
                        summary->summary->delay = route.route->delay;
                    }
                }

                // Access the topology table and update it with new routes
                TopologyTable::RouteInfo routeInfo;
                routeInfo.eigrpInterface = this;
                routeInfo.bandwidthMetric = route.route->bandwidth;
                routeInfo.delayMetric = route.route->delay;
                routeInfo.feasibleDistance = route.route->feasibleDistance;
                routeInfo.reportedDistance = route.route->reportedDistance;
                {
                    std::shared_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
                    routeInfo.nextHop = neighbor->ipAddress;
                }
                routeInfo.hopCount = route.route->hopCount;
                routeInfo.isSuccessor = false;
                routeInfo.isFeasibleSuccessor = false;
                routeInfo.routeType = route.route->routeType;

                // Add or update the route in the topology table
                eigrpProcess.topologyTable->addOrUpdateRoute(neighborIp, route.route->network, route.route->mask, routeInfo);

                // Fetch the updated topology table
                auto bestRouteEntry = eigrpProcess.topologyTable->getEntryForRoute(route.route->network, route.route->mask);
                if (!bestRouteEntry)
                {
                    Logger::getInstance().warn() << "Failed to retrieve topology entry for network: " << route.route->network.toHex() << std::endl;
                    continue;
                }

                auto successorIt = std::find_if(
                    bestRouteEntry->routesByNeighbor.begin(),
                    bestRouteEntry->routesByNeighbor.end(),
                    [](const auto& pair) {return pair.second.isSuccessor;});

                if (successorIt == bestRouteEntry->routesByNeighbor.end())
                {
                    Logger::getInstance().debug() << "No successors found for route: " << route.route->network.toHex() << std::endl;
                    continue;
                }

                // Prepare the administrative distance based on the route type
                RoutingTable::Eigrp* newRoute = route.route;
                newRoute->nextHop = successorIt->second.nextHop;
                newRoute->feasibleDistance = successorIt->second.feasibleDistance;

                // Set administrative distance  based on route type
                if (newRoute->routeType == RoutingTable::Eigrp::RouteType::INTERNAL)
                {
                    newRoute->adminDistance = eigrpProcess.configs.adminDistance.load(std::memory_order_relaxed);
                }
                else if (newRoute->routeType == RoutingTable::Eigrp::RouteType::EXTERNAL)
                {
                    newRoute->adminDistance = eigrpProcess.configs.externalAdminDistance.load(std::memory_order_relaxed);
                }
                else if  (newRoute->routeType == RoutingTable::Eigrp::RouteType::SUMMARY)
                {
                    newRoute->adminDistance = eigrpProcess.configs.adminDistance.load(std::memory_order_relaxed);
                }
                else 
                {
                    continue; // Skip unknown route type
                }

                // Check for existing routes and determine if an update is needed
                auto existingRoute = eigrpProcess.routingInstance->routingTable.getEigrpRoute(route.route->network, route.route->mask, eigrpProcess.addressFamily, eigrpProcess.asNumber);
                if (!existingRoute) onPrefixLearned(); // New prefix learned
                bool routeChange = !existingRoute || (existingRoute->feasibleDistance != newRoute->feasibleDistance);

                if (routeChange)
                {
                    eigrpProcess.routingInstance->routingTable.updateEigrp(newRoute, eigrpProcess.addressFamily, eigrpProcess.asNumber);
                    updatedRoutes.emplace_back(newRoute, false);
                }
            }
        }
        eigrpProcess.notifyRoutingChange(updatedRoutes);

        for (const auto& removedRoute : updatedRoutes)
        {
            if (!removedRoute.withdraw) continue;
            if (auto* summary = isRouteSummarized(removedRoute.route->network, removedRoute.route->mask))
            {
                if (summary->summary->bandwidth == removedRoute.route->bandwidth &&
                    summary->summary->delay == removedRoute.route->delay)
                {
                    auto allEigrp = eigrpProcess.routingInstance->routingTable.getAllEigrpRoutes(eigrpProcess.addressFamily, eigrpProcess.asNumber);
                    uint32_t lowDelay = std::numeric_limits<uint32_t>::max();
                    uint32_t lowBandwidth = std::numeric_limits<uint32_t>::max();
                    for (const auto& route : allEigrp)
                    {
                        if (route->network == summary->summary->network && route->mask == summary->summary->mask)
                        {
                            lowDelay = std::min(lowDelay, route->delay);
                        }
                        if (route->network == summary->summary->network && route->mask == summary->summary->mask)
                        {
                            lowBandwidth = std::min(lowBandwidth, route->bandwidth);
                        }
                    }

                    summary->summary->bandwidth = lowBandwidth;
                    summary->summary->delay = lowDelay;
                }
            }

            eigrpProcess.routingInstance->routingTable.removeEigrp(removedRoute.route->network, removedRoute.route->mask, eigrpProcess.addressFamily, eigrpProcess.asNumber);
            auto successors = eigrpProcess.topologyTable->getSuccessorsForRoute(removedRoute.route->network, removedRoute.route->mask);
            bool routeAdded = false;
            for (auto& route : successors)
            {
                if (routeAdded)
                    eigrpProcess.routingInstance->routingTable.addEigrp(route, eigrpProcess.addressFamily, eigrpProcess.asNumber);
                else
                    eigrpProcess.routingInstance->routingTable.updateEigrp(route, eigrpProcess.addressFamily, eigrpProcess.asNumber);
            }
        }
    }

    void EigrpInterface::handleNeighborDown(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp)
    {
        if (neighbors.find(neighborIp) == neighbors.end() || !neighbor) return;
        
        // Cancel timers
        if (neighbor->holdTimerId != 0)
        {
            eigrpProcess.routingInstance->global.timeManager.cancelTimer(neighbor->holdTimerId);
            neighbor->holdTimerId = 0;
        }
        for (const auto &timerEntry : neighbor->retransmissionTimers)
        {
            eigrpProcess.routingInstance->global.timeManager.cancelTimer(timerEntry.second);
        }

        // Clear reliable packets and sequence tracking
        {
            std::lock_guard<std::mutex> lock(neighbor->reliableMutex);
            neighbor->retransmissionTimers.clear();
            neighbor->reliablePackets.clear();
        }

        // Collect affected routes
        std::vector<std::pair<ByteString, uint8_t>> affectedRoutes;
        for (auto& [destination, entry] : eigrpProcess.topologyTable->getTopologyEntries())
        {
            // If this neighbor was advertising the route
            if (entry->routesByNeighbor.find(neighborIp) != entry->routesByNeighbor.end())
            {
                affectedRoutes.push_back(std::pair(destination, entry->prefixLength));
            }
        }

        // Remove the neighbor rotues from topology
        eigrpProcess.topologyTable->handleNeighborDown(neighborIp);

        // Trigger Active for routes with no feasible successor
        std::vector<RoutingTable::Eigrp*> activeRoutes;
        for (const auto& [prefix, mask] : affectedRoutes)
        {
            // Find the best remaining successor
            auto bestRoute = eigrpProcess.topologyTable->findBestRoute(prefix, mask);
            if (!bestRoute.has_value())
            {
                // If no valid successor is remaining
                RoutingTable::Eigrp* route = eigrpProcess.routingInstance->routingTable.getEigrpRoute(prefix, mask, eigrpProcess.addressFamily, eigrpProcess.asNumber);
                if (route)
                {
                    route->stuckInActive = true;
                    activeRoutes.push_back(route);
                }
            }
            else
            {
                // Reinstall best route if available
                updateRoutingTableForDestination(prefix, mask);
            }
        }

        // Remove neighbor from neighbor list
        {
            std::unique_lock<std::shared_mutex> intLock(neighborMutex);
            if (neighbors.find(neighborIp) == neighbors.end())
            {
                return; // Neighbor already removed
            }
            neighbors.erase(neighborIp);
        }
        {
            std::lock_guard<std::mutex> globalLock(eigrpProcess.neighborMutex);
            eigrpProcess.allNeighbors.erase(neighborIp);
        }

        if (!neighbor->unicast)
        {
            delete neighbor;
            neighbor = nullptr;
        }

        // Launch queries for routes that entered Active
        if (!activeRoutes.empty())
        {
            sendQueryToNeighbors(activeRoutes);
        }

    }
    
    void EigrpInterface::gracefulRestart(EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp)
    {
        auto expireTime = std::chrono::steady_clock::now() + std::chrono::seconds(eigrpProcess.configs.purgeTime.load(std::memory_order_relaxed));
        uint32_t gracefulTimerId = eigrpProcess.routingInstance->global.timeManager.addTimer(expireTime, [&]() {
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
            eigrpProcess.topologyTable->removeRoutesFromNeighbor(neighborIp);
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
        auto entry = eigrpProcess.topologyTable->getEntryForRoute(destination, prefixLength);
        if (!entry)
        {
            eigrpProcess.routingInstance->routingTable.removeEigrp(destination, prefixLength, eigrpProcess.addressFamily, eigrpProcess.asNumber);
            return;
        }
        
        // Find the successor route
        auto successorIt = std::find_if(entry->routesByNeighbor.begin(), entry->routesByNeighbor.end(),
                                        [](const auto &pair) { return pair.second.isSuccessor; });

        if (successorIt != entry->routesByNeighbor.end())
        {
            // Update the routing table accordingly
            RoutingTable::Eigrp* newRoute = new RoutingTable::Eigrp(interfaceKey);
            newRoute->bandwidth = successorIt->second.bandwidthMetric;
            newRoute->delay = successorIt->second.delayMetric;
            newRoute->hopCount = successorIt->second.hopCount;
            newRoute->mtu = eigrpProcess.addressFamily == AddressFamily::IPv4
                ? currentInterfaceInfo->ipv4.mtu.load(std::memory_order_relaxed)
                : currentInterfaceInfo->ipv6.mtu.load(std::memory_order_relaxed);
            newRoute->reliability = 255;
            newRoute->load = eigrpProcess.configs.variance.load(std::memory_order_relaxed);
            newRoute->network = destination;
            newRoute->mask = entry->prefixLength;
            newRoute->nextHop = successorIt->second.nextHop;
            newRoute->metric = successorIt->second.feasibleDistance;
            newRoute->routeType = successorIt->second.routeType;

            // Update the global routing table
            if (eigrpProcess.routingInstance->routingTable.addEigrp(newRoute, eigrpProcess.addressFamily, eigrpProcess.asNumber))
            {
                eigrpProcess.notifyRoutingChange({{newRoute, false}});
            }
        }
        else
        {
            eigrpProcess.routingInstance->routingTable.removeEigrp(destination, prefixLength, eigrpProcess.addressFamily, eigrpProcess.asNumber);
        }
    }

    double EigrpInterface::calculateRTT(EigrpConfigs::NeighborInfo* neighbor, uint32_t sequenceNumber)
    {
        auto sendTimeIt = neighbor->reliablePackets.find(sequenceNumber);

        if (sendTimeIt != neighbor->reliablePackets.end())
        {
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
        return neighbor->srtt;
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
        uint32_t bandwidth = eigrpProcess.configs.lowestBandwidth.load(std::memory_order_relaxed);
        uint32_t delay = currentInterfaceInfo->delay.load(std::memory_order_relaxed);

        RoutingTable::Eigrp* route = new RoutingTable::Eigrp(interfaceKey);
        route->nextHop = configs->nextHopSelf.load(std::memory_order_relaxed) ? getInterfaceIp() : ByteString(summaryRoute.summary->network.size(), '\x00');
        route->bandwidth = (10000000 / bandwidth) * 256;
        route->delay = (delay / 10) * 256;
        route->mtu = eigrpProcess.addressFamily == AddressFamily::IPv4
            ? currentInterfaceInfo->ipv4.mtu.load(std::memory_order_relaxed)
            : currentInterfaceInfo->ipv6.mtu.load(std::memory_order_relaxed);
        route->hopCount = 0;
        route->reliability = 255;
        route->load = eigrpProcess.configs.variance.load(std::memory_order_relaxed);
        route->routeTag = 0;
        route->mask = summaryRoute.summary->mask;
        route->network = summaryRoute.summary->network;
        route->routeType = RoutingTable::Eigrp::RouteType::SUMMARY;

        return route;
    }

    ByteString EigrpInterface::encodeStubOption(const EigrpConfigs::StubConfig& stub)
    {
        uint16_t flags = 0;
        if (stub.advertiseConnected) flags |= 0x0001;
        if (stub.advertiseStatic) flags |= 0x0002;
        if (stub.advertiseSummary) flags |= 0x0004;
        if (stub.advertiseRedistributed) flags |= 0x0008;
        if (stub.advertiseLeakMap) flags |= 0x0010;
        if (stub.receiveOnly) flags |= 0x0020;
        return Functions::numToByte(flags, 2);
    }

    void EigrpInterface::configureAuthentication(uint8_t* keyId, const ByteString* key, EigrpConfigs::AuthType* type, bool enable)
    {
        // Validate neighbor
        {
            std::unique_lock<std::shared_mutex> lock(configs->configsMutex);

            if (!enable)
            {
                configs->authKey.authType = EigrpConfigs::AuthType::NONE;
                configs->authKey.key = "";
                configs->authKey.keyId = 0;
            }

            if (keyId) configs->authKey.keyId = *keyId;
            if (key) configs->authKey.key = *key;
            if (type) configs->authKey.authType = *type;

            if (configs->authKey.authType != EigrpConfigs::AuthType::NONE &&
                configs->authKey.key != "" && 
                configs->authKey.keyId != 0)
            {
                configs->authKey.fullyEnabled.store(true, std::memory_order_release);
            }
        }
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

    EigrpHeader::Option EigrpInterface::generateAuthenticatedTLV(const EigrpHeader& eigrp)
    {
        EigrpHeader::Option authTLV;
        if (!configs->authKey.fullyEnabled.load(std::memory_order_relaxed))
        {
            return authTLV;
        }

        ByteString key;
        uint8_t id;
        EigrpConfigs::AuthType type;
        {
            std::shared_lock<std::shared_mutex> lock(configs->configsMutex);
            key = configs->authKey.key;
            id = configs->authKey.keyId;
            type = configs->authKey.authType;
        }

        authTLV.option = Variable::Eigrp::Option::authentication;

        // Key ID (1 byte) + HMAC placeholder (16 bytes of zeros)
        const size_t hmacLength = (type == EigrpConfigs::AuthType::MD5) ? MD5_DIGEST_LENGTH : SHA_DIGEST_LENGTH;
        authTLV.value = Functions::numToByte(id, 1) + ByteString(hmacLength, 0x00);
        authTLV.length = Functions::numToByte(static_cast<uint32_t>(authTLV.value.size()) + 4, 2);

        // Temporarily add the zeroed Authentication TLV to the EIGRP header
        EigrpHeader tempEigrp = eigrp;
        tempEigrp.options.push_back(authTLV);
        ByteString serializedHeader = serializeEigrpHeader(tempEigrp, /*exclusiveAuthTLV=*/false);

        // Compute HMAC-MD5 over the serialed header
        ByteString computedHMAC;
        if (type == EigrpConfigs::AuthType::MD5)
        {
            computedHMAC = Authentication::generateHMAC(serializedHeader, key, "MD5");
        }
        else if (type == EigrpConfigs::AuthType::SHA1)
        {
            computedHMAC = Authentication::generateHMAC(serializedHeader, key, "SHA1");
        }

        // Replace the zeroed HMAC with the real computed HMAC
        authTLV.value.replace(1, hmacLength, computedHMAC);

        return authTLV;
    }

    void EigrpInterface::setPassive(bool passive)
    {
        configs->isPassive.store(passive, std::memory_order_release);
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
        if (!configs->multicastEnabled.load(std::memory_order_relaxed))
        {
            configs->multicastEnabled.store(true, std::memory_order_release);
        }
    }

    void EigrpInterface::disableMulticast()
    {
        // Check if multicast is already disabled
        if (!configs->multicastEnabled.load(std::memory_order_relaxed)) return;

        configs->multicastEnabled.store(false, std::memory_order_release);
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
        std::unique_lock<std::shared_mutex> intLock(neighborMutex);

        auto it = neighbors.find(ipAddress);
        if (it == neighbors.end()) // Double check
        {
            auto* neighbor = new EigrpConfigs::NeighborInfo(eigrpProcess.routingInstance->global.timeManager, ipAddress, unicast);
            neighbor->macAddress = macAddress;
            neighbors[ipAddress] = neighbor;
            {
                std::lock_guard<std::mutex> globalLock(eigrpProcess.neighborMutex);
                eigrpProcess.allNeighbors[ipAddress] = neighbor;
            }
        }
    }

    void EigrpInterface::addUnicastNeighbor(const ByteString& neighborIp)
    {
        {
            std::unique_lock<std::shared_mutex> intLock(neighborMutex);

            // Check if neighbor already exists
            if (neighbors.find(neighborIp) != neighbors.end())
            {
                // Delete neighbor and replace it with a unicast neighbor if neighbor exists
                delete neighbors[neighborIp];
                neighbors[neighborIp] = nullptr;
                neighbors.erase(neighborIp);
                neighbors[neighborIp] = new EigrpConfigs::NeighborInfo(eigrpProcess.routingInstance->global.timeManager, neighborIp, true);
            }
            else
            {
                // Add the neighbor normally if not present
                neighbors[neighborIp] = new EigrpConfigs::NeighborInfo(eigrpProcess.routingInstance->global.timeManager, neighborIp, true);
            }

            std::lock_guard<std::mutex> globalLock(eigrpProcess.neighborMutex);
            eigrpProcess.allNeighbors[neighborIp] = neighbors[neighborIp];
        }

        // Disable mutlicast if enabled
        if (configs->multicastEnabled.load(std::memory_order_relaxed))
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
        return (eigrpProcess.addressFamily == AddressFamily::IPv4) ? Variable::Multicast::Eigrp::address : Variable::Multicast::Eigrp::addressv6;
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
    
    void EigrpInterface::recordRouteChange()
    {
        if (!eigrpProcess.configs.dampening.load(std::memory_order_relaxed)) return;

        auto now = std::chrono::steady_clock::now();
        routeChangeTimes.push_back(now);

        // Drop old changes outside of interval
        while (!routeChangeTimes.empty() &&
               now - routeChangeTimes.front() > std::chrono::seconds(configs->dampeningInterval.load(std::memory_order_relaxed)))
        {
            routeChangeTimes.pop_front();
        }

        // Supress if too many changes
        if (!isSupressed && routeChangeTimes.size() >= configs->dampeningChange)
        {
            isSupressed.store(true, std::memory_order_release);
            supressedUntil = now + std::chrono::seconds(eigrpProcess.configs.dampeningResetTime.load(std::memory_order_relaxed));
            ++restartCounter;

            if (eigrpProcess.configs.dampeningWarnings)
                std::cout << ""; //TODO warning output

            eigrpProcess.routingInstance->global.timeManager.addTimer(
                supressedUntil,
                [&]() { checkSuppressionStatus(); }
            );
        }
    }

    void EigrpInterface::checkSuppressionStatus()
    {
        if (!isSupressed) return;

        auto now = std::chrono::steady_clock::now();
        if (now >= supressedUntil)
        {
            if (restartCounter >= eigrpProcess.configs.dampeningRestartCount)
            {
                if (eigrpProcess.configs.dampeningWarnings)
                    std::cout << ""; //TODO warning output
                return;
            }

            // Add delay before nect re-evaluation
            supressedUntil = now + std::chrono::seconds(eigrpProcess.configs.dampeningRestart.load(std::memory_order_relaxed));
            isSupressed = false;
            routeChangeTimes.clear();

            if (eigrpProcess.configs.dampeningWarnings.load(std::memory_order_relaxed))
                std::cout << ""; //TODO warning output

            eigrpProcess.routingInstance->global.timeManager.addTimer(
                now + std::chrono::seconds(eigrpProcess.configs.dampeningResetTime.load(std::memory_order_relaxed)),
                [&]() { checkSuppressionStatus(); }
            );
        }
    }

    void EigrpInterface::onPrefixLearned()
    {
        uint32_t maxPrefix = eigrpProcess.configs.maximumPrefix.load(std::memory_order_relaxed);
        if (maxPrefix == 0) return;

        prefixCount.fetch_add(1, std::memory_order_acquire);
        if (prefixCount.load(std::memory_order_relaxed) > maxPrefix)
        {
            isSupressed = true;
            restartCounter++;
            routeChangeTimes.clear();

            if (eigrpProcess.configs.dampeningWarnings.load(std::memory_order_relaxed))
                std::cout << ""; //TODO warning output

            eigrpProcess.routingInstance->global.timeManager.addTimer(
                std::chrono::steady_clock::now() + std::chrono::seconds(eigrpProcess.configs.dampeningResetTime.load(std::memory_order_relaxed)),
                [&]() { checkSuppressionStatus(); }
            );
        }
    }

    inline ByteString EigrpInterface::getInterfaceIp()
    {
        if (currentInterfaceInfo)
        {
            return (eigrpProcess.addressFamily == AddressFamily::IPv4) ? currentInterfaceInfo->ipv4.getAddress() : currentInterfaceInfo->ipv6.getLocalAddress();
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

    std::vector<RoutingTable::Eigrp*> TopologyTable::getSuccessorsForRoute(const ByteString& network, uint8_t mask)
    {
        std::lock_guard<std::mutex> lock(tableMutex);

        std::vector<RoutingTable::Eigrp*> successors;

        ByteString key = network + "/" + std::to_string(mask);
        auto it = topologyEntries.find(key);
        if (it == topologyEntries.end()) return {};

        TopologyEntry* entry = it->second;

        updateSuccessorAndFeasibleSuccessors(entry);

        if (entry->successors.empty())
            return {};

        for (const auto& neighbor : entry->successors)
        {
            const auto& route = entry->routesByNeighbor.at(neighbor);
            if (route.feasibleDistance == std::numeric_limits<uint32_t>::max())
                continue;

            // Construct a routing table entry
            RoutingTable::Eigrp* routingEntry = new RoutingTable::Eigrp(entry->routesByNeighbor[neighbor].eigrpInterface->interfaceKey);
            routingEntry->network = entry->destination;
            routingEntry->mask = entry->prefixLength;
            routingEntry->nextHop = neighbor;
            routingEntry->feasibleDistance = route.feasibleDistance;
            routingEntry->reportedDistance = route.reportedDistance;
            routingEntry->routeType = route.routeType;

            if (route.routeType == RoutingTable::Eigrp::RouteType::EXTERNAL)
                routingEntry->adminDistance = eigrpProcess->configs.externalAdminDistance.load(std::memory_order_relaxed);
            else
                routingEntry->adminDistance = eigrpProcess->configs.adminDistance.load(std::memory_order_relaxed);

            successors.push_back(routingEntry);
        }


        for (const auto& [destination, entry] : topologyEntries)
        {
            if (!entry->successors.empty())
            {
            }
        }

        return successors;
    }

    void TopologyTable::addOrUpdateRoute(const ByteString& neighborIp, const ByteString& destination, uint8_t prefixLength, const RouteInfo &routeInfo)
    {
        std::lock_guard<std::mutex> lock(tableMutex);

        // Create or update the topology table entry
        ByteString key = destination + "/" + std::to_string(prefixLength);
        auto entryIt = topologyEntries.find(key);
        TopologyEntry* entry = nullptr;
        if (entryIt == topologyEntries.end())
        {
            entry = new TopologyEntry();
            entry->destination = destination;
            entry->prefixLength = prefixLength;
            topologyEntries[key] = entry;
        }
        else
        {
            entry = entryIt->second;
        }

        {
            if (routeInfo.reportedDistance > routeInfo.feasibleDistance) return;
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
            // Feasibility Condition: Reported Distance < Best Feasible Distance
            route.isFeasibleSuccessor = (route.reportedDistance < bestFD);

            // If the route is a feasible successor, add it to the feasible successors list
            if (route.isFeasibleSuccessor)
            {
                entry->feasibleSuccessors.push_back(neighbor);
            }

            // Check if the route meets the Successor Condition for being a successor
            bool withinVariance = (route.feasibleDistance <= bestFD * eigrpProcess->configs.variance.load(std::memory_order_relaxed));

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
        if (eigrpProcess->configs.trafficShareMode.load(std::memory_order_relaxed) == EigrpConfigs::TrafficShareMode::Minimum)
        {
            // Only keep the route with the lowest FD
            if (!entry->successors.empty())
            {
                const ByteString& bestNeighbor = entry->successors.front();
                entry->successors = {bestNeighbor}; // Keep only the best
            }
        }
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

    TopologyTable::TopologyEntry* TopologyTable::getEntryForRoute(const ByteString& prefix, uint8_t mask)
    {
        std::lock_guard<std::mutex> lock(tableMutex);

        ByteString key = prefix + "/" + std::to_string(mask);
        auto it = topologyEntries.find(key);
        if (it == topologyEntries.end())
        {
            Logger::getInstance().warn() << "No entry found for destination: " << prefix.toHex() << std::endl;
            return nullptr;
        }

        return it->second;
    }

    std::optional<TopologyTable::RouteInfo> TopologyTable::findBestRoute(const ByteString &prefix, uint8_t mask)
    {
        std::lock_guard<std::mutex> lock(tableMutex);

        ByteString key = prefix + "/" + std::to_string(mask);
        auto it = topologyEntries.find(key);
        if (it == topologyEntries.end())
        {
            Logger::getInstance().warn() << "No topology entry for destination: " << prefix.toHex() << std::endl;
            return std::nullopt;
        }

        const auto& entry = it->second;

        if (entry->successors.empty())
        {
            Logger::getInstance().info() << "No successors available for destination: " << prefix.toHex() << std::endl;
            return std::nullopt;
        }

        // Return the route information for the best successor
        const ByteString& bestNeighbor = entry->successors.front();
        return entry->routesByNeighbor.at(bestNeighbor);
    }

    void TopologyTable::handleRouteFailure(const ByteString &prefix, uint8_t mask, const ByteString& failedNeighborIp)
    {
        std::lock_guard<std::mutex> lock(tableMutex);

        ByteString key = prefix + "/" + std::to_string(mask);
        auto it = topologyEntries.find(key);
        if (it == topologyEntries.end())
        {
            Logger::getInstance().warn() << "No topology entry found for failed route to: " << prefix.toHex();
            return;
        }

        auto& entry = it->second;

        // Remove the failed neighbor's route
        entry->routesByNeighbor.erase(failedNeighborIp);

        // If no remaining neighbors, remove route completely
        if (entry->routesByNeighbor.empty())
        {
            Logger::getInstance().info() << "No remaining routes for destination: " << prefix.toHex() << std::endl;
            topologyEntries.erase(it);
            return;
        }

        // Recalculate successors and feasible successors
        updateSuccessorAndFeasibleSuccessors(entry);
    }

    void TopologyTable::markRouteAsPassive(const ByteString &prefix, uint8_t mask, EigrpInterface *eigrp)
    {
        std::lock_guard<std::mutex> lock(tableMutex);
        ByteString key = prefix + "/" + std::to_string(mask);
        auto it = topologyEntries.find(key);
        if (it != topologyEntries.end())            // Default constructor
        {
            auto entry = it->second;
            entry->isActive = false;

            // Cancel Active timer if running
            if (entry->activeTimerId != 0)
            {
                eigrpProcess->routingInstance->global.timeManager.cancelTimer(entry->activeTimerId);
                entry->activeTimerId = 0;
            }
        }
    }

    bool TopologyTable::removeRoute(const ByteString &prefix, uint8_t mask, const ByteString& neighbor)
    {
        ByteString key = prefix + "/" + std::to_string(mask);
        std::lock_guard<std::mutex> lock(tableMutex);
        auto entryIt = topologyEntries.find(key);
        if (entryIt == topologyEntries.end())
        {
            return false; // Entry does not exist
        }
        entryIt->second->routesByNeighbor.erase(neighbor);
        if (entryIt->second->routesByNeighbor.empty())
        {
            delete entryIt->second;
            topologyEntries.erase(key);
        }
        return true;
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

#pragma endregion
