// Finish auth hash stuff
// add 0003 and 0002 tlv for classic
#include <Eigrp.h>
#include <InterfaceConfigs.h>
#include <Encapsulation.h>
#include <VirtualRouter.h>
#include <Interface.h>
#include <InterfaceType.hpp>
#include <algorithm>
#include <Global.h>
#include <IPPacket.h>
#include <Functions.h>
#include <PacketBuilder.hpp>
#include <InterfaceConfigs.h>
#include <Encryption.hpp>

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

    size_t Eigrp::addCommonTlvs(EigrpHeader& hdr, EigrpInterface& cfg, bool isUpdate, bool isAck, const uint8_t* neighborIp, uint32_t sequenceNumber)
    {
        uint8_t* buffer = hdr.getTrailData();
        size_t offset = 0;
        if (!isAck)
        {
            // Parameter TLV (K-values and Hold Time)
            writeU16(buffer, Variable::Eigrp::Option::parameter);
            writeU16(buffer, 12); // Size
            calculateParameters(buffer + 4, cfg.configs->holdTime.load(std::memory_order_relaxed));

            // Version TLV
            writeU16(buffer + 12, Variable::Eigrp::Option::version);
            writeU16(buffer + 14, 8); // Size
            writeU16(buffer + 16, Variable::Eigrp::Version::release);
            writeU16(buffer + 18, Variable::Eigrp::Version::tls);

            offset = 20;

            // Sequence TLV
            if (isUpdate)
            {
                uint8_t ipSize = static_cast<uint8_t>(addressFamily);
                writeU16(buffer + offset, Variable::Eigrp::Option::sequence);
                writeU16(buffer + offset + 2, 5 + ipSize); // Size
                buffer[offset + 4] = ipSize;
                std::memcpy(buffer + offset + 5, neighborIp, ipSize);
                offset += 5 + ipSize;

                writeU16(buffer + offset, Variable::Eigrp::Option::multicastSequence);
                writeU16(buffer + offset + 2, 8); // Size
                writeU32(buffer + offset + 4, sequenceNumber);
                offset += 8;
            }
        }

        // Create other TLVs if applicable.

        // Stub TLV
        if (isStub())
        {
            writeU16(buffer + offset, Variable::Eigrp::Option::stub);
            writeU16(buffer + offset + 2, 6); // Size
            std::shared_lock<std::shared_mutex> configLock(configs.configsMutex);
            cfg.encodeStubOption(buffer + 4, configs.stubConfig);
            offset += 6;
        }

        // Authentication TLV
        if (cfg.configs->authKey.fullyEnabled.load(std::memory_order_relaxed))
        {
            writeU16(buffer + offset, Variable::Eigrp::Option::authentication);
            writeU16(buffer + offset + 2, 20); // Size
            offset += 4;
            offset += cfg.generateAuthenticatedTLV(buffer);
        }
        return offset;
    }

    void Eigrp::eigrpHello(PacketBuilder& packet, EigrpInterface& eigrpInt, const uint8_t* neighborIp, uint32_t sequenceNumber,  bool ack, bool update)
    {
        addressFamily == AddressFamily::IPv4
            ? IPPacket::reserveIpv4(eigrpInt.currentInterface, packet)
            : IPPacket::reserveIpv6(eigrpInt.currentInterface, packet);

        packet.reserveHeader(HeaderType::EIGRP, EigrpHeader::fixedSize);
        auto nextHeader = packet.nextBuildHeader();
        if (!nextHeader) return;

        // Create the EIGRP Ack packet
        EigrpHeader eigrp;
        eigrp.setBuffer(nextHeader->buffer);

        eigrp.raw->version = 0x02;
        eigrp.setOpcode(Variable::Eigrp::Type::hello);
        std::fill(eigrp.raw->checksum, eigrp.raw->checksum + 2, 0);
        eigrp.setFlagInit(false);
        eigrp.setFlagCondRecv(false);
        eigrp.setFlagRestart(false);
        eigrp.setFlagEndOfTable(false);
        eigrp.setSequence(0);
        eigrp.setAck(0);
        eigrp.setVirtualRouterId(virtualRouterID);
        eigrp.setAutonomousSystem(asNumber);

        // Construct TLVs
        packet.addTLVSize(addCommonTlvs(eigrp, eigrpInt, update, ack, neighborIp, sequenceNumber));
    }

    void Eigrp::eigrpUpdate(EigrpHeader &eigrp, uint32_t sequenceNum, bool init, bool conditional, bool restart, bool endoftable, bool query, bool reply)
    {
        // Create an initiated eigrp update header
        eigrp.raw->version = 0x02;
        if (reply)
        {
            eigrp.setOpcode(Variable::Eigrp::Type::reply);
        }
        else if (query)
        {
            eigrp.setOpcode(Variable::Eigrp::Type::query);
        }
        else
        {
            eigrp.setOpcode(Variable::Eigrp::Type::update);
        }
        std::fill(eigrp.raw->checksum, eigrp.raw->checksum + 2, 0);
        eigrp.setFlagInit(false);
        eigrp.setFlagCondRecv(false);
        eigrp.setFlagRestart(false);
        eigrp.setFlagEndOfTable(false);
        eigrp.setSequence(sequenceNum);
        eigrp.setAck(0);
        eigrp.setVirtualRouterId(virtualRouterID);
        eigrp.setAutonomousSystem(asNumber);
    }

    bool Eigrp::testAddress(const uint8_t* testIp)
    {
        {
            std::shared_lock<std::shared_mutex> configMutex(configs.configsMutex);
            for (const auto& network : configs.networks)
            {
                if (Functions::compareNetworkWithIp(network.ip.raw, testIp, network.mask, addressFamily))
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
            uint8_t ipAddress[16];

            if (addressFamily == AddressFamily::IPv4)
                interfaceInfo.ipv4.getAddress(ipAddress);
            else
                if (!interfaceInfo.ipv6.getGlobalUnicast(ipAddress)) return nullptr;

            EigrpInterface* instance = nullptr;
            EigrpInterfaceInstance* interfaceInstance = nullptr;

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
            auto pairIt = eigrpInterfaceConfigList.find(interface->configs.key);
            if (pairIt != eigrpInterfaceConfigList.end())
            {
                intConfig = pairIt->second;
            }
            else
            {
                // INITIALIZE EIGRP CONFIGURATIONS
                if (namedMode)
                {
                    intConfig = new EigrpConfigs::InterfaceConfigs(interface->configs.key);
                }
                else
                {
                    intConfig = interface->getEigrpConfig(asNumber, addressFamily, false);
                }
                eigrpInterfaceConfigList[interface->configs.key] = intConfig;
            }

            if (addressFamily == AddressFamily::IPv4)
            {
                instance = new EigrpInterface(*this, eigrpInterfaceConfigList[interface->configs.key], interface);
                interfaceInstance->IPv4 = instance;
                eigrpInterfaceList[interface->configs.key] = instance;
                interface->eigrpInterfaceList[asNumber] = interfaceInstance;
                return instance;
            }
            else if (addressFamily == AddressFamily::IPv6)
            {
                instance = new EigrpInterface(*this, intConfig, interface);
                interfaceInstance->IPv6 = instance;
                eigrpInterfaceList[interface->configs.key] = instance;
                return instance;
            }
        }
        return nullptr;
    }

    void Eigrp::updateInterfaceList()
    { {
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
                    uint8_t ipAddress[16];
                    bool ipv6Contained = false;
                    auto& ipInfo = interface->configs;

                    if (namedMode)
                    {
                        std::shared_lock<std::shared_mutex> lock(interfaceMutex);
                        ipv6Contained = eigrpInterfaceConfigList.contains(ipInfo.key) && !eigrpInterfaceConfigList[ipInfo.key]->shutdown;
                    }

                    ipInfo.ipv4.getAddress(ipAddress);
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

                            if (configs.autoSummarizationEnabled.load(std::memory_order_relaxed) && addressFamily != AddressFamily::IPv6)
                            {
                                uint8_t majorNetwork[4];
                                uint8_t defaultMask = Functions::findClassfullNetworkAndMask(majorNetwork, ipAddress);

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

    uint8_t* Eigrp::calculateParameters(uint8_t* out, uint16_t holdTime)
    {
        EigrpConfigs::KValue kvalue;
        {
            std::shared_lock<std::shared_mutex> lock(configs.configsMutex);
            kvalue = configs.kvalue;
        }

        out[0] = kvalue.k1_Bandwidth;
        out[1] = kvalue.k2_Load;
        out[2] = kvalue.k3_Delay;
        out[3] = kvalue.k4_Reliability;
        out[4] = kvalue.k5_MTU;
        out[5] = kvalue.k6_Power;
        writeU16(out + 6, holdTime);

        return out;
    }

    void Eigrp::updateRoutingTableForConnected(EigrpInterface* eigrpInterface)
    {
        std::vector<EigrpConfigs::RoutingUpdate> updatedRoutes{};
        {
            if (!routingInstance) return;

            RoutingTable& routingTable = routingInstance->routingTable;
            std::unordered_set<IPPrefix> connectedNetworks;
            
            std::unordered_map<uint32_t, EigrpInterface*> currentEigrpInterface;
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
                auto updateRoutesForInterface([&](EigrpInterface* eigrpInterfacePtr)
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
                        uint8_t connectedNetwork[16];

                        {
                            if (addressFamily == AddressFamily::IPv4)
                            {
                                connectedMask = interfaceInfo.ipv4.getMask();
                                uint8_t address[4];
                                interfaceInfo.ipv4.getAddress(address);
                                Functions::computeNetworkAddress(connectedNetwork, address, connectedMask, AddressFamily::IPv4);
                            }
                            else if (addressFamily == AddressFamily::IPv6)
                            {
                                IPAddress address;
                                connectedMask = interfaceInfo.ipv6.getGlobalUnicastPair(address.raw);
                                if (address.v6 == 0) return; // No global address available on this interface
                                Functions::computeNetworkAddress(connectedNetwork, address.raw, connectedMask, AddressFamily::IPv6);
                            }
                            else return;
                        }

                        // Compute the connected network
                        connectedNetworks.emplace(connectedNetwork, connectedMask, addressFamily);

                        // Create EIGRP route entry
                        RoutingTable::Eigrp* connectedRoute = new RoutingTable::Eigrp(eigrpInterfacePtr->interfaceKey);
                        connectedRoute->bandwidth = ( 10000000 / eigrpBw ) * 256;
                        connectedRoute->delay = 0;
                        connectedRoute->hopCount = 0;
                        connectedRoute->mtu = mtu;
                        connectedRoute->reliability = 255;
                        connectedRoute->load = configs.variance.load(std::memory_order_relaxed);
                        connectedRoute->mask = connectedMask;
                        connectedRoute->routeType = RoutingTable::Eigrp::RouteType::CONNECTED;

                        std::memcpy(connectedRoute->network.raw, connectedNetwork, static_cast<uint8_t>(addressFamily));
                        if (eigrpInterface && eigrpInterface->configs->nextHopSelf.load(std::memory_order_relaxed))
                            { connectedRoute->nextHop = eigrpInterface->getInterfaceIp(); }
                        else
                            { std::memset(connectedRoute->nextHop.raw, 0, 16); } // indicates directly connected

                        auto existingRoute = routingInstance->routingTable.getEigrpRoute(connectedNetwork, connectedRoute->mask, addressFamily, asNumber);

                        bool hasChanged = !existingRoute ||
                                        existingRoute->feasibleDistance != connectedRoute->feasibleDistance ||
                                        existingRoute->mask != connectedRoute->mask;

                        if (hasChanged)
                        {
                            // Insert into Routing Table
                            routingTable.addEigrp(connectedRoute, addressFamily, asNumber);
                            
                            // Advertise the connected route to eigrp neighbors
                            updatedRoutes.emplace_back(connectedRoute, false);
                        }
                        else
                        {
                            delete connectedRoute;
                            connectedRoute = nullptr;
                        }
                    }
                    else
                    {
                        IPAddress address;
                        uint8_t mask;
                        if (addressFamily == AddressFamily::IPv4)
                        {
                            eigrpInterfacePtr->currentInterface->configs.ipv4.getAddress(address.raw);
                            mask = eigrpInterfacePtr->currentInterface->configs.ipv4.getMask();
                        }
                        else
                        {
                            mask = eigrpInterfacePtr->currentInterface->configs.ipv6.getGlobalUnicastPair(address.raw);
                            if (address.v6 == 0) return;
                        }
                        auto removalRoute = routingInstance->routingTable.getEigrpRoute(address.raw, mask, addressFamily, asNumber);
                        if (removalRoute)
                        {
                            routingInstance->routingTable.removeEigrp(address.raw, mask, addressFamily, asNumber);
                            updatedRoutes.emplace_back(removalRoute, true);
                        }
                    }
                });

                {
                    if (eigrpInterface)
                    {
                        updateRoutesForInterface(eigrpInterface);
                    }
                    else
                    {
                        std::shared_lock<std::shared_mutex> lock(interfaceMutex);
                        for (const auto& [_, eigrpInterfacePtr] : eigrpInterfaceList)
                        {
                            updateRoutesForInterface(eigrpInterfacePtr);
                        }
                    }
                }
            }

            // Remove any routes that are no longer exist
            for (const auto& route : routingInstance->routingTable.getAllEigrpRoutes(addressFamily, asNumber))
            {
                if (route->routeType == RoutingTable::Eigrp::RouteType::CONNECTED)
                {
                    if (!connectedNetworks.contains(IPPrefix{route->network.raw, route->mask, addressFamily}))
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
                routingInstance->routingTable.removeEigrp(route.route->network.raw, route.route->mask, addressFamily, asNumber);
            }
        }
    }

    void Eigrp::notifyRoutingChange(const std::vector<EigrpConfigs::RoutingUpdate>& changedRoutes)
    {
        Logger::getInstance().info() << "Notifying all neighbors for route changes" << std::endl;

        {
            std::shared_lock<std::shared_mutex> lock(interfaceMutex);

            // Adjust summaries based on added/removed routes
            for (const auto [_, eigrpInterfacePtr] : eigrpInterfaceList)
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

    void Eigrp::redistributeRoute(const uint8_t* destination, uint8_t mask, const uint16_t protocol)
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
            std::map<std::pair<IPAddress, uint8_t>, std::vector<RoutingTable::Eigrp*>> classfulGroups;

            // Process all existing EIGRP routes and group by classical networks
            for (RoutingTable::Eigrp* route : routingInstance->routingTable.getAllEigrpRoutes(addressFamily, asNumber))
            {
                if (route->delay == 0xFFFFFFFF || route->routeType == RoutingTable::Eigrp::RouteType::SUMMARY)
                    continue;
                IPAddress major;
                uint8_t mask = Functions::findClassfullNetworkAndMask(major.raw, route->network.raw);
                classfulGroups[{major, mask}].push_back(route);
            }

            // Only summarize when there are 2+ subnets in a classful group
            for (const auto& [majorNet, routes] : classfulGroups)
            {
                if (routes.size() < 2) continue;

                // For each interface, check if summary is missing
                for (const auto& [_, iface] : eigrpInterfaceList)
                {
                    if (!iface->isRouteSummarized(majorNet.first.raw, majorNet.second))
                    {
                        iface->addSummaryRoute(majorNet.first.raw, majorNet.second, true);
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
        std::map<std::pair<IPAddress, uint8_t>, std::vector<RoutingTable::Eigrp*>> grouped;
        for (RoutingTable::Eigrp* route : routingInstance->routingTable.getAllEigrpRoutes(addressFamily, asNumber))
        {
            if (route->delay == 0xFFFFFFFF || route->routeType == RoutingTable::Eigrp::RouteType::SUMMARY)
                continue;
            IPAddress major;
            uint8_t mask = Functions::findClassfullNetworkAndMask(major.raw, route->network.raw);
            grouped[{major, mask}].push_back(route);
        }

        // Step 2: Loop through all known major networks
        std::shared_lock<std::shared_mutex> ifaceLock(interfaceMutex);

        for (const auto& [majorNet, routes] : grouped)
        {
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
                    if (iface->isRouteSummarized(majorNet.first.raw, majorNet.second))
                        iface->removeSummaryRoute(majorNet.first, majorNet.second);
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
                            sr.summary->network == majorNet.first &&
                            sr.summary->mask == majorNet.second)
                        {
                            found = true;

                            // Check if the metric needs updating
                            if (sr.summary->bandwidth != minBandwidth || sr.summary->delay != minDelay)
                            {
                                iface->removeSummaryRoute(majorNet.first, majorNet.second);
                                iface->addSummaryRoute(majorNet.first.raw, majorNet.second, true);
                            }

                            break;
                        }
                    }
                }

                // Not found? Add new summary
                if (!found)
                    iface->addSummaryRoute(majorNet.first.raw, majorNet.second, true);
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

                // Does this still match 2+ connected routes?
                int matchCount = 0;
                for (const auto* route : routingInstance->routingTable.getAllConnectedEigrpRoutes(addressFamily, asNumber))
                {
                    if (Functions::isSubnetOf(route->network.raw, route->mask, it->summary->network.raw, it->summary->mask, addressFamily))
                    {
                        matchCount++;
                        if (matchCount >= 2)
                            break;
                    }
                }

                if (matchCount < 2)
                {
                    iface->removeSummaryRoute(it->summary->network, it->summary->mask);
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

    void Eigrp::addPassiveInterface(uint32_t key, bool add)
    {
        {
            std::shared_lock<std::shared_mutex> lock(configs.configsMutex);
            if (add)
            {
                configs.passiveInterfaces.insert(key);
            }
            else
            {
                configs.passiveInterfaces.erase(key);
            }
        }

        // Make the interface passive if it already exists
        auto intIt = eigrpInterfaceList.find(key);
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
                    newRoute->network = {destination.addr, addressFamily};
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

    void Eigrp::enableUnicastNeighbor(const IPAddress& neighborIp, uint32_t key)
    {
        // Add unicast neighbor to the unicast neighbor list
        {
            std::shared_lock<std::shared_mutex> lock(configs.configsMutex);
            configs.unicastNeighbors[key].emplace(neighborIp);
        }

        // Find the interface to add the neighbor
        {
            std::shared_lock<std::shared_mutex> intLock(interfaceMutex);
            auto intIt = eigrpInterfaceList.find(key);
            if (intIt != eigrpInterfaceList.end())
            {
                intIt->second->addUnicastNeighbor(neighborIp);
            }
        }
    }

    void Eigrp::disableUnicastNeighbor(const IPAddress& neighborIp, uint32_t key)
    {
        // Remove unicast neighbor from the unicast neighbor list
        {
            std::shared_lock<std::shared_mutex> lock(configs.configsMutex);
            configs.unicastNeighbors[key].erase(neighborIp);
        }

        // Find the interface to remove the neighbor from
        {
            std::shared_lock<std::shared_mutex> intLock(interfaceMutex);
            auto intIt = eigrpInterfaceList.find(key);
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
        uint32_t highestIP = 0;
        uint32_t tempIp;
        if (!routerID.isStatic)
        {
            auto processID = [&](Interface* interface)
            {
                if (interface->shutdownFlag.load(std::memory_order_relaxed)) return;
                auto& interfaceInfo = interface->configs;
                tempIp = interfaceInfo.ipv4.getAddress();
                if (tempIp == 0) return;
                if (tempIp < highestIP) return;
                highestIP = tempIp;
            };
            
            {
                std::shared_lock<std::shared_mutex> lock(routingInstance->interfaceMutex);
                for (const auto& [id, interface] : routingInstance->interfaceList)
                {
                    if (interface->configs.interfaceType != InterfaceType::LOOPBACK) continue;
                    processID(interface);
                }
                if (highestIP == 0)
                {
                    for (const auto& [id, interface] : routingInstance->interfaceList)
                    {
                        processID(interface);
                    }
                }
            }
            writeU32(routerID.ID, highestIP);
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
            interfaceKey = currentInterfaceInfo->key;

            // Add pending summary routes if needed
            for (const auto& [network, mask] : configs->pendingSummaryRoutes)
            {
                addSummaryRoute(network.raw, mask);
            }
            configs->pendingSummaryRoutes.clear();

            // Gather locked values for local metric calculation
            uint32_t delay = currentInterfaceInfo->delay.load(std::memory_order_relaxed);
            uint32_t bandwidth = currentInterfaceInfo->bandwidth.load(std::memory_order_relaxed);
            uint8_t load = eigrpProcess.configs.variance.load(std::memory_order_relaxed);
            uint32_t key = currentInterfaceInfo->key;
            uint8_t reliability = 255;

            // Check if this interface is passive
            bool passive = false;
            {
                std::shared_lock<std::shared_mutex> lock(eigrpProcess.configs.configsMutex);
                if (eigrpProcess.configs.passiveInterfaces.find(key) != eigrpProcess.configs.passiveInterfaces.end())
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
            std::vector<IPAddress> unicastNeighbors;
            {
                std::shared_lock<std::shared_mutex> lock(eigrpProcess.configs.configsMutex);
                if (!eigrpProcess.configs.unicastNeighbors[key].empty())
                {
                    // Disable multicast if unicast neighbors are present
                    configs->multicastEnabled.store(false, std::memory_order_release);
                    
                    // Store neighbors to add
                    for (auto neighbor : eigrpProcess.configs.unicastNeighbors[key])
                    {
                        unicastNeighbors.emplace_back(neighbor);
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
        uint8_t network[16];
        {
            if (addressFamily == AddressFamily::IPv4)
            {
                mask = currentInterface->configs.ipv4.getMask();
                uint8_t ipAddress[4];
                Functions::computeNetworkAddress(network, currentInterface->configs.ipv4.getAddress(ipAddress), mask, AddressFamily::IPv4);
            }
            else if (addressFamily == AddressFamily::IPv6)
            {
                IPAddress ip;
                mask = currentInterface->configs.ipv6.getGlobalUnicastPair(ip.raw);
                if (ip.v6 != 0)
                {
                    Functions::computeNetworkAddress(network, ip.raw, mask, AddressFamily::IPv6);
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
        std::unordered_map<IPAddress, EigrpConfigs::NeighborInfo*> neighborsCopy;
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

    void EigrpInterface::processPacket(const EigrpHeader& eigrpPacket, const uint8_t* neighborIp, bool multicast)
    {
        EigrpConfigs::NeighborState neighborState;
        IPAddress neigIp(neighborIp, eigrpProcess.addressFamily);
        // Check if passive

        {
            if (configs->isPassive.load(std::memory_order_relaxed))
            {
                Logger::getInstance().info() << "Interface is passive. Incoming EIGRP packet ignored." << std::endl;
                return;
            }
        }

        // Validate packet version
        if (eigrpPacket.raw->version != 0x02)
        {
            // Version not valid
            return;
        }

        // Check for valid neighbor 
        EigrpConfigs::NeighborInfo* neighbor = nullptr;
        {
            // Neighbor mutex for save access
            std::shared_lock<std::shared_mutex> lock(neighborMutex);
            auto neighborIt = neighbors.find(neigIp);
            if (neighborIt != neighbors.end())
            {
                neighbor = neighborIt->second;
            }
        }

        if (neighbor && (eigrpPacket.getOpcode() != Variable::Eigrp::Type::hello || eigrpPacket.getAck() != 0))
        {
            neighborState = neighbor->neighborState.load(std::memory_order_relaxed);
        }

        // Change neighbor state if needed
        if (neighbor)
        {
            if (neighborState == EigrpConfigs::NeighborState::EXSTART)
            {
                changeNeighborState(neighbor, neigIp, EigrpConfigs::NeighborState::EXCHANGE);
            }
        }

        if (eigrpPacket.getOpcode() == Variable::Eigrp::Type::hello)
        {
            if (eigrpPacket.getAck() == 0)
            {
                processHello(neighbor, eigrpPacket, neigIp, !multicast);
            }
            else if (neighbor)
            {
                processAck(neighbor, eigrpPacket.getAck());
            }
        }
        else if (neighbor)
        {
            switch (eigrpPacket.getOpcode())
            {
                case Variable::Eigrp::Type::update:
                    processUpdate(neighbor, eigrpPacket);
                    break;
                case Variable::Eigrp::Type::reply:
                    processReply(neighbor, neigIp, eigrpPacket);
                    break;
                case Variable::Eigrp::Type::siaReply:
                    processSIAReply(neighbor, neigIp, eigrpPacket);
                    break;
                case Variable::Eigrp::Type::query:
                    processQuery(neighbor, eigrpPacket, neigIp);
                    break;
                case Variable::Eigrp::Type::siaQuery:
                    processSIAQuery(neighbor, eigrpPacket, neigIp);
                    break;
                default:
                    return;
            }
        }
    }

    void EigrpInterface::changeNeighborState(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, EigrpConfigs::NeighborState newState)
    {
        // Make sure neighbor exists
        if (!neighbor) return; // Neighbor is null

        // Check for currect state in order to change state
        auto currentState = neighbor->neighborState.load(std::memory_order_relaxed);
        uint32_t seq = nextSequenceNumber.load(std::memory_order_relaxed);
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

    void EigrpInterface::processHello(EigrpConfigs::NeighborInfo* neighbor, const EigrpHeader& receivedHello, const IPAddress& neighborIp, bool unicast)
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
                    return;
                }
            }
        }

        // Validate the Autonomous System Number (ASN)
        if (receivedHello.getAutonomousSystem() != eigrpProcess.asNumber)
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
            neighbor = new EigrpConfigs::NeighborInfo(eigrpProcess.addressFamily, eigrpProcess.routingInstance->global.timeManager, neighborIp);

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

        std::optional<TLV16Option> authOpt = std::nullopt;

        std::vector<TLV16Option> options;
        auto trail = receivedHello.getTrail();
        parseEigrpOptions(trail.data(), trail.size(), options);

        // Process TLVs
        for (const auto& opt : options)
        {
            if (opt.type == Variable::Eigrp::Option::parameter)
            {
                uint8_t parameters[6];
                eigrpProcess.calculateParameters(parameters, recievedHoldTime);
                if (std::memcmp(parameters, opt.value, 6) != 0) return; // Drop the packet if parameters mismatch

                // Extract Holdtime
                recievedHoldTime = readU16(opt.value + 6);

                // Check for Peer Termination
                if (std::memcmp(opt.value, /*Filled bytes->*/Variable::Mac::broadcast, 5))
                {
                    handleNeighborDown(neighbor, neighborIp);
                    return;
                }
            }
            else if (opt.type == Variable::Eigrp::Option::sequence)
            {
                if (std::memcmp(opt.value + 1, neighborIp.raw, opt.value[0]) != 0)
                    return; // Drop packet if the IPs doesn't match
            }
            else if (opt.type == Variable::Eigrp::Option::multicastSequence)
            {
                neighbor->initSequence = readU32(opt.value);
            }
            else if (opt.type == Variable::Eigrp::Option::authentication)
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
            //auto authTLV = generateAuthenticatedTLV(tempHeader); //TODO
            //if (authTLV.value != authOpt->value) return;
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
        IPAddress neighborIp;
        IPAddress interfaceIp;
        bool initComplete = neighbor->initComplete.load(std::memory_order_relaxed);
        bool processAcks = neighbor->processAcks.load(std::memory_order_relaxed);
        if (currentInterface->shutdownFlag.load(std::memory_order_relaxed))
        {
            interfaceIp = getInterfaceIp();
        }
        {
            std::shared_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
            neighborIp = neighbor->ipAddress;
        }
        
        if (neighborIp == interfaceIp)
        {
            return; // Neighbor IP invalid
        }

        // Extract sequence number
        uint32_t receivedSequenceNumber = receivedUpdate.getSequence();

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
        if (receivedUpdate.getFlagRestart()) {}
        if (receivedUpdate.getFlagInit()) { neighbor->initSequence.store(receivedSequenceNumber); initReceived = true; }
        if (receivedUpdate.getFlagCondRecv()) { conditionalReceive = true; }
        if (receivedUpdate.getFlagEndOfTable()) { endOfTable = true; }

        // Validate sequence number
        {
            uint32_t lastReceivedSequenceNumber = neighbor->lastReceivedSequenceNumber.load(std::memory_order_relaxed);

            if (receivedSequenceNumber <= lastReceivedSequenceNumber && !processAcks)
            {
                sendAckToNeighbor(neighbor, neighborIp, neighbor->lastReceivedSequenceNumber);
            }
            else if (receivedSequenceNumber > lastReceivedSequenceNumber + 1)
            {
                // Buffer out of order packet
                std::unique_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
                neighbor->packetBuffer[receivedSequenceNumber] = EigrpConfigs::NeighborInfo::PacketBuffer{.neighborIp = neighborIp, .eigrp = receivedUpdate}; //TODO copy packet to buffer
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
                if (eigrpProcess.getRouterID() > neighbor->routerID)
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
        if (receivedUpdate.getSequence() != 0 && processAcks)
        {
            processAck(neighbor, receivedUpdate.getAck());
        }

        // Collect routes for batch processing
        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            auto trail = receivedUpdate.getTrail();
            std::vector<TLV16Option> options;
            parseEigrpOptions(trail.data(), trail.size(), options);
            for (const auto &option : options)
            {
                if (option.type == Variable::Eigrp::Option::internalRoute ||
                    option.type == Variable::Eigrp::Option::internalRouteV6 ||
                    option.type == Variable::Eigrp::Option::externalRoute ||
                    option.type == Variable::Eigrp::Option::externalRouteV6)
                {
                    RoutingTable::Eigrp* route = decodeRoute(option.value, option.valueSize, (option.type == Variable::Eigrp::Option::externalRoute), false);
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

    void EigrpInterface::processAck(EigrpConfigs::NeighborInfo* neighbor, const uint32_t sequenceNumber)
    {
        // Ensure the neighbor is valid
        if (!neighbor && !neighbor->processAcks.load(std::memory_order_relaxed))
        {
            Logger::getInstance().warn() << "Invalid neighbor passed to processAck." << std::endl;
            return;
        }

        EigrpConfigs::NeighborInfo::ReliablePacketInfo reliablePacketCopy;
        {
            // Locate the acknowledgement packet in reliablePacket
            std::lock_guard<std::mutex> lock(neighbor->reliableMutex);
            auto pktIt = neighbor->reliablePackets.find(sequenceNumber);

            if (pktIt == neighbor->reliablePackets.end())
            {
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
                return entry.first == sequenceNumber;
            });
        }
        

        // Handle routes associated with the acknowledged packet
        for (const auto& route : reliablePacketCopy.packet.updatedRoutes)
        {
            IPPrefix key(route.route->network.raw, route.route->mask, eigrpProcess.addressFamily);

            {
                std::unique_lock<std::shared_mutex> routeLock(neighbor->neighborDataMutex);
                auto advertIt = neighbor->advertisedRoutes.find(key);

                if (route.withdraw)
                {
                    // Fully remove routes marked for removal
                    if (advertIt != neighbor->advertisedRoutes.end() && advertIt->second.removePending)
                    {
                        neighbor->advertisedRoutes.erase(advertIt);
                    }
                }
                else
                {
                    // Update or add routes
                    if (advertIt == neighbor->advertisedRoutes.end())
                    {
                        neighbor->advertisedRoutes[key] = {route.route, true, false, false};
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
        updateRTTEstimate(neighbor, sequenceNumber);
    }

    void EigrpInterface::processQuery(EigrpConfigs::NeighborInfo* neighbor, const EigrpHeader& receivedQuery, const IPAddress& neighborIp)
    {
        if (!neighbor) return; // Neighbor does not exist

        uint32_t receivedSequenceNumber = receivedQuery.getSequence();

        // Ensure correct last received sequence tracking
        if (receivedSequenceNumber < neighbor->lastReceivedSequenceNumber.load(std::memory_order_relaxed))
            return; // Ignoring duplicate sequence number;

        std::vector<RoutingTable::Eigrp*> queriedRoutes;

        auto trail = receivedQuery.getTrail();
        std::vector<TLV16Option> options;
        parseEigrpOptions(trail.data(), trail.size(), options);

        for (const auto& option : options)
        {
            if (option.type == Variable::Eigrp::Option::internalRoute ||
                option.type == Variable::Eigrp::Option::internalRouteV6)
                queriedRoutes.push_back(decodeRoute(option.value, option.valueSize, false, false));
            else if (option.type == Variable::Eigrp::Option::externalRoute ||
                option.type == Variable::Eigrp::Option::externalRouteV6)
                queriedRoutes.push_back(decodeRoute(option.value, option.valueSize, true, false));
        }
        
        sendAckToNeighbor(neighbor, neighborIp, receivedQuery.getSequence());

        std::vector<RoutingTable::Eigrp*> knownQueryRoutes;
        std::vector<RoutingTable::Eigrp*> knownRoutes;
        std::vector<RoutingTable::Eigrp*> unknownQueryRoutes;

        for (const auto& queriedRoute : queriedRoutes)
        {
            // Check if the queried route exists
            auto existingRoute = eigrpProcess.routingInstance->routingTable.getEigrpRoute(queriedRoute->network.raw, queriedRoute->mask, eigrpProcess.addressFamily, eigrpProcess.asNumber);

            // Check summary routes
            if (!existingRoute)
            {
                std::shared_lock<std::shared_mutex> lock(configs->configsMutex);
                for (const auto& summaryRoute : configs->summaryRoutes) {
                    if (Functions::isSubnetOf(queriedRoute->network.raw, queriedRoute->mask, summaryRoute.summary->network.raw, summaryRoute.summary->mask, eigrpProcess.addressFamily))
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

        if (!knownQueryRoutes.empty() && !knownRoutes.empty())
        {
            // Route is know, send a reply immediately
            sendReplyToNeighbor(neighbor, neighborIp, knownQueryRoutes, knownRoutes, receivedSequenceNumber);
            for (auto route : knownQueryRoutes) { delete route; }
            return;
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
        for (auto query : unknownQueryRoutes)
        {
            IPPrefix queryKey(query->network.raw, query->mask, eigrpProcess.addressFamily);

            if (!eigrpProcess.configs.activeDisabled)
            {
                startActiveTimer(query);
            }
        }
        
        // Delete remaining queried routes
        for (auto route : queriedRoutes) { 
            delete route; 
        }
        queriedRoutes.clear();
    }

    void EigrpInterface::processSIAQuery(EigrpConfigs::NeighborInfo* neighbor, const EigrpHeader& receivedQuery, const IPAddress& neighborIp)
    {
        if (!neighbor) return; // Neighbor does not exist

        uint32_t receivedSequenceNumber = receivedQuery.getSequence();

        if (receivedSequenceNumber < neighbor->lastReceivedSequenceNumber.load(std::memory_order_relaxed))
            return;

        sendAckToNeighbor(neighbor, neighborIp, receivedSequenceNumber);

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

    void EigrpInterface::processReply(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, const EigrpHeader& receivedReply)
    {
        if (!neighbor) return; // Neighbor invalid

        uint32_t receivedSequenceNumber = receivedReply.getSequence();

        if (receivedSequenceNumber < neighbor->lastReceivedSequenceNumber.load(std::memory_order_relaxed))
            return;

        neighbor->lastReceivedSequenceNumber.store(receivedSequenceNumber, std::memory_order_release);

        // Send ack for the received reply
        sendAckToNeighbor(neighbor, neighborIp, receivedSequenceNumber);

        std::vector<RoutingTable::Eigrp*> receivedRoutes;

        auto trail = receivedReply.getTrail();
        std::vector<TLV16Option> options;
        parseEigrpOptions(trail.data(), trail.size(), options);
        for (const auto& option : options)
        {
            if (option.type == Variable::Eigrp::Option::internalRoute ||
                option.type == Variable::Eigrp::Option::internalRouteV6)
            {
                receivedRoutes.push_back(decodeRoute(option.value, option.valueSize, false, false));
            }
            else if (option.type == Variable::Eigrp::Option::externalRoute ||
                option.type == Variable::Eigrp::Option::externalRouteV6)
            {
                receivedRoutes.push_back(decodeRoute(option.value, option.valueSize, true, true));
            }
        }

        if (receivedRoutes.empty()) return;

        for (RoutingTable::Eigrp* route : receivedRoutes)
        {
            IPPrefix key(route->network.raw, route->mask, eigrpProcess.addressFamily);
            auto it = eigrpProcess.outstandingReplies.find(key);
            if (it == eigrpProcess.outstandingReplies.end()) {
                delete route; // no query outstanding for this prefix
                continue;
            }

            auto& queryInfo = it->second;
            auto pendingIt = queryInfo.pendingQueries.find(neighborIp);
            if (pendingIt == queryInfo.pendingQueries.end() ||
                pendingIt->second.sequenceNumber != receivedReply.getAck()) {
                delete route;
                continue;
            }

            // cancel SIA timer
            eigrpProcess.routingInstance->global.timeManager.cancelTimer(pendingIt->second.siaTimerId);
            queryInfo.pendingQueries.erase(pendingIt);

            RoutingTable::Eigrp* current = eigrpProcess.routingInstance->routingTable.getEigrpRoute(
                route->network.raw, route->mask, eigrpProcess.addressFamily, eigrpProcess.asNumber);

            if (route->delay != std::numeric_limits<uint32_t>::max() &&
                isFeasibleSuccessor(route, current)) {
                queryInfo.feasibleRoutes.push_back({neighbor, neighborIp, route, false});
            } else {
                queryInfo.feasibleRoutes.push_back({neighbor, neighborIp, queryInfo.route, /*poisen*/true});
                delete route; // only keep queryInfo.route poisoned
            }

            // finish processing this outstanding reply if all queries resolved
            if (queryInfo.pendingQueries.empty()) {
                RoutingTable::Eigrp* bestRoute = nullptr;
                bool remove = false;

                if (!queryInfo.feasibleRoutes.empty()) {
                    std::sort(queryInfo.feasibleRoutes.begin(), queryInfo.feasibleRoutes.end(),
                        [](const auto& a, const auto& b) {
                            return std::get<2>(a)->feasibleDistance < std::get<2>(b)->feasibleDistance;
                        });
                    bestRoute = std::get<2>(queryInfo.feasibleRoutes.front());
                } else {
                    remove = true;
                }

                // Send reply back to originator of query (if any)
                if (queryInfo.originNeighbor.v6 != 0) {
                    std::shared_lock lock(eigrpProcess.interfaceMutex);
                    for (const auto& [_, iface] : eigrpProcess.eigrpInterfaceList) {
                        if (iface->neighbors.count(queryInfo.originNeighbor)) {
                            iface->sendReplyToNeighbor(
                                iface->neighbors[queryInfo.originNeighbor], queryInfo.originNeighbor,
                                { bestRoute ? bestRoute : queryInfo.route },
                                { bestRoute ? bestRoute : queryInfo.route },
                                queryInfo.sequenceNumber
                            );
                        }
                    }
                }

                if (remove) {
                    RoutingTable::Eigrp* current = eigrpProcess.routingInstance->routingTable.getEigrpRoute(
                        queryInfo.route->network.raw, queryInfo.route->mask,
                        eigrpProcess.addressFamily, eigrpProcess.asNumber);
                    if (current != queryInfo.route)
                        delete queryInfo.route;
                    eigrpProcess.routingInstance->routingTable.removeEigrp(
                        queryInfo.route->network.raw, queryInfo.route->mask,
                        eigrpProcess.addressFamily, eigrpProcess.asNumber);
                }

                for (const auto& r : queryInfo.feasibleRoutes)
                    updateRoutingTable(std::get<0>(r), std::get<1>(r), {{std::get<2>(r), std::get<3>(r)}});

                cancelActiveTimer(queryInfo.route->network, queryInfo.route->mask);

                for (const auto& pq : queryInfo.pendingQueries)
                    eigrpProcess.routingInstance->global.timeManager.cancelTimer(pq.second.siaTimerId);

                eigrpProcess.outstandingReplies.erase(it);
            }
        }

    }
    
    void EigrpInterface::processSIAReply(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, const EigrpHeader& receivedReply)
    {
        if (!neighbor) return; // Neighbor invalid

        uint32_t receivedSequenceNumber = receivedReply.getSequence();

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

                if (outgoing.sequenceNumber != receivedReply.getAck())
                    continue; // Not matching sequence number

                eigrpProcess.routingInstance->global.timeManager.cancelTimer(outgoing.siaTimerId);
                startSIATimer(queryInfo.route, neighborIp, outgoing);

                outgoing.lastSIARefreshTime = now;
            }
        }
    }

    size_t EigrpInterface::calculateMaxRoutesPerPacket(size_t baseSize, AddressFamily af, bool isExternal)
    {
        uint16_t maxPacketSize = eigrpProcess.addressFamily == AddressFamily::IPv4 
            ? currentInterfaceInfo->ipv4.mtu.load(std::memory_order_relaxed)
            : currentInterfaceInfo->ipv6.mtu.load(std::memory_order_relaxed);
        size_t headerSize = baseSize; // Estimate size of EIGRP header and base overhead
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

    void EigrpInterface::sendAckToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, uint32_t sequenceNumber)
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
                PacketBuilder eigrpAckPacketStructure(currentInterface);

                eigrpProcess.eigrpHello(eigrpAckPacketStructure, *this, neighborIp.raw, seq, /*ack=*/true);

                // Send the assembled ACK packet if it contains data
                if (currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed) && !destroy.load(std::memory_order_relaxed))
                {
                    auto sourceIP = getInterfaceIp();
                    IPPacket::BuildIP build = {
                        .iface = currentInterface,
                        .packetInfo = eigrpAckPacketStructure,
                        .destIp = neighborIp.raw,
                        .sourceIp = sourceIP.raw,
                        .DSCP = configs->DSCP.load(std::memory_order_relaxed),
                        .protocolType = Variable::IP::eigrp,
                    };

                    eigrpProcess.addressFamily == AddressFamily::IPv4
                        ? IPPacket::buildIpv4(build)
                        : IPPacket::buildIpv6(build);
                }
            }
            neighbor->pendingAcks.clear();
        }
    }

    void EigrpInterface::sendUpdateToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const std::vector<EigrpConfigs::RoutingUpdate> &routes, EigrpConfigs::UpdateType updateType, bool restart, bool conditional, std::vector<IPAddress> conditionalNeighbors)
    {
        // Determine target IP based on communication mode
        IPAddress interfaceIp = getInterfaceIp();
        uint8_t const* targetIp;
        {
            if (neighbor)
            {
                targetIp = neighbor->ipAddress.raw;
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
                [&](const auto& summary) { return Functions::isSubnetOf(route->network.raw, route->mask, summary.summary->network.raw, summary.summary->mask, eigrpProcess.addressFamily); });
            
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
                if (it != neighbors.end() && (route->nextHop == it->first || Functions::compareNetworkWithIp(route->network.raw, it->second->ipAddress.raw, route->mask, eigrpProcess.addressFamily)))
                {
                    continue;
                }
                if (Functions::compareNetworkWithIp(route->network.raw, interfaceIp.raw, route->mask, eigrpProcess.addressFamily))
                {
                    continue;
                }
            }
            // Check if route requires an update
            IPPrefix key(route->network.raw, route->mask, eigrpProcess.addressFamily);
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
        auto it = filteredRoutes.begin();

        // Set if this is a withdraw update
        bool isConditional = false;
        bool endOfTable = false;
        bool isInit = false;

        bool authentication = configs->authKey.fullyEnabled.load(std::memory_order_relaxed);
        bool stub = eigrpProcess.isStub();

        PacketBuilder eigrpPacket(currentInterface);

        do
        {
            // Create the EIGRP Update packet structure
            eigrpPacket.clear(); // Clear previous packet if there was one

            // Reserve header space.
            eigrpProcess.addressFamily == AddressFamily::IPv4
                ? IPPacket::reserveIpv4(currentInterface, eigrpPacket)
                : IPPacket::reserveIpv6(currentInterface, eigrpPacket);
            eigrpPacket.reserveHeader(HeaderType::EIGRP, 0);

            // Validate header structure
            BuildEntry* nextHeader = eigrpPacket.nextBuildHeader();
            if (unlikely(!nextHeader)) return;

            // Set the eigrp buffer for building
            EigrpHeader eigrp;
            eigrp.setBuffer(nextHeader->buffer);
            uint8_t* trail = eigrp.getTrailData();
            
            // Calculate remaining space available for routes
            uint16_t mtuSize = eigrpProcess.addressFamily == AddressFamily::IPv4
                ? currentInterface->configs.ipv4.mtu.load(std::memory_order_relaxed)
                : currentInterface->configs.ipv6.mtu.load(std::memory_order_relaxed);
            uint16_t maxRouteSize;
            maxRouteSize = mtuSize - (eigrpPacket.bufferOffset + EigrpHeader::fixedSize + (authentication ? 128 : 0) + (stub ? 6 : 0));

            // Get the next sequence number
            uint32_t sequenceNumber = getNextSequenceNumber();

            // Options
            TLV16BufferManager options(trail, eigrpPacket.getMaxHeaderSize(mtuSize));

            // Add routes to the packet
            for (; it != filteredRoutes.end() && options.size() + 64 < maxRouteSize; ++it)
            {
                if (it->route->routeType == RoutingTable::Eigrp::RouteType::EXTERNAL)
                {
                    auto buffer = options.getNextValBuf();
                    if (!buffer) continue;
                    size_t len = encodeExternalRouteOption(buffer, it->route, bandwidthMetric, delay, it->withdraw);
                    options.append(
                        (eigrpProcess.addressFamily == AddressFamily::IPv4) ? Variable::Eigrp::Option::externalRoute : Variable::Eigrp::Option::externalRouteV6,
                        len + 4,
                        nullptr,
                        len
                    );
                }
                else
                {
                    auto buffer = options.getNextValBuf();
                    if (!buffer) continue;
                    size_t len = encodeRouteOption(buffer, it->route, bandwidthMetric, delay, it->withdraw);
                    options.append(
                        (eigrpProcess.addressFamily == AddressFamily::IPv4) ? Variable::Eigrp::Option::internalRoute : Variable::Eigrp::Option::internalRouteV6,
                        len + 4,
                        nullptr,
                        len
                    );
                }
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
                        eigrp.setAck(*neighbor->pendingAcks.begin());
                        neighbor->pendingAcks.erase(neighbor->pendingAcks.begin());
                    }
                }

                // Add stub option
                if (eigrpProcess.isStub())
                {
                    encodeStubOption(options.getNextValBuf(), eigrpProcess.configs.stubConfig);
                    options.append(
                        Variable::Eigrp::Option::stub,
                        6, nullptr, 2
                    );
                }
                // Add authentication TLV if enabled
                if (configs->authKey.fullyEnabled.load(std::memory_order_relaxed))
                {
                    auto opt = options.getNextValBuf(0);
                    uint8_t authSize = generateAuthenticatedTLV(opt);
                    options.append(Variable::Eigrp::Option::authentication, authSize, nullptr, authSize);
                }
            }

            eigrpPacket.addTLVSize(options.size());

            // Assemble and send the packet
            if (currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed) && !destroy.load(std::memory_order_relaxed))
            {
                IPPacket::BuildIP build = {
                    .iface = currentInterface,
                    .packetInfo = eigrpPacket,
                    .destIp = targetIp,
                    .DSCP = configs->DSCP.load(std::memory_order_relaxed),
                    .protocolType = Variable::IP::eigrp
                };

                eigrpProcess.addressFamily == AddressFamily::IPv4
                    ? IPPacket::buildIpv4(build)
                    : IPPacket::buildIpv6(build);
            }
            
            if (!neighbor || (neighbor && neighbor->processAcks))
            {
                if (conditional)
                {
                    eigrp.setFlagCondRecv(true);
                    std::shared_lock<std::shared_mutex> lock(neighborMutex);
                    for (const auto& [address, neighborPtr] : neighbors)
                    {
                        std::vector<EigrpConfigs::RoutingUpdate> neighborSpecificRoutes;
                        for (const auto& route : filteredRoutes)
                        {
                            IPPrefix key(route.route->network.raw, route.route->mask, eigrpProcess.addressFamily);

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
                            setupReliablePacket(
                                neighborPtr,
                                address,
                                EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(
                                    eigrpProcess.addressFamily,
                                    nextHeader->buffer,
                                    nextHeader->length,
                                    address,
                                    neighborSpecificRoutes
                                ), 
                                sequenceNumber
                            );
                        }
                    }
                }
                else
                {
                    for (const auto& [address, neighborPtr] : neighbors)
                    {
                        setupReliablePacket(
                            neighborPtr,
                            address,
                            EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(
                                eigrpProcess.addressFamily,
                                nextHeader->buffer,
                                nextHeader->length,
                                address,
                                filteredRoutes
                            ),
                            sequenceNumber
                        );
                    }
                }
            }
        }
        while (!endOfTable && updateType == EigrpConfigs::UpdateType::FULL);
    }

    void EigrpInterface::sendQueryToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, std::vector<RoutingTable::Eigrp*> failedRoutes)
    {
        uint32_t bandwidthMetric = (10000000 / currentInterfaceInfo->bandwidth.load(std::memory_order_relaxed));
        size_t it = 0;

        bool authentication = configs->authKey.fullyEnabled.load(std::memory_order_relaxed);

        PacketBuilder eigrpQueryPacketStructure(currentInterface);

        // Calculate remaining space available for routes
        uint16_t mtuSize = eigrpProcess.addressFamily == AddressFamily::IPv4
            ? currentInterface->configs.ipv4.mtu.load(std::memory_order_relaxed)
            : currentInterface->configs.ipv6.mtu.load(std::memory_order_relaxed);
        uint16_t maxRouteSize = mtuSize - (eigrpQueryPacketStructure.bufferOffset + EigrpHeader::fixedSize + (authentication ? 128 : 0));

        do
        {
            // Clear Eigrp Packet
            eigrpQueryPacketStructure.clear();

            // Reserve header space.
            eigrpProcess.addressFamily == AddressFamily::IPv4
                ? IPPacket::reserveIpv4(currentInterface, eigrpQueryPacketStructure)
                : IPPacket::reserveIpv6(currentInterface, eigrpQueryPacketStructure);
            eigrpQueryPacketStructure.reserveHeader(HeaderType::EIGRP, EigrpHeader::fixedSize);

            // Validate header structure
            BuildEntry* nextHeader = eigrpQueryPacketStructure.nextBuildHeader();

            // Set the eigrp buffer for building
            EigrpHeader eigrp;
            eigrp.setBuffer(nextHeader->buffer);
            uint8_t* trail = eigrp.getTrailData();

            // Increment sequence number for this route/query
            uint32_t sequenceNumber = getNextSequenceNumber();

            // Options
            TLV16BufferManager options(trail, maxRouteSize);
            std::vector<RoutingTable::Eigrp*> sentRoutes;

            // Construct the Query option
            for (; it < failedRoutes.size() && options.size() + 64 < maxRouteSize; it++)
            { 
                if (failedRoutes[it]->routeType == RoutingTable::Eigrp::RouteType::EXTERNAL)
                {
                    auto buffer = options.getNextValBuf();
                    if (!buffer) continue;
                    size_t len = encodeExternalRouteOption(
                        buffer,
                        failedRoutes[it],
                        bandwidthMetric,
                        0xFFFFFFFF,
                        true
                    );
                    options.append(
                        (eigrpProcess.addressFamily == AddressFamily::IPv4) ? Variable::Eigrp::Option::externalRoute : Variable::Eigrp::Option::externalRouteV6,
                        len + 4,
                        nullptr,
                        len
                    );
                }
                else
                {
                    auto buffer = options.getNextValBuf();
                    if (!buffer) continue;
                    size_t len = encodeRouteOption(
                        buffer,
                        failedRoutes[it],
                        bandwidthMetric,
                        0xFFFFFFFF,
                        true
                    );
                    options.append(
                        (eigrpProcess.addressFamily == AddressFamily::IPv4) ? Variable::Eigrp::Option::internalRoute : Variable::Eigrp::Option::internalRouteV6,
                        len + 4,
                        nullptr,
                        len
                    );
                }
                sentRoutes.push_back(failedRoutes[it]);
            }
            
            if (sentRoutes.empty()) return; // Loop prevention

            // Set other EIGRP header feilds
            eigrp.raw->version = 2;
            eigrp.setOpcode(Variable::Eigrp::Type::query);
            std::memset(eigrp.raw->checksum, 0, 2);
            eigrp.setFlagInit(false);
            eigrp.setFlagCondRecv(false);
            eigrp.setFlagRestart(false);
            eigrp.setFlagEndOfTable(false);
            eigrp.setSequence(sequenceNumber);
            eigrp.setAck(0);
            eigrp.setVirtualRouterId(eigrpProcess.getVirtualRouterID());
            eigrp.setAutonomousSystem(eigrpProcess.asNumber);

            // Generate and append Authentication TLV if enabled for this neighbor
            if (configs->authKey.fullyEnabled.load(std::memory_order_relaxed))
            {
                auto opt = options.getNextValBuf(0);
                uint8_t authSize = generateAuthenticatedTLV(opt);
                options.append(Variable::Eigrp::Option::authentication, authSize, nullptr, authSize);
            }

            eigrpQueryPacketStructure.addTLVSize(options.size());

            // Convert to raw packet ByteString
            if (currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed) && !destroy.load(std::memory_order_relaxed))
            {
                IPPacket::BuildIP build = {
                    .iface = currentInterface,
                    .packetInfo = eigrpQueryPacketStructure,
                    .destIp = neighborIp.raw,
                    .DSCP = configs->DSCP.load(std::memory_order_relaxed),
                    .protocolType = Variable::IP::eigrp
                };

                eigrpProcess.addressFamily == AddressFamily::IPv4
                    ? IPPacket::buildIpv4(build)
                    : IPPacket::buildIpv6(build);
            }

            // Store the packet for possible retransmission (relieable delivery)
            std::vector<EigrpConfigs::RoutingUpdate> formattedRoutes;
            for (const auto& route : sentRoutes)
            {
                formattedRoutes.emplace_back(route, true);
            }
            setupReliablePacket(
                neighbor,
                neighborIp,
                EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(
                    eigrpProcess.addressFamily,
                    nextHeader->buffer,
                    nextHeader->length,
                    neighborIp,
                    formattedRoutes
                ),
                sequenceNumber
            );

            for (RoutingTable::Eigrp* route : sentRoutes)
            {
                IPPrefix key(route->network.raw, route->mask, eigrpProcess.addressFamily);

                if (eigrpProcess.outstandingReplies.find(key) == eigrpProcess.outstandingReplies.end())
                {
                    EigrpConfigs::ActiveRoute& active = eigrpProcess.outstandingReplies[key];
                    active.route = route;
                    active.originNeighbor = neighborIp;
                }

                EigrpConfigs::ActiveRoute& active = eigrpProcess.outstandingReplies[key];

                EigrpConfigs::OutgoingQuery outgoing;
                outgoing.sequenceNumber = sequenceNumber;
                outgoing.lastSIARefreshTime = std::chrono::steady_clock::now();
                startSIATimer(route, neighborIp, outgoing);

                active.pendingQueries[neighborIp] = std::move(outgoing);
            }
        }
        while (it < failedRoutes.size());
    }

    void EigrpInterface::sendSIAQueryToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp)
    {
        //TODO FINISH THIS
        sendQueryToNeighbor(neighbor, neighborIp, {});
    }

    void EigrpInterface::sendQueryToNeighbors(std::vector<RoutingTable::Eigrp*> failedRoutes)
    {
        if (failedRoutes.empty()) return;

        for (const auto& [ip, neighbor] : neighbors)
        {
            sendQueryToNeighbor(neighbor, ip, failedRoutes);
        }
    }

    void EigrpInterface::sendReplyToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, std::vector<RoutingTable::Eigrp*> queryRoutes, std::vector<RoutingTable::Eigrp*> existingRoutes, uint32_t ackNumber)
    {
        uint32_t bandwidthMetric = (10000000 / currentInterfaceInfo->bandwidth.load(std::memory_order_relaxed));
        uint32_t delay = currentInterfaceInfo->delay.load(std::memory_order_relaxed);
        size_t it = 0;

        bool authentication = configs->authKey.fullyEnabled.load(std::memory_order_relaxed);

        PacketBuilder eigrpQueryPacketStructure(currentInterface);
            
        // Calculate remaining space available for routes
        uint16_t mtuSize = eigrpProcess.addressFamily == AddressFamily::IPv4
            ? currentInterface->configs.ipv4.mtu.load(std::memory_order_relaxed)
            : currentInterface->configs.ipv6.mtu.load(std::memory_order_relaxed);
        uint16_t maxRouteSize = mtuSize - (eigrpQueryPacketStructure.bufferOffset + EigrpHeader::fixedSize + (authentication ? 128 : 0));

        do
        {
            // Clear Eigrp Packet
            eigrpQueryPacketStructure.clear();

            // Reserve header space.
            eigrpProcess.addressFamily == AddressFamily::IPv4
                ? IPPacket::reserveIpv4(currentInterface, eigrpQueryPacketStructure)
                : IPPacket::reserveIpv6(currentInterface, eigrpQueryPacketStructure);
            eigrpQueryPacketStructure.reserveHeader(HeaderType::EIGRP, EigrpHeader::fixedSize);

            // Validate header structure
            BuildEntry* nextHeader = eigrpQueryPacketStructure.nextBuildHeader();

            // Set the eigrp buffer for building
            EigrpHeader eigrp;
            eigrp.setBuffer(nextHeader->buffer);
            uint8_t* trail = eigrp.getTrailData();

            // Increment sequence number for this route/query
            uint32_t sequenceNumber = getNextSequenceNumber();

            // Options
            TLV16BufferManager options(trail, maxRouteSize);
            std::vector<RoutingTable::Eigrp*> sentRoutes;

            // Construct the Query option
            for (; it < queryRoutes.size() && options.size() + 64 < maxRouteSize; it++)
            {
                RoutingTable::Eigrp combinedRoute = *existingRoutes[it];
                combinedRoute.network = queryRoutes[it]->network;
                combinedRoute.mask = queryRoutes[it]->mask;
                if (configs->nextHopSelf.load(std::memory_order_relaxed))
                    combinedRoute.nextHop = getInterfaceIp();

                if (combinedRoute.routeType == RoutingTable::Eigrp::RouteType::EXTERNAL)
                {
                    auto buffer = options.getNextValBuf();
                    if (!buffer) continue;
                    size_t len = encodeExternalRouteOption(
                        buffer,
                        &combinedRoute,
                        bandwidthMetric,
                        delay
                    );
                    options.append(
                        (eigrpProcess.addressFamily == AddressFamily::IPv4) ? Variable::Eigrp::Option::externalRoute : Variable::Eigrp::Option::externalRouteV6,
                        len + 4,
                        nullptr,
                        len
                    );
                }
                else
                {
                    auto buffer = options.getNextValBuf();
                    if (!buffer) continue;
                    size_t len = encodeRouteOption(
                        buffer,
                        &combinedRoute,
                        bandwidthMetric,
                        delay
                    );
                    options.append(
                        (eigrpProcess.addressFamily == AddressFamily::IPv4) ? Variable::Eigrp::Option::internalRoute : Variable::Eigrp::Option::internalRouteV6,
                        len + 4,
                        nullptr,
                        len
                    );
                }
            }
            
            // Set other EIGRP header feilds
            eigrp.raw->version = 2;
            eigrp.setOpcode(Variable::Eigrp::Type::reply);
            std::memset(eigrp.raw->checksum, 0, 2); // Will be calculated later
            eigrp.setFlagInit(false);
            eigrp.setFlagCondRecv(false);
            eigrp.setFlagRestart(false);
            eigrp.setFlagEndOfTable(false);
            eigrp.setSequence(sequenceNumber);
            eigrp.setAck(ackNumber);
            eigrp.setVirtualRouterId(eigrpProcess.getVirtualRouterID());
            eigrp.setAutonomousSystem(eigrpProcess.asNumber);

            // Generate and append Authentication TLV if enabled for this neighbor
            if (configs->authKey.fullyEnabled.load(std::memory_order_relaxed))
            {
                auto opt = options.getNextValBuf(0);
                uint8_t authSize = generateAuthenticatedTLV(opt);
                options.append(Variable::Eigrp::Option::authentication, authSize, nullptr, authSize);
            }

            eigrpQueryPacketStructure.addTLVSize(options.size());

            // Convert to raw packet ByteString
            if (currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed) && !destroy.load(std::memory_order_relaxed))
            {
                IPPacket::BuildIP build = {
                    .iface = currentInterface,
                    .packetInfo = eigrpQueryPacketStructure,
                    .destIp = neighborIp.raw,
                    .DSCP = configs->DSCP.load(std::memory_order_relaxed),
                    .protocolType = Variable::IP::eigrp
                };

                eigrpProcess.addressFamily == AddressFamily::IPv4
                    ? IPPacket::buildIpv4(build)
                    : IPPacket::buildIpv6(build);
            }

            // Store the packet for possible retransmission (relieable delivery)
            eigrp.setAck(0);
            setupReliablePacket(
                neighbor,
                neighborIp,
                EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(
                    eigrpProcess.addressFamily,
                    nextHeader->buffer,
                    nextHeader->length,
                    neighborIp
                ),
                sequenceNumber
            );
        }
        while (it < queryRoutes.size());
    }

    void EigrpInterface::sendSIAReplyToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, uint32_t querySequence)
    {
        // Validate neighbor
        if (!neighbor) return;
        
        PacketBuilder eigrpPacket(currentInterface);

        // Reserve header space.
        eigrpProcess.addressFamily == AddressFamily::IPv4
            ? IPPacket::reserveIpv4(currentInterface, eigrpPacket)
            : IPPacket::reserveIpv6(currentInterface, eigrpPacket);
        eigrpPacket.reserveHeader(HeaderType::EIGRP, 0);

        // Validate header structure
        BuildEntry* nextHeader = eigrpPacket.nextBuildHeader();

        // Set the eigrp buffer for building
        EigrpHeader eigrp;
        eigrp.setBuffer(nextHeader->buffer);

        uint32_t currentSeqNum = getNextSequenceNumber();

        eigrp.raw->version = 2;
        eigrp.setOpcode(Variable::Eigrp::Type::siaReply);
        std::memset(eigrp.raw->checksum, 0, 2);
        eigrp.setFlagInit(false);
        eigrp.setFlagCondRecv(false);
        eigrp.setFlagRestart(false);
        eigrp.setFlagEndOfTable(false);
        eigrp.setSequence(currentSeqNum);
        eigrp.setAck(querySequence);
        eigrp.setVirtualRouterId(eigrpProcess.getVirtualRouterID());
        eigrp.setAutonomousSystem(eigrpProcess.asNumber);

        size_t mtu = eigrpProcess.addressFamily == AddressFamily::IPv4
            ? currentInterfaceInfo->ipv4.mtu.load(std::memory_order_relaxed)
            : currentInterfaceInfo->ipv6.mtu.load(std::memory_order_relaxed);

        TLV16BufferManager options(eigrp.getTrail().data(), eigrpPacket.getMaxHeaderSize(mtu));
        // Generate and append Authentication TLV if enabled for this neighbor
        if (configs->authKey.fullyEnabled.load(std::memory_order_relaxed))
        {
            auto opt = options.getNextValBuf(0);
            uint8_t authSize = generateAuthenticatedTLV(opt);
            options.append(Variable::Eigrp::Option::authentication, authSize, nullptr, authSize);
        }

        eigrpPacket.addTLVSize(options.size());

        if (currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed) && !destroy.load(std::memory_order_relaxed))
        {
            IPPacket::BuildIP build = {
                .iface = currentInterface,
                .packetInfo = eigrpPacket,
                .destIp = neighborIp.raw,
                .DSCP = configs->DSCP.load(std::memory_order_relaxed),
                .protocolType = Variable::IP::eigrp
            };

            eigrpProcess.addressFamily == AddressFamily::IPv4
                ? IPPacket::buildIpv4(build)
                : IPPacket::buildIpv6(build);
        }

        // Store the packet for possible retransmission (relieable delivery)
        eigrp.setAck(0);
        setupReliablePacket(
            neighbor,
            neighborIp,
            EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(
                eigrpProcess.addressFamily,
                nextHeader->buffer,
                nextHeader->length,
                neighborIp
            ),
            currentSeqNum
        );
    }

    uint8_t EigrpInterface::encodeRouteOption(uint8_t* encoded, RoutingTable::Eigrp* route, uint32_t currentBandwidthMetric, uint32_t currentDelay, bool removed)
    {
        size_t offset = 0;

        // Include next hop as usual
        uint8_t ipSize = static_cast<uint8_t>(eigrpProcess.addressFamily);
        std::memcpy(encoded, route->nextHop.raw, ipSize);
        offset += ipSize;

        // Update delay: add local interface delay
        uint32_t newDelay = route->delay != std::numeric_limits<uint32_t>::max()
          ? route->delay + ((currentDelay / 10) *256)
          : route->delay;

        // Update Bandwidth: Take the lower bandwidth metric
        uint32_t newBandwidthMetric = std::min(route->bandwidth, currentBandwidthMetric * 256);

        writeU32(encoded + offset, removed ? 0xFFFFFFFF : newDelay);
        offset += 4;
        writeU32(encoded + offset, newBandwidthMetric);
        offset += 4;
        writeU24(encoded + offset, route->mtu);
        offset += 3;
        encoded[offset] = route->hopCount;
        offset += 1;
        encoded[offset] = route->reliability;
        offset += 1;
        encoded[offset] = route->load;
        offset += 1;
        encoded[offset] = route->routeTag;
        offset += 1;
        encoded[offset] = 0x00; //TODO flags
        offset += 1;
        encoded[offset] = route->mask;
        offset += 1;
        offset += Functions::compactNetworkAddress(encoded + offset, route->network.raw, route->mask, eigrpProcess.addressFamily);
        return offset;
    }

    uint8_t EigrpInterface::encodeExternalRouteOption(uint8_t* encoded, RoutingTable::Eigrp* route, uint32_t currentBandwidthMetric, uint32_t currentDelay, bool removed)
    {
        uint8_t offset = 0;

        uint8_t ipSize = static_cast<uint8_t>(eigrpProcess.addressFamily);
        std::memcpy(encoded, route->nextHop.raw, ipSize);
        offset += ipSize;
        std::memcpy(encoded + offset, route->originRouter.raw, ipSize);
        offset += ipSize;
        writeU32(encoded + offset, route->originAS);
        offset += 4;
        writeU32(encoded + offset, route->routeTag);
        offset += 4;

        // Update delay: add local interface delay
        uint32_t newDelay = route->delay != std::numeric_limits<uint32_t>::max()
          ? route->delay + ((currentDelay / 10) *256)
          : route->delay;

        // Update Bandwidth: Take the lower bandwidth metric
        uint32_t newBandwidthMetric = std::min(route->bandwidth, currentBandwidthMetric * 256);

        writeU32(encoded + offset, removed ? 0xFFFFFFFF : newDelay);
        offset += 4;
        writeU32(encoded + offset, newBandwidthMetric);
        offset += 4;
        writeU24(encoded + offset, route->mtu);
        offset += 3;
        encoded[offset] = route->hopCount;
        offset += 1;
        encoded[offset] = route->reliability;
        offset += 1;
        encoded[offset] = route->load;
        offset += 1;
        encoded[offset] = 0x00; //TODO flags
        offset += 1;
        encoded[offset] = route->mask;
        offset += 1;
        offset += Functions::compactNetworkAddress(encoded + offset, route->network.raw, route->mask, eigrpProcess.addressFamily);
        return offset;
    }

    void EigrpInterface::addSummaryRoute(const uint8_t* network, uint8_t mask, bool isAuto)
    {
        if (Functions::compareNetworkWithMask(network, mask, eigrpProcess.addressFamily))
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
            if (isRouteSummarized(route->network.raw, route->mask))
            {
                if (route->bandwidth < minBandwidth)
                    minBandwidth = route->bandwidth;

                if (route->delay < minDelay)
                    minDelay = route->delay;
            }
        }

        // Create summary route
        RoutingTable::Eigrp* route = new RoutingTable::Eigrp(interfaceKey);
        std::memcpy(route->network.raw, network, static_cast<uint8_t>(eigrpProcess.addressFamily));
        route->network.isV6 = eigrpProcess.addressFamily == AddressFamily::IPv6;
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
        if (configs->nextHopSelf.load(std::memory_order_relaxed))
            route->nextHop = getInterfaceIp();

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

    void EigrpInterface::removeSummaryRoute(const IPAddress& network, uint8_t mask)
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
                    route->network.raw,
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

    void EigrpInterface::restoreSummaryRoutes(const IPAddress& summaryNetwork, uint8_t summaryMask)
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


    EigrpConfigs::SummaryRoute* EigrpInterface::isRouteSummarized(const uint8_t* network, uint8_t mask)
    {
        std::shared_lock<std::shared_mutex> configsLock(configs->configsMutex);

        for (auto& sr : configs->summaryRoutes)
        {
            if (Functions::isSubnetOf(network, mask, sr.summary->network.raw, sr.summary->mask, eigrpProcess.addressFamily))
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
            IPPrefix key(route->network.raw, route->mask, eigrpProcess.addressFamily);

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
        RoutingTable::Eigrp withdrawl(interfaceKey);
        withdrawl.network = route->network;
        withdrawl.mask = route->mask;
        withdrawl.routeType = RoutingTable::Eigrp::RouteType::SUMMARY;
        withdrawl.metric = std::numeric_limits<uint32_t>::max();

        // Send withdraw update to the neighbor
        for (const auto& neighbor : unicastNeighbors)
        {
            sendUpdateToNeighbor(neighbor, {{&withdrawl, true}}, EigrpConfigs::UpdateType::PARTIAL);
        }
        if (hasMulticast && configs->multicastEnabled.load(std::memory_order_relaxed))
        {
            sendUpdateToNeighbor(nullptr, {{&withdrawl, true}}, EigrpConfigs::UpdateType::PARTIAL);
        }
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
                    it->summary->network.raw,
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

        uint8_t const* targetIp;
        
        {
            if (neighbor && unicast)
            {
                std::shared_lock<std::shared_mutex> lock(neighbor->neighborDataMutex);
                targetIp = neighbor->ipAddress.raw;
            }
            else if (configs->multicastEnabled.load(std::memory_order_relaxed))
            {
                targetIp = getMulticast();
            }
        }

        PacketBuilder eigrpHello(currentInterface);

        eigrpProcess.eigrpHello(eigrpHello, *this, targetIp, sequenceNumber, false, update);

        auto interface = currentInterface;
        if (!interface || interface->shutdownFlag.load(std::memory_order_acquire) || destroy.load(std::memory_order_acquire))
        {
            return;
        }

        IPPacket::BuildIP build = {
            .iface = currentInterface,
            .packetInfo = eigrpHello,
            .destIp = targetIp,
            .DSCP = configs->DSCP.load(std::memory_order_relaxed),
            .protocolType = Variable::IP::eigrp
        };

        eigrpProcess.addressFamily == AddressFamily::IPv4
            ? IPPacket::buildIpv4(build)
            : IPPacket::buildIpv6(build);
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

    void EigrpInterface::startHoldTimer(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, uint16_t holdTime)
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

    void EigrpInterface::handleHoldTimeExpire(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp)
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
        IPPrefix key(route->network.raw, route->mask, eigrpProcess.addressFamily);
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

    uint32_t EigrpInterface::startSIATimer(RoutingTable::Eigrp* route, const IPAddress& neighborIp, EigrpConfigs::OutgoingQuery& outgoing)
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

    void EigrpInterface::handleSIATimeout(RoutingTable::Eigrp* route, const IPAddress& neighborIp)
    {
        IPPrefix queryKey(route->network.raw, route->mask, eigrpProcess.addressFamily);

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
        IPPrefix queryKey(route->network.raw, route->mask, eigrpProcess.addressFamily);

        auto it = eigrpProcess.outstandingReplies.find(queryKey);
        if (it == eigrpProcess.outstandingReplies.end()) return;

        EigrpConfigs::ActiveRoute& queryInfo = it->second;

        std::vector<IPAddress> failedNeighbors;
        for (const auto& [neighborIp, outgoing] : queryInfo.pendingQueries)
            failedNeighbors.push_back(neighborIp);

        for (const auto& neighborIp : failedNeighbors)
        {
            std::shared_lock<std::shared_mutex> lock(eigrpProcess.interfaceMutex);
            for (const auto& [_, interface] : eigrpProcess.eigrpInterfaceList)
            {
                if (interface->neighbors.count(neighborIp))
                {
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

    void EigrpInterface::cancelActiveTimer(const IPAddress& destination, uint8_t mask)
    {
        IPPrefix key(destination.raw, mask, eigrpProcess.addressFamily);

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

    uint32_t EigrpInterface::startRetransmissionTimer(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, const uint32_t sequenceNumber, double timeout)
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

    void EigrpInterface::handleRetransmissionTimeout(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, const uint32_t sequenceNumber)
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
        PacketBuilder retransmissionPacket(currentInterface);
        {
            // Construct and send the retransmission packet
            eigrpProcess.addressFamily == AddressFamily::IPv4
                ? IPPacket::reserveIpv4(currentInterface, retransmissionPacket)
                : IPPacket::reserveIpv6(currentInterface, retransmissionPacket);
            retransmissionPacket.reserveHeader(HeaderType::EIGRP, 0);
            BuildEntry* nextHeader = retransmissionPacket.nextBuildHeader();
            if (!nextHeader) return;
            std::memcpy(nextHeader->buffer, pktInfoCopy.packet.headerBuffer, pktInfoCopy.packet.headerSize);
            retransmissionPacket.addTLVSize(pktInfoCopy.packet.headerSize);

            if (currentInterface && !currentInterface->shutdownFlag.load(std::memory_order_relaxed) && !destroy.load(std::memory_order_relaxed))
            {
                IPPacket::BuildIP build = {
                    .iface = currentInterface,
                    .packetInfo = retransmissionPacket,
                    .destIp = pktInfoCopy.packet.destination.raw,
                    .DSCP = configs->DSCP.load(std::memory_order_relaxed),
                    .protocolType = Variable::IP::eigrp
                };

                eigrpProcess.addressFamily == AddressFamily::IPv4
                    ? IPPacket::buildIpv4(build)
                    : IPPacket::buildIpv6(build);
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

    RoutingTable::Eigrp* EigrpInterface::decodeRoute(const uint8_t* value, size_t valueSize, bool external, bool summary)
    {
        bool ipv6 = eigrpProcess.addressFamily == AddressFamily::IPv6;
        RoutingTable::Eigrp* route = new RoutingTable::Eigrp(interfaceKey);
        size_t start = 0;
        
        try
        {
            // Parse next hop based on address family
            if (!ipv6)
            {
                if (valueSize < start + 4) throw std::runtime_error("Insufficient data for IPv4 next Hop");
                std::memcpy(route->nextHop.raw, value + start, 4);
                start += 4;
            }
            else if (eigrpProcess.addressFamily == AddressFamily::IPv6)
            {
                if (valueSize < start + 16) throw std::runtime_error("Insufficient date for IPv6 next Hop");
                std::memcpy(route->nextHop.raw, value + start, 16);
                route->nextHop.isV6 = true;
                start += 16;
            }

            if (!external) {
                // Internal Route Parsing
                if (valueSize < start + 4) throw std::runtime_error("Insufficient data for Delay.");
                route->delay = readU32(value + start);
                start += 4;

                if (valueSize < start + 4) throw std::runtime_error("Insufficient data for Bandwidth.");
                route->bandwidth = readU32(value + start);
                start += 4;

                if (valueSize < start + 3) throw std::runtime_error("Insufficient data for MTU.");
                route->mtu = readU24(value + start);
                start += 3;
    
                if (valueSize < start + 1) throw std::runtime_error("Insufficient data for Hop Count.");
                route->hopCount = value[start];
                start += 1;

                if (valueSize < start + 1) throw std::runtime_error("Insufficient data for Reliability.");
                route->reliability = value[start];
                start += 1;

                if (valueSize < start + 1) throw std::runtime_error("Insufficient data for Load.");
                route->load = value[start];
                start += 1;

                if (valueSize < start + 2) throw std::runtime_error("Insufficient data for Route Tag.");
                route->routeTag = value[start];
                start += 2;
                
                //TODO flags

                if (valueSize < start + 1) throw std::runtime_error("Insufficient data for Mask.");
                route->mask = value[start];
                start += 1;

                size_t byteLength = (route->mask + 7) / 8;
                if (valueSize < start + byteLength) throw std::runtime_error("Insufficient data for Network Address.");
                std::memcpy(route->network.raw, value + (valueSize - byteLength), byteLength);
                route->network.isV6 = ipv6;
                start += byteLength;
                route->routeType = summary ? RoutingTable::Eigrp::RouteType::SUMMARY : RoutingTable::Eigrp::RouteType::INTERNAL;
            }
            else 
            {
                // External Route Parsing
                if (eigrpProcess.addressFamily == AddressFamily::IPv4) 
                {
                    if (valueSize < start + 4) throw std::runtime_error("Insufficient data for Origin Router (IPv4).");
                    std::memcpy(route->originRouter.raw, value, 4);
                    start += 4;
                }
                else if (eigrpProcess.addressFamily == AddressFamily::IPv6) 
                {
                    if (valueSize < start + 16) throw std::runtime_error("Insufficient data for Origin Router (IPv6).");
                    std::memcpy(route->originRouter.raw, value, 16);
                    route->originRouter.isV6 = true;
                    start += 16;
                }

                if (valueSize < start + 4) throw std::runtime_error("Insufficient data for Origin AS.");
                route->originAS = readU32(value + start);
                start += 4;

                if (valueSize < start + 4) throw std::runtime_error("Insufficient data for Route Tag (External).");
                route->routeTag = readU32(value + start);
                start += 4;

                if (valueSize < start + 4) throw std::runtime_error("Insufficient data for Extended Metric.");
                route->extendedMetric = readU32(value + start);
                start += 4;

                if (valueSize < start + 1) throw std::runtime_error("Insufficient data for Extended ID.");
                route->extendedId = readU32(value + start);
                start += 1;

                if (valueSize < start + 1) throw std::runtime_error("Insufficient data for Flags.");
                route->flags = value[start];
                start += 1;

                // Parsing additional fields if necessary...
                // Ensure all fields are parsed based on EIGRP specifications

                if (valueSize < start + 4) throw std::runtime_error("Insufficient data for Delay (External).");
                route->delay = readU32(value + start);
                start += 4;

                if (valueSize < start + 4) throw std::runtime_error("Insufficient data for Bandwidth (External).");
                route->bandwidth = readU32(value + start);
                start += 4;

                if (valueSize < start + 3) throw std::runtime_error("Insufficient data for MTU (External).");
                route->mtu = readU24(value + start);
                start += 3;

                if (valueSize < start + 1) throw std::runtime_error("Insufficient data for Hop Count (External).");
                route->hopCount = value[start];
                start += 1;

                if (valueSize < start + 1) throw std::runtime_error("Insufficient data for Reliability (External).");
                route->reliability = value[start];
                start += 1;

                if (valueSize < start + 1) throw std::runtime_error("Insufficient data for Load (External).");
                route->load = value[start];
                start += 1;

                if (valueSize < start + 1) throw std::runtime_error("Insufficient data for Mask (External).");
                route->mask = value[start];
                start += 1;

                size_t byteLength = (route->mask + 7) / 8;
                if (valueSize < start + byteLength) throw std::runtime_error("Insufficient data for Network Address.");
                std::memcpy(route->network.raw, value + (valueSize - byteLength), byteLength);
                route->network.isV6 = ipv6;
                start += byteLength;
                route->routeType = summary ? RoutingTable::Eigrp::RouteType::SUMMARY : RoutingTable::Eigrp::RouteType::INTERNAL;

                route->routeType = RoutingTable::Eigrp::RouteType::EXTERNAL;
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

    void EigrpInterface::updateRoutingTable(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, const std::vector<EigrpConfigs::RoutingUpdate>& routes)
    {
        // Validate neighbor
        if (!neighbor) return; // invalid neighbor

        std::vector<EigrpConfigs::RoutingUpdate> updatedRoutes;
        std::vector<RoutingTable::Eigrp*> withdrawnRoutes;

        for (const auto& route : routes)
        {
            if (route.withdraw)
            {
                eigrpProcess.topologyTable->removeRoute({route.route->network.raw, route.route->mask, eigrpProcess.addressFamily}, neighborIp);
                auto* existingRoute = eigrpProcess.routingInstance->routingTable.getEigrpRoute(route.route->network.raw, route.route->mask, eigrpProcess.addressFamily, eigrpProcess.asNumber);
                if (existingRoute && existingRoute->nextHop == neighborIp)
                {
                    withdrawnRoutes.push_back(existingRoute);
                }
            }
            else
            {
                if (route.route->reportedDistance > route.route->feasibleDistance) continue; // Violation

                if (auto* summary = isRouteSummarized(route.route->network.raw, route.route->mask))
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
                auto bestRouteEntry = eigrpProcess.topologyTable->getEntryForRoute({route.route->network.raw, route.route->mask, eigrpProcess.addressFamily});
                if (!bestRouteEntry)
                {
                    continue;
                }

                auto successorIt = std::find_if(
                    bestRouteEntry->routesByNeighbor.begin(),
                    bestRouteEntry->routesByNeighbor.end(),
                    [](const auto& pair) {return pair.second.isSuccessor;});

                if (successorIt == bestRouteEntry->routesByNeighbor.end())
                {
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
                auto existingRoute = eigrpProcess.routingInstance->routingTable.getEigrpRoute(route.route->network.raw, route.route->mask, eigrpProcess.addressFamily, eigrpProcess.asNumber);
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
            if (auto* summary = isRouteSummarized(removedRoute.route->network.raw, removedRoute.route->mask))
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

            eigrpProcess.routingInstance->routingTable.removeEigrp(removedRoute.route->network.raw, removedRoute.route->mask, eigrpProcess.addressFamily, eigrpProcess.asNumber);
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

    void EigrpInterface::handleNeighborDown(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp)
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
        std::vector<IPPrefix> affectedRoutes;
        for (auto& [destination, entry] : eigrpProcess.topologyTable->getTopologyEntries())
        {
            // If this neighbor was advertising the route
            if (entry->routesByNeighbor.find(neighborIp) != entry->routesByNeighbor.end())
            {
                affectedRoutes.push_back(destination);
            }
        }

        // Remove the neighbor rotues from topology
        eigrpProcess.topologyTable->handleNeighborDown(neighborIp);

        // Trigger Active for routes with no feasible successor
        std::vector<RoutingTable::Eigrp*> activeRoutes;
        for (const auto& prefix : affectedRoutes)
        {
            // Find the best remaining successor
            auto bestRoute = eigrpProcess.topologyTable->findBestRoute(prefix);
            if (!bestRoute.has_value())
            {
                // If no valid successor is remaining
                RoutingTable::Eigrp* route = eigrpProcess.routingInstance->routingTable.getEigrpRoute(prefix.addr, prefix.prefixLength, eigrpProcess.addressFamily, eigrpProcess.asNumber);
                if (route)
                {
                    route->stuckInActive = true;
                    activeRoutes.push_back(route);
                }
            }
            else
            {
                // Reinstall best route if available
                updateRoutingTableForDestination(prefix);
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
    
    void EigrpInterface::gracefulRestart(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp)
    {
        auto expireTime = std::chrono::steady_clock::now() + std::chrono::seconds(eigrpProcess.configs.purgeTime.load(std::memory_order_relaxed));
        uint32_t gracefulTimerId = eigrpProcess.routingInstance->global.timeManager.addTimer(expireTime, [&]() {
            handleNeighborRestart(neighbor, neighborIp);
        });
        neighbor->gracefulRestartTimerId.store(gracefulTimerId, std::memory_order_release);
    }

    void EigrpInterface::handleNeighborRestart(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp)
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

    void EigrpInterface::updateRoutingTableForDestination(const IPPrefix& prefix)
    {
        auto entry = eigrpProcess.topologyTable->getEntryForRoute(prefix);
        if (!entry)
        {
            eigrpProcess.routingInstance->routingTable.removeEigrp(prefix.addr, prefix.prefixLength, eigrpProcess.addressFamily, eigrpProcess.asNumber);
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
            std::memcpy(newRoute->network.raw, prefix.addr, static_cast<uint8_t>(eigrpProcess.addressFamily));
            newRoute->network.isV6 = eigrpProcess.addressFamily == AddressFamily::IPv6;
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
            eigrpProcess.routingInstance->routingTable.removeEigrp(prefix.addr, prefix.prefixLength, eigrpProcess.addressFamily, eigrpProcess.asNumber);
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

    void EigrpInterface::setupReliablePacket(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, const EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet& packet, uint32_t sequenceNum)
    {
        {
            std::lock_guard<std::mutex> lock(neighbor->reliableMutex);
            if (neighbor->reliablePackets.find(sequenceNum) != neighbor->reliablePackets.end())
            {
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
    }

    RoutingTable::Eigrp* EigrpInterface::encodeSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute)
    {
        uint32_t bandwidth = eigrpProcess.configs.lowestBandwidth.load(std::memory_order_relaxed);
        uint32_t delay = currentInterfaceInfo->delay.load(std::memory_order_relaxed);

        RoutingTable::Eigrp* route = new RoutingTable::Eigrp(interfaceKey);
        if (configs->nextHopSelf.load(std::memory_order_relaxed))
            route->nextHop = getInterfaceIp();
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

    uint8_t* EigrpInterface::encodeStubOption(uint8_t* out, const EigrpConfigs::StubConfig& stub)
    {
        uint16_t flags = 0;
        if (stub.advertiseConnected) flags |= 0x0001;
        if (stub.advertiseStatic) flags |= 0x0002;
        if (stub.advertiseSummary) flags |= 0x0004;
        if (stub.advertiseRedistributed) flags |= 0x0008;
        if (stub.advertiseLeakMap) flags |= 0x0010;
        if (stub.receiveOnly) flags |= 0x0020;
        writeU16(out, flags);
        return out;
    }

    void EigrpInterface::configureAuthentication(uint8_t* keyId, const std::string* key, EigrpConfigs::AuthType* type, bool enable)
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

    /*ByteString EigrpInterface::serializeEigrpHeader(const EigrpHeader& eigrp, bool exclusiveAuthTLV)
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
    }*/

    uint8_t EigrpInterface::generateAuthenticatedTLV(uint8_t* out)
    {
        uint8_t keyId;
        EigrpConfigs::AuthType authType;
        std::string key;

        {
            if (!configs->authKey.fullyEnabled.load(std::memory_order_relaxed))
                return 0;

            std::shared_lock lock(configs->configsMutex);
            keyId = configs->authKey.keyId;
            authType = configs->authKey.authType;
            key = configs->authKey.key;
        }

        uint16_t hmacLength = 0;
        switch (authType)
        {
            case EigrpConfigs::AuthType::MD5: hmacLength = MD5_DIGEST_LENGTH; break;
            case EigrpConfigs::AuthType::SHA1: hmacLength = SHA_DIGEST_LENGTH; break;
            case EigrpConfigs::AuthType::SHA256: hmacLength = SHA256_DIGEST_LENGTH; break;
            case EigrpConfigs::AuthType::SHA384: hmacLength = SHA384_DIGEST_LENGTH; break;
            case EigrpConfigs::AuthType::SHA512: hmacLength = SHA512_DIGEST_LENGTH; break;
            case EigrpConfigs::AuthType::NONE: break;
        }

        out[0] = static_cast<uint8_t>(authType);
        out[1] = keyId;
        writeU16(out + 2, hmacLength);
        writeU32(out + 6, configs->authKey.replay.load(std::memory_order_relaxed));
        configs->authKey.replay.fetch_add(1, std::memory_order_seq_cst);
        std::memset(out + 8, 0, 8);

        out[16 + hmacLength] = static_cast<uint8_t>(key.size());
        std::memcpy(out + 17 + hmacLength, key.data(), key.size());

        return 16 + hmacLength;
    }

    void EigrpInterface::appendAuthHMAC(uint8_t* packetStart)
    {
        const uint8_t* ipHeader = packetStart;
        uint8_t ipHeaderLen = (ipHeader[0] & 0x0F) * 4;
        uint8_t* eigrpStart = packetStart + ipHeaderLen;

        uint8_t* cursor = eigrpStart + 20;
        uint8_t* authTLV = nullptr;
        uint16_t hmacLength = 0;

        // Locate the last TLV
        while (true)
        {
            uint16_t type = readU16(cursor);
            uint16_t length = readU16(cursor + 2);

            if (type == Variable::Eigrp::Option::authentication)
            {
                authTLV = cursor;
                hmacLength = readU16(eigrpStart + 6);
                break;
            }

            if (length == 0 || length > 2048)
                break;

            cursor += length;
        }

        if (!authTLV) return;

        // Zero out HMAC field temporarily
        uint8_t* hmacField = authTLV + 16; // starts after fixed TLV body
        std::memset(hmacField, 0x00, hmacLength);

        // Find the hmac key
        uint8_t keySize = hmacField[hmacLength];
        uint8_t* key = hmacField + hmacLength + 1;

        // Step 4: Compute HMAC over entire IP + EIGRP packet
        size_t totalLen = (hmacField + hmacLength) - packetStart;

        switch (hmacLength)
        {
            case MD5_DIGEST_LENGTH:
                Authentication::generateMD5(hmacField, packetStart, totalLen, key, keySize);
                break;
            case SHA_DIGEST_LENGTH:
                Authentication::generateHMAC(hmacField, packetStart, totalLen, key, keySize, Authentication::SHA::SHA1);
                break;
            case SHA256_DIGEST_LENGTH:
                Authentication::generateHMAC(hmacField, packetStart, totalLen, key, keySize, Authentication::SHA::SHA256);
                break;
            case SHA384_DIGEST_LENGTH:
                Authentication::generateHMAC(hmacField, packetStart, totalLen, key, keySize, Authentication::SHA::SHA384);
                break;
            case SHA512_DIGEST_LENGTH:
                Authentication::generateHMAC(hmacField, packetStart, totalLen, key, keySize, Authentication::SHA::SHA512);
                break;
        }
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

    void EigrpInterface::addNeighbor(const IPAddress& ipAddress, const uint8_t* macAddress, bool unicast)
    {
        // Add neighbor only if it doesn't already exist
        std::unique_lock<std::shared_mutex> intLock(neighborMutex);

        auto it = neighbors.find(ipAddress);
        if (it == neighbors.end()) // Double check
        {
            auto* neighbor = new EigrpConfigs::NeighborInfo(
                eigrpProcess.addressFamily,
                eigrpProcess.routingInstance->global.timeManager,
                ipAddress,
                unicast
            );
            std::memcpy(neighbor->macAddress, macAddress, 6);
            neighbors[ipAddress] = neighbor;
            {
                std::lock_guard<std::mutex> globalLock(eigrpProcess.neighborMutex);
                eigrpProcess.allNeighbors[ipAddress] = neighbor;
            }
        }
    }

    void EigrpInterface::addUnicastNeighbor(const IPAddress& neighborIp)
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
                neighbors[neighborIp] = new EigrpConfigs::NeighborInfo(
                    eigrpProcess.addressFamily,
                    eigrpProcess.routingInstance->global.timeManager,
                    neighborIp,
                    true
                );
            }
            else
            {
                // Add the neighbor normally if not present
                neighbors[neighborIp] = new EigrpConfigs::NeighborInfo(eigrpProcess.addressFamily, eigrpProcess.routingInstance->global.timeManager, neighborIp, true);
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

    void EigrpInterface::removeUnicastNeighbor(const IPAddress& neighborIp)
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

    EigrpConfigs::NeighborInfo* EigrpInterface::getNeighborInfo(const IPAddress& neighborIp)
    {
        auto it = neighbors.find(neighborIp);
        if (it != neighbors.end())
        {
            return it->second;
        }
        return nullptr;
    }

    const uint8_t* EigrpInterface::getMulticast()
    {
        return (eigrpProcess.addressFamily == AddressFamily::IPv4) ? Variable::Multicast::Eigrp::address : Variable::Multicast::Eigrp::addressv6;
    }

    
    bool EigrpInterface::isRouteAdvertised(const uint8_t* network, uint8_t mask)
    {
        std::shared_lock<std::shared_mutex> lock(neighborMutex);
        for (const auto& [address, neighbor] : neighbors)
        {
            IPPrefix key(network, mask, eigrpProcess.addressFamily);
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

    inline IPAddress EigrpInterface::getInterfaceIp()
    {
        IPAddress ip;
        ip.isV6 = eigrpProcess.addressFamily == AddressFamily::IPv6;
        ip.isV6 ? currentInterfaceInfo->ipv4.getAddress(ip.raw) : currentInterfaceInfo->ipv6.getLocalAddress(ip.raw);
        return ip;
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

    NamedEigrp::NamedEigrp(uint32_t& as, AddressFamily af, const std::string& name, VirtualRouter* vrf, bool multicast)
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

    std::vector<RoutingTable::Eigrp*> TopologyTable::getSuccessorsForRoute(const IPAddress& network, uint8_t mask)
    {
        std::lock_guard<std::mutex> lock(tableMutex);

        std::vector<RoutingTable::Eigrp*> successors;

        IPPrefix key(network.raw, mask, eigrpProcess->addressFamily);
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

    void TopologyTable::addOrUpdateRoute(const IPAddress& neighborIp, const IPAddress& destination, uint8_t prefixLength, const RouteInfo &routeInfo)
    {
        std::lock_guard<std::mutex> lock(tableMutex);

        // Create or update the topology table entry
        IPPrefix key(destination.raw, prefixLength, eigrpProcess->addressFamily);

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
                const IPAddress& bestNeighbor = entry->successors.front();
                entry->successors = {bestNeighbor}; // Keep only the best
            }
        }
    }

    void TopologyTable::removeRoutesFromNeighbor(const IPAddress& neighborIp)
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

    TopologyTable::TopologyEntry* TopologyTable::getEntryForRoute(const IPPrefix& prefix)
    {
        std::lock_guard<std::mutex> lock(tableMutex);

        auto it = topologyEntries.find(prefix);
        if (it == topologyEntries.end())
        {
            return nullptr;
        }

        return it->second;
    }

    std::optional<TopologyTable::RouteInfo> TopologyTable::findBestRoute(const IPPrefix& prefix)
    {
        std::lock_guard<std::mutex> lock(tableMutex);

        auto it = topologyEntries.find(prefix);
        if (it == topologyEntries.end())
        {
            return std::nullopt;
        }

        const auto& entry = it->second;

        if (entry->successors.empty())
        {
            return std::nullopt;
        }

        // Return the route information for the best successor
        const IPAddress& bestNeighbor = entry->successors.front();
        return entry->routesByNeighbor.at(bestNeighbor);
    }

    void TopologyTable::handleRouteFailure(const IPPrefix& prefix, const IPAddress& failedNeighborIp)
    {
        std::lock_guard<std::mutex> lock(tableMutex);

        auto it = topologyEntries.find(prefix);
        if (it == topologyEntries.end())
        {
            return;
        }

        auto& entry = it->second;

        // Remove the failed neighbor's route
        entry->routesByNeighbor.erase(failedNeighborIp);

        // If no remaining neighbors, remove route completely
        if (entry->routesByNeighbor.empty())
        {
            topologyEntries.erase(it);
            return;
        }

        // Recalculate successors and feasible successors
        updateSuccessorAndFeasibleSuccessors(entry);
    }

    void TopologyTable::markRouteAsPassive(const IPPrefix& prefix, EigrpInterface *eigrp)
    {
        std::lock_guard<std::mutex> lock(tableMutex);
        auto it = topologyEntries.find(prefix);
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

    bool TopologyTable::removeRoute(const IPPrefix& prefix, const IPAddress& neighbor)
    {
        std::lock_guard<std::mutex> lock(tableMutex);
        auto entryIt = topologyEntries.find(prefix);
        if (entryIt == topologyEntries.end())
        {
            return false; // Entry does not exist
        }
        entryIt->second->routesByNeighbor.erase(neighbor);
        if (entryIt->second->routesByNeighbor.empty())
        {
            delete entryIt->second;
            topologyEntries.erase(prefix);
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
                it = topologyEntries.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    
    void TopologyTable::handleNeighborDown(const IPAddress& neighborIp)
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
