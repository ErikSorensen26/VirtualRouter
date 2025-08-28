// IngressBase.h

#ifndef INGRESS_BASE
#define INGRESS_BASE

#include <cstdint>
#include <atomic>
#include <thread>

struct Interface;
struct RoutingInstance;

struct FrameView
{
    uint8_t* payload = nullptr;
    uint32_t length = 0;
    uint32_t index = 0;
};

class IngressBase
{
public:
    IngressBase(const char* ifname, Interface& iface, uint32_t qid)
        : ifname(ifname), iface(iface), qid(qid) {}
    virtual ~IngressBase() = default;

    void start();
    void stop();

protected:

    virtual bool pollFrame(FrameView& out) = 0;
    virtual void releaseFrame(uint32_t index) = 0;
    virtual void waitForData() = 0;
    virtual void stopRx() = 0;
    virtual void waitUntilAllFramesReleased() {}

    void runLoop();

protected:
    const char* ifname;
    Interface& iface;
    const uint32_t qid;

    std::atomic_bool running{false};
    std::thread ingressThread;
};

#endif
