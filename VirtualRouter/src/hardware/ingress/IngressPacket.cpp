// IngressPacket.cpp

#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <chrono>

#include "IngressPacket.h"
#include "hardware/Ifname.h"

#ifndef HOT
#define HOT __attribute__((hot))
#endif
#ifndef ALWAYS_INLINE
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#endif

namespace hardware::ingress
{

static ALWAYS_INLINE bool blockReadyV3(tpacket_block_desc* bd)
{
    return (__atomic_load_n(&bd->hdr.bh1.block_status, __ATOMIC_ACQUIRE) & TP_STATUS_USER) != 0;
}

static ALWAYS_INLINE void blockReleaseV3(tpacket_block_desc* bd)
{
    __atomic_store_n(&bd->hdr.bh1.block_status, TP_STATUS_KERNEL, __ATOMIC_RELEASE);
}

static inline void set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) throw std::runtime_error("fcntl(F_GETFL): " + std::string(std::strerror(errno)));
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        throw std::runtime_error("fcntl(F_SETFL O_NONBLOCK): " + std::string(std::strerror(errno)));
}

IngressPacket::IngressPacket(interface::Interface& iface, const qos::ingress::RxQueueOpts& opts)
    : IngressBase(iface, opts)
{
    setupSocket();
    setupRing();
    mmapRing();
    bindIface();
    setupEvents();
}

IngressPacket::~IngressPacket()
{
    teardownEvents();
    if (ring && ring != MAP_FAILED) munmap(ring, ringLen);
    if (fd >= 0) close(fd);
    delete[] blockNextOff;
    delete[] blockRemain;
    delete[] blkInFlight;
}

void IngressPacket::setupSocket()
{
    fd = ::socket(AF_PACKET, SOCK_RAW, 0);
    if (fd < 0) throw std::runtime_error("socket(AF_PACKET) failed");
    int ver = TPACKET_V3;
    if (setsockopt(fd, SOL_PACKET, PACKET_VERSION, &ver, sizeof(ver)) != 0)
        throw std::runtime_error("setsockopt PACKET_VERSION v3 failed");
    set_nonblock(fd);
}

void IngressPacket::bindIface()
{
    const unsigned int idx = ifnametoindex(opts.ifname.c_str());
    if (idx == 0)
    {
        throw std::runtime_error(
            "ifnametoindex failed for interface '" + opts.ifname + "': " +
            std::string(std::strerror(errno)));
    }

    sockaddr_ll sll{};
    sll.sll_family = AF_PACKET;
    sll.sll_protocol = htons(ETH_P_ALL);
    sll.sll_ifindex = static_cast<int>(idx);

    if (bind(fd, reinterpret_cast<sockaddr*>(&sll), sizeof(sll)) != 0)
    {
        throw std::runtime_error(
            "bind() to interface '" + opts.ifname + "' failed: " +
            std::string(std::strerror(errno)));
    }

    int one = 1;
    (void)setsockopt(fd, SOL_PACKET, PACKET_IGNORE_OUTGOING, &one, sizeof(one));

    if (opts.fanoutGroup > 0)
    {
        int arg = (opts.fanoutGroup & 0xFFFF) | (opts.fanoutMode << 16);
        if (setsockopt(fd, SOL_PACKET, PACKET_FANOUT, &arg, sizeof(arg)) != 0)
            throw std::runtime_error("PACKET_FANOUT failed: " + std::string(std::strerror(errno)));
    }
}

void IngressPacket::setupRing()
{
    const uint32_t hdrlen = TPACKET_ALIGN(TPACKET3_HDRLEN);
    frameSizeRing = TPACKET_ALIGN(hdrlen + opts.snapLen);
    if (frameSizeRing < hdrlen) throw std::runtime_error("bad framesize");

    const uint32_t page = static_cast<uint32_t>(::sysconf(_SC_PAGESIZE));
    auto gcd = [](uint32_t a, uint32_t b) { while (b) { uint32_t t = a % b; a = b; b = t; } return a ? a : 1u; };
    auto lcm = [&](uint32_t a, uint32_t b) { return (a / gcd(a, b)) * b; };
    const uint32_t base = lcm(page, frameSizeRing);

    blockSize = ((1u << 20) + base - 1) / base * base;
    framesPerBlock = blockSize / frameSizeRing;
    if (!framesPerBlock) throw std::runtime_error("framesPerBlock is zero");

    auto next_pow2 = [](uint32_t x) { --x; x |= x >> 1; x |= x >> 2; x |= x >> 4; x |= x >> 8; x |= x >> 16; return ++x; };
    blockNr = next_pow2((opts.frameCount + framesPerBlock - 1) / framesPerBlock);
    blockMask = blockNr - 1;

    delete[] blockNextOff;
    delete[] blockRemain;
    blockNextOff = new uint32_t[blockNr]();
    blockRemain = new uint16_t[blockNr]();

    delete[] blkInFlight;
    blkInFlight = new std::atomic<uint32_t>[blockNr];
    for (uint32_t i = 0; i < blockNr; ++i)
        blkInFlight[i] = 0;

    tpacket_req3 req{};
    req.tp_block_size = blockSize;
    req.tp_block_nr = blockNr;
    req.tp_frame_size = frameSizeRing;
    req.tp_frame_nr = blockNr * framesPerBlock;
    req.tp_retire_blk_tov = opts.retireMs ? opts.retireMs : 1;
    req.tp_feature_req_word = 0;

    if (setsockopt(fd, SOL_PACKET, PACKET_RX_RING, &req, sizeof(req)) != 0)
        throw std::runtime_error("PACKET_RX_RING(V3): " + std::string(strerror(errno)));

    ringLen = static_cast<size_t>(req.tp_block_size) * req.tp_block_nr;
    opts.frameCountActual = req.tp_frame_nr;
    nextIdx = 0;
}

