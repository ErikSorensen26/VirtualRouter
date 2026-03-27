/**
 * @file TxDistributor.h
 * @brief Frame and packet distributor across per-CPU TX queues.
 */

#ifndef TX_DISTRIBUTOR_H
#define TX_DISTRIBUTOR_H

#include <cstdint>
#include <atomic>

namespace hardware { struct PacketSlot; struct FrameHandle; }

namespace qos::egress
{

struct QueueState;

/**
 * @brief Distribution policy used when selecting a TX queue for a frame or packet.
 * @ingroup QOS_EGRESS
 */
enum class TxDistPolicy
{
    BEST_EFFORT, ///< Always route to queue 0; no flow awareness.
    FLOW_HASH,   ///< Consistent hash of the packet's flow hash; preserves per-flow ordering.
    ROUND_ROBIN, ///< Simple round-robin across all available queues.
    WEIGHTED_RR  ///< Weighted round-robin using per-queue weights set via @ref TxDistributor::setWeights.
};

/**
 * @brief Routes frames and packets across a set of per-CPU TX queues.
 * @ingroup QOS_EGRESS
 *
 * `TxDistributor` is the single point through which the forwarding plane
 * selects which hardware TX queue a frame or raw packet slot is delivered to.
 * It supports four distribution policies (@ref TxDistPolicy) and exposes both
 * a frame-lifecycle API (@ref getFrame / @ref send / @ref release) and a direct
 * slot-push API (@ref push / @ref pushTo).
 *
 * ## Frame Lifecycle
 * Frames must be obtained and returned through the same queue to prevent
 * cross-queue index confusion in the underlying EgressBase free ring:
 *
 * 1. `getFrame(frame, policy, hash)` — allocates a frame from the queue
 *    chosen by @p policy; `frame.qid` records which queue was selected.
 * 2. Write the packet payload into the frame.
 * 3. `send(frame)` — enqueues the frame to `frame.qid` and clears `frame.slot`.
 *    Alternatively, `pushTo(frame.qid, frame.slot)` may be used directly.
 * 4. `release(frame)` — cancels the frame and returns it to its queue's free
 *    ring without transmitting; clears `frame.slot`.
 *
 * @warning Calling `send` or `pushTo` with a queue index that differs from the
 * one recorded in `frame.qid` will corrupt the free ring of the originating
 * queue. Always use `frame.qid` for the matching send or release call.
 *
 * ## Architectural Role
 * `TxDistributor` holds a non-owning pointer to the queue array managed by
 * `TxQueueManager`. Queue additions and removals update the array under
 * `TxQueueManager::mu`; `appendQueue` / `popQueue` must only be called while
 * that mutex is held.
 *
 * ## Concurrency Model
 * - `getFrame`, `push`, `pushTo`, `send`, `release` — safe to call from any
 *   thread concurrently; the queue count and round-robin index are accessed
 *   atomically.
 * - `appendQueue`, `popQueue`, `setWeights` — must be called only under
 *   `TxQueueManager::mu`; they are not safe for concurrent use with each other.
 *
 * @see TxQueueManager
 */
class TxDistributor
{
public:
    /**
     * @brief Constructs a distributor over an existing queue array.
     *
     * @param queues  Non-owning pointer to the array of @p size queue state
     *                pointers managed by `TxQueueManager`.
     * @param size    Initial number of active queues.
     *
     * @warning The array pointed to by @p queues must remain valid for the
     *          lifetime of this distributor.
     */
    TxDistributor(QueueState** queues, uint32_t size);

    /**
     * @brief Allocates a frame from the queue chosen by @p policy.
     *
     * On success, `frame.qid` is set to the selected queue index and
     * `frame.slot` points to the allocated buffer. The caller must subsequently
     * call @ref send or @ref release using the same `frame` object.
     *
     * @param frame     Output handle populated with the allocated frame.
     * @param policy    Queue selection strategy; defaults to @ref TxDistPolicy::FLOW_HASH.
     * @param flowHash  Per-flow hash used when @p policy is `FLOW_HASH`; ignored otherwise.
     * @return True if a frame was successfully allocated; false if the selected
     *         queue's free ring is empty.
     */
    bool getFrame(hardware::FrameHandle& frame,
                  TxDistPolicy policy   = TxDistPolicy::FLOW_HASH,
                  uint32_t     flowHash = 0);

