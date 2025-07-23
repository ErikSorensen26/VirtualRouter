#include <RoutingTable.h>
#include <AddressFamily.hpp>
#include <IPAddress.hpp>

bool RoutingTable::addEigrp(Eigrp *route, AddressFamily af, uint32_t as)
{
    IpPrefixKey key(route->network.data, route->mask, af);
    std::lock_guard<std::mutex> lock(tableMutex);

    auto& eigrpTable = (af == AddressFamily::IPv4) ? eigrp[as] : eigrpIPv6[as];
    auto it = eigrpTable.find(key);

    // Handle route removal
    if (route->metric == std::numeric_limits<std::uint32_t>::max())
    {
        if (it != eigrpTable.end())
        {
            delete eigrpTable[key];
            eigrpTable[key] = nullptr;
            eigrpTable.erase(key);
            return true;
        }
        else
        {
            return false;
        }
    }

    // Add route to the routing table
    if (it == eigrpTable.end())
    {
        // New route addition
        eigrpTable[key] = route;
        return true;
    }
    else if (eigrpTable[key]->bandwidth != route->bandwidth || eigrpTable[key]->delay != route->delay)
    {
        delete eigrpTable[key];
        eigrpTable[key] = route;
        return true;
    }
    return false;
}

bool RoutingTable::updateEigrp(Eigrp *route, AddressFamily af, uint32_t as)
{
    // Create key in "network/mask" format
    IpPrefixKey key(route->network.data, route->mask, af);
    std::lock_guard<std::mutex> lock(tableMutex);

    auto& eigrpTable = (af == AddressFamily::IPv4) ? eigrp[as] : eigrpIPv6[as];
    auto it = eigrpTable.find(key);

    // Handle route removal
    if (route->metric == std::numeric_limits<std::uint32_t>::max())
    {
        if (it != eigrpTable.end())
        {
            delete eigrpTable[key];
            eigrpTable[key] = nullptr;
            eigrpTable.erase(key);
            return true;
        }
        else
        {
            return false;
        }
    }

    // Add route to the routing table
    if (it == eigrpTable.end())
    {
        // New route addition
        eigrpTable[key] = route;
        return true;
    }
    else
    {
        // Update existing route
        auto& existingRoute = it->second;
        bool metricChanged = existingRoute->metric != route->metric;
        bool fdChanged = existingRoute->feasibleDistance != route->feasibleDistance;
        bool active = existingRoute->stuckInActive;

        if (metricChanged || fdChanged || active)
        {
            // Update all relevant fields
            if (route->feasibleDistance < existingRoute->feasibleDistance || active ||
                (route->feasibleDistance == existingRoute->feasibleDistance && route->adminDistance < existingRoute->adminDistance))
            {
                delete existingRoute;
                existingRoute = route;
            }

            // Add new nextHop if not already present
            if (existingRoute->nextHopsSet.insert(route->nextHop).second)
            {
                existingRoute->nextHopsVector.push_back(route->nextHop);
            }
            return true;
        }
    }
    return false;
}

void RoutingTable::removeEigrp(const uint8_t* network, uint8_t mask, AddressFamily af, uint32_t as)
{
    std::lock_guard<std::mutex> lock(tableMutex);
    IpPrefixKey key(network, mask, af);
    auto& eigrpTable = (af == AddressFamily::IPv4) ? eigrp[as] : eigrpIPv6[as];
    auto it = eigrpTable.find(key);
    if (it != eigrpTable.end())
    {
        if (it->second)
        {
            delete it->second;
            it->second = nullptr;
        }
        eigrpTable.erase(it);
    }
}

void RoutingTable::removeAllEigrp(AddressFamily af, uint32_t as)
{
    std::lock_guard<std::mutex> lock(tableMutex);
    auto& eigrpTable = (af == AddressFamily::IPv4 ? eigrp[as] : eigrpIPv6[as]);
    for (auto& [_, eigrp] : eigrpTable)
    {
        delete eigrp;
    }
    af == AddressFamily::IPv4 ? eigrp.erase(as) : eigrpIPv6.erase(as);
}

