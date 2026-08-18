/**
 * @file Egress.hpp
 * @brief AF_XDP zero-copy TX backend.
 * @ingroup HARDWARE_EGRESS
 */

#ifndef EGRESS_HPP
#define EGRESS_HPP

#include <linux/if_xdp.h>
#include <linux/if_link.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <atomic>
#include <cstring>
#include <string>
#include <errno.h>
#include <stdexcept>

#include "hardware/PacketSlot.hpp"
#include "hardware/Ifname.h"

namespace hardware::egress
{
/**
 * @brief AF_XDP TX backend: frames carved from a umem area, sent through the kernel TX ring.
 *
 * Allocates a umem area split into fixed-size frames, tracks free frames on a
 * lock-free Treiber stack, and hands completed frames back once the hardware
 * has transmitted them.
 *
 * Single-producer, single-consumer: one thread owns the getFrame()/releaseFrame()
 * pairing and one owns the send()/reclaim() cycle. The free list and TX ring are
 * synchronized with acquire/release fences; reclaim() is what returns a frame to
 * the free list after the consumer index catches up with it.
 *
 * @warning frameCount must be a power of two and frameSize must be page-aligned
 * (ring masking and mmap layout depend on both); send() fails rather than
 * blocking when the ring is full, and both send() and getFrame() reject indices
 * outside the frameCount range.
 */
class Egress
{
public:
    Egress(const char* ifname, uint32_t qid = 0,
           uint32_t frameCount = 1024, uint32_t frameSize = 2048)
      : frameCount(frameCount),
        frameMask(frameCount - 1),
        frameSize(frameSize),
        packetSize(frameSize - sizeof(PacketSlot)),
        qid(qid),
        umemSize(uint64_t(frameCount) * frameSize)
    {
        return;
        static_assert(sizeof(uint32_t) == 4, "Expected 32-bit integers");

        txInFlight.resize(frameCount);
        txInFlightHead = 0;
        txInFlightTail = 0;

        if ((frameSize * frameCount) % 4096 != 0)
            throw std::runtime_error("frameSize must be page-aligned");

        if ((frameCount & (frameCount - 1)) != 0)
            throw std::runtime_error("frameCount must be a power of 2 for ring optimization");

        // Allocates umem area
        if (posix_memalign(&umemArea, 4096, umemSize) != 0)
            throw std::runtime_error("posix_memalign failed");

        uint8_t* buf = reinterpret_cast<uint8_t*>(umemArea);
        for (uint32_t i = 0; i < frameCount; ++i, buf += frameSize)
        { auto* slot = reinterpret_cast<PacketSlot*>(buf + packetSize);
            std::memset(slot, 0, sizeof(PacketSlot));
            *const_cast<uint32_t*>(&slot->index) = i;
        }

        // Allocate free slot stack
        nodePool = static_cast<FreeNode*>(std::calloc(frameCount, sizeof(FreeNode)));
        if (!nodePool) throw std::runtime_error("FreeNode allocation failed");

        for (uint32_t i = 0; i < frameCount; ++i)
        {
            nodePool[i].index = frameCount - 1 - i;
            nodePool[i].next = freeListHead.load(std::memory_order_relaxed);
            freeListHead.store(&nodePool[i], std::memory_order_release);
        }

        setupSocket();
        setupUmem();
        mapTxRing();
        bindSocket(ifname);
        checkWakeSupport();
    }

    ~Egress()
    {
        if (xskFd >= 0) close(xskFd);
        if (umemArea) std::free(umemArea);
        if (txRingArea) munmap(txRingArea, txRingMapSize);
        if (nodePool) std::free(nodePool);
    }

    FrameHandle getFrame()
    {
        FreeNode* current;
        do
        {
            current = freeListHead.load(std::memory_order_acquire);
            if (!current) return {nullptr, nullptr};
        } while (!freeListHead.compare_exchange_weak(
                current, current->next,
                std::memory_order_acq_rel, std::memory_order_acquire));

        uint32_t index = current->index;
        if (index >= frameCount)
            return { nullptr, nullptr };

        uint8_t* payload = reinterpret_cast<uint8_t*>(umemArea) + uint64_t(index) * frameSize;
        PacketSlot* slot = reinterpret_cast<PacketSlot*>(payload + packetSize);

        return { slot, payload };
    }

    inline void releaseFrame(uint32_t index)
    {
        FreeNode* node = &nodePool[index];
        FreeNode* head = freeListHead.load(std::memory_order_relaxed);
        do
        {
            node->next = head;
        } while (!freeListHead.compare_exchange_weak(
            head, node,
            std::memory_order_release, std::memory_order_relaxed));
    }

    inline bool send(uint32_t index, uint32_t length) noexcept
    {
        if (index >= frameCount)
            return false;

        uint32_t prod = *txRingProducer;
        uint32_t cons = *txRingConsumer;
        std::atomic_thread_fence(std::memory_order_acquire);
        
        uint32_t used = (prod - cons) & frameMask;
        if (used >= frameMask) return false; // Ring full

        txRingDesc[prod] = {
            .addr = uint64_t(index) * frameSize,
            .len = length,
            .options = 0
        };

        txInFlight[txInFlightTail] = index;
        txInFlightTail = (txInFlightTail + 1) & frameMask;

        std::atomic_thread_fence(std::memory_order_release);
        *txRingProducer = (prod + 1) & frameMask;

        if (needWakeup)
            ::sendto(xskFd, nullptr, 0, 0, nullptr, 0);

        return true;
    }