void IngressPacket::mmapRing()
{
    ring = ::mmap(nullptr, ringLen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ring == MAP_FAILED) throw std::runtime_error("mmap ring failed");
    (void)mlock(ring, ringLen);
    (void)madvise(ring, ringLen, MADV_WILLNEED);
}

void IngressPacket::setupEvents()
{
    epfd = ::epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) throw std::runtime_error("epoll_create1 failed");
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLERR;
    ev.data.fd = fd;
    if (::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) != 0)
        throw std::runtime_error("epoll_ctl ADD failed");
}

void IngressPacket::teardownEvents()
{
    if (epfd >= 0 && fd >= 0) epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
    if (epfd >= 0) { ::close (epfd); epfd = -1; }
}

void IngressPacket::waitEvent()
{
    constexpr int MAXE = 4;
    epoll_event ev[MAXE];
    for (;;)
    {
        int n = ::epoll_wait(epfd, ev, MAXE, -1);
        if (n < 0)
        {
            if (errno == EINTR) continue;
            throw std::runtime_error("epoll_wait failed");
        }
        flushReturns();
        return;
    }
}

ALWAYS_INLINE HOT bool IngressPacket::pollFrame(FrameView& out)
{
    if (!ring || blockNr == 0) return false;

    const uint32_t start = nextIdx;

    for (uint32_t i = 0; i < blockNr; ++i)
    {
        uint32_t b = (start + i) & blockMask;

        uint8_t* blk = static_cast<uint8_t*>(ring) + static_cast<size_t>(b) * blockSize;
        auto* bd = reinterpret_cast<tpacket_block_desc*>(blk);

        if (!blockReadyV3(bd))
            continue;

        uint16_t& remain = blockRemain[b];
        uint32_t& off = blockNextOff[b];

        if (remain == 0)
        {
            if (blkInFlight[b].load(std::memory_order_acquire) != 0)
                continue;

            remain = bd->hdr.bh1.num_pkts;
            off = bd->hdr.bh1.offset_to_first_pkt;
            blkInFlight[b].store(remain, std::memory_order_relaxed);
        }

        auto* tph = reinterpret_cast<tpacket3_hdr*>(blk + off);

        out.payload = reinterpret_cast<uint8_t*>(tph) + tph->tp_mac;
        out.length = tph->tp_snaplen;
        out.index = (b << 16) | ((bd->hdr.bh1.num_pkts - remain) & 0xFFFFu);

        off += tph->tp_next_offset;

        if (--remain == 0)
        {
            if (blkInFlight[b].load(std::memory_order_acquire) == 0)
            {
                blockReleaseV3(bd);
            }
            nextIdx = (b + 1) & blockMask;
        }
        return true;
    }
    return false;
}

ALWAYS_INLINE HOT void IngressPacket::returnToDevice(uint32_t index)
{
    uint32_t b = index >> 16;

    uint8_t* blk = (uint8_t*)ring + (size_t)b * blockSize;
    auto* bd = (tpacket_block_desc*)blk;

    uint32_t prev = blkInFlight[b].fetch_sub(1, std::memory_order_acq_rel);
    if (prev == 1)
    {
        blockReleaseV3(bd);
    }
}

void IngressPacket::stopRx()
{
    if (fd >= 0) ::shutdown(fd, SHUT_RD);
}

void IngressPacket::waitUntilAllFramesReleased()
{
    using namespace std::chrono_literals;

    if (!ring || blockNr == 0)
        return;

    const auto deadline = std::chrono::steady_clock::now() + 2s;

    while (true)
    {
        bool all = true;
        for (uint32_t b = 0; b < blockNr; ++b)
        {
            uint8_t* blk = (uint8_t*)ring + (size_t)b * blockSize;
            auto* bd = (tpacket_block_desc*)blk;

            const bool user  = blockReadyV3(bd);
            const bool empty = (blockRemain[b] == 0) &&
                               (blkInFlight[b].load(std::memory_order_acquire) == 0);

            if (user && empty)
                blockReleaseV3(bd);

            if (blockReadyV3(bd))
            {
                all = false;
                break;
            }
        }
        if (all) return;
        if (std::chrono::steady_clock::now() > deadline)
        {
            std::cerr << "Warning: timeout waited for PACKET_V3 blocks to free\n";
            return;
        }
        std::this_thread::sleep_for(1ms);
    }
}

} // namespace hardware
