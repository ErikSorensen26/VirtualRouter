// EgressBase.h

#ifndef EGRESS_BASE_H
#define EGRESS_BASE_H

#include <cstdint>
#include <atomic>

#include "qos/egress/TxQueueOpts.hpp"

#define MTU_PADDING 128

namespace interface { class Interface; }
namespace qos::egress { struct TxQueueOpts; }

namespace hardware
{
struct FrameHandle;

static inline uint32_t ceilPow2(uint32_t v)
{
    if (v <= 1) return 1;
    v--;
    v |=  v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return v + 1;
}

static inline void cpuRelax() { asm volatile("pause" ::: "memory"); }
}

namespace hardware::egress
{

class EgressBase
{
public:
    EgressBase(interface::Interface& iface, const qos::egress::TxQueueOpts& opts);
    virtual ~EgressBase();

    bool     getFrame(FrameHandle& frame);
    int      getCpuId()     const noexcept { return opts.cpuId; }
    uint32_t getFrameCount() const noexcept { return hwFrameCount; }

    virtual bool send(uint32_t index, uint32_t length) noexcept = 0;
    virtual void reclaim() = 0;
    virtual void cancel(uint32_t index) = 0;
    virtual void flush() {}
    virtual void waitWritable() {}

protected:
    interface::Interface& iface;
    qos::egress::TxQueueOpts opts;
    const uint32_t qid;

    void initFreeRing(uint32_t frameCount);
    void destroyFreeRing();

    /**
     * MPMC free ring
     *
     * Invariant for slot at ring position p (p & freeMask = i):
     *   freeSeq[i] == p       → empty,  ready for a producer to claim position p
     *   freeSeq[i] == p + 1   → full,   ready for a consumer to claim position p
     *   freeSeq[i] == p + cap → recycled (consumer done), ready for producer at p+cap
     *
     * No spinloop is needed in practice: the "wait" cases only occur when another
     * thread is mid-write and the scheduler hasn't given it a chance to store seq.
     * The cpu_relax() hint is sufficient on x86/ARM.
     */
    void pushFree(uint32_t index);
    bool tryPopFree(uint32_t& outIndex);

    virtual void onAllocNudge() {}
    virtual void mapFrame(uint32_t index, FrameHandle& out) = 0;

    uint32_t packetSize;

private:
    uint32_t*              freeBuf = nullptr;
    std::atomic<uint32_t>* freeSeq = nullptr; // per-slot sequence counters
    alignas(64) std::atomic<uint32_t> freeHead{0};
    alignas(64) std::atomic<uint32_t> freeTail{0};
    uint32_t freeCap      = 0;
    uint32_t freeMask     = 0;
    uint32_t hwFrameCount = 0; ///< Actual frame count passed to initFreeRing().
};

} // namespace hardware::egress

#endif // EGRESS_BASE_H

