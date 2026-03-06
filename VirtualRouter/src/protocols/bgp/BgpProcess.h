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

class VirtualRouter;

namespace BGP
{
class BgpNeighbor;
class Session;

class BgpProcess
{
public:
    BgpProcess(uint32_t as, VirtualRouter* vrf);

    VirtualRouter* routingInstance = nullptr;

    // Getters
    inline Config::BgpRegistry& getConfigs() { return configs.get(); }
    inline const Config::BgpRegistry& getConfigs() const { return configs.get(); }
    inline NeighborTable& getNtable() { return ntable; }
    inline const NeighborTable& getNtable() const { return ntable; }
    inline AttributeManager& getAttrMgr() { return attrMgr; }
    inline const AttributeManager& getAttrMgr() const { return attrMgr; }

    std::unordered_map<TCP::TcpSocketKey, Session>& getSessions() { return sessions; }

    const uint32_t asNumber;
    const uint32_t rid;
private:

    static void onConnect(TCP::ConnCallbackCtx& ctx) noexcept;
    static void onAccept(TCP::AcceptCallbackCtx& ctx) noexcept;
    static void onReceive(TCP::RecvCallbackCtx& ctx) noexcept;


    TCP::Listener listener;
    std::unordered_map<TCP::TcpSocketKey, Session> sessions;

    ProcessQueue scheduler;

    AttributeManager attrMgr;
    NeighborTable ntable;

    Config::Reference<Config::BgpRegistry> configs;
};
}

#endif // BGP_PROCESS_H
