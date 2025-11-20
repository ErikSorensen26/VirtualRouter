// Fib.hpp
#pragma once
#include <atomic>
#include <cstdint>
#include <type_traits>
#include <RCU.hpp>
#include "RibEntry.hpp"

template<typename Addr>
class Fib
{
    static_assert(std::is_unsigned_v<Addr>);
    static constexpr uint8_t W = sizeof(Addr)*8;

    struct Node
    {
        Addr prefix;
        uint8_t length;
        uint8_t bit;
        std::atomic<Node*> left{nullptr};
        std::atomic<Node*> right{nullptr};
        std::atomic<RibEntry<Addr>*>* entry{nullptr};

        Node(Addr p, uint8_t l, uint8_t b, std::atomic<RibEntry<Addr>*>* e)
            : prefix(p), length(l), bit(b), entry(e) {}
    };

    std::atomic<Node*> root{nullptr};

    static constexpr Addr mask(Addr p, uint8_t l)
    {
        if (l == 0) return 0;
        if (l >= W) return p;
        return p & (~Addr(0) << (W - l));
    }

    static uint8_t diffbit(Addr a, Addr b)
    {
        Addr x = a ^ b;
        if (!x) return W;
        for (uint8_t i = 0; i < W; ++i)
        {
            uint8_t bitIndex = W - 1 - i;
            if ((x >> bitIndex) & 1)
                return i;
        }
        return W;
    }

    static bool bitAt(Addr v, uint8_t i)
    {
        if (i >= W) return false;
        uint8_t shift = W - 1 - i;
        return (v >> shift) & 1;
    }

    static void destroy(Node* n)
    {
        if (!n) return;
        delete n;
    }

public:
    RibEntry<Addr>* lookup(Addr a) const
    {
        Node* n = root.load(std::memory_order_acquire);
        if (!n) return nullptr;

        RibEntry<Addr>* best = nullptr;

        while (n)
        {
            RibEntry<Addr>* e = n->entry->load(std::memory_order_acquire);

            if (e && mask(a, n->length) == n->prefix)
                best = e;
            
            bool dir = bitAt(a, n->bit);
            n = dir ? n->right.load(std::memory_order_acquire)
                    : n->left.load(std::memory_order_acquire);
        }
        return best;
    }

private:
    static Node* insertRec(Node* n, Addr pfx, uint8_t len, std::atomic<RibEntry<Addr>*>* ribEntry)
    {
        if (!n) return new Node(pfx, len, W, ribEntry);

        if (n->prefix == pfx && n->length == len)
        {
            return n;
        }

        uint8_t d = diffbit(n->prefix, pfx);

        // need to insert above this node
        if (d < n->bit)
        {
            Node* parent = new Node(pfx, len, d, ribEntry);
            bool dir = bitAt(pfx, d);

            if (dir)
            {
                parent->left.store(n, std::memory_order_relaxed);
            }
            else
            {
                parent->right.store(n, std::memory_order_relaxed);
            }

            return parent;
        }

        bool dir = bitAt(pfx, n->bit);
        std::atomic<Node*>* childPtr = dir ? &n->right : &n->left;
        Node* child = childPtr->load(std::memory_order_acquire);

        if (!child)
        {
            Node* leaf = new Node(pfx, len, W, ribEntry);
            childPtr->store(leaf, std::memory_order_release);
            return n;
        }

        Node* newChild = insertRec(child, pfx, len, ribEntry);
        childPtr->store(newChild, std::memory_order_release);
        return n;
    }

    // <deleted, attampt deletion>
    enum class EraseState : uint8_t { KEEP, REMOVE, NOT_FOUND };
    EraseState eraseRec(Node* n, Addr pfx, uint8_t len)
    {
        if (!n) return EraseState::NOT_FOUND;

        pfx = mask(pfx, len);

        if (n->prefix == pfx && n->length == len)
        {
            if (n->left.load(std::memory_order_relaxed) == nullptr &&
                n->right.load(std::memory_order_relaxed) == nullptr)
            {
                return EraseState::REMOVE;
            }

            return EraseState::KEEP;
        }

        bool dir = bitAt(pfx, n->bit);
        std::atomic<Node*>* childPtr = dir ? &n->right : &n->left;
        Node* child = childPtr->load(std::memory_order_relaxed);
        
        if (!child)
            return EraseState::NOT_FOUND;

        EraseState st = eraseRec(child, pfx, len);

        if (st == EraseState::REMOVE)
        {
            Node* removed = childPtr->exchange(nullptr, std::memory_order_relaxed);
            if (removed)
                RCU::retire([removed]{ destroy(removed); });
            return EraseState::KEEP; // Completed deletion
        }

        return st;
    }

public:
    bool insert(Addr pfx, uint8_t len, std::atomic<RibEntry<Addr>*>* ribEntry)
    {
        pfx = mask(pfx, len);

        Node* oldRoot = root.load(std::memory_order_acquire);
        Node* newRoot = insertRec(oldRoot, pfx, len, ribEntry);

        if (newRoot != oldRoot || !oldRoot)
            root.store(newRoot, std::memory_order_release);
        return true;
    }

    bool erase(Addr p, uint8_t l)
    {
        p = mask(p, l);

        Node* r = root.load(std::memory_order_acquire);
        if (!r) return false;

        EraseState st = eraseRec(r, p, l);

        if (st == EraseState::REMOVE && r->prefix == p && r->length == l)
        {
            Node* old = root.exchange(nullptr, std::memory_order_acq_rel);
            if (old) RCU::retire([old]{ destroy(old); });
            return true;
        }

        return st != EraseState::NOT_FOUND;
    }

    void clear()
    {
        Node* old = root.exchange(nullptr, std::memory_order_acq_rel);
        if (old) RCU::retire([old]{ destroy(old); });
    }
};
