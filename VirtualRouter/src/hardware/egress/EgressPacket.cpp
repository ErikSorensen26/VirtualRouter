// EgressPacket.cpp

#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <iostream>

#include "EgressPacket.h"
#include "qos/egress/TxQueueOpts.hpp"
#include "hardware/PacketSlot.hpp"
#include "hardware/Ifname.h"

#ifndef HOT
#define HOT __attribute__((hot))
#endif
#ifndef ALWAYS_INLINE
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#endif

namespace hardware::egress
{

static inline size_t roundUp(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

static void set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) throw std::runtime_error("fcntl(F_GETFL): " + std::string(std::strerror(errno)));
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        throw std::runtime_error("fcntl(F_SETFL O_NONBLOCK): " + std::string(std::strerror(errno)));
}

EgressPacket::EgressPacket(interface::Interface& iface, const qos::egress::TxQueueOpts& opts)
    : EgressBase(iface, opts), fd(-1), epfd(-1), ring(nullptr)
{
    setupSocket();
    setupRing();
    mmapRing();
    bindIface();
    setupEvents();

    frameBase = reinterpret_cast<uint8_t*>(ring);
    maxPayload = req.tp_frame_size - TPACKET2_HDRLEN - MTU_PADDING
               - static_cast<uint32_t>(alignof(PacketSlot) - 1)
               - static_cast<uint32_t>(sizeof(PacketSlot));

    frameCount = req.tp_frame_nr;

    if (TPACKET2_HDRLEN + packetSize + MTU_PADDING + (alignof(PacketSlot) - 1) + sizeof(PacketSlot) > req.tp_frame_size)
        throw std::runtime_error("packetSize + padding exceeds tx frame size");

    for (uint32_t i = 0; i < frameCount; ++i)
    {
        uint8_t* base = reinterpret_cast<uint8_t*>(ring) + i * req.tp_frame_size;
        auto* h = reinterpret_cast<tpacket2_hdr*>(base);
        // Use release store so the kernel sees a consistent initial state.
        __atomic_store_n(&h->tp_status, TP_STATUS_AVAILABLE, __ATOMIC_RELEASE);
    }

    reclaimCursor = 0;
    initFreeRing(frameCount);
}

EgressPacket::~EgressPacket()
{
    teardownEvents();
    if (fd >= 0)
    {
        tpacket_req zero{};
        ::setsockopt(fd, SOL_PACKET, PACKET_TX_RING, &zero, sizeof(zero));
    }
    if (ring && ring != MAP_FAILED)
    {
        ::munmap(ring, ringLen);
    }
    if (fd >= 0) 
    {
        ::close(fd);
        fd = -1;
    }

}

void EgressPacket::setupSocket()
{
    fd = ::socket(AF_PACKET, SOCK_RAW, 0);
    if (fd < 0)
        throw std::runtime_error("sock(AF_PACKET) failed");

    int ver = TPACKET_V2;
    if (::setsockopt(fd, SOL_PACKET, PACKET_VERSION, &ver, sizeof(ver)) != 0)
        throw std::runtime_error("setsockopt PACKET_VERSION v2 failed");

    int one = 1;
    (void)::setsockopt(fd, SOL_PACKET, PACKET_QDISC_BYPASS, &one, sizeof(one));
    (void)::setsockopt(fd, SOL_PACKET, PACKET_TX_HAS_OFF, &one, sizeof(one));

    set_nonblock(fd);

    // Set close-on-exec so children don't inherit the TX ring FD
    int fdfl = fcntl(fd, F_GETFD);
    if (fdfl >= 0) fcntl(fd, F_SETFD, fdfl | FD_CLOEXEC);
}

