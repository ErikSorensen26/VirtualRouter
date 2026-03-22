// Fsm.h

#ifndef BGP_FSM_H
#define BGP_FSM_H

#include "bgp/BgpTypes.hpp"

namespace BGP
{
using FsmTransitionCallback = std::function<void(FsmState from, FsmState to, FsmEvent trigger)>;
class Session;

class Fsm
{
public:
    explicit Fsm(Session& session) noexcept;

    Fsm(const Fsm&) = delete;
    Fsm& operator=(const Fsm&) = delete;

    void setTransitionCallback(FsmTransitionCallback cb) noexcept
    {
        transitionCallback = std::move(cb);
    }

    void processEvent(FsmEvent event);

    FsmState getState() const noexcept { return state; }

private:
    void handleIdle(FsmEvent event);
    void handleConnect(FsmEvent event);
    void handleActive(FsmEvent event);
    void handleOpenSent(FsmEvent event);
    void handleOpenConfirm(FsmEvent event);
    void handleEstablished(FsmEvent event);

    // Transition helper
    void transitionTo(FsmState newState, FsmEvent trigger);

    // Close TCP, stop timers
    void resetToIdle(bool sendNotification, uint16_t notifCode = BGP_NOTIFICATION_CEASE_UNSPECIFIC);
    
    // Reset TCP, restart ConnectRetry, initiate new outgoing connection.
    void resetAndReconnect();

    // Initiate an outgoing TCP connection.
    void initiateOutgoingTcp();

    // State
    FsmState state{FsmState::IDLE};
    bool passiveMode{false};

    FsmTransitionCallback transitionCallback;

    Session& session;
};
}

#endif // BGP_FSM_H
