// IngressXdp.cpp

#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <net/if.h>

#include "IngressXdp.h"
#include "hardware/Ifname.h"

#ifndef HOT
#define HOT __attribute__((hot))
#endif
#ifndef ALWAYS_INLINE
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#endif

namespace hardware::ingress
{

static inline void xsk_kick(int fd)
{
    (void)::sendto(fd, nullptr, 0, MSG_DONTWAIT, nullptr, 0);
}

static uint32_t nextPow2(uint32_t v)
{
    if (v <= 1) return 1;
    --v; v |= v>>1; v |= v>>2; v |= v>>4; v |= v>>8; v |= v>>16; return v+1;
}

// CONSTRUCTION

IngressXdp::IngressXdp(interface::Interface& iface, const qos::ingress::RxQueueOpts& opts)
    : IngressBase(iface, opts)
{
    frameCount = nextPow2(opts.frameCount ? opts.frameCount : 1024);
    frameMask  = frameCount - 1;
    frameSize  = 4096;
    umemSize   = uint64_t(frameCount) * frameSize;

    rxEntries = frameCount;
    fqEntries = frameCount;
    rxMask    = rxEntries - 1;
    fqMask    = fqEntries - 1;

    setupSocket();
    setupUmem();
    configureRings();
    mmapRings();
    bindSocket();
    checkWakeSupport();
    setupEvents();
    prefillFillRing();
}

IngressXdp::~IngressXdp()
{
    teardownEvents();
    if (rxRingArea && rxRingArea != MAP_FAILED) ::munmap(rxRingArea, rxRingMapSize);
    if (fqArea     && fqArea     != MAP_FAILED) ::munmap(fqArea,     fqMapSize);
    if (xskFd >= 0) ::close(xskFd);
    if (umemArea)
    {
        (void)::munlock(umemArea, umemSize);
        ::free(umemArea);
        umemArea = nullptr;
    }
}

// SETUP

void IngressXdp::setupSocket()
{
    xskFd = ::socket(AF_XDP, SOCK_RAW, 0);
    if (xskFd < 0)
        throw std::runtime_error("socket(AF_XDP): " + std::string(strerror(errno)));

    // Busy-poll hints — kernel ignores these silently if not supported.
    int v = 1;  (void)setsockopt(xskFd, SOL_SOCKET, SO_PREFER_BUSY_POLL, &v, sizeof(v));
    v = 64;     (void)setsockopt(xskFd, SOL_SOCKET, SO_BUSY_POLL_BUDGET,  &v, sizeof(v));
    v = 50;     (void)setsockopt(xskFd, SOL_SOCKET, SO_BUSY_POLL,         &v, sizeof(v));
}

void IngressXdp::setupUmem()
{
    if (posix_memalign(&umemArea, 4096, umemSize) != 0 || !umemArea)
        throw std::runtime_error("posix_memalign(UMEM) failed");

    (void)::mlock(umemArea, umemSize);
#ifdef MADV_HUGEPAGE
    (void)::madvise(umemArea, umemSize, MADV_HUGEPAGE);
#endif
    (void)::madvise(umemArea, umemSize, MADV_WILLNEED);

    struct xdp_umem_reg umr{};
    umr.addr       = reinterpret_cast<uintptr_t>(umemArea);
    umr.len        = umemSize;
    umr.chunk_size = frameSize;
    umr.headroom   = 0;
    umr.flags      = 0;

    if (::setsockopt(xskFd, SOL_XDP, XDP_UMEM_REG, &umr, sizeof(umr)) != 0)
        throw std::runtime_error("XDP_UMEM_REG: " + std::string(strerror(errno)));

    socklen_t len = sizeof(off);
    if (::getsockopt(xskFd, SOL_XDP, XDP_MMAP_OFFSETS, &off, &len) != 0)
        throw std::runtime_error("XDP_MMAP_OFFSETS: " + std::string(strerror(errno)));
}

void IngressXdp::configureRings()
{
    if (::setsockopt(xskFd, SOL_XDP, XDP_UMEM_FILL_RING, &fqEntries, sizeof(fqEntries)) != 0)
        throw std::runtime_error("XDP_UMEM_FILL_RING: " + std::string(strerror(errno)));

    if (::setsockopt(xskFd, SOL_XDP, XDP_RX_RING, &rxEntries, sizeof(rxEntries)) != 0)
        throw std::runtime_error("XDP_RX_RING: " + std::string(strerror(errno)));
}

void IngressXdp::mmapRings()
{
    // RX ring
    rxRingMapSize = off.rx.desc + rxEntries * sizeof(struct xdp_desc);
    rxRingArea = ::mmap(nullptr, rxRingMapSize,
                        PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, xskFd, XDP_PGOFF_RX_RING);
    if (rxRingArea == MAP_FAILED)
        throw std::runtime_error("mmap(RX_RING): " + std::string(strerror(errno)));

    (void)::mlock(rxRingArea, rxRingMapSize);

    rxProducer = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(rxRingArea) + off.rx.producer);
    rxConsumer = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(rxRingArea) + off.rx.consumer);
    rxDesc     = reinterpret_cast<struct xdp_desc*>(static_cast<uint8_t*>(rxRingArea) + off.rx.desc);

    // Fill ring
    fqMapSize = off.fr.desc + fqEntries * sizeof(uint64_t);
    fqArea = ::mmap(nullptr, fqMapSize,
                    PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, xskFd, XDP_UMEM_PGOFF_FILL_RING);
    if (fqArea == MAP_FAILED)
        throw std::runtime_error("mmap(FILL_RING): " + std::string(strerror(errno)));

    (void)::mlock(fqArea, fqMapSize);

    fqProducer = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(fqArea) + off.fr.producer);
    fqConsumer = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(fqArea) + off.fr.consumer);
    fqAddr     = reinterpret_cast<uint64_t*>(static_cast<uint8_t*>(fqArea) + off.fr.desc);

    rxConsShadow = __atomic_load_n(rxConsumer, __ATOMIC_ACQUIRE);
    fqProdShadow = __atomic_load_n(fqProducer, __ATOMIC_ACQUIRE);
}

