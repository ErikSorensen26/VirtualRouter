// TxDistributor.cpp

#include "TxDistributor.h"
#include "hardware/PacketSlot.hpp"
#include "BaseQueue.h"
#include "TxQueueManager.h"
#include "hardware/egress/EgressBase.h"

namespace qos::egress
{

TxDistributor::TxDistributor(QueueState** queues, uint32_t size)
    : qs(queues), N(size), rr(0)
{}

// ---- frame allocation ----------------------------------------------------

bool TxDistributor::getFrame(hardware::FrameHandle& frame,
                              TxDistPolicy policy, uint32_t flowHash)
{
    uint32_t n = N.load(std::memory_order_relaxed);
    if (n == 0 || !qs) return false;

    uint32_t qid = pickQueue(policy, flowHash);
    QueueState* s = qs[qid];
    if (!s || !s->queue) return false;

    if (s->queue->out.getFrame(frame))
    {
        frame.qid = qid;
        return true;
    }
    return false;
}

// ---- frame submission ----------------------------------------------------

void TxDistributor::send(hardware::FrameHandle& frame)
{
    if (!frame.slot) return;
    pushTo(frame.qid, frame.slot);
    frame.slot = nullptr; // ownership transferred; prevent double-submit
}

void TxDistributor::pushTo(uint32_t qid, hardware::PacketSlot* pkt)
{
    uint32_t n = N.load(std::memory_order_relaxed);
    if (!pkt || !qs || qid >= n) return;

    QueueState* s = qs[qid];
    if (!s || !s->queue) return;

    s->queue->enqueue(pkt);
}

void TxDistributor::push(hardware::PacketSlot* pkt, TxDistPolicy policy)
{
    uint32_t n = N.load(std::memory_order_relaxed);
    if (!pkt || !qs || n == 0) return;

    uint32_t qid = pickQueue(policy, pkt->flowHash);
    pushTo(qid, pkt);
}

// ---- frame cancellation --------------------------------------------------

void TxDistributor::release(hardware::FrameHandle& frame)
{
    if (!frame.slot) return;

    uint32_t n = N.load(std::memory_order_relaxed);
    if (!qs || frame.qid >= n) return;

    QueueState* s = qs[frame.qid];
    if (!s || !s->egress) return;

    s->egress->cancel(frame.slot->index);
    frame.slot = nullptr;
}

// ---- weights -------------------------------------------------------------

void TxDistributor::setWeights(const uint16_t* w, uint32_t n)
{
    weights = w;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < n; ++i)
        sum += w[i] ? w[i] : 1u;
    weightSum.store(sum, std::memory_order_relaxed);
}

// ---- queue count ---------------------------------------------------------

void TxDistributor::appendQueue()
{
    N.fetch_add(1, std::memory_order_seq_cst);
}

void TxDistributor::popQueue()
{
    N.fetch_sub(1, std::memory_order_seq_cst);
}

// ---- private helpers -----------------------------------------------------

uint32_t TxDistributor::pickQueue(TxDistPolicy policy, uint32_t flowHash)
{
    uint32_t n = N.load(std::memory_order_relaxed);
    if (n == 0) return 0;

    switch (policy)
    {
        case TxDistPolicy::BEST_EFFORT:
            return 0;

        case TxDistPolicy::FLOW_HASH:
            // Power-of-two fast path; fall back to modulo for odd counts.
            return (n & (n - 1)) ? (flowHash % n) : (flowHash & (n - 1));

        case TxDistPolicy::ROUND_ROBIN:
            return rr.fetch_add(1, std::memory_order_relaxed) % n;

        case TxDistPolicy::WEIGHTED_RR:
            return pickWeighted();
    }
    return 0;
}

uint32_t TxDistributor::pickWeighted()
{
    uint32_t n = N.load(std::memory_order_relaxed);
    if (n == 0) return 0;

    if (!weights)
        return rr.fetch_add(1, std::memory_order_relaxed) % n;

    uint32_t sum = weightSum.load(std::memory_order_relaxed);
    if (sum == 0) return 0;

    uint32_t ticket = rr.fetch_add(1, std::memory_order_relaxed) % sum;

    for (uint32_t i = 0, acc = 0; i < n; ++i)
    {
        acc += weights[i] ? weights[i] : 1u;
        if (ticket < acc) return i;
    }
    return 0;
}

} // namespace qos::egress
