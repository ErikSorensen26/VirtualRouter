/**
 * @file AtomicHashMap.hpp
 * @brief Lock-free chained hash map with RCU-deferred node reclamation.
 */

#include <atomic>
#include <cstddef>
#include <functional>
#include <vector>
#include <memory>
#include <RCU.hpp>

/**
 * @brief Lock-free hash map supporting concurrent reads and serialized writes,
 *        backed by RCU-safe node reclamation.
 * @ingroup TYPES
 *
 * Each bucket is the head of an intrusive singly-linked list of @c Node objects.
 * Bucket heads are stored as @c std::atomic<Node*> so that readers can traverse
 * a bucket chain without holding a lock, observing a consistent snapshot at any
 * point in time.
 *
 * Node memory is managed by a private block allocator to amortize allocation
 * overhead on insertion. Erased nodes are handed to @ref utils::RCU::retire so
 * that readers currently traversing the chain finish before the node's memory
 * is reclaimed.
 *
 * ## Architectural Role
 * Used for tables that are read on every packet (e.g. neighbor or session
 * lookup) and written infrequently (session up/down events). The bucket count
 * is fixed at construction; no rehashing occurs.
 *
 * ## Lifecycle & Ownership
 * The map is constructed with a fixed number of buckets. All nodes are owned
 * by the internal block allocator; values are copied by the @c Node constructor.
 * Destruction does not run RCU retirement — all memory owned by the allocator
 * is freed directly when the map is destroyed, so the map must not be destroyed
 * while readers are active unless the caller coordinates externally.
 *
 * ## Concurrency Model
 * - Multiple concurrent readers are safe via RCU read-side guards.
 * - Only one writer thread at a time. Concurrent inserts or erases on the same
 *   bucket race on the CAS loop and will eventually succeed, but concurrent
 *   modifications to the same key from two writers are not safe.
 *
 * @warning The block allocator is not thread-safe. @c insert must be called
 *          from a single writer thread.
 *
 * @tparam Key    Key type. Must be equality-comparable and hashable by @p Hasher.
 * @tparam Value  Value type. Must be copy-constructible; stored by value in each node.
 * @tparam Hasher Hash functor type. Must satisfy the standard @c Hash concept:
 *                callable as @c size_t(const Key&).
 */
template<typename Key, typename Value, typename Hasher = std::hash<Key>>
class AtomicHashMap {
private:
    struct Node {
        Key key;
        Value value;
        std::atomic<Node*> next;

        Node(const Key& k, const Value& v)
            : key(k), value(v), next(nullptr) {}
    };

    /**
     * @brief Bump allocator that carves @c Node objects out of fixed-size heap blocks.
     *
     * Allocates in blocks of @c block_size to reduce per-node allocation overhead.
     * Objects are placement-new'd into block memory; destructors are not called on
     * individual nodes (the block is freed wholesale when the map is destroyed).
     *
     * @warning Not thread-safe. The owning @c AtomicHashMap must serialize all
     *          calls to @c allocate.
     */
    struct NodeBlockAllocator {
        std::vector<std::unique_ptr<Node[]>> blocks;
        size_t block_size = 1024;   ///< Number of nodes per allocated block.
        size_t index = 0;           ///< Next free slot within the current block.
        Node* current_block = nullptr;

        /**
         * @brief Allocates and constructs a new Node for @p k / @p v.
         *
         * Allocates a fresh block when the current one is exhausted.
         *
         * @param k Key to copy into the new node.
         * @param v Value to copy into the new node.
         * @return Pointer to the newly constructed node.
         */
        Node* allocate(const Key& k, const Value& v) {
            if (!current_block || index == block_size) {
                blocks.push_back(std::make_unique<Node[]>(block_size));
                current_block = blocks.back().get();
                index = 0;
            }
            Node* n = &current_block[index++];
            new (n) Node(k, v);
            return n;
        }
    };

    std::vector<std::atomic<Node*>> buckets;
    Hasher hasher;
    NodeBlockAllocator allocator; ///< Block allocator; not thread-safe; writer-only access.

public:
    /**
     * @brief Constructs the map with @p num_buckets hash buckets.
     *
     * All bucket heads are initialized to null. The bucket count does not change
     * after construction.
     *
     * @param num_buckets Number of hash buckets. A prime value reduces collision
     *                    clustering. Must be > 0.
     */
    explicit AtomicHashMap(size_t num_buckets)
        : buckets(num_buckets) {
        for (auto& b : buckets) b.store(nullptr, std::memory_order_relaxed);
    }

    /**
     * @brief Searches for @p key and copies the associated value into @p out.
     *
     * Traverses the bucket chain under an RCU read-side guard. Safe to call
     * concurrently with other @c find calls or with @c erase (but not @c insert,
     * which allocates without a guard).
     *
     * @param key Key to look up.
     * @param out Populated with the found value on success.
     * @return @c true if the key was found, @c false otherwise.
     */
    bool find(const Key& key, Value& out) {
        utils::RCU::Guard g;
        size_t idx = hasher(key) % buckets.size();
        Node* curr = buckets[idx].load(std::memory_order_acquire);
        while (curr) {
            if (curr->key == key) {
                out = curr->value;
                return true;
            }
            curr = curr->next.load(std::memory_order_acquire);
        }
        return false;
    }

    /**
     * @brief Inserts a new key/value pair at the head of the appropriate bucket.
     *
     * Does not check for an existing key — duplicate keys result in two nodes
     * in the same chain; @c find will return the most recently inserted one.
     * Uses a CAS loop on the bucket head to publish the new node atomically.
     *
     * @param key   Key to insert.
     * @param value Value to associate with @p key.
     * @return Always @c true (reserved for future error paths).
     *
     * @warning Must be called from the single designated writer thread.
     */
    bool insert(const Key& key, const Value& value) {
        size_t idx = hasher(key) % buckets.size();
        Node* new_node = allocator.allocate(key, value);

        utils::RCU::Guard g;
        Node* head;
        do {
            head = buckets[idx].load(std::memory_order_acquire);
            new_node->next.store(head, std::memory_order_relaxed);
        } while (!buckets[idx].compare_exchange_weak(
            head, new_node, std::memory_order_release, std::memory_order_acquire
        ));
        return true;
    }

    /**
     * @brief Removes the first node whose key equals @p key.
     *
     * Unlinks the node from the bucket chain using a CAS on either the bucket
     * head or the predecessor's @c next pointer, then defers the node's
     * destruction through @ref utils::RCU::retire so that concurrent readers
     * finish traversal before the memory is reclaimed.
     *
     * @param key Key to remove.
     * @return @c true if a matching node was found and removed, @c false otherwise.
     *
     * @note Only the first matching node is removed. If duplicate keys were
     *       inserted, subsequent @c find calls may still return the older copy.
     *
     * @warning Must be called from the single designated writer thread.
     */
    bool erase(const Key& key) {
        size_t idx = hasher(key) % buckets.size();
        utils::RCU::Guard g;

        Node* prev = nullptr;
        Node* curr = buckets[idx].load(std::memory_order_acquire);

        while (curr) {
            Node* next = curr->next.load(std::memory_order_acquire);
            if (curr->key == key) {
                if (prev) {
                    prev->next.compare_exchange_strong(curr, next,
                        std::memory_order_release, std::memory_order_acquire);
                } else {
                    buckets[idx].compare_exchange_strong(curr, next,
                        std::memory_order_release, std::memory_order_acquire);
                }
                // Defer reclamation until all current RCU readers have exited.
                utils::RCU::retire([curr]() { delete curr; });
                return true;
            }
            prev = curr;
            curr = next;
        }
        return false;
    }
};
