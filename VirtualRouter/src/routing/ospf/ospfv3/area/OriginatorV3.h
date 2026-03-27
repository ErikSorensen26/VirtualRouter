/**
 * @file OriginatorV3.h
 * @brief OSPFv3 LSA originator: builds and re-originates all OSPFv3 LSA types for one area.
 */

/**
 * @defgroup OSPF_V3 OSPFv3
 * @ingroup OSPF
 * @brief OSPFv3-specific implementations: area originator, LSA database, and packet dispatcher.
 */

/**
 * @defgroup OSPF_V3_AREA OSPFv3 Area
 * @ingroup OSPF_V3
 * @brief OSPFv3 LSA origination for one area.
 */

#ifndef OSPF_ORIGINATOR_V3_H
#define OSPF_ORIGINATOR_V3_H

#include <deque>

#include "ospf/area/Originator.h"

namespace routing::ospf
{

/**
 * @brief OSPFv3 concrete implementation of the @ref Originator base class.
 * @ingroup OSPF_V3_AREA
 *
 * `OriginatorV3` overrides every abstract origination hook in @ref Originator to
 * produce OSPFv3 wire-format LSA bodies. The key difference from OSPFv2 is that
 * OSPFv3 separates topology information from prefix information:
 *
 * - **Router-LSA** (type 0x2001): carries only router links (transit, P2P, virtual).
 * - **Intra-Area-Prefix-LSA** (type 0x2009): carries the stub and loopback prefixes
 *   that in OSPFv2 would appear directly in the Router-LSA or Network-LSA.
 * - **Network-LSA** (type 0x2002): lists attached routers; prefix info is in a
 *   separate Intra-Area-Prefix-LSA associated with this LSA.
 * - **Link-LSA** (type 0x0008): per-link LSA advertising the link-local address and
 *   on-link prefixes to neighbors on the same segment.
 * - **Inter-Area-Prefix-LSA / Inter-Area-Router-LSA** (type 0x2003 / 0x2004): summary
 *   LSAs originated by ABRs, handled via `originateSummary` / `addAsbrLsa`.
 * - **AS-External-LSA** (type 0x4005): redistributed routes, generated via `addExternal`.
 *
 * Because OSPFv3 may need multiple Router-LSAs and multiple Intra-Area-Prefix-LSAs
 * (one per Router-LSA fragment and one per Network-LSA), this class manages two
 * separate LS-ID allocation queues (`routerLsidQueue` and `prefixLsidQueue`) to
 * reuse freed IDs before advancing the high-water marks.
 *
 * ## Architectural Role
 * Owned by an @ref Area. All originations are serialized through the area's
 * `ProcessQueueRef`. The class does not transmit LSAs itself; once an LSA body is
 * built it is handed to the base-class `originateLsa<Policy>()` which drives the
 * throttle / group-pacing machinery before flooding.
 *
 * ## Lifecycle & Ownership
 * Constructed by `Area` when OSPFv3 is enabled. Destroyed when the area is removed
 * or the OSPF process shuts down. `fullRefresh()` is called once at startup to
 * originate the initial LSA set.
 *
 * @warning The LS-ID queues are only valid for the lifetime of the originator. If
 * the originator is destroyed while LSAs are still in the LSDB, stale LS-IDs may
 * be reused on the next instantiation, which can confuse neighbors.
 *
 * @see Originator, OriginatorV2, Area
 */
class OriginatorV3 : public Originator
{
public:
    /**
     * @brief Constructs the OSPFv3 originator for a given area.
     *
     * Initialises LS-ID allocation state. No LSAs are originated until
     * `fullRefresh()` is called.
     *
     * @param a The area that owns this originator.
     */
    OriginatorV3(Area& a);

    /**
     * @brief Destroys the originator and cancels any pending group-pacing timers.
     *
     * Does not expire outstanding LSAs; the LSDB retains them until they age out
     * or the process explicitly flushes them.
     */
    ~OriginatorV3() override;

    /**
     * @brief Originates or refreshes the complete OSPFv3 LSA set for this area.
     *
     * Called at startup and whenever a configuration change requires a full
     * re-advertisement (e.g., area type change). Rebuilds Router-LSA(s),
     * Intra-Area-Prefix-LSA(s), Network-LSA(s), and Link-LSA(s) for all
     * attached interfaces.
     */
    void fullRefresh() override;

