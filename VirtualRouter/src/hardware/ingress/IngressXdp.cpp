// IngressXdp.cpp

#include "IngressXdp.h"
#include <linux/if_link.h>
#include <net/if.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <chrono>

static inline void xsk_kick(int fd)
{
    (void)sendto(fd, nullptr, 0, MSG_DONTWAIT, nullptr, 0);
}
static int ifindex_or_throw(const char* ifname)
{
    int idx = if_nametoindex(ifname);
    if (idx == 0) throw std::runtime_error(std::string("if_nametoindex failed for: ") + ifname);
    return idx;
}

IngressXdp::IngressXdp(const char* ifname, Interface& iface, uint32_t qid,
                       uint32_t frameCount, uint32_t frameSize)
    : IngressBase(ifname, iface, qid), frameCount(frameCount), frameMask(frameCount - 1), frameSize(frameSize), umemSize(uint64_t(frameCount) * frameSize)
{
    if ((frameSize * frameCount) % 4096 != 0)
        throw std::runtime_error("frameSize * frameCount must be page-aligned");
    if ((frameCount & (frameCount - 1)) != 0)
        throw std::runtime_error("frameCount must be power-of-two");

    // allocate UMEM
    if (posix_memalign(&umemArea, 4096, umemSize) != 0)
        throw std::runtime_error("posix_memalign(umem) failed");

    // Per-frame tracking
    frameInUse = new std::atomic_bool[frameCount];
    for (uint32_t i = 0; i < frameCount; ++i) frameInUse[i].store(false, std::memory_order_relaxed);

    rxEntries = frameCount;
    fqEntries = frameCount;

    setupSocket();
    setupUmem();
    configureRings();
    mmapRings();
    bindSocket();
    checkWakeSupport();
    prefillFillRing();
}

void IngressXdp::setupSocket()
{
    xskFd = ::socket(AF_XDP, SOCK_RAW, 0);
    if (xskFd < 0) throw std::runtime_error("socket(AF_XDP): " + std::string(strerror(errno)));
}

void IngressXdp::setupUmem()
{
    struct xdp_umem_reg umr
    {
        .addr = reinterpret_cast<uintptr_t>(umemArea),
        .len = static_cast<uint64_t>(umemSize),
        .chunk_size = frameSize,
        .headroom = 0,
        .flags = 0
    };
    if (setsockopt(xskFd, SOL_XDP, XDP_UMEM_REG, &umr,  sizeof(umr)) != 0)
        throw std::runtime_error("XDP_UMEM_REG: " + std::string(strerror(errno)));

    socklen_t len = sizeof(off);
    if (getsockopt(xskFd, SOL_XDP, XDP_MMAP_OFFSETS, &off, &len) != 0)
        throw std::runtime_error("XDP_MMAP_OFFSETS: " + std::string(strerror(errno)));
}

void IngressXdp::configureRings()
{
    if (setsockopt(xskFd, SOL_XDP, XDP_UMEM_FILL_RING, &fqEntries, sizeof(fqEntries)) != 0)
        throw std::runtime_error("XDP_UMEM_FILL_RING: " + std::string(strerror(errno)));
    if (setsockopt(xskFd, SOL_XDP, XDP_RX_RING, &rxEntries, sizeof(rxEntries)) != 0)
        throw std::runtime_error("XDP_RX_RING: " + std::string(strerror(errno)));
}

void IngressXdp::mmapRings()
{
    // RX
    rxRingMapSize = off.rx.desc + rxEntries * sizeof(struct xdp_desc);
    rxRingArea = mmap(nullptr, rxRingMapSize, PROT_READ | PROT_WRITE, MAP_SHARED, xskFd, XDP_PGOFF_RX_RING);
    if (rxRingArea == MAP_FAILED)
        throw std::runtime_error("mmap(RX_RING): " + std::string(strerror(errno)));
    rxRingProducer = reinterpret_cast<uint32_t*>((uint8_t*)rxRingArea + off.rx.producer);
    rxRingConsumer = reinterpret_cast<uint32_t*>((uint8_t*)rxRingArea + off.rx.consumer);
    rxRingDesc = reinterpret_cast<struct xdp_desc*>((uint8_t*)rxRingArea + off.rx.desc);

    // Fill
    fqMapSize = off.fr.desc + fqEntries * sizeof(uint64_t);
    fqArea = mmap(nullptr, fqMapSize, PROT_READ | PROT_WRITE, MAP_SHARED, xskFd, XDP_UMEM_PGOFF_FILL_RING);
    if (fqArea == MAP_FAILED)
        throw std::runtime_error("mmap(FILL_RING): " + std::string(strerror(errno)));
    fqProducer = reinterpret_cast<uint32_t*>((uint8_t*)fqArea + off.fr.producer);
    fqConsumer = reinterpret_cast<uint32_t*>((uint8_t*)fqArea + off.fr.consumer);
    fqDesc = reinterpret_cast<uint64_t*>((uint8_t*)fqArea + off.fr.desc);
}

