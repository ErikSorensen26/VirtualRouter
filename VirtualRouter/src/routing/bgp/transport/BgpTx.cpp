// BgpTx.cpp

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

    // Extended Next-Hop Encoding
    if (!caps.extendedNextHopEntries.empty())
    {
        uint8_t fragSize = getFragSize(6);
        size_t frags = getFragments(fragSize, caps.extendedNextHopEntries.size());
        for (size_t f = 0; f < frags - 1; ++f)
            addParam(fragSize * 6);
        // last fragment may be smaller
        size_t remainder = caps.extendedNextHopEntries.size() % fragSize;
        addParam(static_cast<uint8_t>((remainder == 0 ? fragSize : remainder) * 6));
    }

    // Extended Message
    if (caps.extendedMessage)
        addParam(0);

    // Graceful Restart
    if (caps.gracefulRestart)
    {
        uint8_t fragSize = getFragSize(4);
        const size_t totalEntries = caps.gracefulFamilies.size();
        const size_t frags = getFragments(fragSize, totalEntries);
        for (size_t f = 0; f < frags - 1; ++f)
            addParam(2 + fragSize * 4); // flags/time only in first frag ideally
        size_t remainder = totalEntries % fragSize;
        addParam(static_cast<uint8_t>(2 + (remainder == 0 ? fragSize : remainder) * 4));
    }

    // 4-byte ASN
    if (caps.asn32bit)
        addParam(4);

    // MULTI-SESSION
    if (!caps.multiSessionFamilies.empty())
    {
        uint8_t fragSize = getFragSize(4);
        size_t frags = getFragments(fragSize, caps.multiSessionFamilies.size());
        for (size_t f = 0; f < frags - 1; ++f)
            addParam(fragSize * 4);
        size_t remainder = caps.multiSessionFamilies.size() % fragSize;
        addParam(static_cast<uint8_t>((remainder == 0 ? fragSize : remainder) * 4));
    }

    // ADD-PATH
    if (!caps.addPathFamilies.empty())
    {
        uint8_t fragSize = getFragSize(4);
        size_t frags = getFragments(fragSize, caps.addPathFamilies.size());
        for (size_t f = 0; f < frags - 1; ++f)
            addParam(fragSize * 4);
        size_t remainder = caps.addPathFamilies.size() % fragSize;
        addParam(static_cast<uint8_t>((remainder == 0 ? fragSize : remainder) * 4));
    }

    // Enhanced Route Refresh
    if (caps.enhancedRouteRefresh)
        addParam(0);

    // LLGR
    if (caps.llgr && !caps.llgrFamilies.empty())
    {
        uint8_t fragSize = getFragSize(7);
        size_t frags = getFragments(fragSize, caps.llgrFamilies.size());
        for (size_t f = 0; f < frags - 1; ++f)
            addParam(fragSize * 7);
        size_t remainder = caps.llgrFamilies.size() % fragSize;
        addParam(static_cast<uint8_t>((remainder == 0 ? fragSize : remainder) * 7));
    }

    // FQDN
    if (caps.fqdn)
        addParam(static_cast<uint8_t>(1 + caps.hostname.size() + 1 + caps.domain.size()));

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
        utils::writeU16(buf, fam.afi);
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
        utils::writeU16(buf, entry.family.afi);
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

                utils::writeU16(buf, entry.family.afi);
                utils::writeU16(buf + 2, static_cast<uint16_t>(entry.family.safi));
                utils::writeU16(buf + 4, entry.nextHopAfi);
            }
        }
    }

    // Extended Message
    if (caps.extendedMessage)
        openParam(BGP_CAPABILITY_EXTENDED_MESSAGE, 0);

    // Graceful Restart
    if (caps.gracefulRestart)
    {
        const uint8_t vlen = static_cast<uint8_t>(2 + caps.gracefulFamilies.size() * 4);
        uint8_t* buf = openParam(BGP_CAPABILITY_GRACEFUL_RESTART, vlen);
        uint16_t flagsTime = caps.restartTime & 0x0FFF;
        if (caps.restarting) flagsTime |= 0x8000;
        utils::writeU16(buf, flagsTime);

        size_t p = 2;
        for (const auto& gf : caps.gracefulFamilies)
        {
            utils::writeU16(buf + p, gf.family.afi);
            buf[p + 2] = gf.family.safi;
            buf[p + 3] = gf.forwardingStatePreserved ? 0x80 : 0x00;
            p += 4;
        }
    }

    // 4-byte ASN
    if (caps.asn32bit)
    {
        uint8_t* buf = openParam(BGP_CAPABILITY_32_BIT_AS, 4);
        utils::writeU32(buf, caps.asn);
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

                utils::writeU16(buf, entry.afi);
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

                utils::writeU16(buf, entry.family.afi);
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
        const uint8_t vlen = static_cast<uint8_t>(caps.llgrFamilies.size() * 7);
        uint8_t* buf = openParam(BGP_CAPABILITY_LLGR, vlen);

        size_t idx = 0;
        for (const auto& lf : caps.llgrFamilies)
        {
            utils::writeU16(buf + idx, lf.family.afi);
            buf[idx + 2] = lf.family.safi;
            buf[idx + 3] = lf.flags;
            utils::writeU24(buf + idx + 4, lf.staleTime & 0x00FFFFFF);
            idx += 7;
        }
    }

    // FQDN
    if (caps.fqdn)
    {
        uint8_t vlen = static_cast<uint8_t>(1 + caps.hostname.size() + 1 + caps.domain.size());
        uint8_t* buf = openParam(BGP_CAPABILITY_FQDN, vlen);
        buf[0] = static_cast<uint8_t>(caps.hostname.size());
        std::memcpy(buf + 1, caps.hostname.data(), caps.hostname.size());
        size_t off = 1 + caps.hostname.size();
        buf[off] = static_cast<uint8_t>(caps.domain.size());
        std::memcpy(buf + off + 1, caps.domain.data(), caps.domain.size());
    }

    // Link-local next hop
    if (caps.linkLocalNextHop)
        openParam(BGP_CAPABILITY_LINK_LOCAL_NEXT_HOP, 0);
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
        utils::writeU16(buf.data() + 2, static_cast<uint16_t>(valueLen));
    }
    else
    {
        auto buf = c.reserveSpan(3);
        attrsSize += 3;
        buf[0] = flags;
        buf[1] = type;
        buf[2] = static_cast<uint8_t>(valueLen);
    }
};

