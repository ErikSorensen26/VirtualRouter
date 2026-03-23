// NeighborConfigs.hpp

#ifndef BGP_NEIGHBOR_CONFIGS_HPP
#define BGP_NEIGHBOR_CONFIGS_HPP

#include <bitset>

#include "configs/registry/router/BgpRegistry.h"
#include "bgp/neighbor/PeerTemplate.h"

namespace routing::bgp
{
struct NeighborConfigs
{
    NeighborConfigs(config::Reference<config::BgpNeighborSessionRegistry>&& cfgs)
        : configs(cfgs) {}

    template <config::BgpNeighborSession F>
    decltype(auto) get()
    {
        if (peerGroup && peerOwnedTable.test(config::toIndex<F>))
            return peerGroup->getSessionConfigs().get<F>();
        return configs->get<F>();
    }

    template <config::BgpNeighborSession F>
    decltype(auto) get() const
    {
        if (peerGroup && peerOwnedTable.test(config::toIndex<F>))
            return std::as_const(peerGroup->getSessionConfigs().get<F>());
        return std::as_const(configs->get<F>());
    }

    static constexpr std::bitset<config::toIndex<config::BgpNeighborSession::COUNT>> peerOwnedTable = []{
        std::bitset<config::toIndex<config::BgpNeighborSession::COUNT>> b;

        b.set(config::toIndex<config::BgpNeighborSession::LOCAL_AS>);
        b.set(config::toIndex<config::BgpNeighborSession::LOCAL_AS_AS>);
        b.set(config::toIndex<config::BgpNeighborSession::LOCAL_AS_NO_PREPEND>);
        b.set(config::toIndex<config::BgpNeighborSession::LOCAL_AS_REPLACE_AS>);
        b.set(config::toIndex<config::BgpNeighborSession::LOCAL_AS_DUAL_AS>);

        return b;
    }();

    bool setPeerGroup(PeerGroup* group)
    {
        if (peerSession)
            return false;
        peerGroup = group;
        configs->setMask(group ? &group->getSessionConfigs() : nullptr);
        return true;
    }

    bool setPeerSessionTemplate(PeerSessionTemplate* ps)
    {
        if (peerGroup)
            return false;
        peerSession = ps;
        configs->setMask(ps ? &ps->getConfigs() : nullptr);
        return true;
    }

    config::Reference<config::BgpNeighborSessionRegistry>& getConfigs() { return configs; }
    const config::Reference<config::BgpNeighborSessionRegistry>& getConfigs() const { return configs; }
    PeerGroup* getPeerGroup() { return peerGroup; }
    const PeerGroup* getPeerGroup() const { return peerGroup; }
    PeerSessionTemplate* getPeerSessionTemplate() { return peerSession; }
    const PeerSessionTemplate* getPeerSessionTemplate() const { return peerSession; }

private:
    PeerGroup* peerGroup = nullptr;
    PeerSessionTemplate* peerSession = nullptr;
    config::Reference<config::BgpNeighborSessionRegistry> configs;
};
} // namespace routing

#endif // BGP_NEIGHBOR_CONFIGS_HPP

