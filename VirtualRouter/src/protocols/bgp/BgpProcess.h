// BgpProcess

#ifndef BGP_PROCESS_H
#define BGP_PROCESS_H

#include <cstdint>
#include <ControlScheduler.h>

#include "tcp/Listener.h"
#include "configs/registry/router/BgpRegistry.h"
#include "bgp/neighbor/NeighborTable.h"

class VirtualRouter;

namespace BGP
{
class BgpNeighbor;
class Connection;

class BgpProcess
{
public:
    BgpProcess(uint32_t as, VirtualRouter* vrf);

    VirtualRouter* routingInstance = nullptr;

    // Getters
    Config::BgpRegistry& getConfigs() { return configs.get(); }
    const Config::BgpRegistry& getConfigs() const { return configs.get(); }
    NeighborTable& getNtable() { return ntable; }
    const NeighborTable& getNtable() const { return ntable; }
private:

    static void onConnect(TCP::ConnCallbackCtx& ctx) noexcept;
    static void onAccept(TCP::AcceptCallbackCtx& ctx) noexcept;
    static void onReceive(TCP::RecvCallbackCtx& ctx) noexcept;

    const uint32_t asNumber;

    TCP::Listener listener;
    std::unordered_map<TCP::TcpSocketKey, Connection> connections;

    ProcessQueue scheduler;

    NeighborTable ntable;

    Config::Reference<Config::BgpRegistry> configs;
};
}

#endif // BGP_PROCESS_H
