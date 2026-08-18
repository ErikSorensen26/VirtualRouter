/**
 * @file IntraOriginator.h
 * @brief Abstract base for intra-area LSA origination (Router and Network LSAs).
 */

#ifndef OSPF_INTRA_ORIGINATOR_H
#define OSPF_INTRA_ORIGINATOR_H

#include "ospf/database/LsdbTypes.hpp"

namespace config { struct OspfAreaRegistry; struct OspfRegistry; struct OspfInterfaceBaseBaseRegistry; }
class Internal_OspfTest;

namespace routing::ospf
{
struct OspfInterfaceId;
class Area;
class OriginatorContext;
class OspfInterfaceBase;
class InterfaceManager;
class TopologyTable;
class NeighborTable;
class Neighbor;

/**
 * @brief Abstract base class that builds the LSAs describing one OSPF area's own topology.
 * @ingroup OSPF_AREA
 *
 * `IntraOriginator` owns the *policy* for intra-area origination: it decides
 * when the local router's Router LSA and Network LSAs must change and computes
 * their new bodies.  It does not implement throttling, group-paced refresh, or
 * the LSDB install path — that *mechanism* lives in the area's
 * @ref OriginatorContext (`area.originContext`), to which every computed body
 * is submitted.  Inter-area summarisation (ABR role) and external
 * redistribution (ASBR role) are likewise out of scope; see
 * @ref InterOriginator and @ref ExternalOriginator.
 *
 * Concrete subclasses (`IntraOriginatorV2`, `IntraOriginatorV3`) implement the
 * version-specific link-state building logic — how Router LSA links are
 * encoded and how Network LSAs are formed.
 *
 * ## Architectural Role
 * `IntraOriginator` sits between the `Area` (which detects topology events and
 * calls `updateInterface`, `fullRefresh`, etc.) and the origination mechanism
 * (`OriginatorContext`), which throttles the submission and installs it into
 * the LSDB / flood pipeline via the area's `processLsa`.
 *
 * ## Lifecycle & Ownership
 * Created and owned by the `Area`.  The @ref OriginatorContext reference
 * stored in the protected `context` member must remain valid for the entire
 * lifetime of the IntraOriginator. All methods run on the area's
 * single-threaded `ProcessQueue`.
 *
 * @warning Subclasses must not submit LSAs from outside the area's process
 * queue thread.
 *
 * @see Area
 * @see OriginatorContext
 * @see IntraOriginatorV2
 * @see IntraOriginatorV3
 */
class IntraOriginator
{
    friend class ::Internal_OspfTest;

    friend class OriginatorContext;
public:

    /**
     * @brief Constructs the originator and binds it to its area's origination context.
     *
     * Does not originate any LSAs at construction time; the first origination
     * is triggered by the area calling `fullRefresh()` after the process starts.
     *
     * @param ctx The origination context this originator submits into.
     */
    IntraOriginator(OriginatorContext& ctx);

    /**
     * @brief Destructs the originator.
     *
     * Does not withdraw any LSAs; outstanding origination timers are owned by
     * the area's `OriginatorContext` and cancelled during area teardown.
     */
    virtual ~IntraOriginator();
    
    /**
     * @brief Factory: builds the version-specific originator for the context's owning process.
     *
     * Returns a heap-allocated @ref IntraOriginatorV3 when the owning process
     * runs OSPFv3, otherwise an @ref IntraOriginatorV2.  The caller (the
     * owning @ref Area) takes ownership and must `delete` it on teardown.
     * Defined in IntraOriginatorFactory.cpp so this header does not depend on
     * the concrete subclasses.
     *
     * @param ctx The origination context the new originator submits into.
     */
    static IntraOriginator& create(OriginatorContext& ctx);

    // PUBLIC ORIGINATIONS

    /**
     * @brief Re-originates all LSAs held by this router in the area from scratch.
     *
     * Called at process startup, after a router-ID change, or after an area
     * reconfiguration that invalidates all previously originated LSAs.
     * Implementations should recompute and resubmit every LSA type the router
     * is responsible for.
     */
    virtual void fullRefresh() = 0;

    /**
     * @brief Re-evaluates and re-originates LSAs affected by a change on the given interface.
     *
     * Called when an interface state machine transitions (e.g. Down→DR,
     * DROther→Backup) or when interface cost changes.  Implementations
     * re-examine the interface and update the Router LSA and, if the interface
     * is a DR segment, the Network LSA.
     *
     * @param ifaceId Interface index that changed.
     */
    virtual void updateInterface(uint32_t ifaceId) = 0;

    /**
     * @brief Sets the LSA identified by `key` to MaxAge and submits it for flooding.
     *
     * Public because the ABR machinery (@ref InterOriginator) flushes this
     * router's ASBR-summary LSAs through it when the last contributing
     * external route is withdrawn.
     *
     * @param key Key of the LSA to expire.
     */
    virtual void expire(LsaKey& key) = 0;

protected:

    // ADDING (version-specific)

