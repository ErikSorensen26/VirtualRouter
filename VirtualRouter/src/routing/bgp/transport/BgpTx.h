// BgpTx.h

#ifndef BGP_TX_H
#define BGP_TX_H

#include <span>
#include <cstdint>

#include "bgp/BgpTypes.hpp"
#include "bgp/rib/RibTypes.hpp"
#include "bgp/session/Session.h"

namespace transport::tcp { class RxConsumer; class Connection; }

namespace routing::bgp
{
class BgpProcess;

class BgpTx
{
public:
    BgpTx() = delete;

    static void buildOpen(transport::tcp::Connection& connection, Session& session); 
    template <typename N>
    static void buildUpdate(transport::tcp::Connection& connection, Session& session, const BuildUpdate<typename N::Nlri>& update);
    static void buildNotification(transport::tcp::Connection& connection, const Notification& notification);
    static void buildKeepalive(transport::tcp::Connection& connection);
    static void buildRouteRefresh(transport::tcp::Connection& connection, Session& session,
        const AfiSafi& family, RouteRefreshReason reason = RouteRefreshReason::Normal);

private:
    static void buildHeader(uint8_t type, uint16_t payloadSize, uint8_t* buf);
    static void appendAttrHdr(uint8_t flags, uint8_t type, size_t valueLen, size_t& attrsSize, transport::tcp::Connection& c);
    static size_t appendPathAttrs(const Session& session, const PathAttribute& pa, transport::tcp::Connection& c);

    template <typename N>
    static size_t appendMpReach(const Session& session, size_t& attrSize, size_t nlriIdx, size_t maxMsg,
        const typename BuildUpdate<typename N::Nlri>::Announcement& update, transport::tcp::Connection& c);

