// RegistryBucket

#include <deque>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <new>
#include <cassert>
#include <algorithm>

namespace Config
{
template <typename T>
class Bucket
{
public:
    struct Handle
    {
        size_t index;
        uint32_t generation;
        bool operator==(const Handle&) const = default;
    };

private:
    struct Slot
    {
        alignas(T) unsigned char storage[sizeof(T)];
        uint32_t generation = 0;
        bool alive = false;

        T* ptr()
        {
            return std::launder(reinterpret_cast<T*>(storage));
        }
    };

    std::deque<Slot> slots;
    std::vector<size_t> free;

public:
    Bucket() = default;
    Bucket(const Bucket&) = delete;
    Bucket& operator=(const Bucket&) = delete;

    Handle create()
    {
        size_t index;

        if (!free.empty())
        {
            index = free.back();
            free.pop_back();
        }
        else
        {
            index = slots.size();
            slots.emplace_back();
        }

        Slot& s = slots[index];
        assert(!s.alive);

        new (s.storage) T;
        s.alive = true;

        return Handle{ index, s.generation };
    }

    T* get(const Handle& h)
    {
        if (h.index >= slots.size())
            return nullptr;

        Slot& s = slots[h.index];

        if (!s.alive || s.generation != h.generation)
            return nullptr;

        return s.ptr();
    }

    const T* get(const Handle& h) const
    {
        return const_cast<Bucket*>(this)->get(h);
    }

    void trimTail()
    {
        while (!slots.empty())
        {
            Slot& s = slots.back();

            if (s.alive)
                break;

            auto it = std::find(free.begin(), free.end(), slots.size() - 1);
            if (it != free.end())
                free.erase(it);

            slots.pop_back();
        }
    }

    void erase(const Handle& h)
    {
        assert(h.index < slots.size());
        Slot& s = slots[h.index];

        if (!s.alive || s.generation != h.generation)
            return;

        s.ptr()->~T();
        s.alive = false;
        ++s.generation;

        free.push_back(h.index);

        trimTail();
    }

    bool alive(const Handle& h) const
    {
        return h.index < slots.size() &&
               slots[h.index].alive &&
               slots[h.index].generation == h.generation;
    }

    size_t capacity() const
    {
        return slots.size();
    }

    size_t freeCount() const
    {
        return free.size();
    }
};
}
