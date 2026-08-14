/**
 * @file BgpRx.h
 * @brief BGP message reception: parsing and validating UPDATE, NOTIFICATION.
 */

/**
 * @defgroup BGP_TRANSPORT BGP Transport
 * @ingroup BGP
 * @brief BGP message parser (RX) and builder (TX).
 */

#ifndef BGP_RX_H
#define BGP_RX_H

#include <span>
#include <cstdint>

#include "bgp/BgpTypes.hpp"
#include "bgp/session/Session.h"
#include "bgp/rib/RibTypes.hpp"

namespace transport::tcp { class RxConsumer; class Connection; }

namespace routing::bgp
{
class Session;
class BgpProcess;
struct Capabilities;

/**
 * @brief Intermediate representation of a parsed BGP UPDATE message.
 * @ingroup BGP_TRANSPORT
 *
 * Carries the decoded path attributes and raw NLRI byte spans from one UPDATE
 * message before AFI-specific NLRI decoding has been performed.  The spans
 * point into the TCP receive buffer and are valid only for the duration of the
 * @ref BgpRx::handleIncoming call that produced them.
 *
 * @warning Do not store or copy this struct beyond the immediate parse call;
 * the underlying byte spans become invalid once the RxConsumer advances.
 */
struct IncomingUpdate
{
    Attributes attrs;   ///< Decoded well-known and optional transitive attributes.
    Path path;          ///< Decoded next-hop, AS path, and community data.
    AfiSafi afi = { BGP_AFI_IPV4, BGP_SAFI_UNICAST }; ///< Address family resolved from MP_REACH/MP_UNREACH, or IPv4 unicast default.

    std::span<uint8_t> withdrawnData; ///< Raw withdrawn-routes field (legacy IPv4) or MP_UNREACH NLRI bytes.
    std::span<uint8_t> nlriData;      ///< Raw NLRI field (legacy IPv4) or MP_REACH NLRI prefix bytes.

    bool sawOrigin  = false; ///< ORIGIN was present; checked against RFC 4271 6.3 mandatory-attribute rules.
    bool sawAsPath  = false; ///< AS_PATH was present.
    bool sawNextHop = false; ///< NEXT_HOP or an MP_REACH next hop was present.

    /// RFC 7606 treat-as-withdraw: decode the announced NLRI as withdrawals instead.
    bool withdrawNlri = false;
};

/**
 * @brief Stateless BGP receive processor: dispatches and parses inbound messages.
 * @ingroup BGP_TRANSPORT
 *
 * All methods are static; BgpRx carries no state of its own.  It acts as the
 * single entry point for bytes arriving on a BGP TCP connection, framing them
 * into complete BGP messages and dispatching each to the appropriate handler.
 *
 * The public surface has two responsibilities:
 * - @ref handleIncoming: drain all complete messages from an RxConsumer and
 *   drive the session FSM for each one.
 * - @ref processUpdate (template): perform AFI-specific NLRI decoding on an
 *   already-parsed @ref IncomingUpdate, producing a typed @ref ParsedUpdate.
 *
 * ## Architectural Role
 * BgpRx is the inbound half of the BGP transport layer, symmetric to
 * @ref BgpTx.  It sits between the TCP receive buffer (RxConsumer) and the
 * session FSM.  All FSM events triggered by inbound messages are posted through
 * the session's owning ProcessQueue, keeping FSM state changes single-threaded.
 *
 * ## Concurrency Model
 * @ref handleIncoming is called from the TCP receive callback, which may run on
 * a different thread from the BGP scheduler.  The method must not touch session
 * state directly — it posts events to the session's ProcessQueue instead.
 *
 * @see BgpTx, Session, IncomingUpdate
 */
class BgpRx
{
public:
    BgpRx() = delete;

    /**
     * @brief Drain all complete BGP messages from @p c and process each one.
     *
     * Reads framed BGP messages from the RxConsumer, validates the 19-byte
     * fixed header (marker, length, type), and dispatches to the appropriate
     * per-message handler.  Sends a NOTIFICATION and closes the session on any
     * parse or validation error.
     *
     * @param s Session that owns this connection.
     * @param c RxConsumer positioned at the start of the next unread byte.
     */
    static void handleIncoming(Session& s, transport::tcp::RxConsumer& c);

