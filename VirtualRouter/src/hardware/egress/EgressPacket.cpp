// EgressPacket.cpp

#include "EgressPacket.h"
#include <TxQueueOpts.hpp>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <PacketSlot.hpp>
#include <HeaderHelpers.hpp>
#include <iostream>

#ifndef HOT
#define HOT __attribute__((hot))
#endif
#ifndef ALWAYS_INLINE
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#endif

static inline size_t roundUp(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

static inline int ifindex_or_throw(const char* ifname)
{
    int idx = if_nametoindex(ifname);
    if (idx == 0) throw std::runtime_error(std::string("if_nametoindex failed: ") + ifname);
    return idx;
}

static void set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) throw std::runtime_error("fcntl(F_GETFL): " + std::string(std::strerror(errno)));
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        throw std::runtime_error("fcntl(F_SETFL O_NONBLOCK): " + std::string(std::strerror(errno)));
}

EgressPacket::EgressPacket(Interface& iface, const TxQueueOpts& opts)
    : EgressBase(iface, opts)
{
    setupSocket();
    bindIface();
    setupRing();
    mmapRing();
    setupEvents();

    frameCount = req.tp_frame_nr;
    frameSize = req.tp_frame_size;

    state = new std::atomic<uint8_t>[frameCount];
    for (uint32_t i = 0; i < frameCount; ++i) state[i].store(0, std::memory_order_relaxed);

    if (TPACKET2_HDRLEN + packetSize + MTU_PADDING + sizeof(PacketSlot) > req.tp_frame_size)
        throw std::runtime_error("packetSize + padding exceeds tx frame size");

    for (uint32_t i = 0; i < frameCount; ++i)
    {
        uint8_t* base = reinterpret_cast<uint8_t*>(ring) + i * req.tp_frame_size;
        auto* h = reinterpret_cast<tpacket2_hdr*>(base);
        h->tp_status = TP_STATUS_AVAILABLE;

        writeU32(base + TPACKET2_HDRLEN + packetSize + MTU_PADDING, i);
    }

    reclaimCursor = 0;
    initFreeRing(frameCount);
}

EgressPacket::~EgressPacket()
{
    teardownEvents();
    if (ring && ring != MAP_FAILED) ::munmap(ring, ringLen);
    if (fd >= 0) ::close(fd);
    delete[] state; state = nullptr;
}

void EgressPacket::setupSocket()
{
    fd = ::socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd < 0) throw std::runtime_error("sock(AF_PACKET) failed");

    int ver = TPACKET_V2;
    if (::setsockopt(fd, SOL_PACKET, PACKET_VERSION, &ver, sizeof(ver)) != 0)
        throw std::runtime_error("setsockopt PACKET_VERSION v2 failed");

    int one = 1;
    (void)::setsockopt(fd, SOL_PACKET, PACKET_QDISC_BYPASS, &one, sizeof(one));

    set_nonblock(fd);
}

void EgressPacket::setupRing()
{
    uint32_t needPayload = packetSize + MTU_PADDING + sizeof(PacketSlot);
    uint32_t fs = TPACKET2_HDRLEN + std::max(opts.snapLen, needPayload);
    fs = (fs + 15u) & ~15u;
    if (fs < 2048u) fs = 2048u;

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

    return;

    /*uint32_t fs = static_cast<uint32_t>(TPACKET2_HDRLEN + opts.snapLen);
    if (TPACKET2_HDRLEN + packetSize + MTU_PADDING + sizeof(PacketSlot) > req.tp_frame_size);
        throw std::runtime_error("packet size too small");
    if (fs < 2048u) fs = 2048u;

    tpacket_req r{};
    r.tp_frame_size = fs;
    r.tp_frame_nr = (opts.frameCount ? opts.frameCount : 1024);

    const size_t page = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
    size_t blk = opts.blockSize.value_or(1u << 20);
    if (blk % page) blk = (1u << 20);
    if (blk < r.tp_frame_size) blk = roundUp(blk, page);
    if (blk % r.tp_frame_size) blk = roundUp(blk, r.tp_frame_size);
    if (blk % page) blk = roundUp(blk, page);

    uint32_t framesPerBlock = static_cast<uint32_t>(blk / r.tp_frame_size);
    if (framesPerBlock == 0) throw std::runtime_error("Invalid blockSize/frameSize combination");

    uint32_t blocksNeeded = (r.tp_frame_nr + framesPerBlock - 1) / framesPerBlock;
    r.tp_block_size = static_cast<unsigned>(blk);
    r.tp_block_nr = blocksNeeded;

    if (::setsockopt(fd, SOL_PACKET, PACKET_TX_RING, &r, sizeof(r)) != 0)
        throw std::runtime_error("setsockopt(PACKET_TX_RING) failed");

    req = r;
    ringLen = static_cast<size_t>(req.tp_block_size) * req.tp_block_nr;*/
}

void EgressPacket::bindIface()
{
    const int ifidx = ifindex_or_throw(opts.ifname.c_str());

    sockaddr_ll sll{};
    sll.sll_family = AF_PACKET;
    sll.sll_protocol = htons(ETH_P_ALL);
    sll.sll_ifindex = ifidx;

    if (::bind(fd, reinterpret_cast<sockaddr*>(&sll), sizeof(sll)) != 0)
        throw std::runtime_error("bind(AF_PACKET) failed: " + std::string(std::strerror(errno)));

    ifidxCached = ifidx;
    kickAddr = {};
    kickAddr.sll_family = AF_PACKET;
    kickAddr.sll_protocol = htons(ETH_P_ALL);
    kickAddr.sll_ifindex = ifidxCached;
    kickAddr.sll_halen = ETH_ALEN;
    std::memset(kickAddr.sll_addr, 0xff, ETH_ALEN);
}

