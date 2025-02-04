#include <RoutingTable.h>

void RoutingTable::addEigrp(Eigrp *route, AddressFamily af, uint32_t as)
{
    // Create key in "network/mask" format
    ByteString key = route->network + ByteString("/") + ByteString(std::to_string(route->mask));
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
            return;
        }
        else
        {
            return;
        }
    }

    // Summary route handling
    if (route->routeType == "summary")
    {
        uint32_t lowestSummaryMetric = std::numeric_limits<uint32_t>::max();
        for (auto existingRoute = eigrpTable.begin(); existingRoute != eigrpTable.end();)
        {
            if (existingRoute->second->network != route->network && existingRoute->second->routeType != "summary" && Functions::isSubnetOf(existingRoute->second->network, existingRoute->second->mask, route->network, route->mask))
            {
                if (existingRoute->second->metric < lowestSummaryMetric)
                {
                    lowestSummaryMetric = existingRoute->second->metric;
                }
                delete existingRoute->second;
                existingRoute->second = nullptr;
                existingRoute = eigrpTable.erase(existingRoute);
            }
            else
            {
                existingRoute++;
            }
        }
    }

    // Add route to the routing table
    if (it == eigrpTable.end())
    {
        // New route addition
        eigrpTable[key] = route;
    }
    else
    {
        // Update existing route
        auto& existingRoute = it->second;
        bool metricChanged = existingRoute->metric != route->metric;
        bool fdChanged = existingRoute->feasibleDistance != route->feasibleDistance;

        if (metricChanged || fdChanged)
        {
            // Update all relevant fields
            if (route->feasibleDistance < existingRoute->feasibleDistance ||
                (route->feasibleDistance == existingRoute->feasibleDistance && route->adminDistance < existingRoute->adminDistance))
            {
                existingRoute = route;
            }

            // Add new nextHop if not already present
            if (existingRoute->nextHopsSet.insert(route->nextHop).second)
            {
                existingRoute->nextHopsVector.push_back(route->nextHop);
            }
        }
    }
}

void RoutingTable::removeEigrp(const ByteString& network, uint8_t mask, AddressFamily af, uint32_t as)
{
    std::lock_guard<std::mutex> lock(tableMutex);
    ByteString key = network + "/" + std::to_string(mask);
    auto& eigrpTable = (af == AddressFamily::IPv4) ? eigrp[as] : eigrpIPv6[as];
    auto it = eigrpTable.find(key);
    if (it != eigrpTable.end())
    {
        delete it->second;
        it->second = nullptr;
        eigrpTable.erase(it);
    }
}

void RoutingTable::updateEigrpWithVariance(Eigrp* route, uint8_t variance, AddressFamily af, uint32_t as)
{
    std::lock_guard<std::mutex> lock(tableMutex);

    ByteString key = route->network + "/" + std::to_string(route->mask);
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
        if (route->routeType == "connected")
        {
            routes.push_back(route);
        }
    }
    return routes;
}

std::optional<RoutingTable::Eigrp*> RoutingTable::getEigrpRoute(const ByteString& destination, const uint8_t mask, AddressFamily af, uint32_t as)
{
    std::lock_guard<std::mutex> lock(tableMutex);
    for (const auto& [key, entry] : af == AddressFamily::IPv4 ? eigrp[as] : eigrpIPv6[as])
    {
        if (entry && entry->network == destination && entry->mask == mask)
        {
            return entry;
        }
    }
    return std::nullopt;
}


void RoutingTable::updateArp(const ArpHeader& recievedArp)
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
    ByteString key = recievedArp.senderIpAddress;
    arp[key] = route;
}

void RoutingTable::updateArp(const ByteString ip, ByteString macAddress, ByteString interfaceAddress)
{
    std::lock_guard<std::mutex> lock(tableMutex);

    // Create route entry
    Arp route;
    route.age = std::chrono::system_clock::now();
    route.interface = interfaceAddress;
    route.ipAddress = ip;
    route.mac = macAddress;

    // Find if the route Exists
    ByteString key = ip;
    const auto it = arp.find(key);
    if (it == arp.end())
    {
        arp[key] = route;
    }
}

std::optional<RoutingTable::Arp> RoutingTable::ArpLookup(const ByteString& ipAddress)
{
    std::lock_guard<std::mutex> lock(tableMutex);
    auto it = arp.find(ipAddress);
    if (it != arp.end())
    {
        return it->second;
    }
    return std::nullopt;
}

