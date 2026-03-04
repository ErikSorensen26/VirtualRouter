// BgpTx.cpp

#include <vector>

#include "packet/headers/embedded/bgp/BgpOpenHeader.hpp"
#include "Transmission.h"
#include "tcp/Connection.h"
#include "bgp/session/Session.h"
#include "bgp/neighbor/Neighbor.h"
#include "bgp/BgpProcess.h"

/*
    void buildOpen(Session& session); 
    template <typename N>
    void buildUpdate(Session& session, const ParsedUpdate<N>& update);
    void buildNotification(Session& session, const Notification& notification);
    void buildKeepalive(Session& session);
    void buildRouteRefresh(Session& session, const AfiSafi& family);
    */

namespace BGP
{
static void buildHeader(uint8_t type, uint16_t payloadSize, uint8_t* buf)
{
    BgpHeader hdr;

    hdr.setBuffer(buf);
    hdr.setMarker();
    hdr.setType(type);
    hdr.setLength(payloadSize + BgpHeader::fixedSize);
}

static size_t computeCapability(const Capabilities& caps)
{
    size_t total = 0;
    size_t curUsed = 0;
    bool paramOpen = false;

    auto openParam = [&]()
    {
        total += 2;
        curUsed = 0;
        paramOpen = true;
    };

    auto ensureRoom = [&](size_t tlvBytes)
    {
        if (!paramOpen)
            openParam();
        else if (curUsed + tlvBytes > 255)
            openParam();
    };

    auto addTlv = [&](size_t valueLen)
    {
        const size_t tlvBytes = 2 + valueLen;
        ensureRoom(tlvBytes);
        total += tlvBytes;
        curUsed += tlvBytes;
    };

    // Multiprotocol Extension
    for (size_t i = 0; i < caps.mpFamilies.size(); ++i)
        addTlv(4);

    // Route Refresh
    if (caps.routeRefresh)
        addTlv(0);

    // ORF
    for (size_t i = 0; i < caps.orfEntries.size(); ++i)
        addTlv(7);

    // Extended Next-Hop Encoding
    if (!caps.extendedNextHopEntries.empty())
        addTlv(caps.extendedNextHopEntries.size() * 6);

    // Extended Message
    if (caps.extendedMessage)
        addTlv(0);

    // Graceful Restart
    if (caps.gracefulRestart)
        addTlv(2 + caps.gracefulFamilies.size() * 4);

    // 4-byte ASN
    if (caps.enhancedRouteRefresh)
        addTlv(0);

    // LLGR
    if (caps.llgr && !caps.llgrFamilies.empty())
        addTlv(caps.llgrFamilies.size() * 7);

    return total;
}

static uint16_t appendCapabilities(const Capabilities& caps, size_t outLen, TCP::Connection& c)
{
    uint16_t capSize = 0;

    auto openParam = [&](uint8_t code, uint8_t dataSize) -> uint8_t*
    {
        assert(dataSize + 2 <= 255);
        uint8_t siz = dataSize + 4;
        std::span<uint8_t> buf = c.reserveSpan(siz);
        capSize += siz;
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
        writeU16(buf, fam.afi);
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
        writeU16(buf, entry.family.afi);
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

                writeU16(buf, entry.family.afi);
                writeU16(buf + 2, static_cast<uint16_t>(entry.family.safi));
                writeU16(buf + 4, entry.nextHopAfi);
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
        writeU16(buf, flagsTime);

        size_t p = 2;
        for (const auto& gf : caps.gracefulFamilies)
        {
            writeU16(buf + p, gf.family.afi);
            buf[p + 2] = gf.family.safi;
            buf[p + 3] = gf.forwardingStatePreserved ? 0x80 : 0x00;
            p += 4;
        }
    }

    // 4-byte ASN
    if (caps.asn32bit)
    {
        uint8_t* buf = openParam(BGP_CAPABILITY_32_BIT_AS, 4);
        writeU32(buf, caps.asn);
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

                writeU16(buf, entry.family.afi);
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
        const uint8_t vlen = static_cast<uint8_t>(caps.llgrFamilies.size() * 4);
        uint8_t* buf = openParam(BGP_CAPABILITY_LLGR, vlen);

        size_t idx = 0;
        for (const auto& lf : caps.llgrFamilies)
        {
            writeU16(buf + idx, lf.family.afi);
            buf[idx + 2] = lf.family.safi;
            buf[idx + 3] = lf.flags;
            writeU24(buf + idx + 4, lf.staleTime & 0x00FFFFFF);
            idx += 7;
        }
    }

    return capSize;
}

void Transmission::buildOpen(Session& session)
{
    TCP::Connection* c = session.getPrimaryConnection();
    if (!c) return;

    const auto& caps = session.getLocalCaps();
    const auto& proc = session.getNeighbor().getProcess();
    const uint32_t localAs = proc.asNumber;
    const uint32_t rid = proc.rid;

    uint16_t openSize = BgpHeader::fixedSize + BgpOpenHeader::fixedSize;
    std::span<uint8_t> buf = c->reserveSpan(openSize);

    BgpOpenHeader open;
    open.setBuffer(buf.data() + BgpHeader::fixedSize);
    open.setVersion(BGP_VERSION);

    const uint16_t myAs2 = (localAs > 65535)
        ? static_cast<uint16_t>(kAsTrans)
        : static_cast<uint16_t>(localAs);

    open.setAsNumber(myAs2);
    open.setHoldTime(session.holdTime);
    open.setIdentifier(rid);

    uint8_t paramLen = appendCapabilities(caps, *c);
    open.setParameterLen(paramLen);

    buildHeader(BGP_TYPE_OPEN, openSize + paramLen, buf.data());
}

void buildUpdate(Session& session, const std::span<uint8_t> nlri, PathAttributeBase& attr)
{
    TCP::Connection* c = session.getPrimaryConnection();
    if (!c) return;
    BgpHeader bgp = buildHeader(BGP_TYPE_OPEN, *c);


}

void Transmission::buildNotification(Session& session, const Notification& notification)
{
    TCP::Connection* c = session.getPrimaryConnection();
    if (!c) return;

    uint16_t notifSize = static_cast<uint8_t>(2 + notification.data.size());
    uint16_t bgpSize = static_cast<uint16_t>(BgpHeader::fixedSize + notifSize);
    std::span<uint8_t> buf = c->reserveSpan(bgpSize);
    uint8_t* notif = buf.data() + BgpHeader::fixedSize;
    writeU16(notif, notification.code);
    std::memcpy(notif + 2, notification.data.data(), notification.data.size());
    buildHeader(BGP_TYPE_NOTIFICATION, notifSize, buf.data());
}

void Transmission::buildKeepalive(Session& session)
{
    TCP::Connection* c = session.getPrimaryConnection();
    if (!c) return;
    std::span<uint8_t> buf =  c->reserveSpan(BgpHeader::fixedSize);
    buildHeader(BGP_TYPE_KEEPALIVE, 0, buf.data());
}

void Transmission::buildRouteRefresh(Session& session, const AfiSafi& family)
{
    TCP::Connection* c = session.getPrimaryConnection();
    if (!c) return;
    BgpHeader bgp = buildHeader(BGP_TYPE_ROUTE_REFRESH, *c);

}
}
