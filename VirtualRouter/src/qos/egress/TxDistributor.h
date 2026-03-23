// TxDistributor.h

#ifndef TX_DISTRIBUTOR_H
#define TX_DISTRIBUTOR_H

#include <cstdint>
#include <atomic>
#include <cstring>

namespace hardware { struct PacketSlot; struct FrameHandle; }
namespace qos::egress
{

struct QueueState;

enum class TxDistPolicy
{
    BEST_EFFORT,
    FLOW_HASH,
    ROUND_ROBIN,
    WEIGHTED_RR
};

class TxDistributor
{
public:
    TxDistributor(QueueState** queue, uint32_t size);

    bool getFrame(hardware::FrameHandle& frame, TxDistPolicy policy = TxDistPolicy::BEST_EFFORT, uint32_t flowHash = 0);
    void setWeights(const uint16_t* w) { weights = w; }
    void push(hardware::PacketSlot* pkt, TxDistPolicy policy = TxDistPolicy::FLOW_HASH);
    void pushTo(uint32_t qid, hardware::PacketSlot* pkt);
    void release(hardware::FrameHandle& frame);

    void appendQueue();
    void popQueue();
    
private:
    QueueState** qs = nullptr;
    std::atomic<uint32_t> N;
    std::atomic<uint32_t> rr;
    const uint16_t* weights = nullptr;

    inline uint32_t pickQueue(TxDistPolicy policy, uint32_t flowHash = 0);
    inline uint32_t pickWeighted();
};

} // namespace qos

#endif // TX_DISTRIBUTOR_H

