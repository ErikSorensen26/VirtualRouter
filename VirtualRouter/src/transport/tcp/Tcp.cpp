// Tcp.cpp

#include <VirtualRouter.h>

#include "Tcp.h"
#include "TcpEngine.h"
#include "interface/InterfaceManager.h"
#include "interface/Interface.h"

namespace transport::tcp
{
Tcp::Tcp(core::VirtualRouter& vrf, Config cfg)
{
    engine = new TcpEngine(vrf, cfg);

    interface::InterfaceManager& ifaceMgr = vrf.getInterfaceManager();

    // Subscribe to interface managers to close tcp connections when interfaces go down or addresses are removed.
    tcpIfDownId = ifaceMgr.subscribe(interface::StateChange::IF_DOWN,
        this,
        [](void* ctx, interface::Interface& iface) {
            auto* engine = static_cast<TcpEngine*>(ctx);
            // Primary IPv4
            auto primaryV4 = iface.configs.ipv4.getPrimaryAddress();
            if (primaryV4.addr != 0)
                engine->dropLocalConnections(types::IPAddress(primaryV4));
            // Global IPv6 unicast addresses
            for (const auto& v6 : iface.configs.ipv6.getGlobalList())
                engine->dropLocalConnections(types::IPAddress(v6));
            // Link-local
            auto ll = iface.configs.ipv6.getLocalAddress();
            if (!ll.isUnspecified())
                engine->dropLocalConnections(types::IPAddress(ll));
        });

    // When a specific IP is explicitly removed, tear down only the
    tcpIPv4DelId = ifaceMgr.subscribe(interface::IPv4Event::IPV4_DEL,
        this,
        [](void* ctx, interface::Interface&, types::IPv4Prefix& prefix) {
            static_cast<TcpEngine*>(ctx)->dropLocalConnections(
                types::IPAddress(prefix.addr));
        });

    tcpIPv6DelId = ifaceMgr.subscribe(interface::IPv6Event::IPV6_DEL,
        this,
        [](void* ctx, interface::Interface&, types::IPv6Prefix& prefix) {
            static_cast<TcpEngine*>(ctx)->dropLocalConnections(
                types::IPAddress(prefix.addr));
        });

    tcpIPv6LlDelId = ifaceMgr.subscribe(interface::IPv6Event::IPV6_LL_DEL,
        this,
        [](void* ctx, interface::Interface&, types::IPv6Prefix& prefix) {
            static_cast<TcpEngine*>(ctx)->dropLocalConnections(
                types::IPAddress(prefix.addr));
        });
}

Tcp::~Tcp()
{
    auto& ifaceMgr = engine->vr.getInterfaceManager();
    ifaceMgr.unsubscribe(interface::InterfaceManager::StateEventMgr::Id{tcpIfDownId});
    ifaceMgr.unsubscribe(interface::InterfaceManager::IPv4EventMgr::Id{tcpIPv4DelId});
    ifaceMgr.unsubscribe(interface::InterfaceManager::IPv6EventMgr::Id{tcpIPv6DelId});
    ifaceMgr.unsubscribe(interface::InterfaceManager::IPv6EventMgr::Id{tcpIPv6LlDelId});

    delete engine;
    engine = nullptr;
}

Listener Tcp::listen(const TcpEndpoint& local, const ListenOptions& opt)
{
    return engine->createListener(local, opt);
}

Connection Tcp::connect(const TcpEndpoint& local, const TcpEndpoint& remote, const ConnectOptions& opt)
{
    return engine->createConnection(local, remote, opt);
}

void Tcp::close(ConnId id)
{
    engine->closeConnection(id);
}

size_t Tcp::pollEvents(std::span<TcpEvent> outEvents, uint32_t timeoutMs)
{
    return engine->pollEvents(outEvents, timeoutMs);
}

size_t Tcp::pump(uint32_t timeoutMs, size_t maxEvents)
{
    return engine->pump(*this, timeoutMs, maxEvents);
}

void Tcp::input(const TcpSegment&)
{
    // TODO
}

} // namespace transport::tcp
