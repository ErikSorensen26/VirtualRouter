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
#include <Ifname.h>

#ifndef HOT
#define HOT __attribute__((hot))
#endif
#ifndef ALWAYS_INLINE
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#endif

static inline size_t roundUp(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

static void set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) throw std::runtime_error("fcntl(F_GETFL): " + std::string(std::strerror(errno)));
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        throw std::runtime_error("fcntl(F_SETFL O_NONBLOCK): " + std::string(std::strerror(errno)));
}

void EgressPacket::dumpRing()
{
    for (uint32_t i = 0; i < frameCount; ++i)
    {
        auto* h = reinterpret_cast<tpacket2_hdr*>(frameBase + size_t(i) * req.tp_frame_size);
        uint32_t st = __atomic_load_n(&h->tp_status, __ATOMIC_ACQUIRE);
        uint8_t st_user = state[i].load(std::memory_order_relaxed);
        if (st == TP_STATUS_SEND_REQUEST || st_user != 0)
            std::fprintf(stderr,
                 "TX slot %u: tp_status=%u state=%u len=%u mac=%u net=%u\n",
                 i, st, st_user, h->tp_len, h->tp_mac, h->tp_net);
    }
}

EgressPacket::EgressPacket(Interface& iface, const TxQueueOpts& opts)
    : EgressBase(iface, opts), fd(-1), epfd(-1), ring(nullptr), kickBatch(16)
{
    setupSocket();
    setupRing();
    mmapRing();
    bindIface();
    setupEvents();

    frameBase = reinterpret_cast<uint8_t*>(ring);
    frameCountCached = req.tp_frame_nr;
    frameSizeCached = req.tp_frame_size;
    maxPayload = req.tp_frame_size - TPACKET2_HDRLEN - MTU_PADDING - static_cast<uint32_t>(sizeof(PacketSlot));

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

    delete[] state;
    state = nullptr;
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
    
    if (h->tp_status != TP_STATUS_AVAILABLE)
        return;
    
    state[index].store(1, std::memory_order_release);
    out.payload = base + TPACKET2_HDRLEN;
}

ALWAYS_INLINE HOT bool EgressPacket::send(uint32_t index, uint32_t length) noexcept
{
    if (index >= req.tp_frame_nr) return false;

    if (length == 0 || length > maxPayload)
    {
        auto* h = reinterpret_cast<tpacket2_hdr*>(reinterpret_cast<uint8_t*>(ring) + size_t(index) * req.tp_frame_size);
        uint8_t expected = state[index].load(std::memory_order_relaxed);
        if (expected != 0)
        {
            state[index].store(0, std::memory_order_relaxed);
            h->tp_status = TP_STATUS_AVAILABLE;
            pushFree(index);
        }
        return false;
    }

    auto* h = reinterpret_cast<tpacket2_hdr*>(
            reinterpret_cast<uint8_t*>(ring) + size_t(index) * req.tp_frame_size);

    if (__atomic_load_n(&h->tp_status, __ATOMIC_ACQUIRE) != TP_STATUS_AVAILABLE)
        return false;

    h->tp_len = length;
    h->tp_snaplen = length;
    h->tp_mac = TPACKET2_HDRLEN;
    __atomic_thread_fence(__ATOMIC_RELEASE);

    __atomic_store_n(&h->tp_status, TP_STATUS_SEND_REQUEST, __ATOMIC_RELEASE);

    state[index].store(2, std::memory_order_relaxed);

    uint32_t pkt =  pendingKicks.fetch_add(1, std::memory_order_relaxed);
    if (/*pkt >= kickBatch*/true)
    {
        kickKernelCached();
    }
    else
    {
        pendingKicks.store(pkt, std::memory_order_relaxed);
    }
    return true;
}

void EgressPacket::onAllocNudge()
{
    reclaim();
}

void EgressPacket::kickKernelCached()
{
    uint32_t current = pendingKicks.exchange(0, std::memory_order_relaxed);
    if (__builtin_expect(current == 0, 1)) return;

    //dumpRing();
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
    usleep(100);
}

void EgressPacket::reclaim()
{
    uint32_t budget = std::min<uint32_t>(frameCount, 4096);
    uint32_t reclaimed = 0;

    for (uint32_t n = 0; n < budget; ++n)
    {
        uint32_t idx = reclaimCursor;
        reclaimCursor = (reclaimCursor + 1) % frameCount;
        auto* h = reinterpret_cast<tpacket2_hdr*>(frameBase + size_t(idx) * req.tp_frame_size);
        uint8_t status = __atomic_load_n(&h->tp_status, __ATOMIC_ACQUIRE);

        if (status == TP_STATUS_AVAILABLE)
        {
            uint8_t expected_state = 2;
            if (state[idx].compare_exchange_strong(expected_state, 0, std::memory_order_acq_rel))
            {
                pushFree(idx);
            }
        }
    }

    if (reclaimed == 0) std::this_thread::sleep_for(std::chrono::microseconds(100));
    
    uint32_t idx = reclaimCursor;
    for (uint32_t i = 0; i < frameCount; ++i)
    {
        auto* h = reinterpret_cast<tpacket2_hdr*>(frameBase + size_t(idx) * req.tp_frame_size);
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
        idx = (idx + 1) % frameCount;
    }

    reclaimCursor = idx;
}

ALWAYS_INLINE HOT void EgressPacket::cancel(uint32_t index)
{
    if (__builtin_expect(index >= req.tp_frame_nr, 0)) return;

    auto* h = reinterpret_cast<tpacket2_hdr*>(
        reinterpret_cast<uint8_t*>(ring) + size_t(index) * req.tp_frame_size);

    uint8_t expected = state[index].load(std::memory_order_acquire);
    if (expected != 0)
    {
        state[index].store(0, std::memory_order_relaxed);
        h->tp_status = TP_STATUS_AVAILABLE;
        pushFree(index);
    }
}
