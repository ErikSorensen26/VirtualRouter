/**
 * @file MultiSession.h
 * @brief Multi-session BGP peering: per-AFI/SAFI session collection with collision detection.
 */

#ifndef BGP_MULTI_SESSION_H
#define BGP_MULTI_SESSION_H

#include <unordered_map>
#include <variant>
#include <cstdint>

#include "bgp/BgpTypes.hpp"
#include "tcp/Connection.h"

namespace routing::bgp
{
class Session;
class BgpProcess;

/**
 * @brief Holds the per-AFI/SAFI sessions and pending connections for a multi-session-capable peer.
 * @ingroup BGP_SESSION
 *
 * RFC 8277-style multi-session BGP maintains a separate TCP connection and
 * BGP session for each negotiated AFI/SAFI.  `MultiSession` collects these
 * child `Session` objects and maps inbound TCP connection IDs to either an
 * already-activated `Session*` or a raw `Connection` awaiting the
 * capability exchange that will reveal its AFI/SAFI.
 *
 * Each `Session` in `sessions` goes through its own independent FSM, so
 * the IPv4 unicast session can be ESTABLISHED while the IPv6 unicast session
 * is still in CONNECT without either blocking the other.
 *
 * ## Architectural Role
 * `MultiSession` is stored inside @ref Session as the active variant of
 * `std::variant<AfiSafi, MultiSession>`.  A regular (single-session) peer
 * keeps the `AfiSafi` arm; a multi-session peer switches to the `MultiSession`
 * arm when capability negotiation completes.  `MultiSession` does not own a
 * scheduler or configuration — all of that remains in the parent `Session`
 * and its owning `Neighbor`.
 *
 * ## Lifecycle & Ownership
 * Created in-place inside the parent `Session` variant when
 * `Session::startActiveMultiSession` or `Session::startPassiveMultiSession`
 * is called.  Destroyed when the parent `Session` is destroyed or reset to
 * single-session mode.  The `sessions` map owns the child `Session` objects;
 * the `connections` map holds non-owning pointers to activated sessions or
 * owning `Connection` objects for connections that are not yet bound to a
 * session.
 *
 * @warning The `connections` variant mixes owned (`Connection`) and non-owned
 * (`Session*`) values.  Callers must not access the `Session*` entry after the
 * corresponding entry in `sessions` has been erased.
 *
 * @see Session
 * @see Neighbor
 */
class MultiSession
{
public:
    std::unordered_map<AfiSafi, Session> sessions;  ///< Active child sessions keyed by AFI/SAFI; one per negotiated family.
    std::unordered_map<uint64_t, std::variant<Session*, transport::tcp::Connection>> connections; ///< Maps TCP ConnId to an activated Session* or a pending Connection awaiting AFI/SAFI resolution.

    /**
     * @brief Looks up the child session that owns the given TCP connection ID.
     *
     * Searches `connections` for the entry matching `id` and, if it holds a
     * `Session*`, returns it.  Returns nullptr if the ID is unknown or if the
     * entry still holds a raw `Connection`.
     *
     * @param id TCP connection identifier to search for.
     * @return Pointer to the matching child @ref Session, or nullptr.
     */
    Session* findMultiSession(uint64_t id);

    /**
     * @brief Promotes a pending connection to an active session for the given AFI/SAFI.
     *
     * Moves the raw `Connection` from `connections[id]` into the child
     * `Session` in `sessions[afiSafi]`, replacing the `Connection` entry with
     * a `Session*`.  Creates the child `Session` in `sessions` if it does not
     * yet exist.
     *
     * @param id      TCP connection ID of the pending connection to activate.
     * @param afiSafi Address family the connection has been identified as serving.
     * @return Pointer to the activated (or existing) child @ref Session.
     *
     * @warning Returns nullptr if no pending connection exists for `id`.
     */
    Session* activateSession(transport::tcp::ConnId id, const AfiSafi& afiSafi);

    /**
     * @brief Closes and removes the connection or session associated with the given TCP connection ID.
     *
     * If the entry holds a raw `Connection`, it is closed and erased.
     * If it holds a `Session*`, the session's connections are closed via
     * `Session::closeAllConnections` and the `connections` entry is erased;
     * the `Session` itself remains in `sessions` until the FSM reaches IDLE.
     *
     * @param id TCP connection identifier to close.
     */
    void close(transport::tcp::ConnId id);
};
} // namespace routing::bgp

#endif // BGP_MULTI_SESSION_H