    /**
     * @brief Triggers re-origination of all LSAs affected by a change on one interface.
     *
     * Updates the Router-LSA, the relevant Intra-Area-Prefix-LSA, the Network-LSA
     * (if the interface is a DR), and the Link-LSA for the affected segment.
     *
     * @param ifaceId Interface index whose state changed.
     */
    void updateInterface(uint32_t ifaceId) override;

    /**
     * @brief Originates or expires a Type-5/Type-7 AS-External-LSA for a redistributed route.
     *
     * @param asbr   Router ID of the originating ASBR (used for NSSA translation).
     * @param lsid   LS-ID to use for the external LSA.
     * @param remove True to expire the LSA; false to originate or refresh it.
     */
    void addExternal(uint32_t asbr, uint32_t lsid, bool remove) override;

    /**
     * @brief Translates a Type-7 NSSA-LSA into a Type-5 AS-External-LSA at an ABR.
     *
     * Called on the ABR when a Type-7 LSA is received from an NSSA area and must
     * be redistributed into the backbone as a Type-5 LSA.
     *
     * @param key    LSDB key of the Type-7 source LSA.
     * @param lsa    Parsed body of the Type-7 LSA.
     * @param expire True to withdraw the translated LSA; false to originate/refresh it.
     */
    void translateNssaToExternal(const LsaKey& key, const LsaBody& lsa, bool expire) override;

    /**
     * @brief Originates or withdraws the inter-area default route (Type-3 LSA for 0.0.0.0/0).
     *
     * Used when this router is an ABR with `default-information originate` configured
     * for stub or NSSA areas.
     *
     * @param add True to originate the default; false to expire it.
     */
    void addStubDefaultRoute(bool add) override;

    /**
     * @brief Originates or refreshes a single Inter-Area-Prefix-LSA (Type-3 summary).
     *
     * @param lsid   LS-ID for the summary LSA.
     * @param prefix Prefix being summarized.
     * @param cost   Cost to advertise.
     * @param expire True to expire the LSA; false (default) to originate/refresh.
     */
    void originateSummary(uint32_t lsid, const types::IPPrefix& prefix, uint32_t cost, bool expire) override;

protected:
    /**
     * @brief Rebuilds the Router-LSA(s) for this area.
     *
     * OSPFv3 may require multiple Router-LSA fragments when the number of links
     * exceeds the LSA size limit. This method manages the `routerLsidQueue` to
     * allocate and retire LS-IDs as fragments are added or removed.
     *
     * @param ifaceId     If set, only the fragment covering this interface is rebuilt;
     *                    if nullopt, all fragments are rebuilt.
     * @param refresh     True if this is a scheduled LSA refresh rather than a topology change.
     * @param fullRefresh True to force a rebuild of all fragments unconditionally.
     */
    void addRouterLsa(std::optional<uint32_t> ifaceId, bool refresh, bool fullRefresh = false) override;

    /**
     * @brief Rebuilds the Network-LSA for an interface on which this router is the DR.
     *
     * @param iface   The DR interface.
     * @param refresh True if this is a scheduled refresh rather than a topology change.
     */
    void addNetworkLsa(const OspfInterface& iface, bool refresh) override;

    std::vector<LsaKey> lastRouterLsas;    ///< LS-IDs of active Router-LSA fragments; tracked to detect which fragments were removed.
    std::vector<LsaKey> lastRouterPrefixes; ///< LS-IDs of Intra-Area-Prefix-LSAs associated with Router-LSAs.
    std::vector<std::pair<uint32_t, std::vector<LsaKey>>> lastNetworkPrefixes; ///< Per-interface Intra-Area-Prefix-LSA LS-IDs associated with Network-LSAs.

    uint32_t maxPrefixLsid{0};          ///< High-water mark for Intra-Area-Prefix LS-ID allocation.
    std::deque<uint32_t> prefixLsidQueue; ///< Freed Intra-Area-Prefix LS-IDs available for reuse.

    /**
     * @brief Allocates the next available Intra-Area-Prefix LS-ID.
     *
     * Returns the front of `prefixLsidQueue` if non-empty; otherwise increments
     * and returns `maxPrefixLsid`.
     */
    uint32_t findNextPrefixLsid();

