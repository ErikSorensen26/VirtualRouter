// BgpProcess

#ifndef BGP_PROCESS_H
#define BGP_PROCESS_H

#include <cstdint>
#include <ControlScheduler.h>

#include "tcp/Listener.h"
#include "configs/registry/router/BgpRegistry.h"
#include "bgp/af/NlriPolicy.hpp"
#include "bgp/neighbor/NeighborTable.h"
#include "bgp/session/Session.h"
#include "bgp/transport/BgpTx.h"
#include "bgp/attributes/AttributeManager.hpp"
#include "bgp/af/AddressFamily.hpp"
#include "bgp/af/AddressFamilyInstance.h" // keep

class VirtualRouter;

namespace BGP
{
class BgpNeighbor;
class Session;

class BgpProcess
{
public:
    BgpProcess(uint32_t as, VirtualRouter* vrf);
    ~BgpProcess();

    VirtualRouter* routingInstance = nullptr;

    // Getters
    Config::BgpRegistry& getConfigs() { return configs.get(); }
    const Config::BgpRegistry& getConfigs() const { return configs.get(); }
    NeighborTable& getNtable() { return ntable; }
    const NeighborTable& getNtable() const { return ntable; }
    AttributeManager& getAttrMgr() { return attrMgr; }
    const AttributeManager& getAttrMgr() const { return attrMgr; }
    uint32_t getRouterId() const noexcept
    {
        auto& rid = getConfigs().get<Config::Bgp::BGP_ROUTER_ID>();
        if (rid.hasValue()) return rid.load();
        return asNumber;
    }

    Session* findSession(const IPAddress& addr);
    void startActiveSession(Neighbor& nbr);
    void startPassiveSession(Neighbor& nbr);
    void shutdownNeighbor(Neighbor& nbr);
    void unshutdownNeighbor(Neighbor& nbr);
    void onSessionEstablished(Session& session);
    void onSessionDown(Session& session);

    AddressFamilyVariant* findAddressFamily(const AfiSafi& afi);

    template <typename F>
    void forEachAf(F&& fn) const
    {
        for (const auto& [afi, afv] : addressFamilies)
            fn(afi);
    }
    void disableAddressFamily(AfiSafi& af);

    template <AfiSafi AF>
    AddressFamily<AF>* findAddressFamily()
    {
        static_assert(hasAddressFamily<AF>(), "AddressFamily not supported");
        if (auto it = addressFamilies.find(AF); it != addressFamilies.end())
            return &std::get<AddressFamily<AF>>(it->second);
        return nullptr;
    }

    template <AfiSafi AF>
    AddressFamily<AF>& enableAddressFamily()
    {
        static_assert(hasAddressFamily<AF>(), "AddressFamily not supported");
        if (auto it = addressFamilies.find(AF); it != addressFamilies.end())
            return std::get<AddressFamily<AF>>(it->second);
        auto [it, ok] = addressFamilies.try_emplace(AF, std::in_place_type<AddressFamily<AF>>, *this, AF);
        return std::get<AddressFamily<AF>>(it->second);
    }

    const uint32_t asNumber;

    static void onConnectCallback(TCP::ConnCallbackCtx& ctx) noexcept;
    static void onAcceptCallback(TCP::AcceptCallbackCtx& ctx) noexcept;
    static void onReceiveCallback(TCP::RecvCallbackCtx& ctx) noexcept;

    ProcessQueueRef getScheduler() { return scheduler.ref(); }

private:

    void scheduleScan();

    TCP::Listener listener;
    std::unordered_map<IPAddress, Session> sessions;
    std::unordered_map<AfiSafi, AddressFamilyVariant> addressFamilies;

    ProcessQueue scheduler;
    AttributeManager attrMgr;
    NeighborTable ntable;

    Config::Reference<Config::BgpRegistry> configs;
};
}

#endif // BGP_PROCESS_H
