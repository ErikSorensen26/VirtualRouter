/**
 * @file BgpTx.h
 * @brief BGP message transmission: encoding UPDATE, NOTIFICATION, KEEPALIVE.
 */

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

/**
 * @brief Stateless BGP message encoder: builds and writes outbound BGP messages.
 * @ingroup BGP_TRANSPORT
 *
 * All methods are static; BgpTx carries no state of its own.  It writes
 * directly into a @ref transport::tcp::Connection by reserving spans from
 * the connection's transmit buffer, ensuring zero-copy construction of
 * variable-length UPDATE messages.
 *
 * UPDATE encoding handles two distinct wire formats:
 * - **Legacy IPv4 unicast**: withdrawn routes and NLRI appear in the fixed
 *   BGP UPDATE fields; path attributes occupy the middle section.
 * - **Multiprotocol (non-IPv4 or non-unicast)**: withdrawn and announced
 *   prefixes are carried inside MP_UNREACH_NLRI and MP_REACH_NLRI attributes.
 *
 * Large UPDATE sets are automatically split across multiple BGP messages to
 * respect the negotiated maximum message size (4096 bytes standard, 65535
 * bytes if Extended Message capability was negotiated).
 *
 * ## Architectural Role
 * BgpTx is the outbound half of the BGP transport layer, symmetric to
 * @ref BgpRx.  It sits between the decision engine / Adj-RIB-Out and the TCP
 * send buffer.  Callers are responsible for supplying correctly formatted
 * @ref BuildUpdate structures; BgpTx only encodes and writes them.
 *
 * @see BgpRx, Session, BuildUpdate
 */
class BgpTx
{
public:
    BgpTx() = delete;

    /**
     * @brief Encode and write a BGP OPEN message for @p session.
     *
     * Includes the local AS number, hold time, router ID, and all locally
     * supported capabilities as optional parameters.
     *
     * @param connection TCP connection to write into.
     * @param session    Session supplying local parameters and capability list.
     */
    static void buildOpen(transport::tcp::Connection& connection, Session& session);

    /**
     * @brief Encode and write one or more BGP UPDATE messages for the given @p update.
     *
     * Splits the announcement and withdrawal lists across as many UPDATE
     * messages as required.  Legacy IPv4 unicast uses the BGP UPDATE wire
     * format directly; all other AFIs use MP_REACH_NLRI / MP_UNREACH_NLRI
     * path attributes.  Add-Path path IDs are prepended to each prefix when
     * the capability was negotiated for this AFI.
     *
     * @tparam N  NLRI policy type. Must provide:
     *              - `N::Nlri`                          — prefix/NLRI type
     *              - `N::afi`                           — `AfiSafi` constant
     *              - `N::nlriEncodedSize(nlri)`         — wire size of one prefix in bytes
     *              - `N::encodeNlri(ptr, nlri)`         — write one prefix at @p ptr
     *
     * @param connection TCP connection to write into.
     * @param session    Session supplying negotiated capabilities and max message size.
     * @param update     Announcements and withdrawals to encode.
     */
    template <typename N>
    static void buildUpdate(transport::tcp::Connection& connection, Session& session, const BuildUpdate<typename N::Nlri>& update);

    /**
     * @brief Encode and write a BGP NOTIFICATION message.
     *
     * Sends the error code, subcode, and optional data bytes from
     * @p notification, then the connection should be closed by the caller.
     *
     * @param connection   TCP connection to write into.
     * @param notification Notification to encode.
     */
    static void buildNotification(transport::tcp::Connection& connection, const Notification& notification);

    /**
     * @brief Encode and write a BGP KEEPALIVE message.
     *
     * A KEEPALIVE is a fixed 19-byte BGP header with no payload.
     *
     * @param connection TCP connection to write into.
     */
    static void buildKeepalive(transport::tcp::Connection& connection);

    /**
     * @brief Encode and write a ROUTE-REFRESH request for the given address family.
     *
     * @param connection TCP connection to write into.
     * @param session    Session context (used to validate the capability was negotiated).
     * @param family     AFI/SAFI to request a refresh for.
     * @param reason     Refresh subtype: Normal, Begin-of-RIB, or End-of-RIB.
     */
    static void buildRouteRefresh(transport::tcp::Connection& connection, Session& session,
        const AfiSafi& family, RouteRefreshReason reason = RouteRefreshReason::Normal);

private:

    /**
     * @brief Write a BGP fixed header into an already-reserved 19-byte buffer.
     *
     * Fills in the 16-byte all-ones marker, the total message length
     * (19 + @p payloadSize), and the message type byte.
     *
     * @param type        BGP message type (e.g. BGP_TYPE_UPDATE).
     * @param payloadSize Byte count of the message payload (excluding the header).
     * @param buf         Pointer to a 19-byte buffer to write into.
     */
    static void buildHeader(uint8_t type, uint16_t payloadSize, uint8_t* buf);