    inline void reclaim()
    {
        uint32_t cons = *txRingConsumer;
        std::atomic_thread_fence(std::memory_order_acquire);

        while(txInFlightHead != txInFlightTail && txInFlightHead != cons)
        {
            uint32_t index = txInFlight[txInFlightHead];
            releaseFrame(index);
            txInFlightHead = (txInFlightHead + 1) & frameMask;
        }
    }

private:
    int xskFd{-1};
    uint32_t frameCount;
    uint32_t frameMask;
    uint32_t frameSize;
    uint32_t packetSize;
    uint32_t qid;
    uint64_t umemSize;
    void* umemArea = nullptr;

    struct FreeNode
    {
        uint32_t index;
        FreeNode* next;
    };

    std::atomic<FreeNode*> freeListHead = nullptr;
    FreeNode* nodePool = nullptr;

    std::vector<uint32_t> txInFlight;
    uint32_t txInFlightHead;
    uint32_t txInFlightTail;

    bool needWakeup = false;
    bool zeroCopy = false;

    void* txRingArea = nullptr;
    size_t txRingMapSize = 0;
    struct xdp_mmap_offsets off{};
    struct xdp_desc* txRingDesc = nullptr;
    uint32_t* txRingProducer = nullptr;
    uint32_t* txRingConsumer = nullptr;

    size_t roundUpToPageSize(size_t size)
    {
        size_t pageSize = sysconf(_SC_PAGESIZE);
        return (size + pageSize - 1) & ~(pageSize - 1);
    }

    void setupSocket()
    {
        xskFd = socket(AF_XDP, SOCK_RAW, 0);
        if (xskFd < 0)
            throw std::runtime_error("socket(AF_XDP): " + std::string(strerror(errno)));
    }

    void setupUmem()
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

        uint32_t ringSize = frameCount;
        if (setsockopt(xskFd, SOL_XDP, XDP_TX_RING, &ringSize, sizeof(ringSize)) != 0)
            throw std::runtime_error("setsockopt(XDP_TX_RING): " + std::string(strerror(errno)));

        socklen_t len = sizeof(off);
        if (getsockopt(xskFd, SOL_XDP, XDP_MMAP_OFFSETS, &off, &len) != 0)
            throw std::runtime_error("getsockopt(XDP_MMAP_OFFSETS): " + std::string(strerror(errno)));
    }

    void mapTxRing()
    {
        txRingMapSize = roundUpToPageSize(off.tx.desc) + roundUpToPageSize(frameCount * sizeof(struct xdp_desc));
        txRingArea = mmap(nullptr, txRingMapSize, PROT_READ | PROT_WRITE, MAP_SHARED, xskFd, XDP_PGOFF_TX_RING);
        if (txRingArea == MAP_FAILED)
            throw std::runtime_error("mmap(TX_RING): " + std::string(strerror(errno)));

        txRingProducer = reinterpret_cast<uint32_t*>((uint8_t*)txRingArea + off.tx.producer);
        txRingConsumer = reinterpret_cast<uint32_t*>((uint8_t*)txRingArea + off.tx.consumer);
        txRingDesc     = reinterpret_cast<struct xdp_desc*>((uint8_t*)txRingArea + off.tx.desc);
    }

    void bindSocket(const char* ifname)
    {
        struct sockaddr_xdp sxdp = {};
        sxdp.sxdp_family = AF_XDP;
        sxdp.sxdp_ifindex = ifnametoindex(ifname);
        if (sxdp.sxdp_ifindex == 0)
            throw std::runtime_error("Invalid interface: " + std::string(ifname));

        sxdp.sxdp_queue_id = qid;
        sxdp.sxdp_flags = XDP_USE_NEED_WAKEUP | XDP_ZEROCOPY;

        // Try ZEROCOPY first
        if (bind(xskFd, reinterpret_cast<struct sockaddr*>(&sxdp), sizeof(sxdp)) == 0)
        {
            zeroCopy = true;
            return;
        }

        // Fallback to copy mode if ZEROCOPY fails
        sxdp.sxdp_flags = XDP_USE_NEED_WAKEUP;
        if (bind(xskFd, reinterpret_cast<struct sockaddr*>(&sxdp), sizeof(sxdp)) != 0)
            throw std::runtime_error("bind(AF_XDP): " + std::string(strerror(errno)));

        zeroCopy = false;
    }

    void checkWakeSupport()
    {
        uint32_t optval = 0;
        socklen_t optlen = sizeof(optval);
        if (getsockopt(xskFd, SOL_XDP, XDP_OPTIONS, &optval, &optlen) == 0)
            needWakeup = (optval & XDP_RING_NEED_WAKEUP);
        else
            needWakeup = false;
    }
};

} // namespace hardware

#endif // EGRESS_HPP

