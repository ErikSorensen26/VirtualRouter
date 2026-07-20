/**
 * @file Retransmission.hpp
 * @brief Per-neighbor retransmission state: DBD, LSR, and LSU tracking.
 */

#ifndef RETRNASMISSION_HPP
#define RETRNASMISSION_HPP

#include "ospf/transmission/OspfPacket.hpp"
#include "ospf/database/LsdbTypes.hpp"
#include "RetransmissionList.h"

namespace routing::ospf
{
class Neighbor;
class UnicastPacket;
struct LsaRecordRef;
struct LsaRecord;
struct LsaKey;

/**
 * @brief Aggregates all retransmission state for one OSPF neighbor adjacency.
 * @ingroup OSPF_NEIGHBOR
 *
 * During adjacency formation and normal operation OSPF requires reliable
 * delivery of Database Description (DBD), Link-State Request (LSR), and
 * Link-State Update (LSU) packets. `Retransmission` bundles:
 *
 * - **`outboundLsus`** — LSAs that have been flooded to this neighbor but not
 *   yet acknowledged. Keyed and stored by `LsaKey → LsaRecordRef`.
 * - **`outboundLsrs`** — LSAs that have been requested from this neighbor but
 *   not yet received. Keyed and stored by `LsaKey → LsaKey`.
 * - **`dbdTimerId`** — ID of the active DBD retransmission timer. A non-zero
 *   value means the last DBD packet has not been acknowledged yet.
 * - **`dbdPacket`** — A copy of the last DBD packet sent, held so it can be
 *   retransmitted verbatim without re-encoding.
 *
 * ## Architectural Role
 * `Retransmission` is owned exclusively by `Neighbor`. It is not shared and
 * has no upward references beyond what `RetransmissionList` holds to the
 * process and interface (used for config access only).
 *
 * ## Lifecycle & Ownership
 * Constructed by `Neighbor` and destroyed with it. Both `RetransmissionList`
 * members are cleared automatically when `Neighbor` transitions to a state
 * that requires a clean slate (e.g. ExStart restart or Down).
 *
 * @see RetransmissionList
 * @see Neighbor
 */
class Retransmission
{
public:
    /**
     * @brief Constructs the retransmission container for a neighbor.
     *
     * Passes the process and interface references to both `RetransmissionList`
     * members so they can read retransmit-limit configuration.
     *
     * @param process  The owning OSPF process.
     * @param iface    The interface on which this neighbor was formed.
     */
    Retransmission(OspfInterfaceBase& iface, const config::OspfRegistry& cfgs)
        : outboundLsus(iface, cfgs), outboundLsrs(iface, cfgs) {}

    uint32_t dbdTimerId; ///< Active DBD retransmission timer ID; 0 when no DBD is pending acknowledgment.

    // RELIABILITY

    /**
     * @brief Returns the outbound LSU retransmission list.
     *
     * Holds LSAs flooded to this neighbor that are awaiting an explicit
     * Link-State Acknowledgment. Entries are removed when the matching
     * LSAck is received or when the LSA is superseded.
     */
    RetransmissionList<LsaKey, LsaRecordRef>& lsus() { return outboundLsus; }

    /**
     * @brief Returns the const outbound LSU retransmission list.
     *
     * Holds LSAs flooded to this neighbor that are awaiting an explicit
     * Link-State Acknowledgment. Entries are removed when the matching
     * LSAck is received or when the LSA is superseded.
     */
    const RetransmissionList<LsaKey, LsaRecordRef>& lsus() const { return outboundLsus; }

    /**
     * @brief Returns the outbound LSR retransmission list.
     *
     * Holds LSA keys that have been requested from this neighbor via
     * Link-State Request but whose corresponding LSUs have not yet arrived.
     * Entries are removed when the LSA is received or the adjacency resets.
     */
    RetransmissionList<LsaKey, LsaKey>& lsrs() { return outboundLsrs; }

    /**
     * @brief Returns the const outbound LSR retransmission list.
     *
     * Holds LSA keys that have been requested from this neighbor via
     * Link-State Request but whose corresponding LSUs have not yet arrived.
     * Entries are removed when the LSA is received or the adjacency resets.
     */
    const RetransmissionList<LsaKey, LsaKey>& lsrs() const { return outboundLsrs; }

    /**
     * @brief Returns true while the DBD retransmission timer is active.
     */
    bool getDbdActive() { return dbdTimerId != 0; }

    UnicastPacket dbdPacket; ///< Encoded copy of the last DBD packet, resent verbatim on timer expiry.

private:
    RetransmissionList<LsaKey, LsaRecordRef> outboundLsus; ///< Per-neighbor LSU retransmission list.
    RetransmissionList<LsaKey, LsaKey>       outboundLsrs; ///< Per-neighbor LSR retransmission list.
};

} // namespace routing::ospf

#endif // RETRNASMISSION_HPP