void IngressXdp::bindSocket()
{
    const int ifidx = ifnametoindex(opts.ifname.c_str());
    if (ifidx <= 0)
        throw std::runtime_error("ifnametoindex failed for '" + opts.ifname + "'");

    struct sockaddr_xdp sxdp{};
    sxdp.sxdp_family   = AF_XDP;
    sxdp.sxdp_ifindex  = static_cast<uint32_t>(ifidx);
    sxdp.sxdp_queue_id = qid;

    // Try zero-copy first; fall back to copy mode.
    sxdp.sxdp_flags = XDP_ZEROCOPY | XDP_USE_NEED_WAKEUP;
    if (::bind(xskFd, reinterpret_cast<sockaddr*>(&sxdp), sizeof(sxdp)) == 0)
    {
        zeroCopy = true;
        return;
    }

    sxdp.sxdp_flags = XDP_USE_NEED_WAKEUP;
    if (::bind(xskFd, reinterpret_cast<sockaddr*>(&sxdp), sizeof(sxdp)) != 0)
        throw std::runtime_error("bind(AF_XDP): " + std::string(strerror(errno)));
}

void IngressXdp::checkWakeSupport()
{
    uint32_t optval = 0;
    socklen_t optlen = sizeof(optval);
    if (::getsockopt(xskFd, SOL_XDP, XDP_OPTIONS, &optval, &optlen) == 0)
        needWakeup = (optval & XDP_RING_NEED_WAKEUP) != 0;
}

void IngressXdp::setupEvents()
{
    epfd = ::epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0)
        throw std::runtime_error("epoll_create1: " + std::string(strerror(errno)));

    evtfd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (evtfd < 0)
        throw std::runtime_error("eventfd: " + std::string(strerror(errno)));

    epoll_event ev{};
    ev.events  = EPOLLIN;
    ev.data.fd = xskFd;
    if (::epoll_ctl(epfd, EPOLL_CTL_ADD, xskFd, &ev) != 0)
        throw std::runtime_error("epoll_ctl ADD xskFd: " + std::string(strerror(errno)));

    epoll_event ev2{};
    ev2.events  = EPOLLIN;
    ev2.data.fd = evtfd;
    if (::epoll_ctl(epfd, EPOLL_CTL_ADD, evtfd, &ev2) != 0)
        throw std::runtime_error("epoll_ctl ADD evtfd: " + std::string(strerror(errno)));
}

void IngressXdp::teardownEvents()
{
    if (epfd >= 0 && xskFd >= 0) ::epoll_ctl(epfd, EPOLL_CTL_DEL, xskFd, nullptr);
    if (epfd >= 0 && evtfd >= 0) ::epoll_ctl(epfd, EPOLL_CTL_DEL, evtfd, nullptr);
    if (evtfd >= 0) { ::close(evtfd); evtfd = -1; }
    if (epfd  >= 0) { ::close(epfd);  epfd  = -1; }
}