size_t BgpTx::appendPathAttrs(const Session& session, const PathAttribute& pa, transport::tcp::Connection& c)
{
    size_t attrSize = 0;

    const bool use4 = session.getNegotiated().asn32bit;
    const bool ebgp = session.isEbgp();

    // ORIGIN
    if (pa.attrs.origin.has_value())
    {
        appendAttrHdr(BGP_ATTR_FLAG_TRANSITIVE, BGP_ATTR_ORIGIN, 1, attrSize, c);
        auto buf = c.reserveSpan(1);
        buf[0] = *pa.attrs.origin;
    }

    // AS PATH
    if (!pa.attrs.asPath.empty())
    {
        size_t asLen = 0;
        for (const auto& seg : pa.attrs.asPath)
            asLen += 2 + seg.asns.size() * (use4 ? 4u : 2u);
        appendAttrHdr(BGP_ATTR_FLAG_TRANSITIVE, BGP_ATTR_AS_PATH, asLen, attrSize, c);
        for (const auto& seg : pa.attrs.asPath)
        {
            auto hdr = c.reserveSpan(2);
            hdr[0] = seg.segmentType;
            hdr[1] = static_cast<uint8_t>(seg.asns.size());

            for (uint32_t asn : seg.asns)
            {
                if (use4)
                {
                    auto buf = c.reserveSpan(4);
                    utils::writeU32(buf.data(), asn);
                }
                else
                {
                    const uint16_t a2 = (asn > 65535)
                        ? static_cast<uint16_t>(kAsTrans)
                        : static_cast<uint16_t>(asn);
                    auto buf = c.reserveSpan(2);
                    utils::writeU16(buf.data(), a2);
                }
            }
        }
    }

    // AS4 PATH
    if (!use4)
    {
        std::vector<AsPathSegment> as4;
        for (const auto& seg : pa.attrs.as4Path)
        {
            AsPathSegment newSeg;
            newSeg.segmentType = seg.segmentType;

            for (uint32_t asn : seg.asns)
            {
                if (asn > 65535)
                    newSeg.asns.push_back(asn);
            }

            if (!newSeg.asns.empty())
                as4.push_back(std::move(newSeg));
        }

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

                for (uint32_t asn : seg.asns)
                {
                    auto buf = c.reserveSpan(4);
                    utils::writeU32(buf.data(), asn);
                }
            }
        }
    }

    // NEXT HOP
    {
        appendAttrHdr(BGP_ATTR_FLAG_TRANSITIVE, BGP_ATTR_NEXT_HOP, 4, attrSize, c);
        auto buf = c.reserveSpan(4);
        utils::writeU32(buf.data(), pa.path.nextHop.v4());
    }

    // MED
    if (pa.attrs.med.has_value())
    {
        appendAttrHdr(BGP_ATTR_FLAG_OPTIONAL, BGP_ATTR_MULTI_EXIT_DISC, 4, attrSize, c);
        auto buf = c.reserveSpan(4);
        utils::writeU32(buf.data(), *pa.attrs.med);
    }

    // LOCAL PREF
    if (pa.attrs.localPref.has_value() && !ebgp)
    {
        appendAttrHdr(BGP_ATTR_FLAG_TRANSITIVE, BGP_ATTR_LOCAL_PREF, 4, attrSize, c);
        auto buf = c.reserveSpan(4);
        utils::writeU32(buf.data(), *pa.attrs.localPref);
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
            utils::writeU32(buf.data(), agg.asn);
            utils::writeU32(buf.data() + 4, agg.speaker.v4());
        }
        else
        {
            const uint16_t a2 = (agg.asn > 65535)
                ? static_cast<uint16_t>(kAsTrans)
                : static_cast<uint16_t>(agg.asn);
            auto buf = c.reserveSpan(6);
            utils::writeU16(buf.data(), a2);
            utils::writeU32(buf.data() + 2, agg.speaker.v4());

            // AS4 AGGREGATOR
            if (agg.asn > 65535)
            {
                appendAttrHdr(BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANSITIVE, BGP_ATTR_AS4_AGGREGATOR, 8, attrSize, c);
                auto buf4 = c.reserveSpan(8);
                utils::writeU32(buf4.data(), agg.asn);
                utils::writeU32(buf4.data() + 4, agg.speaker.v4());
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
            utils::writeU32(buf.data(), comm);
        }
    }

    // ORIGINATOR ID
    if (pa.attrs.originatorId.has_value())
    {
        appendAttrHdr(BGP_ATTR_FLAG_OPTIONAL, BGP_ATTR_ORIGINATOR_ID, 4, attrSize, c);
        auto buf = c.reserveSpan(4);
        utils::writeU32(buf.data(), *pa.attrs.originatorId);
    }

    // CLUSTER LIST
    if (!pa.attrs.clusterList.empty())
    {
        appendAttrHdr(BGP_ATTR_FLAG_OPTIONAL, BGP_ATTR_CLUSTER_LIST, pa.attrs.clusterList.size() * 4, attrSize, c);
        for (uint32_t cid : pa.attrs.clusterList)
        {
            auto buf = c.reserveSpan(4);
            utils::writeU32(buf.data(), cid);
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
            utils::writeU64(buf.data(), ec);
        }
    }

    // AIGP
    if (pa.attrs.aigp.has_value())
    {
        appendAttrHdr(BGP_ATTR_FLAG_OPTIONAL, BGP_ATTR_AIGP, 11, attrSize, c);
        auto buf = c.reserveSpan(11);
        buf[0] = 1;
        utils::writeU16(buf.data() + 1, 11);
        utils::writeU64(buf.data() + 3, *pa.attrs.aigp);
    }

    // LARGE COMMUNITIES
    if (!pa.attrs.largeCommunities.empty())
    {
        appendAttrHdr(BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANSITIVE,
                      BGP_ATTR_LARGE_COMMUNITIES, pa.attrs.largeCommunities.size() * 12, attrSize, c);
        for (const auto& lc : pa.attrs.largeCommunities)
        {
            auto buf = c.reserveSpan(12);
            utils::writeU32(buf.data(), lc[0]);
            utils::writeU32(buf.data() + 4, lc[1]);
            utils::writeU32(buf.data() + 8, lc[2]);
        }
    }

    // UNKNOWN
    for (const auto& ua : pa.attrs.unknownTransitive)
    {
        appendAttrHdr(ua.flags | BGP_ATTR_FLAG_PARTIAL, ua.type, ua.value.size(), attrSize, c);
        auto buf = c.reserveSpan(ua.value.size());
        std::memcpy(buf.data(), ua.value.data(), ua.value.size());
    }

    return attrSize;
}