ByteString RoutingTable::getNextHop(const ByteString& destination, uint8_t mask, uint32_t as)
{
    std::lock_guard<std::mutex> lock(tableMutex);
    auto it = eigrp[as].find(destination + "/" + std::to_string(mask));
    if (it != eigrp[as].end() && !it->second->nextHopsVector.empty())
    {
        static std::atomic<size_t> roundRobinIndex{0};
        return it->second->nextHopsVector[roundRobinIndex++ % it->second->nextHopsVector.size()];
    }
    return ""; // No route found
}

void RoutingTable::printRoutingTable() {
    std::lock_guard<std::mutex> lock(tableMutex);
    Logger::getInstance().debug() << "RoutingTable - RoutingEntry:\n";
    for (const auto& entry : routingTable) {
        Logger::getInstance().debug() << "Destination: " << entry.second.destination
                  << ", Mask: " << entry.second.mask
                  << ", NextHop: " << entry.second.nextHop
                  << ", OutInterface: " << entry.second.outInterface
                  << ", Source: " << entry.second.source
                  << ", Metric: " << entry.second.metric
                  << ", Age: " << Functions::timeToString(entry.second.age)
                  << ", AdmDist: " << entry.second.admDist
                  << "\n";
    }
}
void RoutingTable::printFibTable() {
    std::lock_guard<std::mutex> lock(tableMutex);
    Logger::getInstance().debug() << "RoutingTable - Fib:\n";
    for (const auto& entry : fib) {
        Logger::getInstance().debug() << "Destination: " << entry.second.destination
                  << ", NextHop: " << entry.second.nextHop
                  << ", OutInt: " << entry.second.outInt
                  << ", MAC: " << entry.second.mac
                  << ", Preference: " << entry.second.preference
                  << "\n";
    }
}
void RoutingTable::printArpTable() {
    std::lock_guard<std::mutex> lock(tableMutex);
    Logger::getInstance().debug() << "RoutingTable - Arp:\n";
    for (const auto& entry : arp) {
        Logger::getInstance().debug() << "IP Address: " << entry.second.ipAddress
                  << ", MAC: " << entry.second.mac
                  << ", Interface: " << entry.second.interface
                  << ", Type: " << entry.second.type
                  << ", Age: " << Functions::timeToString(entry.second.age)
                  << "\n";
    }
}
void RoutingTable::printNdpTable() {
    std::lock_guard<std::mutex> lock(tableMutex);
    Logger::getInstance().debug() << "RoutingTable - NDP:\n";
    for (const auto& entry : ndp) {
        Logger::getInstance().debug() << "IP Address: " << entry.second.ipAddress
                  << ", MAC Address: " << entry.second.macAddress
                  << ", Interface: " << entry.second.interface
                  << ", State: " << entry.second.state
                  << ", Age: " << Functions::timeToString(entry.second.age)
                  << "\n";
    }
}
void RoutingTable::printMacTable() {
    std::lock_guard<std::mutex> lock(tableMutex);
    Logger::getInstance().debug() << "RoutingTable - MAC:\n";
    for (const auto& entry : mac) {
        Logger::getInstance().debug() << "MAC: " << entry.second.mac
                  << ", Interface: " << entry.second.interface
                  << ", VLAN ID: " << entry.second.vlanID
                  << ", Type: " << entry.second.type
                  << ", Age: " << Functions::timeToString(entry.second.age)
                  << "\n";
    }
}
void RoutingTable::printRibTable() {
    std::lock_guard<std::mutex> lock(tableMutex);
    Logger::getInstance().debug() << "RoutingTable - Rib:\n";
    for (const auto& entry : rib) {
        Logger::getInstance().debug() << "Destination: " << entry.second.destination
                  << ", Mask: " << entry.second.mask
                  << ", NextHop: " << entry.second.nextHop
                  << ", OutInterface: " << entry.second.outInterface
                  << ", Source: " << entry.second.source
                  << ", Metric: " << entry.second.metric
                  << ", Age: " << Functions::timeToString(entry.second.age)
                  << ", AdmDist: " << entry.second.admDist
                  << ", Tags: ";
        for (const auto& tag : entry.second.tags) {
            Logger::getInstance().debug() << tag << " ";
        }
        Logger::getInstance().debug() << "\n";
    }
}
void RoutingTable::printPrbTable() {
    std::lock_guard<std::mutex> lock(tableMutex);
    Logger::getInstance().debug() << "RoutingTable - Prb:\n";
    for (const auto& entry : prb) {
        Logger::getInstance().debug() << "Source IP: " << entry.second.sourceIp
                  << ", Destination: " << entry.second.destination
                  << ", Source Port: " << entry.second.sourcePort
                  << ", Dest Port: " << entry.second.destPort
                  << ", Protocol: " << entry.second.protocol
                  << ", NextHop: " << entry.second.nextHop
                  << ", OutInterface: " << entry.second.outInterface
                  << ", Match Criteria: " << entry.second.matchCriteria
                  << ", DSCP: " << entry.second.DSCP
                  << "\n";
    }
}
void RoutingTable::printMulticastTable() {
    std::lock_guard<std::mutex> lock(tableMutex);
    Logger::getInstance().debug() << "RoutingTable - Multicast:\n";
    for (const auto& entry : multicast) {
        Logger::getInstance().debug() << "Group: " << entry.second.group
                  << ", Source IP: " << entry.second.sourceIp
                  << ", InInterface: " << entry.second.inInterface
                  << ", RPF: " << entry.second.RPF
                  << ", Protocol: " << entry.second.protocol
                  << ", Age: " << Functions::timeToString(entry.second.age)
                  << ", Route Metric: " << entry.second.routeMetric
                  << ", OutInterfaces: ";
        for (const auto& outInterface : entry.second.outInterface) {
            Logger::getInstance().debug() << outInterface << " ";
        }
        Logger::getInstance().debug() << "\n";
    }
}
void RoutingTable::printAclTable() {
    std::lock_guard<std::mutex> lock(tableMutex);
    Logger::getInstance().debug() << "RoutingTable - ACL:\n";
    for (const auto& entry : acl) {
        Logger::getInstance().debug() << "Source IP: " << entry.second.sourceIp
                  << ", Dest IP: " << entry.second.destIp
                  << ", Protocol: " << entry.second.protocol
                  << ", Source Port Range: " << entry.second.sourcePortRange
                  << ", Dest Port Range: " << entry.second.destPortRange
                  << ", Log String: " << entry.second.logString
                  << ", Action: " << entry.second.action
                  << ", Rule Number: " << entry.second.ruleNum
                  << ", ICMP Code: " << entry.second.icmpCode
                  << ", Age: " << Functions::timeToString(entry.second.age)
                  << ", DSCP: " << entry.second.DSCP
                  << "\n";
    }
}
// void RoutingTable::printEigrpTable() {
//     std::lock_guard<std::mutex> lock(tableMutex);
//     Logger::getInstance().debug() << "RoutingTable - EIGRP:\n";
//     for (const auto& entry : eigrp[]) {
//         Logger::getInstance().debug() << "Network: " << entry.second.network
//                   << ", Next Hop: " << entry.second.nextHop
//                   << ", Out Interface: " << entry.second.interface
//                   << ", Successor: " << entry.second.successor
//                   << ", Feasible Successor: " << entry.second.feasibleSuccessor
//                   << ", Route Source: " << entry.second.routeSource
//                   << ", Route Type: " << entry.second.routeType
//                   << ", Active or Passive: " << entry.second.activeOrPassive
//                   << ", Metric: " << entry.second.metric
//                   << ", Feasible Distance: " << entry.second.feasibleDistance
//                   << ", Reported Distance: " << entry.second.reportedDistance
//                   << ", Admin Distance: " << entry.second.adminDistance
//                   << ", Hold Time: " << entry.second.holdTime
//                   << ", Stuck in Active: " << entry.second.updateTimer
//                   << ", Retransmission Interval: " << entry.second.retransmitInterval
//                   << ", Sequence Number: " << entry.second.sequenceNumber
//                   << ", Route Tag: " << entry.second.routeTag
//                   << ", Hop Count: " << entry.second.hopCount
//                   << ", Bandwidth: " << entry.second.bandwidth
//                   << ", Load: " << entry.second.load
//                   << ", Delay: " << entry.second.delay
//                   << ", Reliability: " << entry.second.reliability
//                   << ", MTU: " << entry.second.mtu
//                   << ", Mask: " << entry.second.mask
//                   << ", Age: " << Functions::timeToString(entry.second.age)
//                   << "\n";
//     }
//}
