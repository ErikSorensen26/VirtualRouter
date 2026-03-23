// Collision.h

#ifndef BGP_COLLISION_H
#define BGP_COLLISION_H

#include <cstdint>

namespace routing::bgp
{
class Session;

/**
 * @class CollisionDetector
 * @brief Resolves simultaneous-open collisions per RFC 4271 §6.8.
 *
 * When two BGP speakers both initiate TCP connections to each other the
 * result is two concurrent sessions.  The collision is resolved by comparing
 * BGP Identifiers (router-IDs):
 *
 *   - The speaker with the HIGHER BGP Identifier keeps its outgoing session
 *     and sends NOTIFICATION (Cease / Connection Collision Resolution) on the
 *     incoming session.
 *   - The speaker with the LOWER BGP Identifier keeps the incoming session
 *     and sends NOTIFICATION on its outgoing session.
 *   - Equal BGP Identifiers: should not occur; both sessions are closed.
 *
 * Usage:
 *   CollisionDetector::resolve(session, localRouterId, peerRouterId)
 *   returns true  → keep this session (drop the other)
 *   returns false → drop this session  (keep the other)
 */
class CollisionDetector
{
public:
    CollisionDetector() = delete;

    /**
     * @brief Determine whether the currently-being-established session
     *        should be kept or dropped.
     *
     * @param isOutgoing   true if this session was initiated by us (active),
     *                     false if it was accepted from the peer (passive).
     * @param localRid     Our BGP Identifier (network-byte-order uint32_t).
     * @param peerRid      Peer's BGP Identifier from OPEN message.
     *
     * @return true  – keep this session; the other should be dumped.
     * @return false – dump this session via OpenCollisionDump event.
     */
    static bool shouldKeep(bool isOutgoing, uint32_t localRid, uint32_t peerRid) noexcept;

    /**
     * @brief Build the NOTIFICATION to send on the session that loses the
     *        collision resolution.
     */
    static uint16_t collisionNotificationCode() noexcept;
};
} // namespace routing

#endif // BGP_COLLISION_H