void BgpTx::buildOpen(transport::tcp::Connection& connection, Session& session)
{
    const auto& caps = session.getLocalCaps();
    const auto& proc = session.getNeighbor().getProcess();
    const uint32_t localAs = caps.asn;
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
    if (capSize >= 255)
    {
        open.setParameterLen(255);
        auto ext = connection.reserveSpan(2);
        utils::writeU16(ext.data(), capSize);
    }
    else
    {
        open.setParameterLen(static_cast<uint8_t>(capSize));
    }

    appendCapabilities(caps, connection);

    const uint16_t extLen = (capSize >= 255) ? 2 : 0;
    buildHeader(BGP_TYPE_OPEN,
                static_cast<uint16_t>(packet::BgpOpenHeader::fixedSize + extLen + capSize),
                buf.data());
}

void BgpTx::buildNotification(transport::tcp::Connection& connection, const Notification& notification)
{
    if (notification.code == 0) return;
    uint16_t notifSize = static_cast<uint8_t>(2 + notification.data.size());
    uint16_t bgpSize = static_cast<uint16_t>(packet::BgpHeader::fixedSize + notifSize);
    std::span<uint8_t> buf = connection.reserveSpan(bgpSize);
    uint8_t* notif = buf.data() + packet::BgpHeader::fixedSize;
    utils::writeU16(notif, notification.code);
    std::memcpy(notif + 2, notification.data.data(), notification.data.size());
    buildHeader(BGP_TYPE_NOTIFICATION, notifSize, buf.data());
}

