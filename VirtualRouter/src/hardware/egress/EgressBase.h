/**
 * @file EgressBase.h
 * @brief Abstract base for all hardware egress (TX) backends.
 */

/**
 * @defgroup HARDWARE Hardware I/O
 * @brief NIC ingress and egress pipelines; backend-agnostic API over AF_XDP and TPACKET.
 */

/**
 * @defgroup HARDWARE_EGRESS Egress
 * @ingroup HARDWARE
 * @brief TX backends: EgressPacket (TPACKET_V2 mmap ring) and EgressSend (AF_PACKET sendto).
 */

#ifndef EGRESS_BASE_H
#define EGRESS_BASE_H

#include <cstdint>
#include <atomic>

#include "qos/egress/TxQueueOpts.hpp"

#define MTU_PADDING 128

namespace interface { class Interface; }
namespace qos::egress { struct TxQueueOpts; }

/**
 * @namespace hardware
 * @brief Top-level namespace for NIC hardware abstractions, packet metadata, and utility helpers.
 */
namespace hardware
{
struct FrameHandle;

static inline uint32_t ceilPow2(uint32_t v)
{
    if (v <= 1) return 1;
    v--;
    v |=  v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return v + 1;
}

static inline void cpuRelax() { asm volatile("pause" ::: "memory"); }
}

/**
 * @namespace hardware::egress
 * @brief Egress (TX) backend implementations and the shared EgressBase.
 *
 * EgressBase owns the MPMC free ring and frame layout logic. Concrete
 * subclasses (@ref EgressPacket, @ref EgressSend) implement the hardware send
 * path. Constructed exclusively by the @ref hardware::egress::create() factory.
 */
namespace hardware::egress
{

/**
 * @brief Backend-agnostic TX base: owns the MPMC frame free ring and frame allocation API.
 * @ingroup HARDWARE_EGRESS
 *
 * EgressBase decouples frame lifetime management from the kernel send
 * mechanism. It maintains a lock-free MPMC free ring (Vyukov sequence-number
 * algorithm) from which callers borrow frame slots, write their payload, then
 * submit via @ref send(). The concrete subclass only needs to implement the
 * hardware-facing operations: @ref mapFrame(), @ref send(), @ref reclaim(),
 * and @ref cancel().
 *
 * Each instance corresponds to exactly one hardware TX queue on one interface.
 *
 * ## Architectural Role
 * EgressBase sits between the QoS layer (@ref BaseQueue / @ref TxDistributor)
 * and the kernel. It does not schedule or queue packets — that is the
 * responsibility of @ref BaseQueue. It only provides frame memory and
 * transmits on demand.
 *
 * ## Lifecycle & Ownership
 * Constructed by @ref hardware::egress::create(). Owned by @ref TxQueueManager
 * via a @ref QueueState entry. Destroyed after the owning @ref BaseQueue has
 * been stopped, ensuring no producer threads are still calling @ref getFrame().
 *
 * ## Concurrency Model
 * - @ref getFrame() / @ref tryPopFree() / @ref pushFree() — MPMC-safe; any
 *   number of threads may call @ref getFrame() simultaneously.
 * - @ref send(), @ref reclaim(), @ref cancel(), @ref flush() — called only
 *   from the single @ref BaseQueue consumer thread.
 *
 * ## Fast Path vs. Slow Path
 * - Fast path: @ref getFrame() → write payload → @ref send() per packet.
 * - Slow path: @ref reclaim() (periodic), @ref waitWritable() (back-pressure).
 *
 * @warning @ref initFreeRing() must be called by the subclass constructor
 * before any @ref getFrame() call. Calling @ref getFrame() before the ring is
 * initialised results in undefined behaviour.
 *
 * @see EgressPacket, EgressSend, TxDistributor
 */
class EgressBase
{
public:
    /**
     * @brief Constructs EgressBase and records interface and queue options.
     *
     * Does not allocate the free ring. Subclasses must call @ref initFreeRing()
     * during their own construction after determining the actual frame count.
     *
     * @param iface  The interface this egress queue transmits on.
     * @param opts   Queue configuration (frame count, snap length, CPU affinity…).
     */
    EgressBase(interface::Interface& iface, const qos::egress::TxQueueOpts& opts);

    /**
     * @brief Destroys the EgressBase and releases the free ring.
     *
     * Calls @ref destroyFreeRing(). Subclasses must have stopped transmitting
     * before this destructor runs; any in-flight @ref getFrame() call at
     * destruction time is a data race.
     */
    virtual ~EgressBase();

    /**
     * @brief Allocates one free frame from the MPMC ring.
     *
     * Pops a frame index from the free ring and calls the subclass
     * @ref mapFrame() to fill in the payload pointer and slot metadata.
     * If the free ring is empty the call returns @c false immediately;
     * the caller must retry or apply back-pressure.
     *
     * @param[out] frame  Populated with the payload pointer, slot pointer,
     *                    and queue ID on success.
     * @return @c true if a frame was allocated; @c false if the ring is empty.
     */
    bool     getFrame(FrameHandle& frame);
    int      getCpuId()     const noexcept { return opts.cpuId; }
    uint32_t getFrameCount() const noexcept { return hwFrameCount; }

