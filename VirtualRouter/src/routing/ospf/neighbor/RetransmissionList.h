/**
 * @file RetransmissionList.h
 * @brief Generic retransmission queue with burst-pacing and per-entry retry limiting.
 */

#ifndef RETRANSMISSION_LIST_H
#define RETRANSMISSION_LIST_H

#include <vector>
#include <unordered_map>
#include <optional>
#include <cstdint>

namespace config { struct OspfRegistry; }

namespace routing::ospf
{
class OspfInterfaceBase;

/**
 * @brief Key-indexed retransmission queue supporting paced burst delivery.
 * @ingroup OSPF_NEIGHBOR
 *
 * `RetransmissionList` provides the reliable-delivery layer required by OSPF
 * for LSU and LSR packets. It stores a flat parallel array of records and keys,
 * backed by a hash map for O(1) key-based lookup and removal.
 *
 * The class supports two operational modes:
 * - **Individual retransmit** — entries are added by key, updated in place if
 *   the key already exists, and erased on acknowledgment.
 * - **Burst retransmit** — `beginRetransmitBurst` snapshots the current count
 *   and `nextInBurst` / `markBurst` iterate through the queue in round-robin
 *   order, capping each entry's retransmission count at the configured limit.
 *
 * When an entry's retransmission count reaches the limit it is automatically
 * removed from the queue. For demand circuits the limit is read from
 * `config::Ospf::RETRANSMISSION_DC_LIMIT`; for regular interfaces from
 * `config::Ospf::RETRANSMISSION_NON_DC_LIMIT`.
 *
 * The internal storage uses a swap-and-pop removal strategy to avoid O(n)
 * shifts: the erased element is replaced by the last element and the size is
 * decremented.
 *
 * ## Architectural Role
 * Instantiated twice inside `Retransmission`: once for outbound LSUs
 * (`Key = LsaKey`, `Record = LsaRecordRef`) and once for outbound LSRs
 * (`Key = LsaKey`, `Record = LsaKey`). Not used outside the neighbor layer.
 *
 * ## Lifecycle & Ownership
 * Owned by `Retransmission`, which is owned by `Neighbor`. `clear()` is called
 * whenever the adjacency resets (ExStart restart or Down transition) to
 * discard all pending entries; it has no scheduler access and does not
 * cancel `retransmitTimerId`/`pacingTimerId` itself -- callers must cancel
 * those via `InterfaceTimers::cancelRetransmissionTimers` /
 * `cancelLsrTimers` before calling `clear()` (see `Neighbor::setState`).
 *
 * @tparam Key     The type used to identify entries. Must be hashable (i.e.
 *                 `std::hash<Key>` must be defined) and equality-comparable.
 * @tparam Record  The value stored per entry. Must be movable; it is
 *                 overwritten in place when `add` is called with an existing key.
 *
 * @warning The `cursor` and `burstRemaining` state is invalidated if entries
 * are erased mid-burst by a concurrent `erase` call. All retransmission timer
 * callbacks must run on the same scheduler thread to avoid this.
 *
 * @see Retransmission
 * @see Neighbor
 */
template <typename Key, typename Record>
class RetransmissionList
{
public:
    /**
     * @brief Constructs the retransmission list.
     * @ingroup OSPF_NEIGHBOR
     *
     * Stores references to the process and interface so that
     * `getMaxRetransmission()` can consult the correct config registers at
     * retransmit time rather than caching a value that may change.
     *
     * @param iface    The interface this list belongs to (provides
     *                 demand-circuit status).
     */
    RetransmissionList(OspfInterfaceBase& iface, const config::OspfRegistry& cfgs)
        : iface(iface), processCfgs(cfgs)
    {}

    uint32_t retransmitTimerId = 0; ///< Active retransmit timer ID; 0 when no periodic retransmit is scheduled.
    uint32_t pacingTimerId = 0;     ///< Active pacing timer ID; 0 when no burst is in progress.

    // RELIABILITY

    /**
     * @brief Adds or replaces an entry in the retransmission queue (move overload).
     *
     * If @p key already exists the stored record is replaced with @p record
     * and the retransmission count is preserved. Otherwise a new entry is
     * appended.
     *
     * @param key     Unique key identifying this entry.
     * @param record  The record to store (moved into the queue).
     * @return True if a new entry was inserted, false if an existing entry
     *         was updated in place.
     */
    bool add(Key& key, Record& record);

