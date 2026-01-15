// OspfNeighbor.h

#ifndef OSPF_NEIGHBOR_H
#define OSPF_NEIGHBOR_H

#include <atomic>
#include <IPAddress.hpp>
#include <optional>
#include <mutex>
#include <LSDB.hpp>
#include <set>
#include <OspfPacket.hpp>
#include "Retransmission.h"

namespace OSPF
{
struct LsaKey;
class Retransmission;
class OspfInterface;
class InterfaceTimers;

class Neighbor
{
public:
    enum class State { DOWN, ATTEMPT, INIT, TWOWAY, EXSTART, EXCHANGE, LOADING, FULL };
    enum class Role { SLAVE, MASTER, NONE };

    explicit Neighbor(OspfInterface& iface, InterfaceTimers& tmgr, uint32_t rid, IPAddress& neighborIp, bool unicast = false);
    ~Neighbor();

    Neighbor(const Neighbor&) = delete;
    Neighbor& operator=(const Neighbor&) = delete;
    Neighbor(Neighbor&&) = delete;
    Neighbor& operator=(Neighbor&&) = delete;

    // State
    State getState() const { return state.load(std::memory_order_relaxed); }
    bool setState(State s);

    // Role
    Role getRole() { return role.load(std::memory_order_relaxed); }
    void setRole(Role r) { role.store(r, std::memory_order_release); }
    bool isMaster() { return getRole() == Role::MASTER; }
    void resetDbExchange();

    void markHeard();

    OspfInterface& getIface() const { return iface; }

public:

    std::optional<LsaKey> currentDbd = std::nullopt;

    const IPAddress ipAddress;
    const bool unicast{false};
    const uint32_t routerID;
    uint16_t mtu = 0;
    uint32_t neighborInterfaceId = 0;    

    std::atomic<uint32_t> currentSeq;

    std::atomic<uint8_t> priority;

    // MultiAccess
    std::atomic<bool> isDr;
    std::atomic<bool> isBdr;

    // Timers
    std::atomic<uint32_t> inactivityTimerId{0};

    // Retransmission
    Retransmission& getRtr() { return rtr; }

private:
    std::atomic<State> state;
    std::atomic<Role> role = Role::NONE;

    Retransmission rtr;
    OspfInterface& iface;
    InterfaceTimers& tmgr;
};
}

#endif // OSPF_NEIGHBOR_H
