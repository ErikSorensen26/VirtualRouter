// IngressBase.h

#ifndef INGRESS_BASE_H
#define INGRESS_BASE_H

#include <atomic>
#include <thread>
#include <cstdint>

#include "qos/ingress/RxQueueOpts.hpp"

namespace interface { class Interface; }

namespace hardware::ingress
{

struct FrameView
{
    uint8_t* payload = nullptr;
    uint32_t length  = 0;
    uint32_t index   = 0;
};

// Base class for all ingress (RX) backends.
//
// Subclasses implement the hardware-facing side:
//   pollFrame()      — dequeue one frame from the ring/socket
//   waitEvent()      — block until more frames arrive (e.g. epoll)
//   returnToDevice() — hand a frame index back to the kernel ring
//   stopRx()         — signal the hardware to stop delivering frames
//
// IngressBase handles the run loop, CPU affinity, and a small per-thread
// batch buffer so returnToDevice() is called in groups rather than one
// at a time.  The run loop is strictly single-threaded, so no atomics or
// MPSC rings are needed for frame returns.
class IngressBase
{
public:
    IngressBase(interface::Interface& iface, const qos::ingress::RxQueueOpts& opts);
    virtual ~IngressBase();

    void start();
    void stop();

    qos::ingress::RxQueueOpts opts;

protected:
    // ---- subclass interface ----
    virtual bool pollFrame(FrameView& out)     = 0;
    virtual void waitEvent()                   = 0;
    virtual void returnToDevice(uint32_t index) = 0;
    virtual void waitUntilAllFramesReleased()  {}
    virtual void stopRx()                      = 0;

    // Called when the batch is about to flush — subclasses may use this
    // to kick a fill ring or similar before returning frames.
    virtual void onReturnFlush() {}

    // ---- helpers for subclasses ----
    // Queue a frame for return; flushes automatically when the batch is full.
    void releaseFrame(uint32_t index);
    // Flush all buffered frame returns to the device immediately.
    void flushReturns();

    void runLoop();

protected:
    interface::Interface& iface;
    const uint32_t        qid;

    std::atomic<bool> running{false};
    std::thread       ingressThread;

private:
    // Small per-thread batch — avoids per-frame returnToDevice() overhead.
    // 64 entries is enough to amortise the cost without adding latency.
    static constexpr uint32_t RETURN_BATCH = 64;
    uint32_t returnBuf[RETURN_BATCH];
    uint32_t returnCount = 0;
};

} // namespace hardware::ingress

#endif // INGRESS_BASE_H
