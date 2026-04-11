/**
 * @file RegistryBucket.hpp
 */

// RegistryBucket.hpp
/*
#ifndef REGISTRY_BUCKET_HPP
#define REGISTRY_BUCKET_HPP

#include <deque>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <new>
#include <cassert>
#include <unordered_map>
#include <utility>

namespace config
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

        uint64_t key{0};
        uint32_t generation{0};
        uint32_t refCount{0};
        bool alive{false};

        T& get()
        {
            return *std::launder(reinterpret_cast<T*>(storage));
        }

        const T& get() const
        {
            return *std::launder(reinterpret_cast<T*>(storage));
        }

        T* ptr()
        {
            return std::launder(reinterpret_cast<T*>(storage));
        }

        const T* ptr() const noexcept
        {
            return std::launder(reinterpret_cast<const T*>(storage));
        }
    };

    std::deque<Slot> slots;
    std::vector<size_t> free;
    std::unordered_map<uint64_t, Handle> keyIndex;
    uint64_t autoKeyCounter{0};

    uint64_t makeAutoKey() noexcept
    {
        return ++autoKeyCounter;
    }

    void trimTail()
    {
        while (!slots.empty() && !slots.back().alive)
        {
            auto idx = slots.size() - 1;
            for (size_t i = 0; i < free.size(); ++i)
            {
                if (free[i] == idx)
                {
                    free[i] = free.back();
                    free.pop_back();
                    break;
                }
            }
            slots.pop_back();
        }
    }

    bool handleValid(const Handle& h) const noexcept
    {
        return h.index < slots.size() &&
               slots[h.index].alive &&
               slots[h.index].generation == h.generation;
    }

public:
    Bucket() = default;
    Bucket(const Bucket&) = delete;
    Bucket& operator=(const Bucket&) = delete;

    template <typename... Args>
    Handle create(uint64_t key, Args&&... args)
    {
        auto it = keyIndex.find(key);
        assert(it == keyIndex.end());

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

        s.key = key;
        new (s.storage) T(std::forward<Args>(args)...);
        s.alive = true;
        s.refCount = 0;

        Handle h{index, s.generation};
        keyIndex.emplace(key, h);
        return h;
    }

    template <typename... Args>
    Handle createAuto(Args&&... args)
    {
        return create(makeAutoKey(), std::forward<Args>(args)...);
    }

    uint64_t slotKey(const Handle& h) const noexcept
    {
        assert(handleValid(h));
        return slots[h.index].key;
    }

    bool find(uint64_t key, Handle& out) const noexcept
    {
        auto it = keyIndex.find(key);
        if (it == keyIndex.end())
            return false;

        out = it->second;
        return handleValid(out);
    }

    T& get(const Handle& h) noexcept
    {
        assert(handleValid(h));
        return slots[h.index].get();
    }

    const T& get(const Handle& h) const
    {
        assert(handleValid(h));
        return slots[h.index].get();
    }

    void addRef(const Handle& h) noexcept
    {
        assert(handleValid(h));
        ++slots[h.index].refCount;
    }

    void releaseRef(const Handle& h) noexcept
    {
        if (!handleValid(h))
            return;

        Slot& s = slots[h.index];
        assert(s.refCount > 0);
        --s.refCount;

        if (s.refCount == 0)
        {
            // Destroy object and free slot
            s.ptr()->~T();
            s.alive = false;
            ++s.generation;

            // Remove key mapping
            keyIndex.erase(s.key);
            s.key = uint64_t{};

            free.push_back(h.index);
            trimTail();
        }
    }

    bool alive(const Handle& h) const noexcept
    {
        return handleValid(h);
    }
};
}

#endif // REGISTRY_BUCKET_HPP
*/