    /**
     * @brief Submits a frame to the queue it was allocated from.
     *
     * Enqueues `frame.slot` to the queue recorded in `frame.qid`, then sets
     * `frame.slot` to nullptr to prevent accidental double-submit.
     *
     * @param frame  Frame previously allocated via @ref getFrame.
     *
     * @warning @p frame must not have been released via @ref release before
     *          calling this function.
     */
    void send(hardware::FrameHandle& frame);

    /**
     * @brief Enqueues a packet slot directly to the specified queue.
     *
     * Bypasses the policy selection; the caller is responsible for choosing
     * the correct queue index. Used by the frame-lifecycle path where `frame.qid`
     * is already known.
     *
     * @param qid  Zero-based index of the destination queue.
     * @param pkt  Packet slot to enqueue.
     */
    void pushTo(uint32_t qid, hardware::PacketSlot* pkt);

    /**
     * @brief Enqueues a packet slot to a queue chosen by @p policy.
     *
     * @param pkt     Packet slot to enqueue.
     * @param policy  Queue selection strategy; defaults to @ref TxDistPolicy::FLOW_HASH.
     */
    void push(hardware::PacketSlot* pkt,
              TxDistPolicy policy = TxDistPolicy::FLOW_HASH);

    /**
     * @brief Cancels a frame and returns it to its queue's EgressBase free ring.
     *
     * Returns the frame without transmitting it. Sets `frame.slot` to nullptr
     * to prevent double-release. Use this path when frame construction fails
     * after @ref getFrame has already been called.
     *
     * @param frame  Frame previously allocated via @ref getFrame.
     */
    void release(hardware::FrameHandle& frame);

    /**
     * @brief Updates per-queue weights used by the weighted round-robin policy.
     *
     * Recomputes the cached weight sum. @p n must equal the current number of
     * active queues; passing a mismatched count will misroute traffic.
     *
     * @param w  Array of @p n per-queue weights; the distributor does not take
     *           ownership of the pointer.
     * @param n  Number of weights, must equal the current active queue count.
     *
     * @note Must be called only while `TxQueueManager::mu` is held.
     */
    void setWeights(const uint16_t* w, uint32_t n);

    /**
     * @brief Increments the active queue count by one.
     *
     * Called by `TxQueueManager` immediately after a new queue has been
     * appended to the underlying array.
     *
     * @note Must be called only while `TxQueueManager::mu` is held.
     */
    void appendQueue();

    /**
     * @brief Decrements the active queue count by one.
     *
     * Called by `TxQueueManager` immediately before removing the last queue
     * from the underlying array.
     *
     * @note Must be called only while `TxQueueManager::mu` is held.
     */
    void popQueue();

private:
    QueueState**          qs      = nullptr; ///< Non-owning pointer to the queue array; owned by TxQueueManager.
    std::atomic<uint32_t> N;                 ///< Current number of active queues; updated atomically.
    std::atomic<uint32_t> rr{0};             ///< Round-robin cursor; advanced atomically on each selection.
    const uint16_t*       weights = nullptr; ///< Per-queue weight array for WEIGHTED_RR; not owned.
    std::atomic<uint32_t> weightSum{0};      ///< Cached sum of all weights; recomputed in setWeights.

    /**
     * @brief Selects a queue index according to @p policy.
     *
     * @param policy    Selection strategy.
     * @param flowHash  Per-flow hash used when @p policy is FLOW_HASH.
     * @return Selected zero-based queue index.
     */
    uint32_t pickQueue(TxDistPolicy policy, uint32_t flowHash = 0);

    /**
     * @brief Selects a queue index using weighted round-robin.
     *
     * Iterates through the weight array to find the queue corresponding to the
     * current round-robin position within the total weight interval.
     *
     * @return Selected zero-based queue index.
     */
    uint32_t pickWeighted();
};

} // namespace qos::egress

#endif // TX_DISTRIBUTOR_H
