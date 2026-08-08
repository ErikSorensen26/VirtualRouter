/**
 * @file NeighborAf.h
 * @brief Per-neighbor, per-address-family state (Adj-RIB-In/-Out, ORF filters, prefix limits).
 */

#ifndef BGP_NEIGHBOR_AF_H
#define BGP_NEIGHBOR_AF_H

#include "bgp/BgpTypes.hpp"
#include "bgp/rib/RibTypes.hpp"
#include "NeighborAfConfigs.hpp"
#include "bgp/af/AddressFamily.hpp"
#include "bgp/af/DirtyState.hpp"

class Internal_BgpTest;

namespace routing::bgp
{
class Neighbor;
class BgpProcess;
class PeerGroup;
class PeerPolicyTemplate;
class Session;
class BgpRx;
class BgpTx;
template <typename> class EgressPolicy;

/**
 * @brief Tracks all per-neighbor, per-AFI/SAFI state for one BGP peer.
 *
 * Each activated address family for a neighbor has exactly one NeighborAf instance.
 * It holds:
 *   - The AF-level config accessor (@ref NeighborAfConfigs).
 *   - Whether multi-protocol extensions were negotiated for this family (@ref mpNegotiated).
 *   - The inbound ORF filter received from the peer and the outbound ORF we advertise to it.
 *   - Maximum-prefix limit state and the optional restart timer.
 *   - Slow-peer detection state.
 *
 * All mutable state (ORF filter, slow-peer flags, maxPfxWarned) is cleared on session reset.
 *
 * @ingroup BGP_NEIGHBOR
 */
class NeighborAf
{
public:
    /**
     * @brief Construct for the given AFI/SAFI and owning Neighbor.
     * @param family The address family this object tracks.
     * @param parent The Neighbor that owns this NeighborAf.
     */
    NeighborAf(const AfiSafi& family, AddressFamilyVariant& af, Neighbor& parent);

    /**
     * @brief Destructor. Cancels any pending maximum-prefix restart timer.
     */
    ~NeighborAf();

    void enqueueSyncAdditionalPaths();
    void enqueueSyncDefaultOriginate();
    void enqueueSyncSlowPeer();
    void enqueueSyncActivate();
    void enqueueSyncAdvertiseDiverse();
    void enqueueMarkAttr(OutAttr attr);
    void enqueueMarkAttrs(OutAttrMask attrs);
    void enqueueMarkInbound(InDirty category);
    void enqueueConnectionRestart();

    // Synchronous mark (caller already on the BGP scheduler thread).
    void markAttr(OutAttr attr);
    void markAttrs(OutAttrMask attrs);

    const AfiSafi family; ///< The address family covered by this object.
    const Neighbor& getParent() const noexcept { return parent; }

private:
    friend class ::Internal_BgpTest;
    template <typename>
    friend class AddressFamilyInstance;
    template <typename>
    friend class EgressPolicy;
    friend PeerTemplateTable;
    friend class Session;
    friend class BgpRx; // reads updateOrfFilter() on inbound ROUTE-REFRESH ORF
    friend class BgpTx; // reads orfOutbound when appending the ORF capability

    bool mpNegotiated; ///< True when MP-BGP was negotiated for this family during OPEN.

    /**
     * @brief Replace the inbound ORF filter with the entries received in a ROUTE-REFRESH ORF.
     * @param entries New ORF prefix entries from the peer.
     */
    void updateOrfFilter(const std::vector<OrfPrefixEntry>& entries);

    /**
     * TODO add doxy comment
     */
    void invalidate();

    /**
     * @brief Schedule a maximum-prefix restart after the configured interval.
     * @param minutes Delay in minutes before the session may be re-established.
     */
    void schedulePfxRestart(uint16_t minutes);

    /**
     * @brief Cancel any pending maximum-prefix restart timer.
     */
    void cancelPfxRestart();

    /**
     * TODO add doxy comment
     */
    Session* getSession() noexcept;

    /**
     * TODO add doxy comment
     */
    std::optional<uint32_t> getRemoteAs() const noexcept;

    /**
     * TODO add doxy comment
     */
    bool isEbgp() const noexcept;

    /**
     * TODO add doxy comment
     */
    bool isConfedEbgp() const noexcept;

    std::vector<OrfPrefixEntry> orfOutbound; ///< ORF filter we advertise TO this peer — set from inbound prefix-list config.
    std::vector<OrfPrefixEntry> orfFilter; ///< ORF filter received FROM this peer — applied to our Adj-RIB-Out. Cleared on session reset.

    // Maximum-prefix tracking. Reset on session reset.
    bool maxPfxWarned = false; ///< True once the maximum-prefix warning threshold has been crossed.
    // Slow peer tracking. Reset on session reset.
    bool isSlowPeer = false;                              ///< True when the peer has been classified as slow.
    std::chrono::steady_clock::time_point slowFirstSeen{}; ///< Timestamp when the peer was first seen as slow.

    OutAttrMask dirtyOut; ///< Outbound attributes marked dirty since the last flush.

    OutAttrMask drainDirtyOut() noexcept
    {
        OutAttrMask m = dirtyOut;
        dirtyOut.reset();
        return m;
    }

private:

    Neighbor& parent;
    NeighborAfConfigs configs;
    AddressFamilyVariant& af;

    struct Private
    {
    private:
        friend class NeighborAf;
        uint32_t maxPfxRestartTimerId = 0; ///< Scheduler timer ID for the max-prefix restart delay.
    } priv;
};
} // namespace routing

#endif // BGP_NEIGHBOR_AF_H

