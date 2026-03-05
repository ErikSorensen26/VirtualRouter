// Session.cpp

#include <VirtualRouter.h>

#include "Session.h"
#include "bgp/session/Collision.h"
#include "bgp/neighbor/Neighbor.h"
#include "bgp/BgpProcess.h"

namespace BGP
{
Session::Session(Neighbor& nbr, ProcessQueueRef queue) noexcept
    : neighbor(nbr),
      base(nbr.getConfigs().get<Config::BgpNeighborSession::BGP_BASE>().local().get()),
      queue(std::move(queue)),
      fsm(*this),
      timers(*this, this->queue)
{
    holdTime = base.get<Config::BgpTransportBase::HOLDTIME>().load();
    keepaliveInterval = holdTime / 3;

    buildLocalCapabilities();

    // Register FSM transition callback
    fsm.setTransitionCallback(
        [this](FsmState from, FsmState to, FsmEvent trigger) {
            this->onFsmTransition(from, to, trigger);
        });
}

Session::~Session()
{
    timers.cancelAll();
    closeAllConnections();
}

void Session::buildLocalCapabilities()
{
    auto procCfg = neighbor.getProcess().getConfigs();

    localCaps.asn32bit = true;
    localCaps.asn = neighbor.getProcess().asNumber;

    localCaps.routeRefresh = true;
    localCaps.enhancedRouteRefresh = true;

    bool grEnabled = procCfg.get<Config::Bgp::BGP_GRACEFUL_RESTART>().load();
    if (grEnabled)
    {
        localCaps.gracefulRestart = true;
        localCaps.restartTime = procCfg.get<Config::Bgp::BGP_GRACEFUL_RESTART_RESTART_TIME>().load();
    }

    localCaps.extendedMessage = true;
}

void Session::acceptConnection(TCP::Connection&& conn)
{
    passiveConn.emplace(std::move(conn));
    primaryConn = &passiveConn.value();
    postEvent(FsmEvent::TCP_CONNECTION_CONFIRMED);
}

void Session::initiateConnection()
{
    auto& proc = neighbor.getProcess();
    auto& tcp = proc.routingInstance->getTcp();

    TCP::ConnectOptions opts;
    opts.callback = BgpProcess::onConnectCallback;
    opts.callbackUser = &proc;
    opts.recvCallback = BgpProcess::onReceiveCallback;
    opts.recvUser = &proc;

    activeConn.emplace(tcp.connect(
        TCP::TcpEndpoint{IPAddress{}, 0},
        TCP::TcpEndpoint{neighbor.neighborAddress, 179},
        opts
    ));

    if (activeConn->ok())
    {
        primaryConn = &activeConn.value();
    }
    else
    {
        activeConn.reset();
        postEvent(FsmEvent::TCP_CONNECTION_FAILS);
    }
}

void Session::closeActiveConnection() noexcept
{
    if (activeConn.has_value())
    {
        auto& proc = neighbor.getProcess();
        auto& tcp = proc.routingInstance->getTcp();
        tcp.close(activeConn->getId());
        activeConn.reset();
    }
    primaryConn = passiveConn.has_value() ? &passiveConn.value() : nullptr;
}

void Session::closeAllConnections() noexcept
{
    auto& proc = neighbor.getProcess();
    auto& tcp = proc.routingInstance->getTcp();

    if (activeConn.has_value())
    {
        tcp.close(activeConn->getId());
        activeConn.reset();
    }
    if (passiveConn.has_value())
    {
        tcp.close(passiveConn->getId());
        passiveConn.reset();
    }
    primaryConn = nullptr;
}

void Session::postEvent(FsmEvent event)
{
    queue.post([this, event]() {
        fsm.processEvent(event);
    });
}

void Session::handleIncoming(TCP::RxConsumer& consumer)
{
    auto& proc = neighbor.getProcess();
    proc.getTransmission().handleIncoming(*this, consumer);
}

void Session::onFsmTransition(FsmState from, FsmState to, FsmEvent /*trigger*/)
{
    auto& proc = neighbor.getProcess();

    if (to == FsmState::ESTABLISHED)
        proc.onSessionEstablished(*this);
    else if (from == FsmState::ESTABLISHED)
        proc.onSessionDown(*this);
}

void Session::sendOpen()
{
    auto& proc = neighbor.getProcess();
}
}