void EgressPacket::setupRing()
{
    uint32_t needPayload = packetSize + MTU_PADDING + (alignof(PacketSlot) - 1) + sizeof(PacketSlot);
    uint32_t fs = TPACKET2_HDRLEN + std::max(opts.snapLen, needPayload);
    fs = (fs + 15u) & ~15u;
    if (fs < 2048) fs = 2048;

    tpacket_req r{};
    r.tp_frame_size = fs;
    r.tp_frame_nr = opts.frameCount ? opts.frameCount : 1024;

    const size_t page = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
    size_t blk = opts.blockSize.value_or(1u << 20);

    if (blk < r.tp_frame_size) blk = r.tp_frame_size;
    blk = roundUp(blk, page);
    blk = roundUp(blk, r.tp_frame_size);

    r.tp_block_size = static_cast<unsigned>(blk);
    uint32_t framesPerBlock = r.tp_block_size / r.tp_frame_size;
    if (framesPerBlock == 0) throw std::runtime_error("Invalid block/frame combo");

    r.tp_block_nr = (r.tp_frame_nr + framesPerBlock - 1) / framesPerBlock;
    r.tp_frame_nr = r.tp_block_nr * framesPerBlock;

    if (::setsockopt(fd, SOL_PACKET, PACKET_TX_RING, &r, sizeof(r)) != 0)
        throw std::runtime_error("setsockopt(PACKET_TX_RING) " + std::string(std::strerror(errno)));

    req = r;
    ringLen = size_t(req.tp_block_size) * req.tp_block_nr;
}

void EgressPacket::bindIface()
{
    const int ifidx = ifnametoindex(opts.ifname.c_str());
    if (ifidx <= 0)
        throw std::runtime_error("ifnametoindex failed for " + opts.ifname);

    sockaddr_ll sll{};
    sll.sll_family = AF_PACKET;
    sll.sll_protocol = htons(ETH_P_ALL);
    sll.sll_ifindex = ifidx;

    if (::bind(fd, reinterpret_cast<sockaddr*>(&sll), sizeof(sll)) != 0)
        throw std::runtime_error("bind(AF_PACKET) failed: " + std::string(std::strerror(errno)));

}

void EgressPacket::mmapRing()
{
    ring = ::mmap(nullptr, ringLen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ring == MAP_FAILED) throw std::runtime_error("mmap TX ring failed");
    if (mlock(ring, ringLen) != 0)
        std::cerr << "Warning: mlock failed: " << std::strerror(errno) << std::endl;
    if (madvise(ring, ringLen, MADV_WILLNEED) != 0)
        std::cerr << "Warning: madvise failed: " << std::strerror(errno) << std::endl;
}

void EgressPacket::setupEvents()
{
    epfd = ::epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) throw std::runtime_error("epoll_create1 failed");
    epoll_event ev{};
    ev.events = EPOLLOUT | EPOLLERR;
    ev.data.fd = fd;
    if (::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) != 0)
        throw std::runtime_error("epoll_ctl ADD failed");
}

void EgressPacket::teardownEvents()
{
    if (epfd >= 0)
    {
        if (fd >= 0)
            ::epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        ::close(epfd);
        epfd = -1;
    }
}

ALWAYS_INLINE HOT void EgressPacket::mapFrame(uint32_t index, FrameHandle& out)
{
    out = {};
    if (index >= req.tp_frame_nr) return;

    uint8_t* base = frameBase + size_t(index) * req.tp_frame_size;
    auto* h = reinterpret_cast<tpacket2_hdr*>(base);

    // Acquire load: must synchronise with the kernel's release-store when it
    // marks a frame TP_STATUS_AVAILABLE after transmission.
    if (__atomic_load_n(&h->tp_status, __ATOMIC_ACQUIRE) != TP_STATUS_AVAILABLE)
        return;

    // Mark allocated — plain store ordered after the acquire above.
    // tp_padding[0] is our user-space state byte; it shares the cache line
    // with tp_status so no extra fetch is needed.
    h->tp_padding[0] = 1;
    out.payload = base + TPACKET2_HDRLEN;
}

