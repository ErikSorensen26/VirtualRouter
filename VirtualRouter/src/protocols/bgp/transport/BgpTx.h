// BgpTx.h

#ifndef BGP_TX_H
#define BGP_TX_H

#include <span>
#include <cstdint>

#include "bgp/BgpTypes.hpp"
#include "bgp/features/Capabilities.h"
#include "bgp/rib/RibTypes.hpp"
#include "bgp/session/Session.h"

struct BgpHeader;
class PacketBuilder;
namespace TCP
{
class RxConsumer;
class Connection;
}

namespace BGP
{
class Session;
class BgpProcess;

class BgpTx
{
public:
    explicit BgpTx(BgpProcess& proc);

    BgpTx(const BgpTx&) = delete;
    BgpTx& operator=(const BgpTx&) = delete;
    BgpTx(BgpTx&&) noexcept = delete;
    BgpTx& operator=(BgpTx&&) noexcept = delete;

    void buildOpen(Session& session); 
    template <typename N>
    void buildUpdate(Session& session, const ParsedUpdate<typename N::Nlri>& update);
    void buildNotification(Session& session, const Notification& notification);
    void buildKeepalive(Session& session);
    void buildRouteRefresh(Session& session, const AfiSafi& family, uint8_t subType);

private:
    static void buildHeader(uint8_t type, uint16_t payloadSize, uint8_t* buf);
    static void appendAttrHdr(uint8_t flags, uint8_t type, size_t valueLen, size_t& attrsSize, TCP::Connection& c);
    static size_t appendNonNlriAttrs(const Session& session, const PathAttributeBase& attrs, TCP::Connection& c);

    template <typename N>
    static size_t appendPathAttrs(const Session& session, const PathAttribute<typename N::Nlri>& attrs, TCP::Connection& c);