void IngressXdp::prefillFillRing()
{
    uint32_t prod = fqProdShadow;
    for (uint32_t i = 0; i < frameCount; ++i)
        fqAddr[(prod + i) & fqMask] = frameAddr(i);
    prod += frameCount;

    std::atomic_thread_fence(std::memory_order_release);
    __atomic_store_n(fqProducer, prod, __ATOMIC_RELEASE);
    fqProdShadow = prod;

    if (needWakeup) xsk_kick(xskFd);
}

// HELPERS

void IngressXdp::kickIfNeeded()
{
    if (needWakeup) xsk_kick(xskFd);
}

ALWAYS_INLINE uint32_t IngressXdp::refillRxCache()
{
    uint32_t prod = __atomic_load_n(rxProducer, __ATOMIC_ACQUIRE);
    uint32_t cons = rxConsShadow;
    uint32_t avail = prod - cons;
    if (avail == 0) return 0;

    uint32_t take = std::min<uint32_t>(avail, RX_CACHE_CAP);

    for (uint32_t i = 0; i < take; ++i)
    {
        const struct xdp_desc& d = rxDesc[(cons + i) & rxMask];
        rxCache[i].idx = static_cast<uint32_t>(d.addr / frameSize);
        rxCache[i].len = d.len;
    }

    cons += take;
    rxConsShadow = cons;
    __atomic_store_n(rxConsumer, cons, __ATOMIC_RELEASE);

    rxHead = 0;
    rxTail = take;
    return take;
}

// RUN LOOP

ALWAYS_INLINE HOT bool IngressXdp::pollFrame(FrameView& out)
{
    if (rxHead == rxTail && refillRxCache() == 0)
        return false;

    const RxItem item = rxCache[rxHead++];
    out.payload = reinterpret_cast<uint8_t*>(umemArea) + frameAddr(item.idx);
    out.length  = item.len;
    out.index   = item.idx;
    outstanding.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void IngressXdp::waitEvent()
{
    // Commit any pending fill ring entries before sleeping so the kernel can
    // refill its RX ring while we wait.
    flushReturns();
    kickIfNeeded();

    constexpr int MAX_EVENTS = 8;
    epoll_event events[MAX_EVENTS];

    for (;;)
    {
        int n = ::epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (n < 0)
        {
            if (errno == EINTR) continue;
            throw std::runtime_error("epoll_wait: " + std::string(strerror(errno)));
        }

        for (int i = 0; i < n; ++i)
        {
            if (events[i].data.fd == evtfd)
            {
                uint64_t x;
                (void)::read(evtfd, &x, sizeof(x));
                return;
            }
            if (events[i].data.fd == xskFd)
                return;
        }
    }
}

// FRAME RETURN

ALWAYS_INLINE HOT void IngressXdp::returnToDevice(uint32_t index)
{
    fqAddr[fqProdShadow & fqMask] = frameAddr(index);
    ++fqProdShadow;
    outstanding.fetch_sub(1, std::memory_order_relaxed);
}

void IngressXdp::onReturnFlush()
{
    // Commit everything accumulated in fqProdShadow since the last flush.
    // Called by IngressBase::flushReturns() after the full returnToDevice batch.
    __atomic_store_n(fqProducer, fqProdShadow, __ATOMIC_RELEASE);
    if (needWakeup) xsk_kick(xskFd);
}

// STOP

void IngressXdp::stopRx()
{
    // Writing to evtfd wakes epoll_wait in waitEvent() so the run loop sees
    // running == false and exits cleanly.
    if (evtfd >= 0)
    {
        uint64_t one = 1;
        (void)::write(evtfd, &one, sizeof(one));
    }
}

void IngressXdp::waitUntilAllFramesReleased()
{
    // Commit any remaining fill ring entries before we start waiting.
    if (fqProdShadow != __atomic_load_n(fqProducer, __ATOMIC_RELAXED))
    {
        __atomic_store_n(fqProducer, fqProdShadow, __ATOMIC_RELEASE);
        if (needWakeup) xsk_kick(xskFd);
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);

    while (outstanding.load(std::memory_order_acquire) != 0)
    {
        if (std::chrono::steady_clock::now() > deadline)
        {
            std::cerr << "Warning: timeout waiting for AF_XDP frames to be released ("
                      << outstanding.load(std::memory_order_relaxed) << " outstanding)\n";
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

} // namespace hardware::ingress
