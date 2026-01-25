// RegistryBucket.hpp

#ifndef REGISTRY_BUCKET_HPP
#define REGISTRY_BUCKET_HPP

#include <deque>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <new>
#include <cassert>
#include <algorithm>
#include <unordered_map>
#include <utility>

namespace Config
{
template <typename T>
concept Hashable =
    requires(const T& v)
    {
        { std::hash<T>{}(v) } -> std::convertible_to<size_t>;
    };

template <typename T>
class Bucket
{
    static_assert(Hashable<typename T::keyType>, "Key type must be hashable");
public:
    using keyType = T::keyType;

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
        keyType key{};

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
    std::unordered_map<keyType, Handle> keyIndex;

    void trimTail()
    {
        while (!slots.empty())
        {
            Slot& s = slots.back();
            if (s.alive)
                break;

            auto idx = slots.size() - 1;
            auto it = std::find(free.begin(), free.end(), idx);
            if (it != free.end())
                free.erase(it);

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
    Handle create(keyType key, Args&&... args)
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

    bool find(keyType key, Handle& out) const noexcept
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
            s.key = keyType{};

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
