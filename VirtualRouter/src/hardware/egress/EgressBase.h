// EgressBase.h

#ifndef EGRESS_BASE_H
#define EGRESS_BASE_H

#include <cstdint>
#include <atomic>
#include <thread>
#include <TxQueueOpts.hpp>

#define MTU_PADDING 128

class Interface;
struct TxQueueOpts;
struct FrameHandle;

class EgressBase
{
public:
    EgressBase(Interface& iface, const TxQueueOpts& opts);
    virtual ~EgressBase();

    bool getFrame(FrameHandle& frame);

    virtual bool send(uint32_t index, uint32_t length) noexcept = 0;
    virtual void reclaim() = 0;
    virtual void cancel(uint32_t index) = 0;
    virtual void flush() {}
    virtual void waitWritable() {}

protected:
    Interface& iface;
    TxQueueOpts opts;
    const uint32_t qid;

    void initFreeRing(uint32_t frameCount);
    void destroyFreeRing();

    void initBusyRing(uint32_t n);
    void destroyBusyRing();

    virtual void onAllocNudge() {}
    virtual void mapFrame(uint32_t index, FrameHandle& out) = 0;

    void pushFree(uint32_t index);
    bool tryPopFree(uint32_t& outIndex);

    bool tryPopBusy(uint32_t& outIndex);
    void pushBusy(uint32_t index);

    uint32_t packetSize;

private:
    // Free ring
    std::atomic<uint32_t>* freeSeq = nullptr;
    uint32_t* freeBuf = nullptr;
    alignas(64) std::atomic<uint32_t> freeHead = 0;
    alignas(64) std::atomic<uint32_t> freeTail = 0;
    uint32_t freeCap = 0;
    uint32_t freeMask = 0;

    // Busy ring
    std::atomic<uint32_t>* busySeq = nullptr;
    uint32_t* busyBuf = nullptr;
    alignas(64) std::atomic<uint32_t> busyHead = 0;
    alignas(64) std::atomic<uint32_t> busyTail = 0;
    uint32_t busyCap = 0;
    uint32_t busyMask = 0;
};

#endif // EGRESS_BASE_H
