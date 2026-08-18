/**
 * @file FloodTypes.hpp
 * @brief Result and action types produced by LSDB install decisions and consumed by the flood pipeline.
 */

#ifndef OSPF_FLOOD_TYPES_HPP
#define OSPF_FLOOD_TYPES_HPP

#include <cstdint>

namespace routing::ospf
{

/**
 * @brief Reason an LSA is being flooded onto a segment.
 * @ingroup OSPF_AREA
 */
enum class FloodReason
{
    REFRESH, ///< Periodic self-refresh; sequence number advanced, content unchanged.
    UPDATE,  ///< Content changed; new instance supersedes the prior one.
    FLUSH,   ///< LSA is being withdrawn; age set to MaxAge before flooding.
};

/**
 * @brief Metadata attached to a flood event describing why the LSA is being sent.
 * @ingroup OSPF_AREA
 */
struct FloodInfo
{
    FloodReason reason;
};

/**
 * @brief Result of comparing two instances of the same LSA (RFC 2328 §13.1).
 * @ingroup OSPF_AREA
 *
 * Drives whether an incoming LSA is installed, acknowledged, or triggers a
 * fight-back origination of a newer self-originated instance.
 */
enum class LsaCompareResult
{
    NEWER, ///< Incoming instance is more recent than the stored one.
    OLDER, ///< Incoming instance is older than the stored one.
    SAME,  ///< Incoming instance is identical to the stored one.
};

/**
 * @brief Action the LSDB install path must take after evaluating an incoming LSA.
 * @ingroup OSPF_AREA
 *
 * Each value corresponds to one branch of the RFC 2328 §13 receive procedure.
 * The action is produced by the comparison logic and consumed by the area's
 * flood manager, which performs storage updates, flooding, and fight-back
 * origination as directed.
 */
enum class InstallAction : uint8_t
{
    REJECT_INVALID,     ///< Checksum invalid or caller explicitly marked the LSA bad; discard silently.
    IGNORE_OLDER,       ///< Incoming is older than the installed instance; send a direct ack with the stored copy.
    IGNORE_DUPLICATE,   ///< Same instance as stored; may still update the stored age if the incoming copy is younger.
    INSTALL_NEWER,      ///< Incoming is newer; replace stored record and flood outward.
    FLUSH_MAX_AGE,      ///< Incoming is newer and carries MaxAge; flood the flush but do not retain in LSDB.
    FIGHT_BACK_SELF,    ///< Self-originated key arrived as newer from a peer; store it, then immediately originate a still-newer instance.
};

/**
 * @brief Aggregated output of one LSDB install evaluation, directing all downstream actions.
 * @ingroup OSPF_AREA
 *
 * Produced by the LSDB comparison and install logic after processing one
 * incoming LSA.  Every field is a directive: the caller reads these flags and
 * fields to decide what storage writes, floods, acknowledgements, and SPF
 * reschedules to perform.
 *
 * ## Architectural Role
 * `InstallResult` is a pure data transfer object.  It carries no behaviour of
 * its own; all side effects are performed by the flood manager and area
 * originator that consume it.  Keeping the decision logic separate from the
 * action execution makes both easier to test.
 */
struct InstallResult final
{
    InstallAction action{InstallAction::IGNORE_OLDER};
    LsaCompareResult compare{LsaCompareResult::SAME};

    // Storage instructions:
    bool newLsa{false};              ///< True if this LSA key has never been seen before (first install).
    bool shouldStoreReplace{false};  ///< Replace the stored header and body with the incoming instance.
    bool shouldUpdateAgeOnly{false}; ///< Same instance; overwrite only the stored header age field.
    bool shouldAck{true};            ///< Send an acknowledgement; false only for LSAs that failed checksum.
    uint16_t newStoredAge{0};        ///< Age to write when `shouldUpdateAgeOnly` is true.

    // Topology impact
    bool affectsSpfGraph{false}; ///< LSA type participates in the SPF graph (Router/Network LSA).
    bool topologyChanged{false}; ///< The graph nodes or edges actually changed, requiring SPF reschedule.

    // Flooding/origination signals:
    bool shouldFlood{false};     ///< Enqueue this incoming instance (or the MaxAge flush) for flooding.
    bool shouldFightBack{false}; ///< Originate a newer self-originated instance to reclaim ownership.
};
} // namespace routing::ospf

#endif // OSPF_FLOOD_TYPES_HPP
