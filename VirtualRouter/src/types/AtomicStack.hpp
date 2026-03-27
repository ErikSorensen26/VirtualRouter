/**
 * @file AtomicStack.hpp
 * @brief Lock-free atomic LIFO stack for concurrent access.
 */

#ifndef ATOMIC_STACK_HPP
#define ATOMIC_STACK_HPP

#include <atomic>

namespace types
{

/**
 * @brief Lock-free LIFO stack for single-producer / multi-consumer or
 *        multi-producer / multi-consumer access patterns.
 * @ingroup TYPES
 *
 * Implemented as a Treiber stack: each push and pop is a CAS loop on the
 * `head` pointer, requiring no mutex. All operations are wait-free on
 * architectures that provide a strong CAS instruction.
 *
 * ## Architectural Role
 * Intended for lightweight inter-thread queuing of small value types (e.g.,
 * free-list indices, recycled packet slots) where lock overhead would
 * dominate. Not suitable for types with non-trivial destructors that must
 * run in a specific order.
 *
 * ## Lifecycle & Ownership
 * Each pushed value is heap-allocated in an internal `Node`. Popped values
 * transfer ownership back to the caller; nodes are deleted immediately after
 * the value is copied out. Any nodes still in the stack when the `AtomicStack`
 * is destroyed are leaked — the destructor is intentionally omitted to keep
 * the type trivially destructible in free-list use cases. Callers must drain
 * the stack before destruction if leak-free operation is required.
 *
 * @warning The ABA problem is not mitigated. Do not use this stack when the
 * same pointer value can be reused across concurrent push/pop pairs without
 * an epoch or hazard-pointer scheme.
 *
 * @tparam T Value type to store. Must be copy-constructible. Prefer small
 *           value types to avoid per-push heap allocation overhead.
 */
template<typename T>
class AtomicStack {
private:
    struct Node {
        T data;
        Node* next;
        Node(T d) : data(d), next(nullptr) {}
    };
    std::atomic<Node*> head; ///< Top of the stack; null when empty.

public:
    /**
     * @brief Pushes a value onto the top of the stack.
     *
     * Allocates a new node and installs it as the new head via a CAS retry
     * loop. The loop spins until no competing push wins the same head slot.
     *
     * @param val Value to push. Copied into the heap-allocated node.
     */
    void push(T val) {
        Node* newNode = new Node(val);
        newNode->next = head.load();
        // Atomic compare-and-swap to update head
        while (!head.compare_exchange_strong(newNode->next, newNode));
    }

    /**
     * @brief Pops the top value from the stack.
     *
     * Removes the head node via a CAS retry loop and copies its value into
     * `result`. Returns false immediately if the stack is empty.
     *
     * @param[out] result Receives the popped value on success.
     * @return True if a value was popped; false if the stack was empty.
     */
    bool pop(T& result) {
        Node* oldHead = head.load();
        while (oldHead && !head.compare_exchange_strong(oldHead, oldHead->next));
        if (!oldHead) return false;

        result = oldHead->data;
        delete oldHead;
        return true;
    }
};

} // namespace types

#endif // ATOMIC_STACK_HPP

