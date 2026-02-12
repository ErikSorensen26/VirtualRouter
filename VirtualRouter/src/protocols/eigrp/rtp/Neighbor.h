// neighbor.h

#ifndef EIGRP_NEIGHBOR_H
#define EIGRP_NEIGHBOR_H

#include <map>
#include <chrono>
#include <IPAddress.hpp>
#include <atomic>
#include <deque>
#include <set>

#include "ReliablePacket.hpp"

struct EigrpHeader; namespace Eigrp
{
class InterfaceTimers;
struct EigrpHeaderInfo;
class EigrpInterface;

enum class TLVType : uint16_t
{
    LEGACY_V4 = 0x0100,
    WIDE_V4 = 0x0600,
    LEGACY_V6 = 0x0400,
    WIDE_V6 = 0x0600
};

class Neighbor
{
public:
    enum class State { DOWN, PENDING, UP };
    enum class Version : uint16_t { LEGACY = 0x0102, WIDE = 0x0200, UNKNOWN = 0x0000 };

    explicit Neighbor(EigrpInterface& iface, InterfaceTimers& tmgr, const IPAddress& neighborIp, Version version, bool unicast = false);
    ~Neighbor();

    Neighbor(const Neighbor&) = delete;
    Neighbor& operator=(const Neighbor&) = delete;
    Neighbor(Neighbor&&) = delete;
    Neighbor& operator=(Neighbor&&) = delete;

    // State
    State getState() const noexcept { return state.load(std::memory_order_relaxed); }
    void setState(State newState) { state.store(newState, std::memory_order_release); }

    // Reliable packet management
    void clearReliable();

    // Control
    void clear();
    bool isActive() const noexcept;

    EigrpInterface& getIface() const { return iface; }

public:

    // Public Fields
    const IPAddress ipAddress;
    const bool unicast{false};
    std::atomic<bool> fullSent = false;
    std::atomic<bool> initComplete{false};
    std::atomic<bool> initInProgress{false};
    std::atomic<bool> resyncInProgress{false};
    std::atomic<bool> isStub{false};
    std::atomic<uint32_t> lastSeqRecv = 0;
    std::atomic<uint32_t> lastSeqAck = 0;

    Version version;
    const TLVType tlvType;
    uint32_t routerID;

    bool pushAck(uint32_t ack);
    bool popAck(uint32_t& ack);
    void removeAck(uint32_t ack);
    bool hasAck(uint32_t ack);

    // Timers
    std::atomic<uint32_t> holdTimerId{0}, gracefulTimerId{0};
    std::atomic<uint16_t> holdTime{0};
    std::atomic<bool> isGraceful{false};
    std::atomic<bool> secondHello{false};

    std::atomic<uint32_t> recvInitSeq{0};
    std::atomic<uint32_t> sentInitSeq{0};
    std::atomic<double> srtt{1.0}, rttvar{0.5}, rto{1.5};

    std::atomic<uint32_t> currentReliable{0};
    std::deque<std::pair<uint32_t, bool>> reliableQueue;
    std::map<uint32_t, UnicastReliablePacket> reliablePackets;
    std::set<uint32_t> activeConditions;
    std::unordered_map<uint32_t, bool> receivedConditions;
    mutable std::mutex reliableMtx;

    std::mutex ackMtx;

private:
    // Internal state
    std::atomic<State> state{State::DOWN};

    std::deque<uint32_t> ackQueue;
    std::set<uint32_t> outstandingAcks;

    EigrpInterface& iface;
    InterfaceTimers& tmgr;
};
}

#endif // EIGRP_NEIGHBOR_H