    /**
     * @brief Builds and submits a new Router LSA instance.
     *
     * @param id          Optional override for the Link-State ID (normally the router ID).
     * @param refresh     True if this is a periodic refresh rather than a topology-driven update.
     * @param fullRefresh True if all links must be recomputed unconditionally.
     */
    virtual void addRouterLsa(std::optional<uint32_t> id, bool refresh, bool fullRefresh = false) = 0;

    /**
     * @brief Builds and submits a new Network LSA for the given DR interface.
     *
     * @param iface   The DR interface for which to originate a Network LSA.
     * @param refresh True if this is a periodic refresh.
     */
    virtual void addNetworkLsa(const OspfInterfaceBase& iface, bool refresh) = 0;

    // REMOVING (version-specific)

    /**
     * @brief Flushes the Network LSA associated with the given interface (MaxAge it).
     *
     * Called when the local router loses DR status on `ifaceId`.
     *
     * @param ifaceId Interface index whose Network LSA should be flushed.
     */
    virtual void removeNetworkLsa(uint32_t ifaceId) = 0;

    // LSA CACHE STORAGE

    LsaAdvKey                                    lastRouterKey{};       ///< Key of the most recently originated Router LSA.
    std::unordered_set<uint32_t>                 networkLsas{};         ///< Interface IDs for which this router holds an active Network LSA.


    /**
     * @brief Installs an originated LSA into the LSDB and triggers flooding.
     *
     * Called by subclasses after computing a new LSA body.  Writes the record
     * via the area's `LsdbTable` and posts a flood event to all appropriate
     * interfaces.
     *
     * @param key  Key of the LSA to install.
     * @param body Decoded body to store.
     */
    void processLsa(LsaKey& key, LsaBody& body);

    // LINK BUILDING HELPERS

    /**
     * @brief Adds the appropriate Router LSA link for the given interface to the in-progress LSA body.
     *
     * Selects transit, point-to-point, stub, or virtual-link encoding based on
     * the interface state machine state and neighbor adjacency status.  May
     * also trigger Network LSA origination if the interface is in DR state.
     *
     * @param router          In-progress Router LSA body being built.
     * @param iface           Interface to add a link for.
     * @param refresh         True if this is a periodic refresh build.
     * @param attemptNetLsa   If true and the interface is DR, attempt to originate a Network LSA.
     */
    void addRouterLink(LsaBody& router, const OspfInterfaceBase& iface, bool refresh, bool attemptNetLsa = false);

    /**
     * @brief Adds a transit (broadcast/NBMA DR-segment) link to the Router LSA body.
     *
     * @param router In-progress Router LSA body.
     * @param iface  DR interface to encode as a transit link.
     * @param nbr    Designated Router neighbor (nullptr if the local router is the DR).
     */
    virtual void addTransitLink(LsaBody& router, const OspfInterfaceBase& iface, const Neighbor* nbr = nullptr) = 0;

    /**
     * @brief Adds a point-to-point link to the Router LSA body.
     *
     * @param router   In-progress Router LSA body.
     * @param iface    Point-to-point interface.
     * @param neighbor The fully adjacent neighbor on this interface.
     */
    virtual void addP2PLink(LsaBody& router, const OspfInterfaceBase& iface, const Neighbor& neighbor) = 0;

    /**
     * @brief Adds a stub (non-transit) network link to the Router LSA body.
     *
     * Stub links represent directly connected subnets with no OSPF neighbor or
     * on interfaces that have not reached Full adjacency.
     *
     * @param router   In-progress Router LSA body.
     * @param iface    Interface to encode as a stub link.
     * @param fullMask If true, encode the link with a /32 (host) mask instead of the interface prefix mask.
     */
    virtual void addStubLink(LsaBody& router, const OspfInterfaceBase& iface, bool fullMask = false) = 0;

    /**
     * @brief Adds a virtual link to the Router LSA body.
     *
     * @param router In-progress Router LSA body.
     * @param iface  Virtual-link interface.
     * @param vNbr   The virtual-link neighbor (must be in Full state).
     */
    virtual void addVirtualLink(LsaBody& router, const OspfInterfaceBase& iface, const Neighbor& vNbr) = 0;

    /**
     * @brief Removes duplicate adjacent links from a Router LSA link list.
     *
     * Applied after all links are collected to ensure that the same
     * (type, ID, data) tuple does not appear more than once in the LSA.
     *
     * @tparam RouterLink  Router LSA link entry type; must be equality-comparable.
     * @param links        Vector of links to deduplicate in-place.
     */
    template <typename RouterLink>
    void uniqueLinks(std::vector<RouterLink>& links);

    OriginatorContext& context; ///< Owning area's origination mechanism; every computed body is submitted through it.
};

template <typename RouterLink>
void IntraOriginator::uniqueLinks(std::vector<RouterLink>& links)
{
    links.erase(std::unique(links.begin(), links.end()), links.end());
}
} // namespace routing

#endif
