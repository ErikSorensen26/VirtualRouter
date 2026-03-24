#include <atomic>
#include <cstddef>
#include <functional>
#include <vector>
#include <memory>
#include <RCU.hpp>

// Lock-free hash map using RCU + block allocations
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

    // block allocator for nodes
    struct NodeBlockAllocator {
        std::vector<std::unique_ptr<Node[]>> blocks;
        size_t block_size = 1024;
        size_t index = 0;
        Node* current_block = nullptr;

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
    NodeBlockAllocator allocator;

public:
    explicit AtomicHashMap(size_t num_buckets) 
        : buckets(num_buckets) {
        for (auto& b : buckets) b.store(nullptr, std::memory_order_relaxed);
    }

    // Lookup
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

    // Insert
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

    // Erase
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
                // safe RCU deletion
                utils::RCU::retire([curr]() { delete curr; });
                return true;
            }
            prev = curr;
            curr = next;
        }
        return false;
    }
};
