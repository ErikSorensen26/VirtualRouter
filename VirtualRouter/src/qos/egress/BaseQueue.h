// BaseQueue.h

#ifndef BASE_QUEUE_H
#define BASE_QUEUE_H

#include <atomic>
#include <thread>
#include <linux/futex.h>
#include <sys/syscall.h>

#include "hardware/PacketSlot.hpp"
#include "hardware/egress/EgressBase.h"

namespace qos::egress
{

using EgressBase = hardware::egress::EgressBase;

class BaseQueue
{
public:
    BaseQueue(EgressBase& egress) : out(egress), lock(0), wakeSignal(0), running(true) {}
    virtual ~BaseQueue(){ stop(); }

    void enqueue(hardware::PacketSlot* pkt);
    void start();
    void stop();
    EgressBase& out;

protected:

    virtual void atomicEnqueue(hardware::PacketSlot* pkt) = 0;
    virtual bool isEmpty() const = 0;
    virtual void dequeueOne() = 0;

    void dequeue(uint32_t frame, uint32_t length);
    void drop(uint32_t frame);

private:
    std::atomic<uint32_t> lock;
    std::atomic<uint32_t> wakeSignal;
    std::atomic<bool> running;
    std::thread runThread;

    void runLoop();

    void futex_wait(std::atomic<uint32_t>* addr, uint32_t expected);
    void futex_wake(std::atomic<uint32_t>* addr, int count);
};

} // namespace qos

#endif // BASE_QUEUE_H