    /**
     * @brief Adds or replaces an entry in the retransmission queue (copy overload).
     *
     * @param key     Unique key identifying this entry.
     * @param record  The record to store (copied into the queue).
     * @return True if a new entry was inserted, false if an existing entry
     *         was updated in place.
     */
    bool add(const Key& key, const Record& record);

    /**
     * @brief Checks whether an entry with the given key is present.
     *
     * @param key  The key to search for.
     * @return True if the key exists in the queue.
     */
    bool has(const Key& key);

    /**
     * @brief Retrieves a copy of the record associated with @p key.
     *
     * @param key  The key to look up.
     * @return The stored record, or `std::nullopt` if the key is not present.
     */
    std::optional<Record> get(const Key& key);

    /**
     * @brief Returns a read-only view of all records currently in the queue.
     *
     * The order of records is not guaranteed to be insertion order after
     * removals (swap-and-pop is used internally).
     */
    const std::vector<Record>& getAll() const;

    /**
     * @brief Removes the entry identified by @p key.
     *
     * Uses swap-and-pop to avoid O(n) shifts: the removed entry is replaced
     * by the last element in the vector, and the index map is updated
     * accordingly. Adjusts `cursor` and `burstRemaining` if a burst is active.
     *
     * @param key  The key of the entry to remove.
     * @return True if the entry was found and removed, false if not present.
     */
    bool erase(const Key& key);

    /**
     * @brief Removes all entries and resets burst state.
     *
     * Called when the adjacency resets to ensure no stale entries are
     * retransmitted to the next incarnation of the neighbor.
     */
    void clear();

    /**
     * @brief Returns true if there are any pending entries in the queue.
     */
    bool getActive() const;

    /**
     * @brief Initialises a retransmit burst over all current entries.
     *
     * Snapshots the current queue size as the burst budget and resets the
     * cursor to the beginning. `nextInBurst` / `markBurst` iterate through
     * the snapshot.
     *
     * @note Starting a new burst while one is already in progress resets the
     * cursor and refreshes the budget, which may cause some entries to be
     * visited twice.
     */
    void beginRetransmitBurst();

    /**
     * @brief Retrieves the next record in the current burst without advancing.
     *
     * The cursor is not advanced until `markBurst` is called. This allows the
     * caller to build a packet containing the record before committing the
     * retransmit.
     *
     * @param[out] recordOut  Populated with the next record if one is available.
     * @return True if a record was written to @p recordOut, false if the burst
     *         budget is exhausted or the queue is empty.
     */
    bool nextInBurst(Record& recordOut);

    /**
     * @brief Marks the burst entry identified by @p key as retransmitted and
     *        advances the cursor.
     *
     * Increments the retransmission counter for the entry. If the counter
     * reaches the configured limit the entry is automatically erased, which
     * handles its own cursor/burst-budget bookkeeping. Otherwise the cursor
     * advances and the burst budget is decremented by one for this send.
     *
     * @param key  Key of the entry just sent.
     */
    void markBurst(Key& key);

    /**
     * @brief Returns true while the current burst still has entries to deliver.
     */
    bool burstActive() const;

private:
    /**
     * @brief Returns the per-entry retransmission limit from configuration.
     *
     * Reads `RETRANSMISSION_DC_LIMIT` for demand circuits or
     * `RETRANSMISSION_NON_DC_LIMIT` for regular interfaces.
     */
    uint8_t getMaxRetransmission();

    size_t   cursor = 0;          ///< Index of the next entry to deliver in the current burst.
    uint32_t burstRemaining = 0;  ///< Number of entries remaining in the current burst.

    /**
     * @brief Per-entry tracking metadata stored alongside the key map.
     */
    struct OutboundInfo
    {
        size_t  index;               ///< Current index in the parallel outbound / outboundKeys vectors.
        uint8_t retransmissions{0};  ///< Number of times this entry has been retransmitted.
    };

    std::unordered_map<Key, OutboundInfo> outboundInfo; ///< O(1) key → index lookup.
    std::vector<Key>    outboundKeys;                   ///< Keys in the same order as outbound records.
    std::vector<Record> outbound;                       ///< Packed array of pending records.

    OspfInterfaceBase& iface; ///< Used to determine whether demand-circuit limits apply.
    const config::OspfRegistry& processCfgs; ///< Global process configs for retransmission limit.
};

} // namespace routing::ospf

#endif // RETRANSMISSION_LIST_H
