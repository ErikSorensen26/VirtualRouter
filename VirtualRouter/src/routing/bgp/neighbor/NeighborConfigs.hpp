/**
 * @file NeighborConfigs.hpp
 * @brief Session-level configuration accessor for a BGP neighbor with peer-group/session-template inheritance.
 */

#ifndef BGP_NEIGHBOR_CONFIGS_HPP
#define BGP_NEIGHBOR_CONFIGS_HPP

#include <bitset>

#include "configs/registry/router/BgpRegistry.h"
#include "bgp/neighbor/PeerTemplate.h"

namespace routing::bgp
{

/**
 * @brief Holds session-level configuration for one BGP neighbor, with optional inheritance
 *        from a PeerGroup or PeerSessionTemplate.
 *
 * Config reads are transparently redirected to the peer-group's session registry for fields
 * listed in the compile-time @ref peerOwnedTable bitmask.  All other fields are read from
 * the per-neighbor session registry.
 *
 * At most one of @c peerGroup or @c peerSession may be set at a time.
 *
 * @ingroup BGP_NEIGHBOR
 */
struct NeighborConfigs
{
    /**
     * @brief Construct with an owned reference to the per-neighbor session config registry.
     * @param cfgs Owning reference to the BgpNeighborSessionRegistry for this neighbor.
     */
    NeighborConfigs(config::BgpNeighborSessionRegistry& cfgs)
        : configs(cfgs) {}

    /**
     * @brief Read a BgpNeighborSession config field, falling back to the peer-group when applicable.
     * @tparam F Config field tag.
     * @return Reference to the config field value.
     */
    template <config::BgpNeighborSession F>
    decltype(auto) get()
    {
        if (peerGroup && peerOwnedTable.test(config::toIndex<F>))
            return peerGroup->getSessionConfigs().get<F>();
        return configs.get<F>();
    }

    /**
     * @brief Read a BgpNeighborSession config field (const overload).
     * @tparam F Config field tag.
     * @return Const reference to the config field value.
     */
    template <config::BgpNeighborSession F>
    decltype(auto) get() const
    {
        if (peerGroup && peerOwnedTable.test(config::toIndex<F>))
            return std::as_const(peerGroup->getSessionConfigs().get<F>());
        return std::as_const(configs.get<F>());
    }

    /**
     * @brief Compile-time bitmask of BgpNeighborSession fields that are inherited from the
     *        peer-group rather than stored per-neighbor.
     *
     * Currently covers LOCAL_AS and its sub-options.
     */
    static constexpr std::bitset<config::toIndex<config::BgpNeighborSession::COUNT>> peerOwnedTable = []{
        std::bitset<config::toIndex<config::BgpNeighborSession::COUNT>> b;

        b.set(config::toIndex<config::BgpNeighborSession::LOCAL_AS>);
        b.set(config::toIndex<config::BgpNeighborSession::LOCAL_AS_AS>);
        b.set(config::toIndex<config::BgpNeighborSession::LOCAL_AS_NO_PREPEND>);
        b.set(config::toIndex<config::BgpNeighborSession::LOCAL_AS_REPLACE_AS>);
        b.set(config::toIndex<config::BgpNeighborSession::LOCAL_AS_DUAL_AS>);

        return b;
    }();

    /**
     * @brief Attach this neighbor to a PeerGroup for session-config inheritance.
     *
     * Fails if a PeerSessionTemplate is already set.  Pass @c nullptr to detach.
     *
     * @param group Pointer to the PeerGroup, or nullptr to detach.
     * @return @c true on success; @c false if a session template is already bound.
     */
    bool setPeerGroup(PeerGroup* group)
    {
        if (peerSession)
            return false;
        peerGroup = group;
        configs.setMask(group ? &group->getSessionConfigs() : nullptr);
        return true;
    }

    /**
     * @brief Attach this neighbor to a PeerSessionTemplate for session-config inheritance.
     *
     * Fails if a PeerGroup is already set.  Pass @c nullptr to detach.
     *
     * @param ps Pointer to the PeerSessionTemplate, or nullptr to detach.
     * @return @c true on success; @c false if a peer-group is already bound.
     */
    bool setPeerSessionTemplate(PeerSessionTemplate* ps)
    {
        if (peerGroup)
            return false;
        peerSession = ps;
        configs.setMask(ps ? &ps->getConfigs() : nullptr);
        return true;
    }

    config::BgpNeighborSessionRegistry& getConfigs() { return configs; }
    const config::BgpNeighborSessionRegistry& getConfigs() const { return configs; }
    PeerGroup* getPeerGroup() { return peerGroup; }
    const PeerGroup* getPeerGroup() const { return peerGroup; }
    PeerSessionTemplate* getPeerSessionTemplate() { return peerSession; }
    const PeerSessionTemplate* getPeerSessionTemplate() const { return peerSession; }

private:
    PeerGroup* peerGroup = nullptr;        ///< Non-owning; set when this neighbor belongs to a peer-group.
    PeerSessionTemplate* peerSession = nullptr; ///< Non-owning; set when a session template is applied.
    config::BgpNeighborSessionRegistry& configs; ///< Per-neighbor session config registry (owned reference).
};
} // namespace routing

#endif // BGP_NEIGHBOR_CONFIGS_HPP