    /**
     * @brief Reserve and write a path-attribute type-length header into @p c.
     *
     * Handles the extended-length flag automatically: if @p valueLen exceeds
     * 255 the Extended Length flag is set and a 2-byte length field is written;
     * otherwise a 1-byte length is used.  Increments @p attrsSize by the total
     * bytes written (flag + type + length).
     *
     * @param flags     Attribute flags byte (e.g. BGP_ATTR_FLAG_OPTIONAL).
     * @param type      Attribute type code.
     * @param valueLen  Length of the attribute value that follows.
     * @param attrsSize Accumulator for total attribute bytes written; updated in place.
     * @param c         TCP connection to write the header into.
     */
    static void appendAttrHdr(uint8_t flags, uint8_t type, size_t valueLen, size_t& attrsSize, transport::tcp::Connection& c);

    /**
     * @brief Encode all standard path attributes for one announcement and write them to @p c.
     *
     * Writes ORIGIN, AS_PATH, NEXT_HOP (IPv4 only), MED, LOCAL_PREF,
     * COMMUNITIES, and any unknown transitive attributes carried in @p pa.
     * eBGP egress policy (LOCAL_PREF stripping, AS prepend) must be applied
     * by the caller before invoking this method.
     *
     * @param session Session context used to determine iBGP vs. eBGP and 4-byte ASN support.
     * @param pa      Path attribute set to encode.
     * @param c       TCP connection to write into.
     * @return Total bytes written for all path attributes.
     */
    static size_t appendPathAttrs(const Session& session, const PathAttribute& pa, transport::tcp::Connection& c);

    /**
     * @brief Encode an MP_REACH_NLRI attribute carrying as many prefixes as fit in @p maxMsg.
     *
     * Writes the AFI, SAFI, next-hop (with optional link-local for IPv6), SNPA
     * count (zero), and as many NLRI entries from @p update starting at
     * @p nlriIdx as can fit within the remaining message budget.
     *
     * @tparam N  NLRI policy type — same constraints as @ref buildUpdate.
     *
     * @param session   Session context used to resolve the Add-Path flag for this AFI.
     * @param attrSize  Running total of attribute bytes written; incremented in place.
     * @param nlriIdx   Index into the announcement's NLRI vector to start from.
     * @param maxMsg    Maximum total UPDATE message size in bytes.
     * @param update    Announcement carrying the NLRI vector and path attributes.
     * @param c         TCP connection to write into.
     * @return Number of NLRI entries written; caller should advance @p nlriIdx by this value.
     */
    template <typename N>
    static size_t appendMpReach(const Session& session, size_t& attrSize, size_t nlriIdx, size_t maxMsg,
        const typename BuildUpdate<typename N::Nlri>::Announcement& update, transport::tcp::Connection& c);

    /**
     * @brief Encode an MP_UNREACH_NLRI attribute carrying as many withdrawn prefixes as fit.
     *
     * Writes the AFI, SAFI, and as many entries from @p withdraws starting at
     * @p withdrawIdx as can fit within the remaining message budget.
     *
     * @tparam N  NLRI policy type — same constraints as @ref buildUpdate.
     *
     * @param session     Session context used to resolve the Add-Path flag.
     * @param attrSize    Running total of attribute bytes written; incremented in place.
     * @param withdrawIdx Index into @p withdraws to start from.
     * @param maxMsg      Maximum total UPDATE message size in bytes.
     * @param unreach     MP_UNREACH descriptor carrying the AFI/SAFI to encode.
     * @param withdraws   Full withdrawn-prefix span for this update.
     * @param c           TCP connection to write into.
     * @return Number of withdrawn entries written; caller should advance @p withdrawIdx by this value.
     */
    template <typename N>
    static size_t appendMpUnreach(const Session& session, size_t& attrSize, size_t withdrawIdx, size_t maxMsg,
        const MpUnreach& unreach, std::span<const NlriPath<typename N::Nlri>> withdraws, transport::tcp::Connection& c);
};

/**
 * @brief Compute how many NLRI entries from @p nlri fit within @p maxNlriSize bytes.
 * @ingroup BGP_TRANSPORT
 *
 * Iterates prefixes in order and accumulates encoded sizes until the budget is
 * exhausted.  Add-Path path IDs (4 bytes each) are counted when @p addPath is true.
 *
 * @tparam N  NLRI policy type providing `N::nlriEncodedSize(nlri)`.
 *
 * @param nlri         Span of NLRI entries to measure.
 * @param addPath      Whether Add-Path path IDs are included in the wire encoding.
 * @param maxNlriSize  Maximum total byte budget for all NLRI entries.
 * @return A pair of { number of entries that fit, total byte size of those entries }.
 */
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

/**
 * @brief Write a sequence of NLRI entries into a TCP connection buffer.
 * @ingroup BGP_TRANSPORT
 *
 * Reserves one buffer span per NLRI entry and writes the optional Add-Path
 * path ID followed by the encoded prefix.
 *
 * @tparam N  NLRI policy type providing `N::nlriEncodedSize` and `N::encodeNlri`.
 *
 * @param nlri    NLRI entries to encode.
 * @param addPath Whether to prepend a 4-byte path ID before each entry.
 * @param c       TCP connection to write into.
 */
