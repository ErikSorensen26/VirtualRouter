#include <RoutingTable.h>

void RoutingTable::UpdateEigrp(const Eigrp& route)
{
    std::lock_guard<std::mutex> lock(tableMutex);
    std::string key = route.network + "/" + std::to_string(route.mask);
    eigrp[key] = route;
}

void RoutingTable::RemoveEigrp(const std::string& network, int mask)
{
    std::lock_guard<std::mutex> lock(tableMutex);
    std::string key = network + "/" + std::to_string(mask);
    auto it = eigrp.find(key);
    if (it != eigrp.end())
    {
        eigrp.erase(it);
    }
}

std::vector<RoutingTable::Eigrp> RoutingTable::GetAllEigrpRoutes()
{
    std::lock_guard<std::mutex> lock(tableMutex);
    std::vector<Eigrp> routes;
    for (const auto& [key, route] : eigrp)
    {
        routes.push_back(route);
    }
    return routes;
}

std::optional<RoutingTable::Eigrp> RoutingTable::GetEigrpRoute(const std::string& destination, const int mask)
{
    std::lock_guard<std::mutex> lock(tableMutex);
    for (const auto& [key, entry] : eigrp)
    {
        if (entry.network == destination, entry.mask == mask)
        {
            return entry;
        }
    }
    return std::nullopt;
}


void RoutingTable::UpdateArp(const arpHeader& recievedArp)
{
    std::lock_guard<std::mutex> lock(tableMutex);

    // Create route entry
    Arp route;
    route.age = std::chrono::system_clock::now();
    route.interface = recievedArp.targetIpAddress;
    route.ipAddress = recievedArp.senderIpAddress;
    route.mac = recievedArp.senderHardwareAddress;
    route.type = recievedArp.opcode;

    // Find if the route Exists
    std::string key = recievedArp.senderIpAddress;
    arp[key] = route;
}

void RoutingTable::UpdateArp(const string ip, string mac, string interfaceAddress)
{
    std::lock_guard<std::mutex> lock(tableMutex);

    // Create route entry
    Arp route;
    route.age = std::chrono::system_clock::now();
    route.interface = interfaceAddress;
    route.ipAddress = ip;
    route.mac = mac;

    // Find if the route Exists
    std::string key = ip;
    const auto it = arp.find(key);
    if (it == arp.end())
    {
        arp[key] = route;
    }
}

std::optional<RoutingTable::Arp> RoutingTable::ArpLookup(const std::string& ipAddress)
{
    std::lock_guard<std::mutex> lock(tableMutex);
    for (const auto& [key, entry] : arp)
    {
        if (entry.ipAddress == ipAddress)
        {
            return entry;
        }
    }
    return std::nullopt;
}