void RoutingTable::removeEigrpWithOutInterface(AddressFamily af, uint32_t as, uint32_t out)
{
    std::lock_guard<std::mutex> lock(tableMutex);
    auto& eigrpTable = (af == AddressFamily::IPv4 ? eigrp[as] : eigrpIPv6[as]);
    for (auto it = eigrpTable.begin(); it != eigrpTable.end();)
    {
        if (it->second->interface == out)
        {
            delete it->second;
            it = eigrpTable.erase(it);
        }
        else
        {
            ++it;
        }
    }
    af == AddressFamily::IPv4 ? eigrp.erase(as) : eigrpIPv6.erase(as);
}

void RoutingTable::updateEigrpWithVariance(Eigrp* route, uint8_t variance, AddressFamily af, uint32_t as)
{
    std::lock_guard<std::mutex> lock(tableMutex);

    IpPrefixKey key(route->network.data, route->mask, af);
    RoutingTable::Eigrp* existingRoute = (af == AddressFamily::IPv4) ? eigrp[as][key] : eigrpIPv6[as][key];
    double minMetric = existingRoute->metric;

    // Allow routes within variance range
    if (route->metric <= minMetric * variance)
    {
        // Avoid duplicate nextHops with the set
        if (existingRoute->nextHopsSet.insert(route->nextHop).second)
        {
            existingRoute->nextHopsVector.push_back(route->nextHop);
        }

        // Update metric to the minimum
        existingRoute->metric = std::min(existingRoute->metric, route->metric);
        existingRoute->feasibleDistance = std::min(existingRoute->feasibleDistance, route->feasibleDistance);
        existingRoute->reportedDistance = std::min(existingRoute->reportedDistance, route->reportedDistance);
        existingRoute->routeType = route->routeType;
        existingRoute->interface = route->interface;
    }
}

std::vector<RoutingTable::Eigrp*> RoutingTable::getAllEigrpRoutes(AddressFamily af, uint32_t as)
{
    std::lock_guard<std::mutex> lock(tableMutex);
    std::vector<Eigrp*> routes;
    for (const auto& [key, route] : af == AddressFamily::IPv4 ? eigrp[as] : eigrpIPv6[as])
    {
        routes.push_back(route);
    }
    return routes;
}

std::vector<RoutingTable::Eigrp*> RoutingTable::getAllConnectedEigrpRoutes(AddressFamily af, uint32_t as)
{
    std::lock_guard<std::mutex> lock(tableMutex);
    std::vector<Eigrp*> routes;
    for (const auto& [key, route] : af == AddressFamily::IPv4 ? eigrp[as] : eigrpIPv6[as])
    {
        if (route->routeType == RoutingTable::Eigrp::RouteType::CONNECTED)
        {
            routes.push_back(route);
        }
    }
    return routes;
}

RoutingTable::Eigrp* RoutingTable::getEigrpRoute(const uint8_t* destination, const uint8_t mask, AddressFamily af, uint32_t as)
{
    std::lock_guard<std::mutex> lock(tableMutex);
    for (const auto& [key, entry] : af == AddressFamily::IPv4 ? eigrp[as] : eigrpIPv6[as])
    {
        if (entry && std::memcmp(entry->network.raw, destination, af == AddressFamily::IPv4 ? 4 : 16) && entry->mask == mask)
        {
            return entry;
        }
    }
    return nullptr;
}

bool RoutingTable::getNextHop(uint8_t* out, const uint8_t* destination, uint8_t mask, uint32_t as, AddressFamily af)
{
    std::lock_guard<std::mutex> lock(tableMutex);
    IpPrefixKey key(destination, mask, af);
    auto it = eigrp[as].find(key);
    if (it != eigrp[as].end() && !it->second->nextHopsVector.empty())
    {
        static std::atomic<size_t> roundRobinIndex{0};
        std::memcpy(out, it->second->nextHopsVector[roundRobinIndex++ % it->second->nextHopsVector.size()].data, af == AddressFamily::IPv4 ? 4 : 16);
        return true;
    }
    return false;
}