template <typename N>
void appendNlri(std::span<const NlriPath<typename N::Nlri>> nlri, bool addPath, transport::tcp::Connection& c)
{
    for (const NlriPath<typename N::Nlri>& n : nlri)
    {
        const size_t entrySize = (addPath ? 4u : 0u) + N::nlriEncodedSize(n.nlri);
        std::span<uint8_t> buf = c.reserveSpan(entrySize);
        if (addPath)
            utils::write<uint32_t>(buf.data(), n.pathId);
        N::encodeNlri(buf.data() + (addPath ? 4 : 0), n.nlri);
        c.commit(entrySize);
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
            utils::write<uint16_t>(buf.data(), N::afi.afi);
            buf[2] = N::afi.safi;
            c.commit(3);
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
            c.commit(1 + nhLen);
        }

        {
            auto snpa = c.reserveSpan(1);
            snpa[0] = 0;
            c.commit(1);
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
        utils::write<uint16_t>(buf.data(), mp.family.afi);
        buf[2] = mp.family.safi;
        c.commit(3);
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
            connection.commit(packet::BgpHeader::fixedSize);

            if constexpr (!isLegacyV4)
            {
                auto wdLenBuf = connection.reserveSpan(2);
                connection.commit(2);
                auto attrLenBuf = connection.reserveSpan(2);
                connection.commit(2);

                size_t attrBytes = appendPathAttrs(session, a.attrs, connection);

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
                utils::write<uint16_t>(wdLenBuf.data(), 0);
                utils::write<uint16_t>(attrLenBuf.data(), static_cast<uint16_t>(attrBytes));
            }
            else
            {
                auto wdLenBuf = connection.reserveSpan(2);
                connection.commit(2);

                size_t wdBudget = maxMsg - packet::BgpHeader::fixedSize - 4;
                std::span<const NlriPath<typename N::Nlri>> wdSpan(
                    update.withdrawn.data() + withdrawIdx,
                    update.withdrawn.size() - withdrawIdx
                );
                auto [wdEntries, wdBytes] = computeNlriLen<N>(wdSpan, addPath, wdBudget);
                appendNlri<N>(wdSpan.subspan(0, wdEntries), addPath, connection);
                withdrawIdx += wdEntries;

                auto attrLenBuf = connection.reserveSpan(2);
                connection.commit(2);

                size_t attrBytes = appendPathAttrs(session, a.attrs, connection);

                size_t nlriBudget = maxMsg - packet::BgpHeader::fixedSize - 4 - attrBytes - wdBytes;
                std::span<const NlriPath<typename N::Nlri>> nlriSpan(
                    a.nlri.data() + nlriIdx,
                    a.nlri.size() - nlriIdx
                );
                auto [nlriEntries, nlriBytes] = computeNlriLen<N>(nlriSpan, addPath, nlriBudget);
                appendNlri<N>(nlriSpan.subspan(0, nlriEntries), addPath, connection);
                nlriIdx += nlriEntries;

                buildHeader(BGP_TYPE_UPDATE, static_cast<uint16_t>(4 + wdBytes + attrBytes + nlriBytes), hdrBuf.data());
                utils::write<uint16_t>(wdLenBuf.data(), static_cast<uint16_t>(wdBytes));
                utils::write<uint16_t>(attrLenBuf.data(), static_cast<uint16_t>(attrBytes));
            }
        }
    }

    // Drain remaining withdraws
    while (withdrawIdx < update.withdrawn.size())
    {
        auto hdrBuf = connection.reserveSpan(packet::BgpHeader::fixedSize);
        connection.commit(packet::BgpHeader::fixedSize);
        auto wdLenBuf = connection.reserveSpan(2);
        connection.commit(2);

        if constexpr (!isLegacyV4)
        {
            auto attrLenBuf = connection.reserveSpan(2);
            connection.commit(2);

            size_t attrBytes = 0;
            MpUnreach mp{N::afi};
            std::span<NlriPath<typename N::Nlri>> wdSpan{update.withdrawn.data(), update.withdrawn.size()};
            size_t withdrawnAdded = appendMpUnreach<N>(session, attrBytes,
                withdrawIdx, maxMsg, mp, wdSpan, connection);
            withdrawIdx += withdrawnAdded;

            buildHeader(BGP_TYPE_UPDATE, static_cast<uint16_t>(4 + attrBytes), hdrBuf.data());
            utils::write<uint16_t>(wdLenBuf.data(), 0);
            utils::write<uint16_t>(attrLenBuf.data(), static_cast<uint16_t>(attrBytes));
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

            auto attrLenBuf = connection.reserveSpan(2);
            connection.commit(2);

            buildHeader(BGP_TYPE_UPDATE, static_cast<uint16_t>(4 + wdBytes), hdrBuf.data());
            utils::write<uint16_t>(wdLenBuf.data(), static_cast<uint16_t>(wdBytes));
            utils::write<uint16_t>(attrLenBuf.data(), 0);
        }
    }
}
} // namespace routing

#endif // BGP_TX_H
