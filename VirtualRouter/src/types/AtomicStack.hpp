// AtomicStack.hpp

#ifndef ATOMIC_STACK_HPP
#define ATOMIC_STACK_HPP

#include <atomic>

namespace types
{

template<typename T>
class AtomicStack {
private:
    struct Node {
        T data;
        Node* next;
        Node(T d) : data(d), next(nullptr) {}
    };
    std::atomic<Node*> head;

public:
    void push(T val) {
        Node* newNode = new Node(val);
        newNode->next = head.load();
        // Atomic compare-and-swap to update head
        while (!head.compare_exchange_strong(newNode->next, newNode));
    }

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

