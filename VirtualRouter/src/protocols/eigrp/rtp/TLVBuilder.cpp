// EigrpInterfacePacketEngine.cpp

#include "TLVBuilder.h"

namespace Eigrp
{

uint8_t TLVBuilder::encodeRouteOption(uint8_t* out, size_t maxSize, const RouteInfo* route, uint64_t currentBandwidth, uint64_t currentDelay, RouteType type)
{
    RouteData data(const_cast<RouteInfo*>(route)->routeInfo);
    const bool wide = isWide(type);
    const bool external = isExternal(type);
    data.offset = 0;
    data.v6 = route->routeInfo.prefix.af == AddressFamily::IPv6;
    data.value = out;
    data.valueSize = maxSize;
    const uint8_t ipSize = data.v6 ? 16 : 4;

    bool max = route->routeInfo.delay == std::numeric_limits<uint64_t>::max();

    const uint64_t delay = max
        ? route->routeInfo.delay + ((currentDelay / 10) * 256ULL)
        : route->routeInfo.delay;

    const uint64_t bw = std::min(route->routeInfo.bandwidth, currentBandwidth * 256);

    if (!wide)
    {
        std::memcpy(out, route->routeInfo.nextHop.raw, ipSize);
        data.offset += ipSize;
        if (external)
            if (!encodeExternal(data)) return 0;
        if (!encodeClassicMetric(data, delay, bw)) return 0;
        if (!encodeDestination(data)) return 0;
    }
    else
    {
        writeU16(out + data.offset, route->routeInfo.wide.topology); data.offset += 2;
        writeU16(out + data.offset, route->routeInfo.wide.afi); data.offset += 2;
        writeU32(out + data.offset, route->routeInfo.wide.rid); data.offset += 4;
        if (!encodeWideMetric(data, delay, bw)) return 0;
        std::memcpy(out, route->routeInfo.nextHop.raw, ipSize);
        data.offset += ipSize;
        if (external)
            if (!encodeExternal(data)) return 0;
        if (!encodeDestination(data)) return 0;
    }

    if (data.r.bandwidth > currentBandwidth)
        data.r.bandwidth = currentBandwidth;

    return static_cast<uint8_t>(data.offset);
}

std::pair<std::optional<ReceivedRoute>, bool> TLVBuilder::decodeRoute(const TLV16Option& routeOpt, uint32_t originIface, TLVType type)
{
    ReceivedRoute r{};
    r.originInterface = originIface;
    RouteData data(r);
    data.value = const_cast<uint8_t*>(routeOpt.value);
    data.valueSize = routeOpt.valueSize;

    // Determine route characteristics
    const uint16_t tlvType = routeOpt.type;
    const uint8_t* value = routeOpt.value;

    // Deduce wide/classic, internal/external, and v6
    const bool wide = (tlvType == 0x0602 || tlvType == 0x0603);
    const bool external = (tlvType == 0x0603 || tlvType == 0x0403 || tlvType == 0x0103);
    data.v6 = (tlvType == 0x0402 | tlvType == 0x0403 || readU16(routeOpt.value) == 2);
    r.prefix.af = data.v6 ? AddressFamily::IPv6 : AddressFamily::IPv4;

    // Validate type
    if ((tlvType & 0xFF00) != static_cast<uint16_t>(type))
        return { std::nullopt, false };

    const uint8_t ipSize = data.v6 ? 16 : 4;

    if (!wide)
    {
        std::memcpy(r.nextHop.raw, value + data.offset, ipSize);
        data.offset += ipSize;
        if (external)
            if (!decodeExternal(data)) return { std::nullopt, true };
        if (!decodeClassicMetric(data)) return { std::nullopt, true };
        if (!decodeDestination(data)) return { std::nullopt, true };
    }
    else
    {
        r.wide.topology = readU16(value + data.offset); data.offset += 2;
        r.wide.afi = readU16(value + data.offset); data.offset += 2;
        r.wide.rid = readU32(value + data.offset); data.offset += 4;
        if (!decodeWideMetric(data)) return { std::nullopt, true };
        std::memcpy(r.nextHop.raw, value + data.offset, ipSize);
        if (external)
            if (!decodeExternal(data)) return { std::nullopt, true };
        if (!decodeDestination(data)) return { std::nullopt, true };
    }

    return { r, true };
}

bool TLVBuilder::decodeClassicMetric(RouteData& data)
{
    if (data.offset + 16  > data.valueSize) return false;
    data.r.delay = ((uint64_t)readU32(data.value + data.offset) * 10ULL * 1'000'000ULL) / 256ULL; data.offset += 4;
    data.r.bandwidth = (10'000'000ULL * 256ULL) / readU32(data.value + data.offset); data.offset += 4;
    data.r.mtu = readU24(data.value + data.offset); data.offset += 3;
    data.r.hopCount = data.value[data.offset++];
    data.r.reliability = data.value[data.offset++];
    data.r.load = data.value[data.offset++];
    data.r.tag = data.value[data.offset++];
    data.offset++;

    if (data.r.delay > 0xFFFFFFull)
        data.r.delay = std::numeric_limits<uint64_t>::max();

    return true;
}

bool TLVBuilder::encodeClassicMetric(RouteData& data, const uint64_t& delay, const uint64_t& bw)
{
    if (data.offset + 16 > data.valueSize) return false;
    uint32_t scaledDelay = static_cast<uint32_t>((delay * 256ULL) / (10ULL * 1'000'000ULL));
    uint32_t scaledBW = static_cast<uint32_t>((10'000'000 * 256ULL) / (bw > 10'000'000 ? 10'000'000 : bw));
    writeU32(data.value + data.offset, scaledDelay >= 0xFFFFFF ? 0xFFFFFFFF : scaledDelay);
    data.offset += 4;
    writeU32(data.value + data.offset, scaledBW);
    data.offset += 4;
    writeU24(data.value + data.offset, data.r.mtu);
    data.offset += 3;
    data.value[data.offset] = data.r.hopCount += 1;
    data.value[data.offset + 1] = data.r.reliability;
    data.value[data.offset + 2] = data.r.load;
    data.value[data.offset + 3] = static_cast<uint8_t>(data.r.tag);
    data.value[data.offset + 4] = 0; /* flags */
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
    data.r.mtu = readU24(data.value + data.offset); data.offset += 3;
    data.r.hopCount = data.value[data.offset++];
    data.r.delay = readU48(data.value + data.offset); data.offset += 6;
    data.r.bandwidth = readU48(data.value + data.offset); data.offset += 6;
    data.offset += 2; // Reserved
    data.r.wide.wideFlags = readU16(data.value + data.offset); data.offset += 2;

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
    data.value[data.offset++] = data.r.hopCount += 1;
    writeU48(data.value + data.offset, delay); data.offset += 6;
    writeU48(data.value + data.offset, bw); data.offset += 6;
    writeU16(data.value + data.offset, 0); data.offset += 2; // reserved
    writeU16(data.value + data.offset, data.r.wide.wideFlags); data.offset += 2; // flags
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
    uint8_t prefSize = data.v6
        ? (plen == 128) ? 16 : ((plen / 8) + 1)
        : ((plen - 1) / 8) + 1;
    if (data.offset + prefSize > data.valueSize) return false;
    std::memcpy(data.r.prefix.addr, data.value + data.offset, prefSize);
    data.r.prefix.prefixLength = plen;
    data.offset += prefSize;
    return true;
}

bool TLVBuilder::encodeDestination(RouteData& data)
{
    uint8_t plen = data.r.prefix.prefixLength;
    uint8_t prefSize = data.v6
        ? (plen == 128) ? 16 : ((plen / 8) + 1)
        : ((plen - 1) / 8) + 1;
    if (data.offset + prefSize > data.valueSize) return false;
    data.value[data.offset] = plen; data.offset += 1;
    std::memcpy(data.value + data.offset, data.r.prefix.addr, prefSize);
    data.offset += prefSize;
    return true;
}

uint8_t* TLVBuilder::encodeStubOption(uint8_t* out, const EigrpConfigs::StubConfig& stub)
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

uint8_t* TLVBuilder::calculateParameters(uint8_t* out, const EigrpConfigs::KValue& kvalue, uint16_t holdTime)
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
