// NextHopWatcher.hpp

#ifndef NEXT_HOP_WATCHER_HPP
#define NEXT_HOP_WATCHER_HPP

#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>
#include <type_traits>
#include <unordered_map>

#include "fib/Fib.hpp"

template <typename Addr>
class NextHopWatcher
{
    static_assert(std::is_unsigned_v<Addr>, "Addr must be an unsigned integer");
    static constexpr uint8_t W = sizeof(Addr) * 8;

public:
    using Callback = void (*)(void*);
    using WatchId  = uint32_t; // 0 = invalid

private:
    struct Control
    {
        enum class State : uint8_t
        {
            ACTIVE,
            CANCELED,
            FIRED
        };

        WatchId id = 0;
        std::atomic<State> state{State::ACTIVE};

        explicit Control(WatchId watchId) noexcept
            : id(watchId)
        {}
    };

    struct CallbackNode
    {
        void* ctx = nullptr;
        Callback fn = nullptr;
        Control* control = nullptr;
        CallbackNode* next = nullptr;

        CallbackNode(void* c, Callback f, Control* ctl) noexcept
            : ctx(c), fn(f), control(ctl), next(nullptr)
        {}
    };

    struct Bucket
    {
        enum class State : uint8_t
        {
            EMPTY,
            ADDING,
            ACTIVE,
            FIRING
        };

        Addr hop{};
        std::atomic<CallbackNode*> callbacks{nullptr};
        std::atomic<State> state{State::EMPTY};

        explicit Bucket(Addr h) noexcept
            : hop(h)
        {}
    };

    struct TrieNode
    {
        std::atomic<TrieNode*> left{nullptr};
        std::atomic<TrieNode*> right{nullptr};
        std::atomic<Bucket*> bucket{nullptr};
    };

private:
    std::unordered_map<Addr, Bucket*> exactBuckets;
    std::unordered_map<WatchId, Control*> idMap;
    std::mutex createMtx;
    std::atomic<TrieNode*> root{nullptr};
    Fib<Addr>& fib;
    WatchId nextId = 1;

private:
    static constexpr Addr mask(Addr p, uint8_t l) noexcept
    {
        if (l == 0) return 0;
        if (l >= W) return p;
        return p & (~Addr(0) << (W - l));
    }

    static bool bitAt(Addr v, uint8_t i) noexcept
    {
        if (i >= W) return false;
        const uint8_t shift = W - 1 - i;
        return (v >> shift) & 1;
    }

    static void deleteCallbackList(CallbackNode* n) noexcept
    {
        while (n)
        {
            CallbackNode* next = n->next;
            delete n;
            n = next;
        }
    }

    static void destroyTrie(TrieNode* n) noexcept
    {
        if (!n) return;

        TrieNode* l = n->left.load(std::memory_order_relaxed);
        TrieNode* r = n->right.load(std::memory_order_relaxed);

        destroyTrie(l);
        destroyTrie(r);
        delete n;
    }

    TrieNode* ensureRootLocked()
    {
        TrieNode* r = root.load(std::memory_order_acquire);
        if (!r)
        {
            r = new TrieNode();
            root.store(r, std::memory_order_release);
        }
        return r;
    }

    TrieNode* ensureLeafLocked(Addr hop)
    {
        TrieNode* n = ensureRootLocked();

        for (uint8_t i = 0; i < W; ++i)
        {
            const bool dir = bitAt(hop, i);

            if (dir)
            {
                TrieNode* child = n->right.load(std::memory_order_acquire);
                if (!child)
                {
                    child = new TrieNode();
                    n->right.store(child, std::memory_order_release);
                }
                n = child;
            }
            else
            {
                TrieNode* child = n->left.load(std::memory_order_acquire);
                if (!child)
                {
                    child = new TrieNode();
                    n->left.store(child, std::memory_order_release);
                }
                n = child;
            }
        }

        return n;
    }

    Bucket* getOrCreateBucketLocked(Addr hop)
    {
        auto it = exactBuckets.find(hop);
        if (it != exactBuckets.end())
            return it->second;

        Bucket* b = new Bucket(hop);
        TrieNode* leaf = ensureLeafLocked(hop);
        leaf->bucket.store(b, std::memory_order_release);
        exactBuckets.emplace(hop, b);
        return b;
    }

    WatchId allocateIdLocked() noexcept
    {
        if (idMap.size() >= static_cast<size_t>(std::numeric_limits<WatchId>::max() - 1))
            return 0;

        while (true)
        {
            WatchId id = nextId++;
            if (id == 0)
                continue;

            if (idMap.find(id) == idMap.end())
                return id;
        }
    }

    TrieNode* findPrefixSubtree(Addr prefix, uint8_t length) const noexcept
    {
        prefix = mask(prefix, length);

        TrieNode* n = root.load(std::memory_order_acquire);
        if (!n)
            return nullptr;

        for (uint8_t i = 0; i < length; ++i)
        {
            const bool dir = bitAt(prefix, i);
            n = dir ? n->right.load(std::memory_order_acquire)
                    : n->left.load(std::memory_order_acquire);

            if (!n)
                return nullptr;
        }

        return n;
    }

    template <typename Fn>
    static void forEachBucketInSubtree(TrieNode* n, Fn&& fn) noexcept
    {
        if (!n)
            return;

        if (Bucket* b = n->bucket.load(std::memory_order_acquire))
            fn(b);

        TrieNode* l = n->left.load(std::memory_order_acquire);
        TrieNode* r = n->right.load(std::memory_order_acquire);

        if (l) forEachBucketInSubtree(l, fn);
        if (r) forEachBucketInSubtree(r, fn);
    }

