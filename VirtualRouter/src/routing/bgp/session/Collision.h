/**
 * @file Collision.h
 * @brief BGP session collision detection and resolution.
 */

#ifndef BGP_COLLISION_H
#define BGP_COLLISION_H

#include <cstdint>

namespace routing::bgp
{
class Session;

/**
 * @brief Resolves simultaneous-open collisions per RFC 4271 §6.8.
 * @ingroup BGP_SESSION
 *
 * When two BGP speakers both initiate TCP connections to each other the
 * result is two concurrent sessions.  The collision is resolved by comparing
 * BGP Identifiers (router-IDs): the speaker with the higher ID keeps its
 * outgoing session, the speaker with the lower ID keeps the incoming session,
 * and the loser's session is sent a Cease / Connection Collision Resolution
 * NOTIFICATION.  Equal BGP Identifiers should not occur; both sessions are
 * closed in that case.
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

