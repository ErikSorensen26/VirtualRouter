// Ingress.cpp

#include "Ingress.h"
#include <linux/if_xdp.h>
#include <linux/if_link.h>
#include <net/if.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <errno.h>
#include <Interface.h>
#include <Global.h>
#include <VirtualRouter.h>
#include <Process.h>
#include <Decapsulation.h>

Ingress::Ingress(const char* ifname, Interface& iface, uint32_t qid,
                 uint32_t frameCount, uint32_t frameSize)
  : iface(iface),
    frameCount(frameCount),
    frameMask(frameCount - 1),
    frameSize(frameSize),
    qid(qid),
    umemSize(uint64_t(frameCount) * frameSize)
{
    if (frameSize % 4096 != 0)
        throw std::runtime_error("frameSize must be page-aligned");

    if ((frameCount & (frameCount - 1)) != 0)
        throw std::runtime_error("frameCount must be a power of 2 for ring optimization");

    if (posix_memalign(&umemArea, 4096, umemSize) != 0)
        throw std::runtime_error("posix_memalign failed");

    frameInUse = new std::atomic_bool[frameCount];
    for (uint32_t i = 0; i < frameCount; ++i)
        frameInUse[i].store(false, std::memory_order_relaxed);

    setupSocket();
    setupUmem();
    mapRxRing();
    bindSocket(ifname);
    checkWakeSupport();
}

Ingress::~Ingress()
{
    if (xskFd > 0) close(xskFd);
    if (umemArea) std::free(umemArea);
    if (rxRingArea) munmap(rxRingArea, rxRingMapSize);
    delete[] frameInUse;
}

void Ingress::start()
{
    running.store(true, std::memory_order_release);
    ingressThread = std::thread([this] { this->runLoop(); });
}

void Ingress::stop()
{
    shutdown(xskFd, SHUT_RD);

    // Wait until all in-flight frames are returned by thread pool
    waitUntilAllFramesReleased();

    running.store(false, std::memory_order_release);
    if (ingressThread.joinable())
        ingressThread.join();
}

