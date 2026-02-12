// OspfNeighbor.h

#ifndef OSPF_NEIGHBOR_H
#define OSPF_NEIGHBOR_H

#include <atomic>
#include <optional>
#include <IPAddress.hpp>

#include "ospf/transmission/OspfPacket.hpp"
#include "ospf/database/LsaKey.hpp"
#include "ospf/neighbor/Retransmission.hpp"

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
    State getState() const { return state; }
    bool setState(State s);

    // Role
    Role getRole() { return role; }
    void setRole(Role r) { role = r; }
    bool isMaster() { return getRole() == Role::MASTER; }
    void resetDbExchange();

    OspfInterface& getIface() const { return iface; }

    std::atomic<uint32_t> dr{0};
    std::atomic<uint32_t> bdr{0};

    bool isDr() { return dr.load(std::memory_order_relaxed) == routerID; }
    bool isBdr() { return bdr.load(std::memory_order_relaxed) == routerID; }

public:

    std::optional<LsaKey> currentDbd = std::nullopt;

    const IPAddress ipAddress;
    const bool unicast{false};
    const uint32_t routerID;
    const uint16_t mtu;
    uint32_t neighborInterfaceId = 0;    
    std::atomic<uint32_t> lastAuthSeq{0};

    std::atomic<uint32_t> currentSeq;

    std::atomic<uint8_t> priority;

    // Timers
    std::atomic<uint32_t> inactivityTimerId{0};

    // Retransmission
    Retransmission& getRtr() { return rtr; }

    std::atomic<bool> isTransit{true};

private:
    State state;
    Role role = Role::NONE;

    Retransmission rtr;
    OspfInterface& iface;
    InterfaceTimers& tmgr;
};
}

#endif // OSPF_NEIGHBOR_H
