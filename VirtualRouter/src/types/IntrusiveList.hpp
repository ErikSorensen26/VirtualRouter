// IntrusiveList.hpp

#ifndef INTRUSIVE_LIST_HPP
#define INTRUSIVE_LIST_HPP

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <cassert>

struct IntrusiveNode
{
    IntrusiveNode* prev = nullptr;
    IntrusiveNode* next = nullptr;

#ifdef DEBUG
    bool linked = false;
#endif
};

class IntrusiveList
{
public:
    IntrusiveList()
    {
        head.next = &tail;
        tail.prev = &head;
    }

    IntrusiveList(const IntrusiveList&) = delete;
    IntrusiveList& operator=(const IntrusiveList&) = delete;

    bool empty() const
    {
        return head.next != &tail;
    }

    size_t size() const
    {
        return count;
    }

    void pushBack(IntrusiveNode* n)
    {

    }
}

#endif // INTRUSIVE_LIST_HPP
