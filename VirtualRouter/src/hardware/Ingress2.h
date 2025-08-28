// Ingress.h

#ifndef INGRESS_H
#define INGRESS_H

#include <linux/if_xdp.h>
#include <atomic>
#include <thread>

class Interface;

class Ingress
{
public:
    struct FrameView
    {
        uint8_t* payload = nullptr;
        uint32_t length = 0;
        uint32_t index = 0;
    };

    Ingress(const char* ifname, Interface& iface, uint32_t qid = 0,
            uint32_t frameCount = 4096, uint32_t frameSize = 2048);

    ~Ingress();

    void start();
    void stop();

    bool pollFrame(FrameView& out);
    void releaseFrame(uint32_t index);

private:

    Interface& iface;

    int xskFd{-1};
    uint32_t frameCount;
    uint32_t frameMask;
    uint32_t frameSize;
    uint32_t qid;
    uint64_t umemSize;
    void* umemArea = nullptr;

    std::atomic_bool* frameInUse = nullptr;

    bool needWakeup = false;
    bool zeroCopy = false;

    void* rxRingArea = nullptr;
    size_t rxRingMapSize = 0;

    struct xdp_mmap_offsets off{};
    struct xdp_desc* rxRingDesc = nullptr;
    uint32_t* rxRingProducer = nullptr;
    uint32_t* rxRingConsumer = nullptr;

    std::atomic<uint32_t> wakeSignal{0};
    std::atomic_bool running{false};
    std::thread ingressThread;

    void runLoop();
    void waitUntilAllFramesReleased();

    void setupSocket();
    void setupUmem();
    void mapRxRing();
    void bindSocket(const char* ifname);
    void checkWakeSupport();
};

#endif // INGRESS_H
