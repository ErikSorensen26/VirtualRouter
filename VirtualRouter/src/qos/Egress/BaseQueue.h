// BaseQueue.h

#ifndef BASE_QUEUE_H
#define BASE_QUEUE_H

#include <atomic>
#include <thread>
#include <PacketSlot.hpp>
#include <linux/futex.h>
#include <sys/syscall.h>

class Egress;

class BaseQueue
{
public:
    void enqueue(PacketSlot* pkt);

    void start();
    void stop();

protected:
    BaseQueue(Egress& egress) : out(egress), lock(0), wakeSignal(0), running(true) {}

    virtual void atomicEnqueue(PacketSlot* pkt) = 0;
    virtual bool isEmpty() const = 0;
    virtual void dequeueOne() = 0;

    void dequeue(uint32_t frame, uint32_t length);
    void drop(uint32_t frame);

    virtual ~BaseQueue();

private:
    Egress& out;
    std::atomic<uint32_t> lock;
    std::atomic<uint32_t> wakeSignal;
    std::atomic<bool> running;
    std::thread runThread;

    void runLoop();

    static void futex_wait(std::atomic<uint32_t>* addr, uint32_t expected);
    static void futex_wake(std::atomic<uint32_t>* addr, int count);
};

#endif // BASE_QUEUE_H