    template <typename N>
    static size_t appendMpUnreach(const Session& session, size_t& attrSize, size_t withdrawIdx, size_t maxMsg,
        const MpUnreach& unreach, std::span<const NlriPath<typename N::Nlri>> withdraws, transport::tcp::Connection& c);
};

template <typename N>
static std::pair<size_t, size_t> computeNlriLen(std::span<const NlriPath<typename N::Nlri>> nlri, bool addPath, size_t maxNlriSize)
{
    size_t nlriSize = 0;
    size_t total = 0;

    for (const NlriPath<typename N::Nlri>& n : nlri)
    {
        size_t len = (addPath ? 4u : 0u) + N::nlriEncodedSize(n.nlri);
        if (total + len > maxNlriSize)
            break;

        total += len;
        nlriSize++;
    }

    return {nlriSize, total};
}

template <typename N>
void appendNlri(std::span<const NlriPath<typename N::Nlri>> nlri, bool addPath, transport::tcp::Connection& c)
{
    for (const NlriPath<typename N::Nlri>& n : nlri)
    {
        const size_t entrySize = (addPath ? 4u : 0u) + N::nlriEncodedSize(n.nlri);
        std::span<uint8_t> buf = c.reserveSpan(entrySize);
        if (addPath)
            utils::writeU32(buf.data(), n.pathId);
        N::encodeNlri(buf.data() + (addPath ? 4 : 0), n.nlri);
    }
}

template <typename N>
size_t BgpTx::appendMpReach(const Session& session, size_t& attrSize, size_t nlriIdx, size_t maxMsg,
    const typename BuildUpdate<typename N::Nlri>::Announcement& update, transport::tcp::Connection& c)
{
    // MP REACH NLRI
    {
        std::span<const NlriPath<typename N::Nlri>> nlri(update.nlri.data() + nlriIdx, update.nlri.size() - nlriIdx);

        const types::IPAddress& nextHop = update.attrs.path.nextHop;
        const auto& linkLocal = update.attrs.path.linkLocal;
        constexpr bool isV6 = (N::afi.afi == BGP_AFI_IPV6);
        const size_t nhLen = isV6 ? (linkLocal.has_value() ? 32u : 16u) : 4u;

        bool addPath = false;
        for (const auto& ap : session.getNegotiated().addPathFamilies)
            if (ap.family == N::afi) { addPath = true; break; }

        // Calculate initial length
        size_t vlen = 5 + nhLen;
        // Calculate amount of nlri fields that can fit
        auto [nlriEntries, nlriTotal] = computeNlriLen<N>(nlri, addPath, maxMsg - vlen);

        // Adjust values based on results
        nlri = nlri.subspan(0, nlriEntries);
        vlen += nlriTotal;

        appendAttrHdr(BGP_ATTR_FLAG_OPTIONAL, BGP_ATTR_MP_REACH_NLRI, vlen, attrSize, c);

        // AFI + SAFI
        {
            auto buf = c.reserveSpan(3);
            utils::writeU16(buf.data(), N::afi.afi);
            buf[2] = N::afi.safi;
        }

        // Next-Hop length + bytes
        {
            auto buf = c.reserveSpan(1 + nhLen);
            buf[0] = static_cast<uint8_t>(nhLen);
            if constexpr (isV6)
            {
                std::memcpy(buf.data() + 1, nextHop.raw, 16);
                if (linkLocal.has_value())
                    std::memcpy(buf.data() + 17, linkLocal.value().raw, 16);
            }
            else
            {
                std::memcpy(buf.data() + 1, nextHop.raw, 4);
            }
        }

        {
            auto snpa = c.reserveSpan(1);
            snpa[0] = 0;
        }

        appendNlri<N>(nlri, addPath, c);
        return nlriEntries;
    }
}

template <typename N>
size_t BgpTx::appendMpUnreach(const Session& session, size_t& attrSize, size_t withdrawIdx, size_t maxMsg,
    const MpUnreach& mp, std::span<const NlriPath<typename N::Nlri>> withdrawn, transport::tcp::Connection& c)
{
    // MP UNREACH NLRI
    std::span<NlriPath<typename N::Nlri>> nlri = {withdrawn.data() + withdrawIdx, withdrawn.size() - withdrawIdx};

    bool addPath = false;
    for (const auto& ap : session.getNegotiated().addPathFamilies)
        if (ap.family == mp.family) { addPath = true; break; }

    // Calculate amount of nlri fields that can fit
    auto [nlriEntries, nlriTotal] = computeNlriLen<N>(nlri, addPath, maxMsg - 3);
    const size_t vlen = 3 + nlriTotal;

    // Adjust values absed on results
    nlri = nlri.subspan(0, nlriEntries);

    appendAttrHdr(BGP_ATTR_FLAG_OPTIONAL, BGP_ATTR_MP_UNREACH_NLRI, vlen, attrSize, c);
    
    {
        auto buf = c.reserveSpan(3);
        utils::writeU16(buf.data(), mp.family.afi);
        buf[2] = mp.family.safi;
    }

    appendNlri<N>(nlri, addPath, c);

    return nlriEntries;
}

template <typename N>
void BgpTx::buildUpdate(transport::tcp::Connection& connection, Session& session, const BuildUpdate<typename N::Nlri>& update)
{
    constexpr AfiSafi ipv4uni { BGP_AFI_IPV4, BGP_SAFI_UNICAST };
    constexpr bool isLegacyV4 = N::afi == ipv4uni;

    const auto& neg = session.getNegotiated();

    const size_t maxMsg = neg.extendedMessage ? 65535u : 4096u;

    bool addPath = false;
    for (const auto& ap : neg.addPathFamilies)
        if (ap.family == ipv4uni) { addPath = true; break; }

    size_t withdrawIdx = 0;

    for (const auto& a : update.announcements)
    {
        size_t nlriIdx = 0;

        while (nlriIdx < a.nlri.size() || withdrawIdx < update.withdrawn.size())
        {
            auto hdrBuf = connection.reserveSpan(packet::BgpHeader::fixedSize);
            auto wdLenBuf = connection.reserveSpan(2);
            auto attrLenBuf = connection.reserveSpan(2);

            size_t attrBytes = appendPathAttrs(session, a.attrs, connection);

            if constexpr (!isLegacyV4)
            {
                size_t withdrawnAdded = 0;
                size_t nlriAdded = 0;

                if (withdrawIdx < update.withdrawn.size())
                {
                    MpUnreach mp{N::afi};
                    std::span<const NlriPath<typename N::Nlri>> wdSpan(update.withdrawn.data(), update.withdrawn.size());
                    withdrawnAdded = appendMpUnreach<N>(session, attrBytes,
                        withdrawIdx, maxMsg, mp, wdSpan, connection);
                    withdrawIdx += withdrawnAdded;
                }

                if (nlriIdx < a.nlri.size())
                {
                    nlriAdded = appendMpReach<N>(session, attrBytes,
                        nlriIdx, maxMsg, a, connection);
                    nlriIdx += nlriAdded;
                }

                buildHeader(BGP_TYPE_UPDATE, static_cast<uint16_t>(4 + attrBytes), hdrBuf.data());
                utils::writeU16(wdLenBuf.data(), 0);
                utils::writeU16(attrLenBuf.data(), static_cast<uint16_t>(attrBytes));
            }
            else
            {
                size_t wdBudget = maxMsg - packet::BgpHeader::fixedSize - 4 - attrBytes;
                std::span<const NlriPath<typename N::Nlri>> wdSpan(
                    update.withdrawn.data() + withdrawIdx,
                    update.withdrawn.size() - withdrawIdx
                );
                auto [wdEntries, wdBytes] = computeNlriLen<N>(wdSpan, addPath, wdBudget);
                appendNlri<N>(wdSpan.subspan(0, wdEntries), addPath, connection);
                withdrawIdx += wdEntries;

                size_t nlriBudget = maxMsg - packet::BgpHeader::fixedSize - 4 - attrBytes - wdBytes;
                std::span<const NlriPath<typename N::Nlri>> nlriSpan(
                    a.nlri.data() + nlriIdx,
                    a.nlri.size() - nlriIdx
                );
                auto [nlriEntries, nlriBytes] = computeNlriLen<N>(nlriSpan, addPath, nlriBudget);
                appendNlri<N>(nlriSpan.subspan(0, nlriEntries), addPath, connection);
                nlriIdx += nlriEntries;

                buildHeader(BGP_TYPE_UPDATE, static_cast<uint16_t>(4 + wdBytes + attrBytes + nlriBytes), hdrBuf.data());
                utils::writeU16(wdLenBuf.data(), static_cast<uint16_t>(wdBytes));
                utils::writeU16(attrLenBuf.data(), static_cast<uint16_t>(attrBytes));
            }
        }
    }

    // Drain remaining withdraws
    while (withdrawIdx < update.withdrawn.size())
    {
        auto hdrBuf = connection.reserveSpan(packet::BgpHeader::fixedSize);
        auto wdLenBuf = connection.reserveSpan(2);
        auto attrLenBuf = connection.reserveSpan(2);

        if constexpr (!isLegacyV4)
        {
            size_t attrBytes = 0;
            MpUnreach mp{N::afi};
            std::span<NlriPath<typename N::Nlri>> wdSpan{update.withdrawn.data(), update.withdrawn.size()};
            size_t withdrawnAdded = appendMpUnreach<N>(session, attrBytes,
                withdrawIdx, maxMsg, mp, wdSpan, connection);
            withdrawIdx += withdrawnAdded;

            buildHeader(BGP_TYPE_UPDATE, static_cast<uint16_t>(4 + attrBytes), hdrBuf.data());
            utils::writeU16(wdLenBuf.data(), 0);
            utils::writeU16(attrLenBuf.data(), static_cast<uint16_t>(attrBytes));
        }
        else
        {
            size_t wdBudget = maxMsg - packet::BgpHeader::fixedSize - 4;
            std::span<const NlriPath<typename N::Nlri>> wdSpan(
                update.withdrawn.data() + withdrawIdx,
                update.withdrawn.size() - withdrawIdx
            );
            auto [wdEntries, wdBytes] = computeNlriLen<N>(wdSpan, addPath, wdBudget);
            appendNlri<N>(wdSpan.subspan(0, wdEntries), addPath, connection);
            withdrawIdx += wdEntries;

            buildHeader(BGP_TYPE_UPDATE, static_cast<uint16_t>(4 + wdBytes), hdrBuf.data());
            utils::writeU16(wdLenBuf.data(), static_cast<uint16_t>(wdBytes));
            utils::writeU16(attrLenBuf.data(), 0);
        }
    }
}
} // namespace routing

#endif // BGP_TX_H