void EgressPacket::mmapRing()
{
    ring = ::mmap(nullptr, ringLen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ring == MAP_FAILED) throw std::runtime_error("mmap TX ring failed");
    (void)mlock(ring, ringLen);
    (void)madvise(ring, ringLen, MADV_WILLNEED);
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
    if (epfd >= 0 && fd >= 0) ::epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
    if (epfd >= 0) { ::close(epfd); epfd = -1; }
}

ALWAYS_INLINE HOT void EgressPacket::mapFrame(uint32_t index, FrameHandle& out)
{
    out = {};
    if (index >= req.tp_frame_nr) return;

    auto* h = reinterpret_cast<tpacket2_hdr*>(
        reinterpret_cast<uint8_t*>(ring) + size_t(index) * req.tp_frame_size);

    if (h->tp_status != TP_STATUS_AVAILABLE) return;

    state[index].store(1, std::memory_order_relaxed);

    uint8_t* base = reinterpret_cast<uint8_t*>(h);
    out.payload = base + TPACKET2_HDRLEN;
}

ALWAYS_INLINE HOT bool EgressPacket::send(uint32_t index, uint32_t length) noexcept
{
    if (index >= req.tp_frame_nr) return false;

    const uint32_t max_payload =
        req.tp_frame_size - TPACKET2_HDRLEN - MTU_PADDING - static_cast<uint32_t>(sizeof(PacketSlot));

    if (length == 0 || length > max_payload) {
        // Length is invalid for this ring; recycle immediately if we still “own” it.
        auto* h = reinterpret_cast<tpacket2_hdr*>(
            reinterpret_cast<uint8_t*>(ring) + size_t(index) * req.tp_frame_size);
        if (h->tp_status == TP_STATUS_AVAILABLE) {
            state[index].store(0, std::memory_order_relaxed);
            pushFree(index);
        }
        return false;
    }

    auto* h = reinterpret_cast<tpacket2_hdr*>(
            reinterpret_cast<uint8_t*>(ring) + size_t(index) * req.tp_frame_size);

    __u32 expected_status = TP_STATUS_AVAILABLE;
    if (!__atomic_compare_exchange_n(&h->tp_status, &expected_status, TP_STATUS_SEND_REQUEST, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return false;

    h->tp_len = h->tp_snaplen = length;
    std::atomic_thread_fence(std::memory_order_release);
    state[index].store(2, std::memory_order_relaxed);

    const uint32_t pk = pendingKicks.fetch_add(1, std::memory_order_relaxed) + 1;
    if (pk >= kickBatch) kickKernelCached();
    return true;
}

void EgressPacket::onAllocNudge()
{
    reclaim();
}

void EgressPacket::kickKernelCached()
{
    uint32_t current = pendingKicks.exchange(0, std::memory_order_relaxed);
    if (current == 0) return;
    if(::sendto(fd, nullptr, 0, MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&kickAddr), sizeof(kickAddr)) < 0)
    {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            fprintf(stderr, "kick failed: $s\n", strerror(errno));
    }
}

void EgressPacket::flush()
{
    kickKernelCached();
}

void EgressPacket::waitWritable()
{
    if (epfd < 0) return;
    constexpr int MAXE = 4;
    epoll_event ev[MAXE];
    while (true)
    {
        int n = ::epoll_wait(epfd, ev, MAXE, -1);
        if (n < 0)
        {
            if (errno == EINTR) continue;
            return;
        }
        return;
    }
}

void EgressPacket::reclaim()
{
    constexpr uint32_t BUDGET = 4096;
    uint32_t reclaimed = 0, scanned = 0;

    while (scanned < BUDGET && reclaimed < 32)
    {
        uint32_t idx = reclaimCursor;
        reclaimCursor = (reclaimCursor + 1) % frameCount;
        ++scanned;

        auto* h = reinterpret_cast<tpacket2_hdr*>(
            reinterpret_cast<uint8_t*>(ring) + size_t(idx) * req.tp_frame_size);

        uint8_t current_status = __atomic_load_n(&h->tp_status, __ATOMIC_ACQUIRE);

        if (current_status == TP_STATUS_AVAILABLE)
        {
            uint8_t expected_state = 2;
            if (state[idx].compare_exchange_strong(expected_state, 0, std::memory_order_acq_rel))
            {
                pushFree(idx);
                ++reclaimed;
            }
        }
    }

    if (reclaimed > 0) return;

    kickKernelCached();
    struct timespec ts = {0, 10000};
    nanosleep(&ts, nullptr);

    for (uint32_t i = 0; i < frameCount; ++i)
    {
        uint32_t idx = (reclaimCursor + 1) % frameCount;

        auto* h = reinterpret_cast<tpacket2_hdr*>(reinterpret_cast<uint8_t*>(ring) + size_t(idx) * req.tp_frame_size);

        __u32 current_status = __atomic_load_n(&h->tp_status, __ATOMIC_ACQUIRE);

        if (current_status == TP_STATUS_AVAILABLE)
        {
            uint8_t expected_state = 2;
            if (state[idx].compare_exchange_strong(expected_state, 0, std::memory_order_acq_rel))
            {
                pushFree(idx);
                reclaimed++;
            }
        }
    }

    reclaimCursor = (reclaimCursor + frameCount) % frameCount;
}

ALWAYS_INLINE HOT void EgressPacket::cancel(uint32_t index)
{
    if (index >= req.tp_frame_nr) return;

    auto* h = reinterpret_cast<tpacket2_hdr*>(
        reinterpret_cast<uint8_t*>(ring) + size_t(index) * req.tp_frame_size);

    if (h->tp_status == TP_STATUS_AVAILABLE)
    {
        state[index].store(0, std::memory_order_relaxed);
        pushFree(index);
    }
}
