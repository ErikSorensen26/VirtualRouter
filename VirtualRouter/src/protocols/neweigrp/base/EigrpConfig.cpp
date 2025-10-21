// EigrpConfigManager

#include <EigrpConfig.h>
#include <AddressFamily.hpp>

namespace Protocol
{
void EigrpConfig::addNetworkRange(const EigrpConfigs::Network& newNetwork)
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

size_t EigrpConfig::addCommonTlvs(EigrpHeader& hdr, EigrpInterface& cfg, bool isUpdate, bool isAck, const uint8_t* neighborIp, uint32_t sequenceNumber)
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

void EigrpConfig::redistributeRoute(const uint8_t* destination, uint8_t mask, const uint16_t protocol)
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

void EigrpConfig::enableAutoSummary(bool enable)
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

void EigrpConfig::recomputeAutoSummaries()
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

void EigrpConfig::enableStub(bool isStub, bool advertiseConnected, bool advertiseLeakMap, bool advertiseStatic, bool advertiseSummary, bool advertiseRedistributed)
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

void EigrpConfig::addPassiveInterface(uint32_t key, bool add)
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
}
