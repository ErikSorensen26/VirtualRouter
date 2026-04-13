/**
 * @file NeighborAfConfigs.hpp
 * @brief Per-neighbor, per-address-family configuration accessor with peer-group/policy-template inheritance.
 */

#ifndef BGP_NEIGHBOR_AF_CONFIGS_HPP
#define BGP_NEIGHBOR_AF_CONFIGS_HPP

#include <bitset>

#include "configs/registry/router/BgpRegistry.h"
#include "bgp/neighbor/PeerTemplate.h"

namespace routing::bgp
{

/**
 * @brief Holds AF-level configuration for one neighbor + one AFI/SAFI, with optional
 *        inheritance from a PeerGroup or PeerPolicyTemplate.
 *
 * Config reads are transparently redirected to the peer-group or policy-template registry
 * for fields that are owned at the group/template level (tracked by the compile-time
 * bitsets @ref peerOwnedTable and @ref peerOwnedBaseTable).  All other fields are read
 * from the per-neighbor registry.
 *
 * At most one of @c peerGroup or @c peerPolicy may be set at a time.
 *
 * @ingroup BGP_NEIGHBOR
 */
struct NeighborAfConfigs
{
    /**
     * @brief Construct with a specific AFI/SAFI and an owned config registry reference.
     * @param fam  The address family this object covers.
     * @param cfgs Owning reference to the per-neighbor AF config registry.
     */
    NeighborAfConfigs(const AfiSafi& fam, config::BgpNeighborRegistry& cfgs)
        : family(fam), configs(cfgs)
    {}

    /**
     * @brief Read a BgpNeighbor config field, falling back to the peer-group when applicable.
     * @tparam F Config field tag.
     * @return Reference to the config field value.
     */
    template <config::BgpNeighbor F>
    decltype(auto) get()
    {
        if (peerGroup && peerOwnedTable.test(config::toIndex<F>))
            return (*peerConfigs).reg.get<F>();
        return configs.reg.get<F>();
    }

    /**
     * @brief Read a BgpAfBase config field, falling back to the peer-group when applicable.
     * @tparam F Config field tag.
     * @return Reference to the config field value.
     */
    template <config::BgpAfBase F>
    decltype(auto) get()
    {
        if (peerGroup && peerOwnedBaseTable.test(config::toIndex<F>))
            return peerConfigs->reg.get<config::BgpNeighbor::AF_BASE>().get().reg.get<F>();
        return peerConfigs->reg.get<config::BgpNeighbor::AF_BASE>().get().reg.get<F>();
    }

    /**
     * @brief Read a BgpNeighbor config field (const overload).
     * @tparam F Config field tag.
     * @return Const reference to the config field value.
     */
    template <config::BgpNeighbor F> decltype(auto) get() const
    {
        if (peerGroup && peerOwnedTable.test(config::toIndex<F>))
            return std::as_const(peerConfigs->reg.get<F>());
        return std::as_const(configs.reg.get<F>());
    }

    /**
     * @brief Compile-time bitmask of BgpAfBase fields that are inherited from the peer-group
     *        rather than stored per-neighbor.
     */
    static constexpr std::bitset<config::toIndex<config::BgpAfBase::COUNT>> peerOwnedBaseTable = []{
        std::bitset<config::toIndex<config::BgpAfBase::COUNT>> b;

        b.set(config::toIndex<config::BgpAfBase::ADDITIONAL_PATHS_RECEIVE>);
        b.set(config::toIndex<config::BgpAfBase::ADDITIONAL_PATHS_SEND>);
        b.set(config::toIndex<config::BgpAfBase::ADVERTISE_ADDITIONAL_PATHS_ALL>);
        b.set(config::toIndex<config::BgpAfBase::ADVERTISE_ADDITIONAL_PATHS_BEST>);
        b.set(config::toIndex<config::BgpAfBase::ADVERTISE_ADDITIONAL_GROUP_BEST>);
        b.set(config::toIndex<config::BgpAfBase::ADVERTISE_BEST_EXTERNAL>);

        return b;
    }();

