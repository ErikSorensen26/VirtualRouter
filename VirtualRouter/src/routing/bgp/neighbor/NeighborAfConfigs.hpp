// NeighborAfConfigs.hpp

#ifndef BGP_NEIGHBOR_AF_CONFIGS_HPP
#define BGP_NEIGHBOR_AF_CONFIGS_HPP

#include <bitset>

#include "configs/registry/router/BgpRegistry.h"
#include "bgp/neighbor/PeerTemplate.h"

namespace BGP
{
struct NeighborAfConfigs
{
    NeighborAfConfigs(const AfiSafi& fam, Config::Reference<Config::BgpNeighborRegistry>&& cfgs)
        : family(fam), configs(cfgs)
    {}

    template <Config::BgpNeighbor F>
    decltype(auto) get()
    {
        if (peerGroup && peerOwnedTable.test(Config::toIndex<F>))
            return peerConfigs->get<F>();
        return configs->get<F>();
    }

    template <Config::BgpAfBase F>
    decltype(auto) get()
    {
        if (peerGroup && peerOwnedBaseTable.test(Config::toIndex<F>))
            return peerConfigs->get<Config::BgpNeighbor::AF_BASE>().local()->get<F>();
        return peerConfigs->get<Config::BgpNeighbor::AF_BASE>().local()->get<F>();
    }

    template <Config::BgpNeighbor F> decltype(auto) get() const
    {
        if (peerGroup && peerOwnedTable.test(Config::toIndex<F>))
            return std::as_const(peerConfigs->get<F>());
        return std::as_const(configs->get<F>());
    }

    static constexpr std::bitset<Config::toIndex<Config::BgpAfBase::COUNT>> peerOwnedBaseTable = []{
        std::bitset<Config::toIndex<Config::BgpAfBase::COUNT>> b;

        b.set(Config::toIndex<Config::BgpAfBase::ADDITIONAL_PATHS_RECEIVE>);
        b.set(Config::toIndex<Config::BgpAfBase::ADDITIONAL_PATHS_SEND>);
        b.set(Config::toIndex<Config::BgpAfBase::ADVERTISE_ADDITIONAL_PATHS_ALL>);
        b.set(Config::toIndex<Config::BgpAfBase::ADVERTISE_ADDITIONAL_PATHS_BEST>);
        b.set(Config::toIndex<Config::BgpAfBase::ADVERTISE_ADDITIONAL_GROUP_BEST>);
        b.set(Config::toIndex<Config::BgpAfBase::ADVERTISE_BEST_EXTERNAL>);

        return b;
    }();

    static constexpr std::bitset<Config::toIndex<Config::BgpNeighbor::COUNT>> peerOwnedTable = []{
        std::bitset<Config::toIndex<Config::BgpNeighbor::COUNT>> b;

        b.set(Config::toIndex<Config::BgpNeighbor::ADVERTISE_DIVERSE_PATH_BACKUP>);
        b.set(Config::toIndex<Config::BgpNeighbor::ADVERTISE_DIVERSE_PATH_MPATH>);
        b.set(Config::toIndex<Config::BgpNeighbor::ADVERTISE_MAP>);
        b.set(Config::toIndex<Config::BgpNeighbor::ADVERTISE_MAP_EXIST_CONDITION>);
        b.set(Config::toIndex<Config::BgpNeighbor::ADVERTISE_MAP_NON_EXIST_CONDITION>);
        b.set(Config::toIndex<Config::BgpNeighbor::ANNOUNCE_RPKI_STATE>);
        b.set(Config::toIndex<Config::BgpNeighbor::ORF_BOTH>);
        b.set(Config::toIndex<Config::BgpNeighbor::ORF_RECEIVE>);
        b.set(Config::toIndex<Config::BgpNeighbor::DISTRIBUTE_LIST_OUT>);
        b.set(Config::toIndex<Config::BgpNeighbor::DISTRIBUTE_LIST_OUT_INTERFACE>);
        b.set(Config::toIndex<Config::BgpNeighbor::FILTER_LIST_OUT>);
        b.set(Config::toIndex<Config::BgpNeighbor::NEXT_HOP_SELF>);
        b.set(Config::toIndex<Config::BgpNeighbor::NEXT_HOP_SELF_ALL>);
        b.set(Config::toIndex<Config::BgpNeighbor::NEXT_HOP_UNCHANGED>);
        b.set(Config::toIndex<Config::BgpNeighbor::PREFIX_LIST_OUT>);
        b.set(Config::toIndex<Config::BgpNeighbor::REMOVE_PRIVATE_AS>);
        b.set(Config::toIndex<Config::BgpNeighbor::REMOVE_PRIVATE_AS_ALL>);
        b.set(Config::toIndex<Config::BgpNeighbor::ROUTE_MAP_OUT>);
        b.set(Config::toIndex<Config::BgpNeighbor::ROUTE_REFLECTOR_CLIENT>);
        b.set(Config::toIndex<Config::BgpNeighbor::ROUTE_SERVER_CLIENT>);
        b.set(Config::toIndex<Config::BgpNeighbor::ROUTE_SERVER_CLIENT_CONTEXT>);
        b.set(Config::toIndex<Config::BgpNeighbor::SEND_COMMUNITY>);
        b.set(Config::toIndex<Config::BgpNeighbor::SEND_COMMUNITY_BOTH>);
        b.set(Config::toIndex<Config::BgpNeighbor::SEND_COMMUNITY_EXTENDED>);
        b.set(Config::toIndex<Config::BgpNeighbor::SEND_COMMUNITY_STANDARD>);
        b.set(Config::toIndex<Config::BgpNeighbor::UNSUPPRESS_MAP>);

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

    Config::BgpNeighborRegistry& getConfigs() { return configs.get(); }
    const Config::BgpNeighborRegistry& getConfigs() const { return configs.get(); }
    PeerGroup* getPeerGroup() { return peerGroup; }
    const PeerGroup* getPeerGroup() const { return peerGroup; }
    PeerPolicyTemplate* getPeerPolicyTemplate() { return peerPolicy; }
    const PeerPolicyTemplate* getPeerPolicyTemplate() const { return peerPolicy; }

private:
    AfiSafi family;

    PeerGroup* peerGroup = nullptr;
    Config::BgpNeighborRegistry* peerConfigs = nullptr;

    PeerPolicyTemplate* peerPolicy = nullptr;
    Config::Reference<Config::BgpNeighborRegistry> configs;
};
}

#endif // BGP_NEIGHBOR_AF_CONFIGS_HPP
