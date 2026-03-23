// NeighborAfConfigs.hpp

#ifndef BGP_NEIGHBOR_AF_CONFIGS_HPP
#define BGP_NEIGHBOR_AF_CONFIGS_HPP

#include <bitset>

#include "configs/registry/router/BgpRegistry.h"
#include "bgp/neighbor/PeerTemplate.h"

namespace routing::bgp
{
struct NeighborAfConfigs
{
    NeighborAfConfigs(const AfiSafi& fam, config::Reference<config::BgpNeighborRegistry>&& cfgs)
        : family(fam), configs(cfgs)
    {}

    template <config::BgpNeighbor F>
    decltype(auto) get()
    {
        if (peerGroup && peerOwnedTable.test(config::toIndex<F>))
            return peerConfigs->get<F>();
        return configs->get<F>();
    }

    template <config::BgpAfBase F>
    decltype(auto) get()
    {
        if (peerGroup && peerOwnedBaseTable.test(config::toIndex<F>))
            return peerConfigs->get<config::BgpNeighbor::AF_BASE>().local()->get<F>();
        return peerConfigs->get<config::BgpNeighbor::AF_BASE>().local()->get<F>();
    }

    template <config::BgpNeighbor F> decltype(auto) get() const
    {
        if (peerGroup && peerOwnedTable.test(config::toIndex<F>))
            return std::as_const(peerConfigs->get<F>());
        return std::as_const(configs->get<F>());
    }

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

    bool setPeerGroup(PeerGroup* group)
    {
        if (peerPolicy)
            return false;
        peerGroup = group;
        peerConfigs = group ? group->getAfConfigs(family) : nullptr;
        if (peerGroup) assert(peerConfigs);
        configs->setMask(group ? group->getAfConfigs(family) : nullptr);
        return true;
    }

    bool setPeerPolicyTemplate(PeerPolicyTemplate* pp)
    {
        if (peerGroup)
            return false;
        peerPolicy = pp;
        configs->setMask(peerPolicy ? &pp->getConfigs() : nullptr);
        return true;
    }

    config::BgpNeighborRegistry& getConfigs() { return configs.get(); }
    const config::BgpNeighborRegistry& getConfigs() const { return configs.get(); }
    PeerGroup* getPeerGroup() { return peerGroup; }
    const PeerGroup* getPeerGroup() const { return peerGroup; }
    PeerPolicyTemplate* getPeerPolicyTemplate() { return peerPolicy; }
    const PeerPolicyTemplate* getPeerPolicyTemplate() const { return peerPolicy; }

private:
    AfiSafi family;

    PeerGroup* peerGroup = nullptr;
    config::BgpNeighborRegistry* peerConfigs = nullptr;

    PeerPolicyTemplate* peerPolicy = nullptr;
    config::Reference<config::BgpNeighborRegistry> configs;
};
} // namespace routing

#endif // BGP_NEIGHBOR_AF_CONFIGS_HPP

