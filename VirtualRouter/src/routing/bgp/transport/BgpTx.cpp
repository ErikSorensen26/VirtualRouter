// BgpTx.cpp

#include <algorithm>
#include <utility>
#include <vector>

#include "packet/headers/embedded/bgp/BgpOpenHeader.hpp"
#include "BgpTx.h"
#include "tcp/Connection.h"
#include "bgp/neighbor/Neighbor.h"
#include "bgp/BgpProcess.h"

namespace routing::bgp
{
void BgpTx::buildHeader(uint8_t type, uint16_t payloadSize, uint8_t* buf)
{
    packet::BgpHeader hdr;

    hdr.setBuffer(buf);
    hdr.setMarker();
    hdr.setType(type);
    hdr.setLength(payloadSize + packet::BgpHeader::fixedSize);
}

/**
 * @brief Returns the hostname/domain byte counts the FQDN capability can actually encode.
 *
 * Each name carries a one-byte length prefix, and the pair must fit inside one
 * capability parameter (253 bytes of value). Both the size computation and the
 * emitter use this so the declared length always matches the bytes written.
 */
static std::pair<size_t, size_t> fqdnLengths(const Capabilities& caps)
{
    constexpr size_t kMaxCapValue = 253;

    size_t hostLen = std::min<size_t>(caps.hostname.size(), 255);
    size_t domLen  = std::min<size_t>(caps.domain.size(), 255);

    if (2 + hostLen + domLen > kMaxCapValue)
    {
        hostLen = std::min(hostLen, kMaxCapValue - 2);
        domLen  = std::min(domLen, kMaxCapValue - 2 - hostLen);
    }
    return {hostLen, domLen};
}

/// Families the GR capability can encode: 2 flag/time bytes + 4 per family, within one parameter.
static size_t grFamilyCount(const Capabilities& caps)
{
    return std::min<size_t>(caps.gracefulFamilies.size(), (253 - 2) / 4);
}

/// Families the LLGR capability can encode: 7 bytes per family, within one parameter.
static size_t llgrFamilyCount(const Capabilities& caps)
{
    return std::min<size_t>(caps.llgrFamilies.size(), 253 / 7);
}

static uint16_t computeCapabilityLen(const Capabilities& caps)
{
    uint16_t capSize = 0;

    auto addParam = [&](uint8_t dataSize)
    {
        capSize += 4 + dataSize; // type(1) + len(1) + code(1) + len(1) + data
    };

    auto getFragSize = [](uint8_t blockSize) -> uint8_t
    {
        return 253 / blockSize;
    };

    auto getFragments = [](uint8_t fragSize, size_t blockCount) -> size_t
    {
        return (blockCount + fragSize - 1) / fragSize;
    };

    // Multiprotocol Extensions
    for (size_t i = 0; i < caps.mpFamilies.size(); ++i)
        addParam(4);

    // Route Refresh
    if (caps.routeRefresh)
        addParam(0);

    // ORF
    for (size_t i = 0; i < caps.orfEntries.size(); ++i)
        addParam(7);

    auto addFragmented = [&](size_t entryCount, uint8_t blockSize)
    {
        if (entryCount == 0)
            return;
        const uint8_t fragSize = getFragSize(blockSize);
        const size_t frags = getFragments(fragSize, entryCount);
        for (size_t f = 0; f < frags; ++f)
        {
            const size_t inFrag = std::min<size_t>(fragSize, entryCount - f * fragSize);
            addParam(static_cast<uint8_t>(inFrag * blockSize));
        }
    };

    // Extended Next-Hop Encoding
    addFragmented(caps.extendedNextHopEntries.size(), 6);

    // Extended Message
    if (caps.extendedMessage)
        addParam(0);

    // Graceful Restart: single parameter carrying flags/time plus every family,
    // matching appendCapabilities.
    if (caps.gracefulRestart)
        addParam(static_cast<uint8_t>(2 + grFamilyCount(caps) * 4));

    // 4-byte ASN
    if (caps.asn32bit)
        addParam(4);

    // MULTI-SESSION
    addFragmented(caps.multiSessionFamilies.size(), 4);

    // ADD-PATH
    addFragmented(caps.addPathFamilies.size(), 4);

    // Enhanced Route Refresh
    if (caps.enhancedRouteRefresh)
        addParam(0);

    // LLGR: single parameter carrying every family, matching appendCapabilities.
    if (caps.llgr && !caps.llgrFamilies.empty())
        addParam(static_cast<uint8_t>(llgrFamilyCount(caps) * 7));

    // FQDN
    if (caps.fqdn)
    {
        const auto [hostLen, domLen] = fqdnLengths(caps);
        addParam(static_cast<uint8_t>(1 + hostLen + 1 + domLen));
    }

    // Link-local next hop
    if (caps.linkLocalNextHop)
        addParam(0);

    return capSize;
}

static void appendCapabilities(const Capabilities& caps, transport::tcp::Connection& c)
{
    auto openParam = [&](uint8_t code, uint8_t dataSize) -> uint8_t*
    {
        assert(dataSize + 2 <= 253);
        uint8_t siz = dataSize + 4;
        std::span<uint8_t> buf = c.reserveSpan(siz);
        buf[0] = BGP_PARAMETER_CAPABILITY;
        buf[1] = dataSize + 2;
        buf[2] = code;
        buf[3] = dataSize;
        c.commit(siz);
        return buf.data() + 4;
    };

    auto getFragSize = [](uint8_t blockSize) -> uint8_t
    {
        return 253 / blockSize;
    };

    auto getFragments = [](uint8_t fragSize, size_t blockCount) -> size_t
    {
        return (blockCount + fragSize - 1) / fragSize;
    };

    // Multiprotocol Extensions
    for (const auto& fam : caps.mpFamilies)
    {
        uint8_t* buf = openParam(BGP_CAPABILITY_MULTIPROTOCOL, 4);
        utils::write<uint16_t>(buf, fam.afi);
        buf[2] = 0;
        buf[3] = fam.safi;
    }

    // Route Refresh
    if (caps.routeRefresh)
        openParam(BGP_CAPABILITY_ROUTE_REFRESH, 0);

    // ORF
    for (const auto& entry : caps.orfEntries)
    {
        uint8_t* buf = openParam(BGP_CAPABILITY_OUTBOUND_FILTER, 7);
        utils::write<uint16_t>(buf, entry.family.afi);
        buf[2] = 0;
        buf[3] = entry.family.safi;
        buf[4] = 1;
        buf[5] = entry.orfType;
        buf[6] = entry.sendReceive;
    }

    // Extended Next-Hop Encoding
    if (!caps.extendedNextHopEntries.empty())
    {
        uint8_t fragSize = getFragSize(6);
        const size_t totalEntries = caps.extendedNextHopEntries.size();
        const size_t totalFragments = getFragments(fragSize, totalEntries);

        for (size_t f = 0; f < totalFragments; ++f)
        {
            size_t startIdx = f * fragSize;
            size_t entriesInThisFrag = std::min<size_t>(fragSize, totalEntries - startIdx);

            uint8_t* frag = openParam(BGP_CAPABILITY_EXTENDED_NEXT_HOP, static_cast<uint8_t>(entriesInThisFrag * 6));

            for (size_t e = 0; e < entriesInThisFrag; ++e)
            {
                const auto& entry = caps.extendedNextHopEntries[startIdx + e];
                uint8_t* buf = frag + (e * 6);

                utils::write<uint16_t>(buf, entry.family.afi);
                utils::write<uint16_t>(buf + 2, static_cast<uint16_t>(entry.family.safi));
                utils::write<uint16_t>(buf + 4, entry.nextHopAfi);
            }
        }
    }

    // Extended Message
    if (caps.extendedMessage)
        openParam(BGP_CAPABILITY_EXTENDED_MESSAGE, 0);

    // Graceful Restart
    if (caps.gracefulRestart)
    {
        const size_t count = grFamilyCount(caps);
        const uint8_t vlen = static_cast<uint8_t>(2 + count * 4);
        uint8_t* buf = openParam(BGP_CAPABILITY_GRACEFUL_RESTART, vlen);
        uint16_t flagsTime = caps.restartTime & 0x0FFF;
        if (caps.restarting) flagsTime |= 0x8000;
        utils::write<uint16_t>(buf, flagsTime);

        size_t p = 2;
        for (size_t i = 0; i < count; ++i)
        {
            const auto& gf = caps.gracefulFamilies[i];
            utils::write<uint16_t>(buf + p, gf.family.afi);
            buf[p + 2] = gf.family.safi;
            buf[p + 3] = gf.forwardingStatePreserved ? 0x80 : 0x00;
            p += 4;
        }
    }

    // 4-byte ASN
    if (caps.asn32bit)
    {
        uint8_t* buf = openParam(BGP_CAPABILITY_32_BIT_AS, 4);
        utils::write<uint32_t>(buf, caps.asn);
    }

    // MULTI-SESSION
    if (!caps.multiSessionFamilies.empty())
    {
        uint8_t fragSize = getFragSize(4);
        const size_t totalEntries = caps.multiSessionFamilies.size();
        const size_t totalFragments = getFragments(fragSize, totalEntries);

        for (size_t f = 0; f < totalFragments; ++f)
        {
            size_t startIdx = f * fragSize;
            size_t entriesInThisFrag = std::min<size_t>(fragSize, totalEntries - startIdx);

            uint8_t* frag = openParam(BGP_CAPABILITY_MULTI_SESSION, static_cast<uint8_t>(entriesInThisFrag * 4));

            for (size_t e = 0; e < entriesInThisFrag; ++e)
            {
                const auto& entry = caps.multiSessionFamilies[startIdx + e];
                uint8_t* buf = frag + (e * 4);

                utils::write<uint16_t>(buf, entry.afi);
                buf[2] = entry.safi;
                buf[3] = 0;
            }
        }
    }

    // ADD-PATH
    if (!caps.addPathFamilies.empty())
    {
        uint8_t fragSize = getFragSize(4);
        const size_t totalEntries = caps.addPathFamilies.size();
        const size_t totalFragments = getFragments(fragSize, totalEntries);

        for (size_t f = 0; f < totalFragments; ++f)
        {
            size_t startIdx = f * fragSize;
            size_t entriesInThisFrag = std::min<size_t>(fragSize, totalEntries - startIdx);

            uint8_t* frag = openParam(BGP_CAPABILITY_ADD_PATH, static_cast<uint8_t>(entriesInThisFrag * 4));

            for (size_t e = 0; e < entriesInThisFrag; ++e)
            {
                const auto& entry = caps.addPathFamilies[startIdx + e];
                uint8_t* buf = frag + (e * 4);

                utils::write<uint16_t>(buf, entry.family.afi);
                buf[2] = entry.family.safi;
                buf[3] = entry.sendReceive;
            }
        }
    }

    // Enhanced Route Refresh
    if (caps.enhancedRouteRefresh)
        openParam(BGP_CAPABILITY_ENHANCED_ROUTE_REFRESH, 0);

    // LLGR
    if (caps.llgr && !caps.llgrFamilies.empty())
    {
        const size_t count = llgrFamilyCount(caps);
        const uint8_t vlen = static_cast<uint8_t>(count * 7);
        uint8_t* buf = openParam(BGP_CAPABILITY_LLGR, vlen);

        size_t idx = 0;
        for (size_t i = 0; i < count; ++i)
        {
            const auto& lf = caps.llgrFamilies[i];
            utils::write<uint16_t>(buf + idx, lf.family.afi);
            buf[idx + 2] = lf.family.safi;
            buf[idx + 3] = lf.flags;
            utils::write<uint32_t, 3>(buf + idx + 4, lf.staleTime & 0x00FFFFFF);
            idx += 7;
        }
    }

    // FQDN
    if (caps.fqdn)
    {
        // Each name is length-prefixed by one byte and the whole capability must fit
        // in a single parameter, so clamp both to what the encoding can carry.
        const auto [hostLen, domLen] = fqdnLengths(caps);

        uint8_t vlen = static_cast<uint8_t>(1 + hostLen + 1 + domLen);
        uint8_t* buf = openParam(BGP_CAPABILITY_FQDN, vlen);
        buf[0] = static_cast<uint8_t>(hostLen);
        std::memcpy(buf + 1, caps.hostname.data(), hostLen);
        size_t off = 1 + hostLen;
        buf[off] = static_cast<uint8_t>(domLen);
        std::memcpy(buf + off + 1, caps.domain.data(), domLen);
    }

    // Link-local next hop
    if (caps.linkLocalNextHop)
        openParam(BGP_CAPABILITY_LINK_LOCAL_NEXT_HOP, 0);
}

/**
 * @brief Splits AS-PATH segments so none exceeds the 255-ASN wire limit.
 *
 * The segment length field is a single byte (RFC 4271 4.3), so a segment holding
 * more than 255 ASNs cannot be encoded. Prepending can push a segment past that
 * bound; splitting preserves both the ASN order and the effective path length.
 */
static std::vector<AsPathSegment> splitAsPathSegments(const std::vector<AsPathSegment>& segs)
{
    constexpr size_t kMaxAsnPerSegment = 255;

    std::vector<AsPathSegment> out;
    out.reserve(segs.size());

    for (const auto& seg : segs)
    {
        if (seg.asns.size() <= kMaxAsnPerSegment)
        {
            out.push_back(seg);
            continue;
        }
        for (size_t off = 0; off < seg.asns.size(); off += kMaxAsnPerSegment)
        {
            const size_t n = std::min(kMaxAsnPerSegment, seg.asns.size() - off);
            AsPathSegment part;
            part.segmentType = seg.segmentType;
            part.asns.assign(seg.asns.begin() + off, seg.asns.begin() + off + n);
            out.push_back(std::move(part));
        }
    }
    return out;
}

void BgpTx::appendAttrHdr(uint8_t flags, uint8_t type, size_t valueLen, size_t& attrsSize, transport::tcp::Connection& c)
{
    attrsSize += valueLen;

    if (valueLen > 255)
    {
        flags |= BGP_ATTR_FLAG_EXTENDED_LENGTH;
        auto buf = c.reserveSpan(4);
        attrsSize += 4;
        buf[0] = flags;
        buf[1] = type;
        utils::write<uint16_t>(buf.data() + 2, static_cast<uint16_t>(valueLen));
        c.commit(4);
    }
    else
    {
        auto buf = c.reserveSpan(3);
        attrsSize += 3;
        buf[0] = flags;
        buf[1] = type;
        buf[2] = static_cast<uint8_t>(valueLen);
        c.commit(3);
    }
};

size_t BgpTx::appendPathAttrs(const Session& session, const PathAttribute& pa, transport::tcp::Connection& c)
{
    size_t attrSize = 0;

    const bool use4 = session.getNegotiated().asn32bit;
    const bool ebgp = session.neighbor.isEbgp();

    // ORIGIN
    if (pa.attrs.origin.has_value())
    {
        appendAttrHdr(BGP_ATTR_FLAG_TRANSITIVE, BGP_ATTR_ORIGIN, 1, attrSize, c);
        auto buf = c.reserveSpan(1);
        buf[0] = *pa.attrs.origin;
        c.commit(1);
    }

    // AS PATH
    if (!pa.attrs.asPath.empty())
    {
        const std::vector<AsPathSegment> asSegs = splitAsPathSegments(pa.attrs.asPath);

        size_t asLen = 0;
        for (const auto& seg : asSegs)
            asLen += 2 + seg.asns.size() * (use4 ? 4u : 2u);
        appendAttrHdr(BGP_ATTR_FLAG_TRANSITIVE, BGP_ATTR_AS_PATH, asLen, attrSize, c);
        for (const auto& seg : asSegs)
        {
            auto hdr = c.reserveSpan(2);
            hdr[0] = seg.segmentType;
            hdr[1] = static_cast<uint8_t>(seg.asns.size());
            c.commit(2);

            for (uint32_t asn : seg.asns)
            {
                if (use4)
                {
                    auto buf = c.reserveSpan(4);
                    utils::write<uint32_t>(buf.data(), asn);
                    c.commit(4);
                }
                else
                {
                    const uint16_t a2 = (asn > 65535)
                        ? static_cast<uint16_t>(kAsTrans)
                        : static_cast<uint16_t>(asn);
                    auto buf = c.reserveSpan(2);
                    utils::write<uint16_t>(buf.data(), a2);
                    c.commit(2);
                }
            }
        }
    }

    if (!use4)
    {
        const bool hasWideAsn = std::any_of(
            pa.attrs.asPath.begin(), pa.attrs.asPath.end(),
            [](const AsPathSegment& s) {
                return std::any_of(s.asns.begin(), s.asns.end(),
                                   [](uint32_t asn) { return asn > 65535; });
            });

        std::vector<AsPathSegment> as4;
        if (hasWideAsn)
            as4 = splitAsPathSegments(pa.attrs.asPath);

        if (!as4.empty())
        {
            size_t as4Len = 0;
            for (const auto& seg : as4)
                as4Len += 2 + seg.asns.size() * 4u;

            appendAttrHdr(BGP_ATTR_FLAG_TRANSITIVE | BGP_ATTR_FLAG_OPTIONAL, BGP_ATTR_AS4_PATH, as4Len, attrSize, c);

            for (const auto& seg : as4)
            {
                auto hdr = c.reserveSpan(2);
                hdr[0] = seg.segmentType;
                hdr[1] = static_cast<uint8_t>(seg.asns.size());
                c.commit(2);

                for (uint32_t asn : seg.asns)
                {
                    auto buf = c.reserveSpan(4);
                    utils::write<uint32_t>(buf.data(), asn);
                    c.commit(4);
                }
            }
        }
    }

    // NEXT HOP
    if (pa.path.nextHop.isIPv4())
    {
        appendAttrHdr(BGP_ATTR_FLAG_TRANSITIVE, BGP_ATTR_NEXT_HOP, 4, attrSize, c);
        auto buf = c.reserveSpan(4);
        utils::write<uint32_t>(buf.data(), pa.path.nextHop.v4());
        c.commit(4);
    }

    // MED
    if (pa.attrs.med.has_value())
    {
        appendAttrHdr(BGP_ATTR_FLAG_OPTIONAL, BGP_ATTR_MULTI_EXIT_DISC, 4, attrSize, c);
        auto buf = c.reserveSpan(4);
        utils::write<uint32_t>(buf.data(), *pa.attrs.med);
        c.commit(4);
    }

    // LOCAL PREF
    if (pa.attrs.localPref.has_value() && !ebgp)
    {
        appendAttrHdr(BGP_ATTR_FLAG_TRANSITIVE, BGP_ATTR_LOCAL_PREF, 4, attrSize, c);
        auto buf = c.reserveSpan(4);
        utils::write<uint32_t>(buf.data(), *pa.attrs.localPref);
        c.commit(4);
    }

    // ATOMIC AGGREGATE
    if (pa.attrs.atomicAggregate)
        appendAttrHdr(BGP_ATTR_FLAG_TRANSITIVE, BGP_ATTR_ATOMIC_AGGREGATE, 0, attrSize, c);

    // AGGREGATE
    if (pa.attrs.asAggregator.has_value())
    {
        const auto& agg = *pa.attrs.asAggregator;
        const size_t vlen = use4 ? 8u : 6u;
        appendAttrHdr(BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANSITIVE, BGP_ATTR_AGGREGATOR, vlen, attrSize, c);

        if (use4)
        {
            auto buf = c.reserveSpan(8);
            utils::write<uint32_t>(buf.data(), agg.asn);
            utils::write<uint32_t>(buf.data() + 4, agg.speaker.v4());
            c.commit(8);
        }
        else
        {
            const uint16_t a2 = (agg.asn > 65535)
                ? static_cast<uint16_t>(kAsTrans)
                : static_cast<uint16_t>(agg.asn);
            auto buf = c.reserveSpan(6);
            utils::write<uint16_t>(buf.data(), a2);
            utils::write<uint32_t>(buf.data() + 2, agg.speaker.v4());
            c.commit(6);

            // AS4 AGGREGATOR
            if (agg.asn > 65535)
            {
                appendAttrHdr(BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANSITIVE, BGP_ATTR_AS4_AGGREGATOR, 8, attrSize, c);
                auto buf4 = c.reserveSpan(8);
                utils::write<uint32_t>(buf4.data(), agg.asn);
                utils::write<uint32_t>(buf4.data() + 4, agg.speaker.v4());
                c.commit(8);
            }
        }
    }

    // COMMUNITIES
    if (!pa.attrs.communities.empty())
    {
        appendAttrHdr(BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANSITIVE, BGP_ATTR_COMMUNITIES, pa.attrs.communities.size() * 4, attrSize, c);
        for (uint32_t comm : pa.attrs.communities)
        {
            auto buf = c.reserveSpan(4);
            utils::write<uint32_t>(buf.data(), comm);
            c.commit(4);
        }
    }

    // ORIGINATOR ID
    if (pa.attrs.originatorId.has_value())
    {
        appendAttrHdr(BGP_ATTR_FLAG_OPTIONAL, BGP_ATTR_ORIGINATOR_ID, 4, attrSize, c);
        auto buf = c.reserveSpan(4);
        utils::write<uint32_t>(buf.data(), *pa.attrs.originatorId);
        c.commit(4);
    }

    // CLUSTER LIST
    if (!pa.attrs.clusterList.empty())
    {
        appendAttrHdr(BGP_ATTR_FLAG_OPTIONAL, BGP_ATTR_CLUSTER_LIST, pa.attrs.clusterList.size() * 4, attrSize, c);
        for (uint32_t cid : pa.attrs.clusterList)
        {
            auto buf = c.reserveSpan(4);
            utils::write<uint32_t>(buf.data(), cid);
            c.commit(4);
        }
    }

    // EXTENDED COMMUNITIES
    if (!pa.attrs.extendedCommunities.empty())
    {
        appendAttrHdr(BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANSITIVE,
                      BGP_ATTR_EXTENDED_COMMUNITIES, pa.attrs.extendedCommunities.size() * 8, attrSize, c);
        for (const auto& ec : pa.attrs.extendedCommunities)
        {
            auto buf = c.reserveSpan(8);
            utils::write<uint64_t>(buf.data(), ec);
            c.commit(8);
        }
    }

    // AIGP
    if (pa.attrs.aigp.has_value())
    {
        appendAttrHdr(BGP_ATTR_FLAG_OPTIONAL, BGP_ATTR_AIGP, 11, attrSize, c);
        auto buf = c.reserveSpan(11);
        buf[0] = 1;
        utils::write<uint16_t>(buf.data() + 1, 11);
        utils::write<uint64_t>(buf.data() + 3, *pa.attrs.aigp);
        c.commit(11);
    }

    // LARGE COMMUNITIES
    if (!pa.attrs.largeCommunities.empty())
    {
        appendAttrHdr(BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANSITIVE,
                      BGP_ATTR_LARGE_COMMUNITIES, pa.attrs.largeCommunities.size() * 12, attrSize, c);
        for (const auto& lc : pa.attrs.largeCommunities)
        {
            auto buf = c.reserveSpan(12);
            utils::write<uint32_t>(buf.data(), lc[0]);
            utils::write<uint32_t>(buf.data() + 4, lc[1]);
            utils::write<uint32_t>(buf.data() + 8, lc[2]);
            c.commit(12);
        }
    }

    // UNKNOWN
    for (const auto& ua : pa.attrs.unknownTransitive)
    {
        appendAttrHdr(ua.flags | BGP_ATTR_FLAG_PARTIAL, ua.type, ua.value.size(), attrSize, c);
        auto buf = c.reserveSpan(ua.value.size());
        std::memcpy(buf.data(), ua.value.data(), ua.value.size());
        c.commit(ua.value.size());
    }

    return attrSize;
}

void BgpTx::buildOpen(transport::tcp::Connection& connection, Session& session)
{
    const auto& caps = session.getLocalCaps();
    const auto& proc = session.process;
    const uint32_t localAs = caps.asn != 0 ? caps.asn : proc.asNumber;
    const uint32_t rid = proc.getRouterId();

    uint16_t openSize = packet::BgpHeader::fixedSize + packet::BgpOpenHeader::fixedSize;
    std::span<uint8_t> buf = connection.reserveSpan(openSize);

    packet::BgpOpenHeader open;
    open.setBuffer(buf.data() + packet::BgpHeader::fixedSize);
    open.setVersion(BGP_VERSION);

    const uint16_t myAs2 = (localAs > 65535)
        ? static_cast<uint16_t>(kAsTrans)
        : static_cast<uint16_t>(localAs);

    open.setAsNumber(myAs2);
    open.setHoldTime(session.holdTime);
    open.setIdentifier(rid);

    uint16_t capSize = computeCapabilityLen(caps);
    const bool extended = capSize >= 255;

    open.setParameterLen(extended ? 255 : static_cast<uint8_t>(capSize));

    connection.commit(openSize);

    if (extended)
    {
        auto ext = connection.reserveSpan(2);
        utils::write<uint16_t>(ext.data(), capSize);
        connection.commit(2);
    }

    appendCapabilities(caps, connection);

    const uint16_t extLen = extended ? 2 : 0;
    buildHeader(BGP_TYPE_OPEN,
                static_cast<uint16_t>(packet::BgpOpenHeader::fixedSize + extLen + capSize),
                buf.data());
}

void BgpTx::buildNotification(transport::tcp::Connection& connection, const Notification& notification)
{
    if (notification.code == 0) return;

    const size_t maxData = kMaxMessageLen - packet::BgpHeader::fixedSize - 2;
    const size_t dataLen = std::min(notification.data.size(), maxData);

    uint16_t notifSize = static_cast<uint16_t>(2 + dataLen);
    uint16_t bgpSize = static_cast<uint16_t>(packet::BgpHeader::fixedSize + notifSize);
    std::span<uint8_t> buf = connection.reserveSpan(bgpSize);
    uint8_t* notif = buf.data() + packet::BgpHeader::fixedSize;
    utils::write<uint16_t>(notif, notification.code);
    if (dataLen)
        std::memcpy(notif + 2, notification.data.data(), dataLen);
    buildHeader(BGP_TYPE_NOTIFICATION, notifSize, buf.data());
    connection.commit(bgpSize);
}

void BgpTx::buildKeepalive(transport::tcp::Connection& connection)
{
    std::span<uint8_t> buf =  connection.reserveSpan(packet::BgpHeader::fixedSize);
    buildHeader(BGP_TYPE_KEEPALIVE, 0, buf.data());
    connection.commit(packet::BgpHeader::fixedSize);
}

void BgpTx::buildRouteRefresh(transport::tcp::Connection& connection, Session& session,
    const AfiSafi& family, RouteRefreshReason reason)
{
    const auto& neg = session.getNegotiated();

    // Downgrade BORR/EORR to Normal if enhanced route refresh was not negotiated.
    uint8_t subtype = static_cast<uint8_t>(reason);
    if (!neg.enhancedRR && reason != RouteRefreshReason::Normal)
        subtype = BGP_ROUTE_REFRESH_NORMAL;

    // Append ORF TLV if we have an outbound filter and ORF is negotiated.
    static const std::vector<OrfPrefixEntry> kNoOrf;
    NeighborAf* afNbr = session.neighbor.findAfNeighbor(family);
    const auto& orfOutbound = afNbr ? afNbr->orfOutbound : kNoOrf;
    const bool sendOrf = !orfOutbound.empty() && neg.canSendOrf(family, BGP_ORF_TYPE_PREFIX_LIST);

    if (sendOrf)
    {
        uint16_t orfPayload = 0;
        for (const auto& e : orfOutbound)
            orfPayload += (e.action == BGP_ORF_ACTION_REMOVE_ALL)
                ? 1u : static_cast<uint16_t>(8 + (e.prefix.prefixLength + 7) / 8);

        auto hdrBuf = connection.reserveSpan(packet::BgpHeader::fixedSize);
        buildHeader(BGP_TYPE_ROUTE_REFRESH,
            static_cast<uint16_t>(4 + 3 + orfPayload), hdrBuf.data());
        connection.commit(packet::BgpHeader::fixedSize);

        {
            auto buf = connection.reserveSpan(4);
            utils::write<uint16_t>(buf.data(), family.afi);
            buf[2] = BGP_ORF_WHEN_IMMEDIATE;
            buf[3] = family.safi;
            connection.commit(4);
        }
        {
            auto buf = connection.reserveSpan(3);
            buf[0] = BGP_ORF_TYPE_PREFIX_LIST;
            utils::write<uint16_t>(buf.data() + 1, orfPayload);
            connection.commit(3);
        }
        for (const auto& e : orfOutbound)
        {
            uint8_t am = static_cast<uint8_t>((e.action << 6) | (e.match & 0x01));
            if (e.action == BGP_ORF_ACTION_REMOVE_ALL)
            {
                connection.reserveSpan(1)[0] = am;
                connection.commit(1);
                continue;
            }
            uint8_t pfxBytes = static_cast<uint8_t>((e.prefix.prefixLength + 7) / 8);
            size_t entrySize = static_cast<size_t>(8 + pfxBytes);
            auto buf = connection.reserveSpan(entrySize);
            buf[0] = am;
            utils::write<uint32_t>(buf.data() + 1, e.sequence);
            buf[5] = e.minLen;
            buf[6] = e.maxLen;
            buf[7] = e.prefix.prefixLength;
            if (pfxBytes > 0) {
                if (e.prefix.isIPv4())
                    utils::write<uint32_t>(buf.data() + 8, e.prefix.v4(), pfxBytes);
                else
                    utils::write<__uint128_t>(buf.data() + 8, e.prefix.v6(), pfxBytes);
            }
            connection.commit(entrySize);
        }
        return;
    }

    auto buf = connection.reserveSpan(packet::BgpHeader::fixedSize + 4);
    buildHeader(BGP_TYPE_ROUTE_REFRESH, 4, buf.data());
    uint8_t* rr = buf.data() + packet::BgpHeader::fixedSize;
    utils::write<uint16_t>(rr, family.afi);
    rr[2] = subtype;
    rr[3] = family.safi;
    connection.commit(packet::BgpHeader::fixedSize + 4);
}
} // namespace routing