    void runAndDeleteCallbacks(Bucket* b) noexcept
    {
        CallbackNode* list = b->callbacks.exchange(nullptr, std::memory_order_acq_rel);

        while (list)
        {
            CallbackNode* next = list->next;

            typename Control::State prev =
                list->control->state.exchange(Control::State::FIRED,
                                              std::memory_order_acq_rel);

            if (prev == Control::State::ACTIVE && list->fn)
                list->fn(list->ctx);

            {
                std::lock_guard<std::mutex> lock(createMtx);
                idMap.erase(list->control->id);
            }
            delete list->control;
            delete list;
            list = next;
        }

        b->state.store(Bucket::State::EMPTY, std::memory_order_release);
    }

    bool tryFireBucketIfDead(Bucket* b) noexcept
    {
        if (!b)
            return false;

        using State = typename Bucket::State;

        if (b->state.load(std::memory_order_acquire) != State::ACTIVE)
            return false;

        if (fib.lookup(b->hop) != nullptr)
            return false;

        State expected = State::ACTIVE;
        if (!b->state.compare_exchange_strong(expected,
                                              State::FIRING,
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire))
        {
            return false;
        }

        runAndDeleteCallbacks(b);
        return true;
    }

    void forceFireBucket(Bucket* b) noexcept
    {
        if (!b)
            return;

        using State = typename Bucket::State;

        while (true)
        {
            State st = b->state.load(std::memory_order_acquire);

            if (st == State::EMPTY)
                return;

            if (st == State::FIRING || st == State::ADDING)
            {
                std::this_thread::yield();
                continue;
            }

            State expected = State::ACTIVE;
            if (b->state.compare_exchange_strong(expected,
                                                 State::FIRING,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire))
            {
                runAndDeleteCallbacks(b);
                return;
            }
        }
    }

public:
    explicit NextHopWatcher(Fib<Addr>& f)
        : fib(f)
    {}

    NextHopWatcher(const NextHopWatcher&) = delete;
    NextHopWatcher& operator=(const NextHopWatcher&) = delete;

    ~NextHopWatcher()
    {
        for (auto& [_, b] : exactBuckets)
        {
            CallbackNode* list = b->callbacks.exchange(nullptr, std::memory_order_acq_rel);
            while (list)
            {
                CallbackNode* next = list->next;
                if (list->control->state.load(std::memory_order_relaxed) ==
                        Control::State::CANCELED)
                    delete list->control;
                delete list;
                list = next;
            }
            delete b;
        }
        exactBuckets.clear();

        // Free Controls for watches that are still active (bucket never fired).
        for (auto& [_, ctl] : idMap)
            delete ctl;
        idMap.clear();

        TrieNode* oldRoot = root.exchange(nullptr, std::memory_order_acq_rel);
        destroyTrie(oldRoot);
    }

    WatchId add(Addr hop, void* ctx, Callback fn)
    {
        if (!fn)
            return 0;

        Bucket* b = nullptr;
        Control* ctl = nullptr;
        CallbackNode* node = nullptr;

        {
            std::lock_guard<std::mutex> lock(createMtx);

            b = getOrCreateBucketLocked(hop);

            WatchId id = allocateIdLocked();
            if (id == 0)
                return 0;

            ctl = new Control(id);
            idMap.emplace(id, ctl);

            node = new CallbackNode(ctx, fn, ctl);
        }

        using State = typename Bucket::State;

        while (true)
        {
            State st = b->state.load(std::memory_order_acquire);

            if (st == State::FIRING)
            {
                std::this_thread::yield();
                continue;
            }

            State expected = st;
            if ((st == State::EMPTY || st == State::ACTIVE) &&
                b->state.compare_exchange_weak(expected,
                                               State::ADDING,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire))
            {
                break;
            }
        }

        CallbackNode* head;
        do
        {
            head = b->callbacks.load(std::memory_order_acquire);
            node->next = head;
        }
        while (!b->callbacks.compare_exchange_weak(head,
                                                   node,
                                                   std::memory_order_release,
                                                   std::memory_order_acquire));

        b->state.store(State::ACTIVE, std::memory_order_release);

        tryFireBucketIfDead(b);
        return ctl->id;
    }

    bool remove(WatchId id) noexcept
    {
        if (id == 0)
            return false;

        std::lock_guard<std::mutex> lock(createMtx);

        auto it = idMap.find(id);
        if (it == idMap.end())
            return false;

        Control* ctl = it->second;

        typename Control::State expected = Control::State::ACTIVE;
        bool ok = ctl->state.compare_exchange_strong(expected,
                                                     Control::State::CANCELED,
                                                     std::memory_order_acq_rel,
                                                     std::memory_order_acquire);
        if (ok)
            idMap.erase(it);  // reclaim id; ctl freed by runAndDeleteCallbacks on bucket fire

        return ok;
    }

    void announcePrefixRemoved(Addr prefix, uint8_t length) noexcept
    {
        TrieNode* start = findPrefixSubtree(prefix, length);
        if (!start)
            return;

        forEachBucketInSubtree(start, [&](Bucket* b)
        {
            tryFireBucketIfDead(b);
        });
    }

    void announceAllGone() noexcept
    {
        TrieNode* start = root.load(std::memory_order_acquire);
        if (!start)
            return;

        forEachBucketInSubtree(start, [&](Bucket* b)
        {
            forceFireBucket(b);
        });
    }
};

#endif // NEXT_HOP_WATCHER_HPP
