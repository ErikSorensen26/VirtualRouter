// IngressXdp.cpp

/*#include "IngressXdp.h"
#include <linux/if_link.h>
#include <net/if.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <Likely.hpp>

namespace hardware::ingress
{

static inline void xsk_kick(int fd)
{
    (void)::sendto(fd, nullptr, 0, MSG_DONTWAIT, nullptr, 0);
}
static int ifindex_or_throw(const char* ifname)
{
    int idx = if_nametoindex(ifname);
    if (idx == 0) throw std::runtime_error(std::string("if_nametoindex failed for: ") + ifname);
    return idx;
}

IngressXdp::IngressXdp(const char* ifname, interface::Interface& iface, uint32_t qid, uint32_t frameCount, uint32_t frameSize)
    : IngressBase(ifname, iface, qid), frameCount(frameCount), frameMask(frameCount - 1), frameSize(frameSize), umemSize(uint64_t(frameCount) * frameSize)
{
    if ((frameSize * frameCount) % 4096 != 0)
        throw std::runtime_error("frameSize * frameCount must be page-aligned");
    if ((frameCount & (frameCount - 1)) != 0)
        throw std::runtime_error("frameCount must be power-of-two");

    // allocate UMEM
    if (posix_memalign(&umemArea, 4096, umemSize) != 0)
        throw std::runtime_error("posix_memalign(umem) failed");

    (void)mlock(umemArea, umemSize);
#ifdef MADV_HUGEPAGE
    (void)madvise(umemArea, umemSize, MADV_HUGEPAGE);
#endif
    (void)madvise(umemArea, umemSize, MADV_WILLNEED);

    rxEntries = frameCount;
    fqEntries = frameCount;
    rxMask = rxEntries - 1;
    fqMask = fqEntries - 1;

    setupSocket();
    setupUmem();
    configureRings();
    mmapRings();
    bindSocket();
    checkWakeSupport();
    setupEvents();
    prefillFillRing();
}

IngressXdp::~IngressXdp() {
    teardownEvents();
    if (xskFd >= 0) ::close(xskFd);
    if (rxRingArea) ::munmap(rxRingArea, rxRingMapSize);
    if (fqArea)     ::munmap(fqArea, fqMapSize);
    if (umemArea)
    {
        (void)munlock(umemArea, umemSize);
        ::free(umemArea);
        umemArea = nullptr;
    }
}

void IngressXdp::setupSocket()
{
    xskFd = ::socket(AF_XDP, SOCK_RAW, 0);
    if (xskFd < 0)
        throw std::runtime_error("socket(AF_XDP): " + std::string(strerror(errno)));

    int prefer = 1;
    setsockopt(xskFd, SOL_SOCKET, SO_PREFER_BUSY_POLL, &prefer, sizeof(prefer));
    int budget = 64;
    setsockopt(xskFd, SOL_SOCKET, SO_BUSY_POLL_BUDGET, &budget, sizeof(budget));
    int us = 50;
    setsockopt(xskFd, SOL_SOCKET, SO_BUSY_POLL, &us, sizeof(us));
}

void IngressXdp::setupUmem()
{
    struct xdp_umem_reg umr;
    std::memset(&umr, 0, sizeof(umr));
    umr.addr = reinterpret_cast<uintptr_t>(umemArea);
    umr.len = static_cast<uint64_t>(umemSize);
    umr.chunk_size = frameSize;
    umr.headroom = 0;
    umr.flags = 0;

    if (::setsockopt(xskFd, SOL_XDP, XDP_UMEM_REG, &umr,  sizeof(umr)) != 0)
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
    rxRingArea = mmap(nullptr, rxRingMapSize, PROT_READ | PROT_WRITE, MAP_SHARED, xskFd, XDP_PGOFF_RX_RING);
    if (rxRingArea == MAP_FAILED)
        throw std::runtime_error("mmap(RX_RING): " + std::string(strerror(errno)));

    rxProducer = reinterpret_cast<uint32_t*>((uint8_t*)rxRingArea + off.rx.producer);
    rxConsumer = reinterpret_cast<uint32_t*>((uint8_t*)rxRingArea + off.rx.consumer);
    rxDesc     = reinterpret_cast<struct xdp_desc*>((uint8_t*)rxRingArea + off.rx.desc);

    // Fill ring
    fqMapSize = off.fr.desc + fqEntries * sizeof(uint64_t);
    fqArea = ::mmap(nullptr, fqMapSize, PROT_READ | PROT_WRITE, MAP_SHARED, xskFd, XDP_UMEM_PGOFF_FILL_RING);
    if (fqArea == MAP_FAILED)
        throw std::runtime_error("mmap(FILL_RING): " + std::string(strerror(errno)));

    fqProducer = reinterpret_cast<uint32_t*>((uint8_t*)fqArea + off.fr.producer);
    fqConsumer = reinterpret_cast<uint32_t*>((uint8_t*)fqArea + off.fr.consumer);
    fqAddr     = reinterpret_cast<uint64_t*>((uint8_t*)fqArea + off.fr.desc);

    rxProdCached = *rxProducer;
    rxConsShadow = *rxConsumer;
    fqProdShadow = *fqProducer;
}

void IngressXdp::bindSocket()
{
    struct sockaddr_xdp sxdp{};
    sxdp.sxdp_family = AF_XDP;
    sxdp.sxdp_ifindex = ifindex_or_throw(ifname);
    sxdp.sxdp_queue_id = qid;

    // try zero copy
    sxdp.sxdp_flags = XDP_ZEROCOPY | XDP_USE_NEED_WAKEUP;
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

    if (::getsockopt(xskFd, SOL_XDP, XDP_OPTIONS, &optval, &optlen) == 0)
        needWakeup = (optval & XDP_RING_NEED_WAKEUP) != 0;
    else
        needWakeup = false;
}

void IngressXdp::setupEvents()
{
    epfd = ::epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0)
        throw std::runtime_error("epoll_create1: " + std::string(std::strerror(errno)));

    evtfd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (evtfd < 0)
        throw std::runtime_error("eventfd: " + std::string(std::strerror(errno)));

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = xskFd;
    if (::epoll_ctl(epfd, EPOLL_CTL_ADD, xskFd, &ev) != 0)
        throw std::runtime_error("epoll_ctl ADD xskFd: " + std::string(std::strerror(errno)));

    epoll_event ev2{};
    ev2.events = EPOLLIN;
    ev2.data.fd = evtfd;
    if (::epoll_ctl(epfd, EPOLL_CTL_ADD, evtfd, &ev2) != 0)
        throw std::runtime_error("epoll_ctl ADD evtfd: " + std::string(strerror(errno)));
}

void IngressXdp::teardownEvents()
{
    if (epfd >= 0 && xskFd >= 0) ::epoll_ctl(epfd, EPOLL_CTL_DEL, xskFd, nullptr);
    if (epfd >= 0 && evtfd >= 0) ::epoll_ctl(epfd, EPOLL_CTL_DEL, evtfd, nullptr);
    if (evtfd >= 0) { ::close(evtfd); evtfd = -1; }
    if (epfd >= 0) { ::close(epfd); epfd = -1; }
}

void IngressXdp::prefillFillRing()
{
    const uint32_t mask = fqMask;
    uint32_t prod = fqProdShadow;

    for (uint32_t i = 0; i < frameCount; ++i)
        fqAddr[(prod + i) & mask] = frameAddr(i);

    prod += frameCount;

    std::atomic_thread_fence(std::memory_order_release);
    *fqProducer = prod;
    fqProdShadow = prod;

    if (needWakeup) xsk_kick(xskFd);
}

inline void IngressXdp::kickIfNeeded()
{
    if (needWakeup) xsk_kick(xskFd);
}

void IngressXdp::waitEvent()
{
    kickIfNeeded();
    flushReturned(1024);

    constexpr int MAX_EVENTS = 8;
    epoll_event events[MAX_EVENTS];

    for (;;)
    {
        int n = ::epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (n < 0)
        {
            if (errno == EINTR) continue;
            throw std::runtime_error("epoll_wait: " + std::string(std::strerror(errno)));
        }
        for (int i = 0; i < n; ++i)
        {
            const int fd = events[i].data.fd;
            const uint32_t ev = events[i].events;

            if (fd == evtfd)
            {
                uint64_t x;
                (void)::read(evtfd, &x, sizeof(x));
                return;
            }
            if (fd == xskFd)
            {
                if (ev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
                    throw std::runtime_error("epoll xskFd error");
                return;
            }
        }
    }
}

void IngressXdp::signalStop()
{
    stopping.store(true, std::memory_order_release);
    if (evtfd >= 0)
    {
        uint64_t one = 1;
        (void)::write(evtfd, &one, sizeof(one));
    }
}

inline uint32_t IngressXdp::refillRxCache()
{
    uint32_t prod = *rxProducer;
    std::atomic_thread_fence(std::memory_order_acquire);

    uint32_t cons = rxConsShadow;
    uint32_t avail = prod - cons;
    if (avail == 0) return 0;

    uint32_t take = std::min<uint32_t>(avail, RX_CACHE_CAP);

    // Fill cache
    for (uint32_t i = 0; i < take; ++i)
    {
        const struct xdp_desc& d = rxDesc[(cons + 1) & rxMask];
        const uint32_t idx = uint32_t(d.addr / frameSize);
        rxCache[i].idx = idx;
        rxCache[i].len = d.len;
    }

    cons += take;
    rxConsShadow = cons;
    std::atomic_thread_fence(std::memory_order_release);
    *rxConsumer = cons;

    rxHead = 0;
    rxTail = take;
    return take;
}

bool IngressXdp::pollFrame(FrameView& out)
{
    if (rxHead != rxTail)
    {
        const auto item = rxCache[rxHead++];
        out.payload = reinterpret_cast<uint8_t*>(umemArea) + (uint64_t(item.idx) * frameSize);
        out.length = item.len;
        out.index = item.idx;
        outstanding.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    if (refillRxCache() == 0)
        return false;

    const auto item = rxCache[rxHead++];
    out.payload = reinterpret_cast<uint8_t*>(umemArea) + (uint64_t(item.idx) * frameSize);
    out.length = item.len;
    out.index = item.idx;
    outstanding.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void IngressXdp::returnToDevice(uint32_t index)
{
    const uint32_t pos = fqProdShadow;
    fqAddr[pos & fqMask] = frameAddr(index);
    fqProdShadow = pos + 1;

    ++fqSinceCommit;

    uint32_t used = fqProdShadow - *fqConsumer;
    if (fqSinceCommit >= FQ_COMMIT_EVERY || used >= (fqEntries - 1))
    {
        std::atomic_thread_fence(std::memory_order_release);
        *fqProducer = fqProdShadow;
        fqSinceCommit = 0;
        if (needWakeup) xsk_kick(xskFd);
    }

    outstanding.fetch_sub(1, std::memory_order_relaxed);
}

void IngressXdp::onReturnNudge()
{
    if (evtfd >= 0)
    {
        uint64_t one = 1;
        ::write(evtfd, &one, sizeof(one));
    }
}

void IngressXdp::stopRx() {}

void IngressXdp::waitUntilAllFramesReleased()
{
    if (fqSinceCommit)
    {
        std::atomic_thread_fence(std::memory_order_release);
        *fqProducer = fqProdShadow;
        fqSinceCommit = 0;
        if (needWakeup) xsk_kick(xskFd);
    }

    constexpr auto timeout = std::chrono::milliseconds(2000);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (outstanding.load(std::memory_order_acquire) == 0) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

#ifndef NDEBUG
    throw std::runtime_error("Timeout waitinf for AF_XDP frames to be released");
#else
    uint32_t left = outstanding.load(std::memory_order_relaxed);
    std::cerr << "Warning: timeout; " << left << " AF_XDP frames stull outstanding\n";
#endif
}*/
