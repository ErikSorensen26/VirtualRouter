// EigrpInterfacePacketEngine.cpp

#include "EigrpInterfacePacketEngine.h"

namespace Protocol
{

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
}
