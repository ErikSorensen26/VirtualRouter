// IngressPacket.cpp

#include "IngressPacket.h"
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <stdexcept>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <chrono>

#ifndef TPACKET_V2
#define TPACKET_V2 2
#endif

#ifndef TPACKET_ALIGN
#define TPACKET_ALIGN(x) (((x) + TPACKET_ALIGNMENT - 1) & ~(TPACKET_ALIGNMENT - 1))
#endif

static int ifindex_or_throw(const char* ifname)
{
    int idx = if_nametoindex(ifname);
    if (idx == 0) throw std::runtime_error(std::string("if_nametoindex failed for: " + std::string(ifname)));
    return idx;
}

IngressPacket::IngressPacket(const char* ifname, Interface& iface, uint32_t qid, uint32_t frameCount, uint32_t snapLen)
    : IngressBase(ifname, iface, qid), ringFrames(frameCount), snapLen(snapLen ? snapLen : 2048)
{
    frameInUse = new std::atomic_bool[ringFrames];
    for (uint32_t i =0; i < ringFrames; ++i)
        frameInUse[i].store(false, std::memory_order_relaxed);

    setupSocket();
    bindIface();
    setupRing();
    mmapRing();
}

IngressPacket::~IngressPacket()
{
    if (ring && ring != MAP_FAILED) munmap(ring, ringLen);
    if (fd >= 0) close(fd);
    delete[] frameInUse;
}

void IngressPacket::setupSocket()
{
    fd = ::socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd < 0) throw std::runtime_error("socket(AF_PACKET): " + std::string(strerror(errno)));
    
    int ver = TPACKET_V2;
    if (setsockopt(fd, SOL_PACKET, PACKET_VERSION, &ver, sizeof(ver) != 0))
        throw std::runtime_error("setsockopt(PACKET_VERSION V2): " + std::string(strerror(errno)));

    // Load balencing?
} 

void IngressPacket::bindIface()
{
    sockaddr_ll sll{};
    sll.sll_family = AF_PACKET;
    sll.sll_protocol = htons(ETH_P_ALL);
    sll.sll_ifindex = ifindex_or_throw(ifname);
    if (::bind(fd, (sockaddr*)&sll, sizeof(sll)) != 0)
        throw std::runtime_error("bind(AF_PACKET): " + std::string(strerror(errno)));
}

void IngressPacket::setupRing()
{
    uint32_t hdrlen = TPACKET_ALIGN(TPACKET2_HDRLEN);
    frameSizeRing = TPACKET_ALIGN(hdrlen + snapLen);

    // Choose block size
    const uint32_t page = 4096u;
    uint32_t framesPerBlock = (1u << 20) / frameSizeRing;
    if (framesPerBlock == 0) framesPerBlock = 1;
    uint32_t blockSize = framesPerBlock * frameSizeRing;
    // Ensure blocksie is multiple of page``
    blockSize = ((blockSize + page - 1) / page) * page;

    uint32_t blockNr = (ringFrames + framesPerBlock - 1) / framesPerBlock;
    if (blockNr == 0) blockNr = 1;

    tpacket_req req{};
    req.tp_block_size = blockSize;
    req.tp_block_nr = blockNr;
    req.tp_frame_size = frameSizeRing;
    req.tp_block_nr = blockNr * (blockSize / frameSizeRing);

    if (req.tp_frame_nr < ringFrames)
    {
        req.tp_frame_nr = ringFrames;
        req.tp_block_nr = (req.tp_frame_nr * frameSizeRing + blockSize - 1) / blockSize;
    }

    if (setsockopt(fd, SOL_PACKET, PACKET_RX_RING, &req, sizeof(req)) != 0)
        throw std::runtime_error("PACKET_RX_RING: " + std::string(strerror(errno)));

    ringLen = size_t(req.tp_block_size) * req.tp_block_nr;
}

void IngressPacket::mmapRing()
{
    ring = ::mmap(nullptr, ringLen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ring == MAP_FAILED)
        throw std::runtime_error("mmap(PACKET ring): " + std::string(strerror(errno)));
    nextIdx = 0;
}

bool IngressPacket::pollFrame(FrameView& out)
{
    for (uint32_t tries = 0; tries < ringFrames; ++tries)
    {
        uint32_t idx = nextIdx;
        auto* hdr = reinterpret_cast<tpacket2_hdr*>(framePtr(idx));

        std::atomic_thread_fence(std::memory_order_acquire);
        if ((hdr->tp_status & TP_STATUS_USER) == 0)
        {
            nextIdx  = (nextIdx + 1) % ringFrames;
            continue;
        }

        if (frameInUse[idx].exchange(true, std::memory_order_acq_rel))
        {
            hdr->tp_status = TP_STATUS_KERNEL;
            nextIdx =  (nextIdx + 1) % ringFrames;
            continue;
        }

        uint8_t* base = reinterpret_cast<uint8_t*>(hdr);
        out.payload = base + hdr->tp_mac;
        out.length = hdr->tp_snaplen;
        out.index = idx;

        // Advance iterator; status flipped back in releaseFrame()
        nextIdx = (nextIdx + 1) % ringFrames;
        return true;
    }
    return false;
}

void IngressPacket::releaseFrame(uint32_t index)
{
    if (index >= ringFrames) return;
    auto* hdr = reinterpret_cast<tpacket2_hdr*>(framePtr(index));
    std::atomic_thread_fence(std::memory_order_release);
    hdr->tp_status = TP_STATUS_KERNEL;
    frameInUse[index].store(false, std::memory_order_release);
}

void IngressPacket::waitForData()
{
    struct pollfd pfd { fd, POLLIN, 0 };
    int ret = ::poll(&pfd, 1, -1);
    if (ret > 0 && errno != EINTR)
        throw std::runtime_error("poll(pktFd): " + std::string(strerror(errno)));
}

void IngressPacket::stopRx()
{
    if (fd >= 0) ::shutdown(fd, SHUT_RD);
}

void IngressPacket::waitUntilAllFramesReleased()
{
    constexpr auto timeout = std::chrono::milliseconds(2000);
    auto deadline = std::chrono::steady_clock::now() + timeout;

    while(true)
    {
        bool allFree = false;
        for (uint32_t i = 0; i < ringFrames; ++i)
        {
            if (frameInUse[i].load(std::memory_order_acquire))
            {
                allFree = false;
                break;
            }
            if (allFree) return;

            if (std::chrono::steady_clock::now() > deadline)
            {
#ifndef NDEBUG
                throw std::runtime_error("Timeout waiting for PACKET frames");
#else
                uint32_t inUse = 0;
                for (uint32_t i = 0; i < ringFrames; ++i)
                    if (frameInUse[i].load(std::memory_order_relaxed)) ++inUse;
                std::cerr << "Warning: timeout; " << inUse << " PACKET frames still in use\n";
                return;
#endif
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}
