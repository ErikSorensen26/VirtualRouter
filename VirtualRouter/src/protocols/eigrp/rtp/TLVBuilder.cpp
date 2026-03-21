// EigrpInterfacePacketEngine.cpp

#include "TLVBuilder.h"
#include "eigrp/interface/EigrpInterface.h"
#include "eigrp/topology/TopologyTable.h"

namespace EIGRP
{

uint8_t TLVBuilder::encodeRouteOption(EigrpInterface& iface, uint8_t* out, size_t maxSize, const RouteInfo* route, uint64_t currentBandwidth, uint64_t currentDelay, RouteType type)
{
    RouteData data(const_cast<RouteInfo*>(route)->routeInfo);
    const bool wide = isWide(type);
    const bool external = isExternal(type);
    const bool named = isNamed(type);
    data.offset = 0;
    data.v6 = route->routeInfo.prefix.isIPv6();
    data.value = out;
    data.valueSize = maxSize;
    const uint8_t ipSize = data.v6 ? 16 : 4;

    bool max = route->routeInfo.delay == std::numeric_limits<uint64_t>::max();

    const uint64_t delay = max
        ? route->routeInfo.delay
        : route->routeInfo.delay + ((currentDelay / 10) * 10'000'000ULL);

    const uint64_t bw = std::min(route->routeInfo.bandwidth, currentBandwidth);

    auto writeIpAddr = [&](uint8_t* dst, const IPAddress& ip) {
        if (data.v6) writeU128(dst, ip.v6());
        else writeU32(dst, ip.v4());
    };

    if (!wide)
    {
        if (iface.configs.get<Config::EigrpInterface::NEXT_HOP_SELF>().load())
            writeIpAddr(out, iface.ifaceAddress);
        else
            writeIpAddr(out, route->routeInfo.nextHop);

        data.offset += ipSize;
        if (external)
            if (!encodeExternal(data)) return 0;
        if (!encodeClassicMetric(data, delay, bw)) return 0;
        if (!encodeDestination(data)) return 0;
    }
    else
    {
        if (named)
        {
            writeU16(out + data.offset, route->routeInfo.wide.topology); data.offset += 2;
            writeU16(out + data.offset, route->routeInfo.wide.afi); data.offset += 2;
            writeU32(out + data.offset, route->routeInfo.wide.rid); data.offset += 4;
        }
        if (!encodeWideMetric(data, delay, bw)) return 0;

        if (iface.configs.get<Config::EigrpInterface::NEXT_HOP_SELF>().load())
            writeIpAddr(out, iface.ifaceAddress);
        else
            writeIpAddr(out, route->routeInfo.nextHop);

        data.offset += ipSize;
        if (external)
            if (!encodeExternal(data)) return 0;
        if (!encodeDestination(data)) return 0;
    }

    if (data.r.bandwidth > currentBandwidth)
        data.r.bandwidth = currentBandwidth;

    return static_cast<uint8_t>(data.offset);
}

std::optional<ReceivedRoute> TLVBuilder::decodeRoute(const TLV16Option& routeOpt, uint32_t originIface, AddressFamily af)
{
    ReceivedRoute r{};
    r.originInterface = originIface;
    RouteData data(r);
    data.value = const_cast<uint8_t*>(routeOpt.value);
    data.valueSize = routeOpt.valueSize;

    // Determine route characteristics
    const uint8_t* value = routeOpt.value;

    // Deduce wide/classic, internal/external, and v6
    const bool wide = isWide(static_cast<RouteType>(routeOpt.type));
    const bool external = isExternal(static_cast<RouteType>(routeOpt.type));
    const bool named = isNamed(static_cast<RouteType>(routeOpt.type));
    data.v6 = af == AddressFamily::IPv6;
    // Set prefix AF marker via IPPrefix constructor
    r.prefix = IPPrefix(af);

    const uint8_t ipSize = data.v6 ? 16 : 4;

    if (!wide)
    {
        r.nextHop = IPAddress(value + data.offset, af);
        data.offset += ipSize;
        if (external)
            if (!decodeExternal(data)) return std::nullopt;
        if (!decodeClassicMetric(data)) return std::nullopt;
        if (!decodeDestination(data)) return std::nullopt;
    }
    else
    {
        if (named)
        {
            r.wide.topology = readU16(value + data.offset); data.offset += 2;
            r.wide.afi = readU16(value + data.offset); data.offset += 2;
            r.wide.rid = readU32(value + data.offset); data.offset += 4;
        }
        if (!decodeWideMetric(data)) return std::nullopt;
        r.nextHop = IPAddress(value + data.offset, af);
        data.offset += ipSize;
        if (external)
            if (!decodeExternal(data)) return std::nullopt;
        if (!decodeDestination(data)) return std::nullopt;
    }

    return r;
}

bool TLVBuilder::decodeClassicMetric(RouteData& data)
{
    if (data.offset + 16  > data.valueSize) return false;
    uint32_t dl = readU32(data.value + data.offset); data.offset += 4;
    uint32_t bw = readU32(data.value + data.offset); data.offset += 4;
    data.r.delay = (dl != 0) ? (((uint64_t)dl * 10ULL * 1'000'000ULL) / 256ULL) : dl;
    data.r.bandwidth = (bw != 0) ? ((10'000'000ULL * 256ULL) / bw) : bw;
    data.r.mtu = static_cast<uint16_t>(readU24(data.value + data.offset)); data.offset += 3;
    data.r.hopCount = data.value[data.offset++];
    data.r.reliability = data.value[data.offset++];
    data.r.load = data.value[data.offset++];
    data.r.tag = data.value[data.offset++];
    data.r.flags = data.value[data.offset++];
    data.offset++;

    if (data.r.delay > 0xFFFFFFull)
        data.r.delay = std::numeric_limits<uint64_t>::max();

    return true;
}

bool TLVBuilder::encodeClassicMetric(RouteData& data, const uint64_t& delay, const uint64_t& bw)
{
    if (data.offset + 16 > data.valueSize) return false;
    uint32_t scaledDelay = (delay != 0 && delay != std::numeric_limits<uint64_t>::max()) ? static_cast<uint32_t>((delay * 256ULL) / (10ULL * 1'000'000ULL)) : static_cast<uint32_t>(delay);
    uint32_t scaledBW = (bw != 0 && bw != std::numeric_limits<uint64_t>::max()) ? static_cast<uint32_t>((10'000'000 * 256ULL) / (bw > 10'000'000 ? 10'000'000 : bw)) : static_cast<uint32_t>(bw);
    writeU32(data.value + data.offset, scaledDelay >= 0xFFFFFFFF ? 0xFFFFFFFF : scaledDelay);
    data.offset += 4;
    writeU32(data.value + data.offset, scaledBW);
    data.offset += 4;
    writeU24(data.value + data.offset, data.r.mtu);
    data.offset += 3;
    data.value[data.offset] = static_cast<uint8_t>(data.r.hopCount + 1);
    data.value[data.offset + 1] = data.r.reliability;
    data.value[data.offset + 2] = data.r.load;
    data.value[data.offset + 3] = static_cast<uint8_t>(data.r.tag);
    data.value[data.offset + 4] = data.r.flags;
    data.offset += 5;
    return true;
}

bool TLVBuilder::decodeWideMetric(RouteData& data)
{
    uint8_t attLen = data.value[data.offset++] * 2;
    if (data.offset + 28 + attLen > data.valueSize) return false;
    data.r.wide.priority = data.value[data.offset++];
    data.r.reliability = data.value[data.offset++];
    data.r.load = data.value[data.offset++];
    data.r.mtu = static_cast<uint16_t>(readU24(data.value + data.offset)); data.offset += 3;
    data.r.hopCount = data.value[data.offset++];
    data.r.delay = readU48(data.value + data.offset); data.offset += 6;
    data.r.bandwidth = readU48(data.value + data.offset); data.offset += 6;
    data.offset += 2; // Reserved
    data.r.flags = data.value[data.offset]; data.offset += 2;

    if (data.r.delay > 0xFFFFFFFFull)
        data.r.delay = std::numeric_limits<uint64_t>::max();

    if (attLen != 0)
    {
        data.r.wide.allocate(data.value + data.offset, attLen);
        data.offset += attLen;
    }
    return true;
}

bool TLVBuilder::encodeWideMetric(RouteData& data, const uint64_t& delay, const uint64_t& bw)
{
    if (data.offset + 28 + data.r.wide.size() > data.valueSize) return false;
    data.value[data.offset++] = data.r.wide.data.size() > 0
        ? static_cast<uint8_t>(data.r.wide.data.size() / 2) : 0; // offset
    data.value[data.offset++] = 0; // proprity
    data.value[data.offset++] = data.r.reliability;
    data.value[data.offset++] = data.r.load;
    writeU24(data.value + data.offset, data.r.mtu); data.offset += 3;
    data.value[data.offset++] = static_cast<uint8_t>(data.r.hopCount + 1);
    writeU48(data.value + data.offset, delay); data.offset += 6;
    writeU48(data.value + data.offset, bw); data.offset += 6;
    writeU16(data.value + data.offset, 0); data.offset += 2; // reserved
    data.value[data.offset] = data.r.flags; data.offset += 2; // flags
    if (data.r.wide.data.size() != 0)
    {
        std::memcpy(data.value + data.offset, data.r.wide.data.data(), data.r.wide.data.size());
        data.offset += data.r.wide.data.size();
    }
    return true;
}

bool TLVBuilder::decodeExternal(RouteData& data)
{
    if (data.offset + 20 > data.valueSize) return false;
    data.r.external.originRouter = readU32(data.value + data.offset); data.offset += 4;
    data.r.external.originAS = readU32(data.value + data.offset); data.offset += 4;
    data.r.tag = readU32(data.value + data.offset); data.offset += 4;
    data.r.external.externalMetric = readU32(data.value + data.offset); data.offset += 4;
    data.offset += 2; // Reserved
    data.r.external.type = data.value[data.offset++];
    data.r.external.flags = data.value[data.offset++];
    return true;
}

bool TLVBuilder::encodeExternal(RouteData& data)
{
    if (data.offset + 20 > data.valueSize) return false;
    writeU32(data.value + data.offset, data.r.external.originRouter); data.offset += 4;
    writeU32(data.value + data.offset, data.r.external.originAS); data.offset += 4;
    writeU32(data.value + data.offset, data.r.tag); data.offset += 4;
    writeU32(data.value + data.offset, data.r.external.externalMetric); data.offset += 4;
    data.offset += 2; // Reserved
    data.value[data.offset++] = data.r.external.type;
    data.value[data.offset++] = data.r.external.flags;
    return true;
}

bool TLVBuilder::decodeDestination(RouteData& data)
{
    uint8_t plen = data.value[data.offset++];
    uint8_t prefSize = (plen + 7) / 8;
    if (data.offset + prefSize > data.valueSize) return false;
    AddressFamily af = data.v6 ? AddressFamily::IPv6 : AddressFamily::IPv4;
    data.r.prefix = IPPrefix(data.value + data.offset, plen, af, true);
    data.offset += prefSize;
    return true;
}

bool TLVBuilder::encodeDestination(RouteData& data)
{
    uint8_t plen = data.r.prefix.prefixLength;
    uint8_t prefSize = (plen + 7) / 8;
    if (data.offset + prefSize > data.valueSize) return false;
    data.value[data.offset] = plen; data.offset += 1;
    if (data.v6)
        writeBytes(data.value + data.offset, data.r.prefix.v6(), prefSize);
    else
        writeBytes(data.value + data.offset, data.r.prefix.v4(), prefSize);
    data.offset += prefSize;
    return true;
}

uint8_t* TLVBuilder::encodeStubOption(uint8_t* out, const EIGRP::StubConfig& stub)
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

uint8_t* TLVBuilder::calculateParameters(uint8_t* out, const EIGRP::KValue& kvalue, uint16_t holdTime)
{
    out[0] = kvalue.k1_Bandwidth;
    out[1] = kvalue.k2_Load;
    out[2] = kvalue.k3_Delay;
    out[3] = kvalue.k4_Reliability;
    out[4] = kvalue.k5_MTU;
    out[5] = kvalue.k6_Power;
    if (holdTime != 0) writeU16(out + 6, holdTime);

    return out;
}
}