ALWAYS_INLINE HOT bool EgressPacket::send(uint32_t index, uint32_t length) noexcept
{
    if (index >= req.tp_frame_nr) return false;

    if (length == 0 || length > maxPayload)
    {
        cancel(index);
        return false;
    }

    auto* h = reinterpret_cast<tpacket2_hdr*>(
            reinterpret_cast<uint8_t*>(ring) + size_t(index) * req.tp_frame_size);

    // tp_status is guaranteed AVAILABLE here — mapFrame() verified it and the
    // kernel cannot change it until we store TP_STATUS_SEND_REQUEST below.
    h->tp_len     = length;
    h->tp_snaplen = length;
    h->tp_mac     = TPACKET2_HDRLEN;
    // Mark submitted before the release-store; the release on tp_status
    // ensures tp_padding[0] is visible to any thread that then acquires it.
    h->tp_padding[0] = 2;
    __atomic_store_n(&h->tp_status, TP_STATUS_SEND_REQUEST, __ATOMIC_RELEASE);

    // Accumulate pending kicks; flush() (called by BaseQueue after draining the
    // batch) will do a single sendto(MSG_DONTWAIT) to hand them all to the kernel.
    pendingKicks.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void EgressPacket::onAllocNudge()
{
    // Quick scan: check a small window around the cursor.  If that's not
    // enough, getFrame will fall through to waitWritable which does a full scan.
    reclaimImpl(64);
}

void EgressPacket::kickKernelCached()
{
    uint32_t current = pendingKicks.exchange(0, std::memory_order_relaxed);
    if (__builtin_expect(current == 0, 0)) return;

    ssize_t ret = ::sendto(fd, nullptr, 0, MSG_DONTWAIT, nullptr, 0);

    if (ret < 0)
    {
        int err = errno;
        if (err != EAGAIN && err != EWOULDBLOCK && err != EINTR)
            fprintf(stderr, "kick failed: %s\n", strerror(errno));
    }
}

void EgressPacket::flush()
{
    kickKernelCached();
}

void EgressPacket::waitWritable()
{
    kickKernelCached();
    epoll_event ev[1];
    ::epoll_wait(epfd, ev, 1, 1);
    reclaim();
}

void EgressPacket::reclaim()
{
    reclaimImpl(0); // full scan
}

void EgressPacket::reclaimImpl(uint32_t limit)
{
    uint32_t idx = reclaimCursor;
    uint32_t count = frameCount;
    uint32_t found = 0;


    for (uint32_t i = 0; i < count; ++i)
    {
        auto* h = reinterpret_cast<tpacket2_hdr*>(frameBase + size_t(idx) * req.tp_frame_size);

        if (__atomic_load_n(&h->tp_status, __ATOMIC_ACQUIRE) == TP_STATUS_AVAILABLE)
        {
            // Plain load/store: ordering is provided by the acquire on tp_status above.
            // cancel() only operates on state==1; reclaimImpl only on state==2 — no race.
            if (h->tp_padding[0] == 2)
            {
                h->tp_padding[0] = 0;
                pushFree(idx);
                ++found;
                if (limit != 0 && found >= limit)
                {
                    if (++idx == count) idx = 0;
                    reclaimCursor = idx;
                    return;
                }
            }
        }

        if (++idx == count) idx = 0;
    }

    reclaimCursor = idx;
}

ALWAYS_INLINE HOT void EgressPacket::cancel(uint32_t index)
{
    if (__builtin_expect(index >= req.tp_frame_nr, 0)) return;

    auto* h = reinterpret_cast<tpacket2_hdr*>(
        reinterpret_cast<uint8_t*>(ring) + size_t(index) * req.tp_frame_size);

    if (h->tp_padding[0] != 0)
    {
        h->tp_padding[0] = 0;
        __atomic_store_n(&h->tp_status, TP_STATUS_AVAILABLE, __ATOMIC_RELEASE);
        pushFree(index);
    }
}

} // namespace hardware