    /**
     * @brief Transmits the frame described by @p index.
     *
     * Places the frame on the wire (or kernel TX ring) and returns without
     * blocking. The frame must have been allocated via @ref getFrame() and
     * the payload must be fully written before this call.
     *
     * @param index   Frame index as recorded in @ref PacketSlot::index.
     * @param length  Number of valid payload bytes to transmit.
     * @return @c true if the frame was queued/sent; @c false on a transient
     *         kernel error (frame is not automatically cancelled).
     */
    virtual bool send(uint32_t index, uint32_t length) noexcept = 0;

    /**
     * @brief Reclaims completed TX frames and returns them to the free ring.
     *
     * Polls the kernel ring for frames the NIC has finished transmitting and
     * calls @ref pushFree() for each, making them available for re-use. Must
     * be called periodically from the consumer thread to prevent free-ring
     * exhaustion.
     */
    virtual void reclaim() = 0;

    /**
     * @brief Cancels a frame without transmitting it.
     *
     * Returns the frame at @p index to the free ring. Used when a producer
     * decides not to send after calling @ref getFrame().
     *
     * @param index  Frame index to cancel; must have been obtained via @ref getFrame().
     */
    virtual void cancel(uint32_t index) = 0;

    /**
     * @brief Flushes any buffered frames to the kernel.
     *
     * Default implementation is a no-op. Subclasses that batch-kick the
     * kernel (e.g. @ref EgressPacket) override this to issue a final sendmsg/sendto
     * so buffered frames are not stranded at end-of-batch.
     */
    virtual void flush() {}

    /**
     * @brief Blocks until the TX ring has capacity for at least one more frame.
     *
     * Used for back-pressure when the ring is full. Default is a no-op.
     * @ref EgressPacket overrides this with an epoll_wait on the socket.
     */
    virtual void waitWritable() {}

protected:
    interface::Interface& iface; ///< Interface this queue transmits on.
    qos::egress::TxQueueOpts opts; ///< Configuration snapshot (immutable after construction).
    const uint32_t qid; ///< Hardware queue index assigned at construction.

    /**
     * @brief Allocates and initialises the MPMC free ring.
     *
     * Must be called exactly once from the subclass constructor. Rounds
     * @p frameCount up to the next power of two if it is not already one.
     *
     * @param frameCount  Number of frame slots to manage.
     * @warning Calling this more than once leaks the previous allocation.
     */
    void initFreeRing(uint32_t frameCount);

    /**
     * @brief Frees the memory allocated by @ref initFreeRing().
     *
     * Called automatically by the destructor. Safe to call if
     * @ref initFreeRing() was never called.
     */
    void destroyFreeRing();

    /**
     * @brief MPMC free ring
     *
     * Invariant for slot at ring position p (p & freeMask = i):
     *   freeSeq[i] == p       → empty,  ready for a producer to claim position p
     *   freeSeq[i] == p + 1   → full,   ready for a consumer to claim position p
     *   freeSeq[i] == p + cap → recycled (consumer done), ready for producer at p+cap
     *
     * No spinloop is needed in practice: the "wait" cases only occur when another
     * thread is mid-write and the scheduler hasn't given it a chance to store seq.
     * The cpu_relax() hint is sufficient on x86/ARM.
     */
    /**
     * @brief Returns a frame index to the free ring. MPMC-safe.
     * @param index  Frame index to recycle; must have been obtained from this ring.
     */
    void pushFree(uint32_t index);

    /**
     * @brief Attempts to pop one free frame index from the ring. MPMC-safe.
     * @param[out] outIndex  Set to the claimed frame index on success.
     * @return @c true if a frame was claimed; @c false if the ring is empty.
     */
    bool tryPopFree(uint32_t& outIndex);

    /**
     * @brief Called by @ref getFrame() after a successful pop.
     *
     * Subclasses may use this hook to proactively kick reclaim or wake the
     * kernel when the free ring is running low. Default is a no-op.
     */
    virtual void onAllocNudge() {}

    /**
     * @brief Maps a frame index to its payload pointer and slot metadata.
     *
     * Called by @ref getFrame() after claiming a slot. Must fill @p out with
     * a valid payload pointer, the @ref PacketSlot embedded after the payload,
     * and the queue ID.
     *
     * @param index  Frame index claimed from the free ring.
     * @param[out] out  Populated with the frame's payload and slot addresses.
     */
    virtual void mapFrame(uint32_t index, FrameHandle& out) = 0;

    uint32_t packetSize; ///< Bytes per frame slot (payload region only, excluding PacketSlot).

private:
    uint32_t*              freeBuf = nullptr; ///< Frame-index storage array (freeCap elements).
    std::atomic<uint32_t>* freeSeq = nullptr; ///< Per-slot sequence counters (Vyukov algorithm).
    alignas(64) std::atomic<uint32_t> freeHead{0}; ///< Consumer cursor. Cache-line isolated.
    alignas(64) std::atomic<uint32_t> freeTail{0}; ///< Producer cursor. Cache-line isolated.
    uint32_t freeCap      = 0; ///< Ring capacity (power of 2).
    uint32_t freeMask     = 0; ///< freeCap - 1; used for fast modulo.
    uint32_t hwFrameCount = 0; ///< Actual frame count passed to initFreeRing().
};

} // namespace hardware::egress

#endif // EGRESS_BASE_H