    uint32_t maxRouterLsid{0};           ///< High-water mark for Router-LSA fragment LS-ID allocation.
    std::deque<uint32_t> routerLsidQueue; ///< Freed Router-LSA fragment LS-IDs available for reuse.

    /**
     * @brief Allocates the next available Router-LSA fragment LS-ID.
     *
     * Returns the front of `routerLsidQueue` if non-empty; otherwise increments
     * and returns `maxRouterLsid`.
     */
    uint32_t findNextRouterLsid();

    /**
     * @brief Sets the MaxAge on an LSA to flush it from the LSDB and all neighbors.
     *
     * @param key LSDB key of the LSA to expire.
     */
    void expire(LsaKey& key) override;

    /**
     * @brief Originates or refreshes an Inter-Area-Router-LSA (Type-4) for an ASBR.
     *
     * @param asbr    Router ID of the ASBR being advertised.
     * @param refresh True if this is a scheduled refresh.
     */
    void addAsbrLsa(uint32_t asbr, bool refresh = false) override;

    /**
     * @brief Expires all Intra-Area-Prefix-LSAs associated with a Network-LSA when the DR role is lost.
     *
     * @param ifaceId Interface index whose Network-LSA is being removed.
     */
    void removeNetworkLsa(uint32_t ifaceId) override;

    /**
     * @brief Rebuilds the Intra-Area-Prefix-LSA(s) that reference Router-LSA fragments.
     *
     * Called after `addRouterLsa` to synchronize stub and loopback prefix advertisements
     * with any Router-LSA fragment changes.
     *
     * @param routerLsas  List of (Router-LSA key, optional changed flag) pairs from `addRouterLsa`.
     * @param refresh     True if this is a scheduled refresh.
     */
    void addRouterPrefixLsa(std::vector<std::pair<LsaKey, std::optional<bool>>>& routerLsas, bool refresh);

    /**
     * @brief Rebuilds the Intra-Area-Prefix-LSA associated with a Network-LSA.
     *
     * Advertises the on-link prefixes for a segment on which this router is the DR.
     *
     * @param iface   DR interface whose prefixes are being advertised.
     * @param refresh True if this is a scheduled refresh.
     */
    void addNetworkPrefixLsa(const OspfInterface& iface, bool refresh);

    /**
     * @brief Originates or refreshes the Link-LSA for an interface.
     *
     * The Link-LSA (type 0x0008) advertises the interface's link-local address
     * and on-link IPv6 prefixes to neighbors on the same link.
     *
     * @param iface   Interface whose Link-LSA is being built.
     * @param refresh True if this is a scheduled refresh.
     */
    void addLinkLsa(const OspfInterface& iface, bool refresh);

    /**
     * @brief Appends a transit (DR/BDR segment) link record to a Router-LSA body.
     *
     * @param router  Router-LSA body being built.
     * @param iface   Broadcast or NBMA interface.
     * @param nbr     DR neighbor, or null if the DR is this router.
     */
    void addTransitLink(LsaBody& router, const OspfInterface& iface, const Neighbor* nbr = nullptr) override;

    /**
     * @brief Appends a point-to-point link record to a Router-LSA body.
     *
     * @param router   Router-LSA body being built.
     * @param iface    P2P interface.
     * @param neighbor The fully adjacent neighbor on this interface.
     */
    void addP2PLink(LsaBody& router, const OspfInterface& iface, const Neighbor& neighbor) override;

    /**
     * @brief Appends a stub link record for an interface with no full adjacencies.
     *
     * In OSPFv3 stub links in the Router-LSA carry no prefix; the prefix is
     * advertised separately via the Intra-Area-Prefix-LSA. This method records
     * only the link type and metric.
     *
     * @param router   Router-LSA body being built.
     * @param iface    Stub interface.
     * @param fullMask True to advertise a host (/128) prefix instead of the interface prefix.
     */
    void addStubLink(LsaBody& router, const OspfInterface& iface, bool fullMask = false) override;

    /**
     * @brief Appends a virtual link record to a Router-LSA body.
     *
     * @param router  Router-LSA body being built.
     * @param iface   Virtual link interface.
     * @param vNbr    The fully adjacent virtual neighbor.
     */
    void addVirtualLink(LsaBody& router, const OspfInterface& iface, const Neighbor& vNbr) override;
};

} // namespace routing

#endif // OSPF_ORIGINATOR_V3_H