    /**
     * @brief Decode AFI-specific NLRI from a pre-parsed update into a typed @ref ParsedUpdate.
     *
     * Iterates over the raw withdrawn and announcement byte spans in @p uinfo,
     * decoding each prefix using the NLRI policy @p N.  Add-Path path IDs are
     * consumed when the capability was negotiated for this AFI.
     *
     * @tparam N  NLRI policy type. Must provide:
     *              - `N::Nlri`             — the prefix/NLRI type
     *              - `N::afi`              — `AfiSafi` constant for this address family
     *              - `N::decodeNlri(ptr, nlri)` — decode one prefix; returns bytes consumed or 0 on error
     *
     * @param session      Session context used to check negotiated capabilities.
     * @param uinfo        Partially parsed update carrying raw NLRI byte spans.
     * @param update       Output: populated with typed withdrawn and announced prefixes.
     * @param notification Output: populated with error code if parsing fails.
     * @return True on success; false if a malformed prefix was encountered, in which
     *         case @p notification carries the appropriate error code.
     */
    template <typename N>
    static bool processUpdate(Session& session, IncomingUpdate& uinfo, ParsedUpdate<typename N::Nlri>& update, Notification& notification);

private:

    /**
     * @brief Parse and validate a BGP OPEN message, then post the open event to the FSM.
     *
     * @param c            Session that received the OPEN.
     * @param cid          Connection ID of the transport connection.
     * @param data         Payload bytes (excluding the fixed BGP header).
     * @param notification Populated with error details if the OPEN is rejected.
     * @return True if the OPEN was well-formed and accepted; false otherwise.
     */
    static bool processOpen(Session& c, uint64_t cid, std::span<uint8_t> data, Notification& notification);

    /**
     * @brief Parse a BGP UPDATE message and dispatch per-AFI route processing.
     *
     * Decodes path attributes and NLRI fields, resolves the address family, and
     * invokes the per-AF handler on the owning BgpProcess.
     *
     * @param c            Session context.
     * @param data         Payload bytes (excluding the fixed BGP header).
     * @param notification Populated with error details on failure.
     * @return True if the UPDATE was well-formed; false on any parse error.
     */
    static bool processUpdate(Session& c, std::span<uint8_t> data, Notification& notification);

    /**
     * @brief Process a received NOTIFICATION message and post the error event to the FSM.
     *
     * @param c            Session context.
     * @param data         Payload bytes (excluding the fixed BGP header).
     * @param notification Populated with decoded error code and subcode.
     * @return Always returns false; a received NOTIFICATION always terminates the session.
     */
    static bool processNotification(Session& c, std::span<uint8_t> data, Notification& notification);

    /**
     * @brief Validate a KEEPALIVE message and post the keepalive event to the FSM.
     *
     * KEEPALIVE has no payload; this validates that the length field is exactly
     * 19 (header only) and resets the hold-timer.
     *
     * @param c            Session context.
     * @param data         Payload bytes; must be empty for a valid KEEPALIVE.
     * @param notification Populated with an error code if the message is malformed.
     * @return True if valid; false if the message length is wrong.
     */
    static bool processKeepalive(Session& c, std::span<uint8_t> data, Notification& notification);

    /**
     * @brief Process a ROUTE-REFRESH request and trigger an outbound refresh for the given AFI.
     *
     * @param c            Session context.
     * @param data         Payload bytes containing AFI, reserved, and SAFI fields.
     * @param notification Populated with error details if the message is malformed.
     * @return True if the message was well-formed and processed.
     */
    static bool processRouteRefresh(Session& c, std::span<uint8_t> data, Notification& notification);

    /**
     * @brief Decode the OPEN capabilities optional parameter into @p out.
     *
     * Iterates the capability TLVs in the OPEN optional-parameters field and
     * populates the negotiated capability set.  Unknown capability codes are
     * silently ignored per RFC 5492.
     *
     * @param data Bytes of the optional-parameters field.
     * @param out  Capability set to populate.
     */
    static void parseCapabilities(std::span<uint8_t> data, Capabilities& out);

    /**
     * @brief Decode all path attributes from an UPDATE message into @p uinfo.
     *
     * Walks the type-length-value attribute list, validates flags and lengths,
     * and extracts each recognized attribute into the appropriate field of
     * @p uinfo.  AS4_PATH and AS4_AGGREGATOR are reconciled with their AS2
     * counterparts per RFC 4893 §4.2.3 after all attributes are parsed.
     *
     * @param session Session context used to check peer capabilities (e.g. 4-byte ASN).
     * @param data    Raw path-attributes field bytes.
     * @param uinfo   Output: attributes and path fields populated on success.
     * @param error   Populated with an error code if any attribute is malformed.
     * @return True if all attributes parsed correctly; false on any error.
     */
    static bool parsePathAttributes(Session& session, std::span<uint8_t> data, IncomingUpdate& uinfo, Notification& error);