    void buildUpdate(Session& session, const std::span<uint8_t> nlri, PathAttributeBase& attr);
};

template <typename N>
static size_t computeNlriLen(const std::vector<typename N::Nlri>& nlri, bool addPath)
{
    size_t total = 0;
    for (const auto& n : nlri)
        total += (addPath ? 4u : 0u) + N::nlriEncodedSize(n);
    return total;
}

template <typename N>
void appendNlri(const std::vector<typename N::Nlri>& nlri, bool addPath, TCP::Connection& c)
{
    for (const auto& n : nlri)
    {
        const size_t entrySize = (addPath ? 4u : 0u) + N::nlriEncodedSize(n);
        std::span<uint8_t> buf = c.reserveSpan(entrySize);
        if (addPath)
        {
            std::memset(buf.data(), 0, 3);
            buf[3] = 0x01;
        }
        N::encodeNlri(buf.data() + (addPath ? 4 : 0), n);
    }
}

template <typename N>
size_t BgpTx::appendPathAttrs(const Session& session, const PathAttribute<typename N::Nlri>& attrs, TCP::Connection& c)
{
    size_t attrSize = appendNonNlriAttrs(session, attrs, c);

    // MP REACH NLRI
    if (attrs.mpReach.has_value())
    {
        const MpReach<typename N::Nlri>& mp = *attrs.mpReach;
        const bool isV6 = (mp.family.afi == BGP_AFI_IPV6);
        const size_t nhLen = isV6 ? (mp.linkLocal.has_value() ? 32u : 16u) : 4u;

        bool addPath = false;
        for (const auto& ap : session.getNegotiated().addPathFamilies)
            if (ap.family == mp.family) { addPath = true; break; }

        const size_t vlen = 5 + nhLen + computeNlriLen<N>(mp.nlri, addPath);
        appendAttrHdr(BGP_ATTR_FLAG_OPTIONAL, BGP_ATTR_MP_REACH_NLRI, vlen, attrSize, c);

        // AFI + SAFI
        {
            auto buf = c.reserveSpan(3);
            writeU16(buf.data(), mp.family.afi);
            buf[2] = mp.family.safi;
        }

        // Next-Hop length + bytes
        {
            auto buf = c.reserveSpan(1 + nhLen);
            buf[0] = static_cast<uint8_t>(nhLen);
            if (isV6)
            {
                std::memcpy(buf.data() + 1, mp.nextHop.raw, 16);
                if (mp.linkLocal.has_value())
                    std::memcpy(buf.data() + 17, mp.linkLocal.value().raw, 16);
            }
            else
            {
                std::memcpy(buf.data() + 1, mp.nextHop.raw, 4);
            }
        }

        {
            auto snpa = c.reserveSpan(1);
            snpa[0] = 0;
        }
        appendNlri<N>(mp.nlri, addPath, c);
    }

    // MP UNREACH NLRI
    if (attrs.mpUnreach.has_value())
    {
        const MpUnreach<typename N::Nlri>& mp = *attrs.mpUnreach;
        
        bool addPath = false;
        for (const auto& ap : session.getNegotiated().addPathFamilies)
            if (ap.family == mp.family) { addPath = true; break; }

        const size_t vlen = 3 + computeNlriLen<N>(mp.nlri, addPath);
        appendAttrHdr(BGP_ATTR_FLAG_OPTIONAL, BGP_ATTR_MP_UNREACH_NLRI, vlen, attrSize, c);
        
        {
            auto buf = c.reserveSpan(3);
            writeU16(buf.data(), mp.family.afi);
            buf[2] = mp.family.safi;
        }
        appendNlri(mp.nlri, addPath, c);
    }

    return attrSize;
}

template <typename N>
void BgpTx::buildUpdate(Session& session, const ParsedUpdate<typename N::Nlri>& update)
{
    TCP::Connection* c = session.getPrimaryConnection(); 
    if (!c) return;

    const auto& neg = session.getNegotiated();
    const AfiSafi ipv4uni { BGP_AFI_IPV4, BGP_SAFI_UNICAST };

    const size_t maxMsg = neg.extendedMessage ? 65535u : 4096u;
    const size_t msgOverhead = BgpHeader::fixedSize + 4;
    const size_t maxNlriPerMsg = maxMsg - msgOverhead;

    bool addPathV4 = false;
    for (const auto& ap : neg.addPathFamilies)
        if (ap.family == ipv4uni) { addPathV4 = true; break; }

    const size_t withdrawnBytes = computeNlriLen<N>(update.withdrawn, addPathV4);

    if (!update.withdrawn.empty())
    {
        size_t i = 0;
        while (i < update.withdrawn.size())
        {
            size_t batchBytes = 0;
            size_t batchStart = i;

            while (i < update.withdrawn.size())
            {
                const size_t entrySize = (addPathV4 ? 4u : 0u)
                    + N::nlriEncodedSize(update.withdrawn[i]);
                if (batchBytes + entrySize > maxNlriPerMsg)
                    break;
                batchBytes += entrySize;
                ++i;
            }

            auto hdrBuf = c->reserveSpan(BgpHeader::fixedSize);
            auto withdrawLenBuf = c->reserveSpan(2);

            for (size_t j = batchStart; j < i; ++j)
            {
                const size_t entrySize = (addPathV4 ? 4u : 0u)
                    + N::nlriEncodedSize(update.withdrawn[j]);
                auto buf = c->reserveSpan(entrySize);
                if (addPathV4)
                {
                    std::memset(buf.data(), 0, 3);
                    buf[3] = 0x01;
                }
                N::encodeNlri(buf.data() + (addPathV4 ? 4 : 0), update.withdrawn[j]);
            }

            auto attrLenBuf = c->reserveSpan(2);

            buildHeader(BGP_TYPE_UPDATE, static_cast<uint16_t>(batchBytes + 4), hdrBuf.data());
            writeU16(withdrawLenBuf.data(), static_cast<uint16_t>(batchBytes));
            writeU16(attrLenBuf.data(), 0);
        }
    }

    for (const auto& [attrs, prefixes] : update.announced)
    {
        const bool isLegacyV4 = !attrs.mpReach.has_value() && attrs.nextHop.has_value();

        if (!isLegacyV4)
        {
            auto hdrBuf = c->reserveSpan(BgpHeader::fixedSize);
            auto wdLenBuf = c->reserveSpan(2);
            auto attrLenBuf = c->reserveSpan(2);
            const size_t attrBytes = appendPathAttrs<N>(session, attrs, *c);

            const uint16_t payloadLen = 4 + attrBytes;
            buildHeader(BGP_TYPE_UPDATE, payloadLen, hdrBuf.data());
            writeU16(wdLenBuf.data(), 0);
            writeU16(attrLenBuf.data(), static_cast<uint16_t>(attrBytes));
            continue;
        }

        size_t i = 0;
        while (i < prefixes.size())
        {
            auto hdrBuf = c->reserveSpan(BgpHeader::fixedSize);
            auto wdLenBuf = c->reserveSpan(2);
            auto attrLenBuf = c->reserveSpan(2);
            const size_t attrBytes = appendPathAttrs<N>(session, attrs, *c);

            const size_t roomForNlri = maxMsg - msgOverhead - attrBytes;
            size_t batchBytes = 0;
            size_t batchStart = i;

            while (i < prefixes.size())
            {
                const size_t entrySize = (addPathV4 ? 4u : 0u)
                    + N::nlriEncodedSize(prefixes[i]);
                if (batchBytes + entrySize > roomForNlri)
                    break;
                batchBytes += entrySize;
                ++i;
            }

            for (size_t j = batchStart; j < i; ++i)
            {
                const size_t entrySize = (addPathV4 ? 4u : 0u)
                    + N::nlriEncodedSize(prefixes[j]);
                auto buf = c->reserveSpan(entrySize);
                if (addPathV4)
                {
                    std::memset(buf.data(), 0, 3);
                    buf[3] = 0x01;
                }
                N::encodeNlri(buf.data() + (addPathV4 ? 4 : 0), prefixes[j]);
            }

            const uint16_t payloadLen = 4 + attrBytes + batchBytes;
            buildHeader(BGP_TYPE_UPDATE, payloadLen, hdrBuf.data());
            writeU16(wdLenBuf.data(), 0);
            writeU16(attrLenBuf.data(), static_cast<uint16_t>(attrBytes));
        }
    }
}
}

#endif // BGP_TX_H