void BgpTx::buildKeepalive(transport::tcp::Connection& connection)
{
    std::span<uint8_t> buf =  connection.reserveSpan(packet::BgpHeader::fixedSize);
    buildHeader(BGP_TYPE_KEEPALIVE, 0, buf.data());
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
    const auto& orfOutbound = session.getNeighbor().getAfNeighbor(family).orfOutbound;
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

        {
            auto buf = connection.reserveSpan(4);
            utils::writeU16(buf.data(), family.afi);
            buf[2] = BGP_ORF_WHEN_IMMEDIATE;
            buf[3] = family.safi;
        }
        {
            auto buf = connection.reserveSpan(3);
            buf[0] = BGP_ORF_TYPE_PREFIX_LIST;
            utils::writeU16(buf.data() + 1, orfPayload);
        }
        for (const auto& e : orfOutbound)
        {
            uint8_t am = static_cast<uint8_t>((e.action << 6) | (e.match & 0x01));
            if (e.action == BGP_ORF_ACTION_REMOVE_ALL)
            {
                connection.reserveSpan(1)[0] = am;
                continue;
            }
            uint8_t pfxBytes = static_cast<uint8_t>((e.prefix.prefixLength + 7) / 8);
            auto buf = connection.reserveSpan(static_cast<size_t>(8 + pfxBytes));
            buf[0] = am;
            utils::writeU32(buf.data() + 1, e.sequence);
            buf[5] = e.minLen;
            buf[6] = e.maxLen;
            buf[7] = e.prefix.prefixLength;
            if (pfxBytes > 0) {
                if (e.prefix.isIPv4())
                    utils::writeBytes(buf.data() + 8, e.prefix.v4(), pfxBytes);
                else
                    utils::writeBytes(buf.data() + 8, e.prefix.v6(), pfxBytes);
            }
        }
        return;
    }

    auto buf = connection.reserveSpan(packet::BgpHeader::fixedSize + 4);
    buildHeader(BGP_TYPE_ROUTE_REFRESH, 4, buf.data());
    uint8_t* rr = buf.data() + packet::BgpHeader::fixedSize;
    utils::writeU16(rr, family.afi);
    rr[2] = subtype;
    rr[3] = family.safi;
}
} // namespace routing
