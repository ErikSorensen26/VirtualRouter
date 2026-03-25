// TxDistributor.h

#ifndef TX_DISTRIBUTOR_H
#define TX_DISTRIBUTOR_H

#include <cstdint>
#include <atomic>

namespace hardware { struct PacketSlot; struct FrameHandle; }

namespace qos::egress
{

struct QueueState;

enum class TxDistPolicy
{
    BEST_EFFORT,  // always queue 0
    FLOW_HASH,    // consistent hash of pkt->flowHash
    ROUND_ROBIN,
    WEIGHTED_RR
};

// Routes frames and packets across a set of per-CPU TX queues.
//
// Frame lifecycle
// ---------------
//   1. getFrame(frame, policy, hash)   — allocate a frame from a chosen queue's
//                                        EgressBase; frame.qid records which queue.
//   2. <write payload into frame>
//   3. send(frame)  OR  pushTo(frame.qid, frame.slot)
//                                      — enqueue to the SAME queue that supplied
//                                        the frame (must match to avoid cross-queue
//                                        frame index confusion).
//   4. release(frame)                  — cancel: return frame to its queue's free ring.
//
// Thread safety
// -------------
//   getFrame / push / pushTo / send / release — safe to call from any thread.
//   appendQueue / popQueue — called only under TxQueueManager::mu.
//   setWeights             — called only under TxQueueManager::mu.
class TxDistributor
{
public:
    TxDistributor(QueueState** queues, uint32_t size);

    // Allocate a frame from a queue chosen by policy.
    bool getFrame(hardware::FrameHandle& frame,
                  TxDistPolicy policy   = TxDistPolicy::FLOW_HASH,
                  uint32_t     flowHash = 0);

    // Submit a frame through the queue it was allocated from (frame.qid).
    // Sets frame.slot = nullptr on success to prevent double-submit.
    void send(hardware::FrameHandle& frame);

    // Enqueue a packet to a specific queue by index.
    void pushTo(uint32_t qid, hardware::PacketSlot* pkt);

    // Enqueue a packet to a queue chosen by policy.
    void push(hardware::PacketSlot* pkt,
              TxDistPolicy policy = TxDistPolicy::FLOW_HASH);

    // Cancel a frame: return it to its queue's EgressBase free ring.
    // Sets frame.slot = nullptr.
    void release(hardware::FrameHandle& frame);

    // Update per-queue weights for WEIGHTED_RR.  n must equal current queue count.
    void setWeights(const uint16_t* w, uint32_t n);

    // Called by TxQueueManager under its mutex when adding/removing queues.
    void appendQueue();
    void popQueue();

private:
    QueueState**               qs      = nullptr;
    std::atomic<uint32_t>      N;
    std::atomic<uint32_t>      rr{0};
    const uint16_t*            weights = nullptr;
    std::atomic<uint32_t>      weightSum{0}; // cached sum; recomputed in setWeights

    uint32_t pickQueue(TxDistPolicy policy, uint32_t flowHash = 0);
    uint32_t pickWeighted();
};

} // namespace qos::egress

#endif // TX_DISTRIBUTOR_H