    /**
     * @brief Verify the well-known mandatory attributes are present (RFC 4271 6.3).
     *
     * Only applies to UPDATEs that carry reachable NLRI; a withdraw-only UPDATE is
     * required to carry none of them. Must be called after the legacy IPv4 NLRI span
     * has been resolved, since that determines whether NLRI is present at all.
     *
     * @param uinfo Parsed update whose `saw*` flags were set by @ref parsePathAttributes.
     * @param error Populated with MISSING_ATTR and the offending type code on failure.
     * @return True if all mandatory attributes are present.
     */
    static bool checkMandatoryAttributes(const IncomingUpdate& uinfo, Notification& error);

    /**
     * @brief Determine which address family an UPDATE targets by inspecting MP_REACH/MP_UNREACH.
     *
     * Used for multi-session deployments where each TCP session carries exactly
     * one AFI/SAFI.  Returns the AFI/SAFI pair decoded from the MP extension
     * attribute, or @c std::nullopt if neither attribute is present.
     *
     * @param data Raw path-attributes field bytes.
     * @return The resolved AfiSafi, or nullopt if no MP extension attribute is found.
     */
    static std::optional<AfiSafi> resolveMultiSessionAf(std::span<uint8_t> data);
};

template <typename N>
bool BgpRx::processUpdate(Session& session, IncomingUpdate& uinfo, ParsedUpdate<typename N::Nlri>& update, Notification& error)
{
    const bool addPath = session.getNegotiated().findAddPath(N::afi) != nullptr;

    // Withdrawn NLRI
    for (size_t pos = 0; pos < uinfo.withdrawnData.size();)
    {
        uint32_t pathId = 0;

        if (addPath)
        {
            if (pos + 4 > uinfo.withdrawnData.size())
            {
                error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST;
                return false;
            }
            pathId = utils::read<uint32_t>(uinfo.withdrawnData.data() + pos);
            pos += 4;
        }

        if (pos >= uinfo.withdrawnData.size())
        {
            error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST;
            return false;
        }
        uint8_t prefixLength = uinfo.withdrawnData[pos];
        size_t claimedBytes = 1u + (static_cast<size_t>(prefixLength) + 7u) / 8u;
        if (pos + claimedBytes > uinfo.withdrawnData.size())
        {
            error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST;
            return false;
        }

        typename N::Nlri nlri{};
        size_t consumed = N::decodeNlri(uinfo.withdrawnData.data() + pos, nlri);
        if (consumed == 0 || pos + consumed > uinfo.withdrawnData.size())
        {
            error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST;
            return false;
        }
        update.withdrawn.push_back({nlri, pathId});
        pos += consumed;
    }

    // Legacy IPv4 NLRI

    for (size_t pos = 0; pos < uinfo.nlriData.size();)
    {
        uint32_t pathId = 0;

        if (addPath)
        {
            if (pos + 4 > uinfo.nlriData.size())
            {
                error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST;
                return false;
            }
            pathId = utils::read<uint32_t>(uinfo.nlriData.data() + pos);
            pos += 4;
        }

        if (pos >= uinfo.nlriData.size())
        {
            error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST;
            return false;
        }
        uint8_t prefixLength = uinfo.nlriData[pos];
        size_t claimedBytes = 1u + (static_cast<size_t>(prefixLength) + 7u) / 8u;
        if (pos + claimedBytes > uinfo.nlriData.size())
        {
            error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST;
            return false;
        }

        typename N::Nlri nlri{};
        size_t consumed = N::decodeNlri(uinfo.nlriData.data() + pos, nlri);
        if (consumed == 0 || pos + consumed > uinfo.nlriData.size())
        {
            error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST;
            return false;
        }
        if (uinfo.withdrawNlri)
            update.withdrawn.push_back({nlri, pathId});
        else
            update.announcements.push_back({nlri, pathId});
        pos += consumed;
    }

    if (!uinfo.withdrawNlri)
        update.attrs = PathAttribute{uinfo.attrs, uinfo.path};

    return true;
}
} // namespace routing::bgp

#endif // BGP_RX_H
