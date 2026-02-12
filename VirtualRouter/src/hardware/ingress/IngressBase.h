// IngressBase.h

#ifndef INGRESS_BASE_H
#define INGRESS_BASE_H

#include <atomic>
#include <thread>
#include <cstdint>

#include "qos/ingress/RxQueueOpts.hpp"

struct FrameView
{
    uint8_t* payload = nullptr;
    uint32_t length = 0;
    uint32_t index = 0;
};

class Interface;
struct RxQueueOpts;

class IngressBase
{
public:
    IngressBase(Interface& iface, const RxQueueOpts& opts);
    virtual ~IngressBase();

    void start();
    void stop();

    RxQueueOpts opts;

protected:

    virtual bool pollFrame(FrameView& out) = 0;
    virtual void waitEvent() = 0;
    virtual void returnToDevice(uint32_t index) = 0;
    virtual void waitUntilAllFramesReleased() {}
    virtual void stopRx() = 0;

    void releaseFrame(uint32_t index);
    void flushReturned(uint32_t maxBatch);
    void flushLocalBatch();

    virtual void onReturnNudge() {}
    
    void runLoop();

protected:
    Interface& iface;
    const uint32_t qid;

    std::atomic<bool> running = false;
    std::thread ingressThread;

    static constexpr uint32_t RETURN_RING_CAP = 1024;
    uint32_t returnBuf[RETURN_RING_CAP];
    std::atomic<uint32_t> returnSeq[RETURN_RING_CAP];
    alignas(64) std::atomic<uint32_t> head{0};
    alignas(64) std::atomic<uint32_t> tail{0};
    alignas(64) std::atomic_flag returnDrainOwner = ATOMIC_FLAG_INIT;
};

#endif // INGRESS_BASE_H
