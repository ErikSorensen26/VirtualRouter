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

void TxDistributor::pushTo(uint32_t qid, hardware::PacketSlot* pkt)
{
    uint32_t n = N.load(std::memory_order_relaxed);
    if (qid >= n || !qs) return;

    QueueState* s = qs[qid];
    if (!s || !s->queue) return;
    s->queue->enqueue(pkt);
}

void TxDistributor::push(hardware::PacketSlot* pkt, TxDistPolicy policy)
{
    uint32_t n = N.load(std::memory_order_relaxed);
    if (n == 0 || !qs || !pkt)
        return;

    uint32_t flowHash = pkt->flowHash;
    uint32_t qid = pickQueue(policy, flowHash);

    pushTo(qid, pkt);
}

uint32_t TxDistributor::pickQueue(TxDistPolicy policy, uint32_t flowHash)
{
    switch (policy)
    {
        case TxDistPolicy::BEST_EFFORT:
            return 0;
        case TxDistPolicy::FLOW_HASH:
        {
            uint32_t n = N.load(std::memory_order_relaxed);
            return (n & (n - 1)) ? (flowHash % n) : (flowHash & (n - 1));
        }
        case TxDistPolicy::ROUND_ROBIN:
            return rr.fetch_add(1, std::memory_order_relaxed) % N.load(std::memory_order_relaxed);
        case TxDistPolicy::WEIGHTED_RR:
            return pickWeighted();
    }
    return 0;
}

void TxDistributor::release(hardware::FrameHandle& frame)
{
    qs[frame.qid]->egress->cancel(frame.slot->index);
}

uint32_t TxDistributor::pickWeighted()
{
    uint32_t n = N.load(std::memory_order_relaxed);
    if (n == 0) return 0;

    if (!weights)
        return rr.fetch_add(1, std::memory_order_relaxed) % N;
    
    uint32_t ticket = rr.fetch_add(1, std::memory_order_relaxed);
    uint32_t sum = 0;
    for (uint32_t i = 0; i < n; ++i) sum += weights[i] ? weights[i] : 1;

    uint32_t t = (sum ? (ticket % sum) : 0);
    for (uint32_t i = 0, acc = 0; i < n; ++i)
    {
        acc += (weights[i] ? weights[i] : 1);
        if (t < acc) return i;
    }
    return 0;
}

bool TxDistributor::getFrame(hardware::FrameHandle& frame, TxDistPolicy policy, uint32_t flowHash)
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

void TxDistributor::appendQueue()
{
    N.fetch_add(1, std::memory_order_seq_cst);
}

void TxDistributor::popQueue()
{
    N.fetch_sub(1, std::memory_order_seq_cst);
}

} // namespace qos