    /**
     * @brief Compile-time bitmask of BgpNeighbor fields that are inherited from the peer-group
     *        rather than stored per-neighbor.
     */
    static constexpr std::bitset<config::toIndex<config::BgpNeighbor::COUNT>> peerOwnedTable = []{
        std::bitset<config::toIndex<config::BgpNeighbor::COUNT>> b;

        b.set(config::toIndex<config::BgpNeighbor::ADVERTISE_DIVERSE_PATH_BACKUP>);
        b.set(config::toIndex<config::BgpNeighbor::ADVERTISE_DIVERSE_PATH_MPATH>);
        b.set(config::toIndex<config::BgpNeighbor::ADVERTISE_MAP>);
        b.set(config::toIndex<config::BgpNeighbor::ADVERTISE_MAP_EXIST_CONDITION>);
        b.set(config::toIndex<config::BgpNeighbor::ADVERTISE_MAP_NON_EXIST_CONDITION>);
        b.set(config::toIndex<config::BgpNeighbor::ANNOUNCE_RPKI_STATE>);
        b.set(config::toIndex<config::BgpNeighbor::ORF_BOTH>);
        b.set(config::toIndex<config::BgpNeighbor::ORF_RECEIVE>);
        b.set(config::toIndex<config::BgpNeighbor::DISTRIBUTE_LIST_OUT>);
        b.set(config::toIndex<config::BgpNeighbor::DISTRIBUTE_LIST_OUT_INTERFACE>);
        b.set(config::toIndex<config::BgpNeighbor::FILTER_LIST_OUT>);
        b.set(config::toIndex<config::BgpNeighbor::NEXT_HOP_SELF>);
        b.set(config::toIndex<config::BgpNeighbor::NEXT_HOP_SELF_ALL>);
        b.set(config::toIndex<config::BgpNeighbor::NEXT_HOP_UNCHANGED>);
        b.set(config::toIndex<config::BgpNeighbor::PREFIX_LIST_OUT>);
        b.set(config::toIndex<config::BgpNeighbor::REMOVE_PRIVATE_AS>);
        b.set(config::toIndex<config::BgpNeighbor::REMOVE_PRIVATE_AS_ALL>);
        b.set(config::toIndex<config::BgpNeighbor::ROUTE_MAP_OUT>);
        b.set(config::toIndex<config::BgpNeighbor::ROUTE_REFLECTOR_CLIENT>);
        b.set(config::toIndex<config::BgpNeighbor::ROUTE_SERVER_CLIENT>);
        b.set(config::toIndex<config::BgpNeighbor::ROUTE_SERVER_CLIENT_CONTEXT>);
        b.set(config::toIndex<config::BgpNeighbor::SEND_COMMUNITY>);
        b.set(config::toIndex<config::BgpNeighbor::SEND_COMMUNITY_BOTH>);
        b.set(config::toIndex<config::BgpNeighbor::SEND_COMMUNITY_EXTENDED>);
        b.set(config::toIndex<config::BgpNeighbor::SEND_COMMUNITY_STANDARD>);
        b.set(config::toIndex<config::BgpNeighbor::UNSUPPRESS_MAP>);

        return b;
    }();

    /**
     * @brief Attach this neighbor AF to a PeerGroup for config inheritance.
     *
     * Fails if a PeerPolicyTemplate is already set.  Passing @c nullptr detaches
     * any existing peer-group association.
     *
     * @param group Pointer to the PeerGroup, or nullptr to detach.
     * @return @c true on success; @c false if a policy template is already bound.
     */
    bool setPeerGroup(PeerGroup* group)
    {
        if (peerPolicy)
            return false;
        peerGroup = group;
        peerConfigs = group ? group->getAfConfigs(family) : nullptr;
        if (peerGroup) assert(peerConfigs);
        configs.reg.setMask(group ? &group->getAfConfigs(family)->reg : nullptr);
        return true;
    }

    /**
     * @brief Attach this neighbor AF to a PeerPolicyTemplate for config inheritance.
     *
     * Fails if a PeerGroup is already set.  Passing @c nullptr detaches any existing
     * policy-template association.
     *
     * @param pp Pointer to the PeerPolicyTemplate, or nullptr to detach.
     * @return @c true on success; @c false if a peer-group is already bound.
     */
    bool setPeerPolicyTemplate(PeerPolicyTemplate* pp)
    {
        if (peerGroup)
            return false;
        peerPolicy = pp;
        configs.reg.setMask(peerPolicy ? &pp->getConfigs().reg : nullptr);
        return true;
    }

    config::BgpNeighborRegistry& getConfigs() { return configs; }
    const config::BgpNeighborRegistry& getConfigs() const { return configs; }
    PeerGroup* getPeerGroup() { return peerGroup; }
    const PeerGroup* getPeerGroup() const { return peerGroup; }
    PeerPolicyTemplate* getPeerPolicyTemplate() { return peerPolicy; }
    const PeerPolicyTemplate* getPeerPolicyTemplate() const { return peerPolicy; }

private:
    AfiSafi family; ///< The address family this object covers.

    PeerGroup* peerGroup = nullptr;            ///< Non-owning; set when this neighbor belongs to a peer-group.
    config::BgpNeighborRegistry* peerConfigs = nullptr; ///< AF-level config from the peer-group (non-owning).

    PeerPolicyTemplate* peerPolicy = nullptr;  ///< Non-owning; set when a policy template is applied.
    config::BgpNeighborRegistry& configs; ///< Per-neighbor AF config registry (owned reference).
};
} // namespace routing

#endif // BGP_NEIGHBOR_AF_CONFIGS_HPP