void IngressXdp::bindSocket()
{
    struct sockaddr_xdp sxdp{};
    sxdp.sxdp_family = AF_XDP;
    sxdp.sxdp_ifindex = ifindex_or_throw(ifname);
    sxdp.sxdp_queue_id = qid;

    // try zero copy
    sxdp.sxdp_flags = XDP_USE_NEED_WAKEUP | XDP_ZEROCOPY;
    if (::bind(xskFd, reinterpret_cast<sockaddr*>(&sxdp), sizeof(sxdp)) == 0)
    {
        zeroCopy = true;
        return;
    }
    // fallback to copy
    sxdp.sxdp_flags = XDP_USE_NEED_WAKEUP;
    if (::bind(xskFd, reinterpret_cast<sockaddr*>(&sxdp), sizeof(sxdp)) != 0)
        throw std::runtime_error("bind(AF_XDP) copy-mode: " + std::string(strerror(errno)));
    zeroCopy = false;
}

void IngressXdp::checkWakeSupport()
{
    uint32_t optval = 0;
    socklen_t optlen = sizeof(optval);
    if (getsockopt(xskFd, SOL_XDP, XDP_OPTIONS, &optval, &optlen) == 0)
        needWakeup = (optval & XDP_RX_RING_NEED_WAKEUP);
    else
        needWakeup =  false;
}

void IngressXdp::prefillFillRing()
{
    uint32_t prod = *fqProducer;
    const uint32_t mask = fqEntries - 1;
    for (uint32_t i = 0; i < frameCount; ++i)
    {
        fqDesc[prod & mask] = frameAddr(i);
        prod++;
    }
    std::atomic_thread_fence(std::memory_order_release);
    *fqProducer = prod;
    kickIfNeeded();
}

void IngressXdp::kickIfNeeded()
{
    if (needWakeup) xsk_kick(xskFd);
}

bool IngressXdp::pollFrame(FrameView& out)
{
    uint32_t cons = *rxRingConsumer;
    std::atomic_thread_fence(std::memory_order_acquire);
    if (cons == *rxRingProducer)
        return false;
    
    const uint32_t mask = rxEntries - 1;
    const xdp_desc& desc = rxRingDesc[cons & mask];

    uint32_t idx = uint32_t(desc.addr / frameSize);
    if (idx >= frameCount || frameInUse[idx].exchange(true, std::memory_order_acq_rel))
    {
        *rxRingConsumer = (cons + 1) & mask;
        std::atomic_thread_fence(std::memory_order_release);
        return false;
    }

    out.payload = reinterpret_cast<uint8_t*>(umemArea) + (idx * frameSize);
    out.length = desc.len;
    out.index = idx;

    *rxRingConsumer = (cons + 1) & mask;
    std::atomic_thread_fence(std::memory_order_release);
    return true;
}

void IngressXdp::releaseFrame(uint32_t index)
{
    if (index >= frameCount) return;
    uint32_t prod = *fqProducer;
    const uint32_t mask = fqEntries - 1;
    fqDesc[prod & mask] = frameAddr(index);
    ++prod;

    std::atomic_thread_fence(std::memory_order_release);
    *fqProducer = prod;

    frameInUse[index].store(false, std::memory_order_release);
    kickIfNeeded();
}

void IngressXdp::waitForData()
{
    kickIfNeeded();
    struct pollfd pfd{ xskFd, POLLIN, 0 };
    int ret = ::poll(&pfd, 1, -1);
    if (ret < 0 && errno != EINTR)
        throw std::runtime_error("poll(xskFd): " + std::string(strerror(errno)));
}

void IngressXdp::stopRx()
{
    if (xskFd >= 0) ::shutdown(xskFd, SHUT_RD);
}

void IngressXdp::waitUntilAllFramesReleased()
{
    constexpr auto timeout = std::chrono::milliseconds(2000);
    auto deadline = std::chrono::steady_clock::now() + timeout;

    while(true)
    {
        bool allFree = true;
        for (uint32_t i = 0; i < frameCount; ++i)
        {
            if (frameInUse[i].load(std::memory_order_acquire))
            {
                allFree = false;
                break;
            }
            if (allFree) return;
#ifndef NDEBUG
            throw std::runtime_error("Timeout waiting for frames to return");
#else
            uint32_t inUse = 0;
            for (uint32_t i = 0; ii < frameCount; ++i)
                if (frameInUse[i].load(std::memory_order_release)) ++inUse;
            std::cerr << "Warning: timeout; " << inUse << " frames still in use\n";
            return;
#endif
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}