void Ingress::waitUntilAllFramesReleased()
{
    constexpr auto timeout = std::chrono::milliseconds(2000); // Timeout
    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (true)
    {
        bool allFree = true;

        // Checks if all frames are returned.
        for (uint32_t i = 0; i < frameCount; ++i)
        {
            if (frameInUse[i].load(std::memory_order_acquire))
            {
                allFree = false;
                break;
            }
        }

        if (allFree)
            return;

        if (std::chrono::steady_clock::now() > deadline)
        {
        #ifdef DEBUG
            throw std::runtime_error("Timeout waiting for packets to empty");
        #else
            uint32_t inUse = 0;
            for (uint32_t i = 0; i < frameCount; ++i)
                if (frameInUse[i].load(std::memory_order_relaxed))
                    ++inUse;

            std::cerr << "Warning: timed out, " << inUse << " packets still pending and will be dropped\n";
            break;
        #endif
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void Ingress::runLoop()
{
    FrameView frame;
    while (true)
    {
        bool any = false;

        if (!running.load(std::memory_order_acquire))
            break;

        while (pollFrame(frame))
        {
            any = true;
            iface.routingInstance->global.threadPool.enqueue([this, frm = frame]() {
                PacketInfo packetInfo;
                inspect(packetInfo, frm.payload, frm.length);
                decapsulate(packetInfo, frm.payload, frm.length);
                processPacket(frm.payload, frm.length, packetInfo, iface.routingInstance, &iface);
                releaseFrame(frm.index);
            });
        }

        if (!any)
        {
            struct pollfd pfd = { xskFd, POLLIN, 0 };
            int ret = poll(&pfd, 1, -1);
            if (ret < 0 && errno != EINTR)
                throw std::runtime_error("poll(xskFd): " + std::string(strerror(errno)));
        }
    }
}

bool Ingress::pollFrame(FrameView& out)
{
    uint32_t cons = *rxRingConsumer;
    std::atomic_thread_fence(std::memory_order_acquire);

    if (cons == *rxRingProducer)
        return false;

    const xdp_desc& desc = rxRingDesc[cons & frameMask];
    uint32_t index = desc.addr / frameSize;
    if (index >= frameCount || frameInUse[index].exchange(true))
        return false;

    out.payload = reinterpret_cast<uint8_t*>(umemArea) + (index * frameSize);
    out.length = desc.len;
    out.index = index;

    *rxRingConsumer = (cons + 1) & frameMask;
    std::atomic_thread_fence(std::memory_order_release);
    return true;
}

void Ingress::releaseFrame(uint32_t index)
{
    if (index < frameCount)
        frameInUse[index].store(false, std::memory_order_release);
}

void Ingress::setupSocket()
{
    xskFd = socket(AF_XDP, SOCK_RAW, 0);
    if (xskFd < 0)
        throw std::runtime_error("socket(AF_XDP): " + std::string(strerror(errno)));
}

void Ingress::setupUmem()
{
    struct xdp_umem_reg umr {
        .addr = reinterpret_cast<uintptr_t>(umemArea),
        .len = static_cast<uint32_t>(umemSize),
        .chunk_size = frameSize,
        .headroom = 0,
        .flags = 0
    };

    if (setsockopt(xskFd, SOL_XDP, XDP_UMEM_REG, &umr, sizeof(umr)) != 0)
        throw std::runtime_error("setsockopt(XDP_UMEM_REG): " + std::string(strerror(errno)));

    socklen_t len = sizeof(off);
    if (getsockopt(xskFd, SOL_XDP, XDP_MMAP_OFFSETS, &off, &len) != 0)
        throw std::runtime_error("getsockopt(XDP_MMAP_OFFSETS): " + std::string(strerror(errno)));
}

void Ingress::mapRxRing()
{
    rxRingMapSize = off.rx.desc + frameCount * sizeof(struct xdp_desc);
    rxRingArea = mmap(nullptr, rxRingMapSize, PROT_READ|PROT_WRITE, MAP_SHARED, xskFd, XDP_PGOFF_RX_RING);
    if (rxRingArea == MAP_FAILED)
        throw std::runtime_error("mmap(RX_RING): " + std::string(strerror(errno)));

    rxRingProducer = reinterpret_cast<uint32_t*>((uint8_t*)rxRingArea + off.rx.producer);
    rxRingConsumer = reinterpret_cast<uint32_t*>((uint8_t*)rxRingArea + off.rx.consumer);
    rxRingDesc     = reinterpret_cast<struct xdp_desc*>((uint8_t*)rxRingArea + off.rx.desc);
}

void Ingress::bindSocket(const char* ifname)
{
    struct sockaddr_xdp sxdp = {};
    sxdp.sxdp_family = AF_XDP;
    sxdp.sxdp_ifindex = if_nametoindex(ifname);
    sxdp.sxdp_queue_id = qid;

    sxdp.sxdp_flags = XDP_USE_NEED_WAKEUP | XDP_ZEROCOPY;
    if (bind(xskFd, reinterpret_cast<sockaddr*>(&sxdp), sizeof(sxdp)) == 0)
    {
        zeroCopy = true;
        return;
    }

    sxdp.sxdp_flags = XDP_USE_NEED_WAKEUP;
    if (bind(xskFd, reinterpret_cast<sockaddr*>(&sxdp), sizeof(sxdp)) != 0)
        throw std::runtime_error("bind(AF_XDP): " + std::string(strerror(errno)));

    zeroCopy = false;
}

void Ingress::checkWakeSupport()
{
    uint32_t optval = 0;
    socklen_t optlen = sizeof(optval);
    if (getsockopt(xskFd, SOL_XDP, XDP_OPTIONS, &optval, &optlen) == 0)
        needWakeup = (optval & XDP_RING_NEED_WAKEUP);
    else
        needWakeup = false;
}
