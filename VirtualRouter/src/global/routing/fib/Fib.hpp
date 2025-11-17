// Fib.hpp

#ifndef FIB_HPP
#define FIB_HPP

#include <cstdint>
#include <type_traits>
#include <atomic>
#include <RCU.hpp>
#include "RibEntry.hpp"

template <typename AddrType>
class Fib
{
    static_assert(std::is_unsigned_v<AddrType>, "AddrType must be unsigned integral");

    struct Node
    {
        AddrType prefix;
        uint8_t length;
        std::atomic<RibEntry<AddrType>*> entry{nullptr};
        std::atomic<Node*> left{nullptr};
        std::atomic<Node*> right{nullptr};
        Node(AddrType pfx, uint8_t len)
            : prefix(pfx), length(len) {}
    };

    std::atomic<Node*> root;

public:
    Fib() = default;
    ~Fib() { clear(); }

    RibEntry<AddrType>* lookup(AddrType addr, RCU::ThreadEpoch* te) const noexcept
    {
        RCU::Guard g(te);
        Node* n = root.load(std::memory_order_acquire);
        RibEntry<AddrType>* last = nullptr;

        while (n)
        {
            if (match(addr, n->prefix, n->length))
                last = const_cast<RibEntry<AddrType>*>(n->entry.load(std::memory_order_acquire));
            n = getBit(addr, n->length) ? n->right.load() : n->left.load();
        }
        return last;
    }

    std::atomic<RibEntry<AddrType>*>* getEntry(AddrType prefix, uint8_t length) noexcept
    {
        Node* oldRoot = root.load(std::memory_order_acquire);
        Node* newRoot = clonePath(oldRoot, prefix, length);
        Node* node = insertRecursive(newRoot, prefix, length);
        root.store(newRoot, std::memory_order_release);

        if (oldRoot)
            RCU::retire([oldRoot]{ destroyRecursive(oldRoot); });
        return &(node->entry);
    }

    void clearEntry(AddrType& prefix, uint8_t length) noexcept
    {
        Node* oldRoot = root.load(std::memory_order_acquire);
        Node* newRoot = clonePath(oldRoot, prefix, length);
        clearRecursive(newRoot, prefix, length);
        root.store(newRoot, std::memory_order_release);

        if (oldRoot)
            RCU::retire([oldRoot]{ destroyRecursive(oldRoot); });
    }

    void clear() noexcept
    {
        Node* n = root.exchange(nullptr, std::memory_order_acq_rel);
        if (n) destroyRecursive(n);
    }

private:
    static constexpr uint8_t bitWidth() noexcept { return sizeof(AddrType) * 8; }

    static constexpr bool getBit(AddrType value, uint8_t idx) noexcept
    {
        if (idx >= bitWidth()) return false;
        AddrType mask = AddrType(1) << (bitWidth() - 1 - idx);
        return (value & mask) != 0;
    }

    static constexpr AddrType mask(AddrType prefix, uint8_t len) noexcept
    {
        if (len == 0) return AddrType(0);
        if (len >= bitWidth()) return prefix;
        AddrType m = (~AddrType(0)) << (bitWidth() - len);
        return prefix & m;
    }

    static constexpr bool match(AddrType addr, AddrType prefix, uint8_t len) noexcept
    {
        return mask(addr, len) == mask(prefix, len);
    }

    static constexpr uint8_t commonPrefixLength(AddrType a, AddrType b) noexcept
    {
        AddrType diff = a ^ b;
        if (!diff) return bitWidth();
        uint8_t n = 0;
        while (!(diff & (AddrType(1) << (bitWidth() - 1 - n)))) ++n;
        return n;
    }

    static Node* clonePath(Node* src, AddrType prefix, uint8_t len)
    {
        if (!src) return nullptr;
        Node* newNode = new Node(src->prefix, src->length);
        newNode->entry = src->entry.load(std::memory_order_acquire);
        
        bool goRight = getBit(prefix, src->length);
        Node* next = (goRight ? src->right.load() : src->left.load());
        Node* clonedChild = clonePath(next, prefix, len);

        if (goRight)
        {
            newNode->left.store(src->left.load());
            newNode->right.store(clonedChild);
        }
        else
        {
            newNode->left.store(clonedChild);
            newNode->right.store(src->right.load());
        }
        return newNode;
    }

    static Node* insertRecursive(Node*& node, AddrType prefix, uint8_t length) noexcept
    {
        if (!node)
        {
            node = new Node(prefix, length);
            return node;
        }

        uint8_t common = commonPrefixLength(prefix, node->prefix);
        if (common >= length && common >= node->length)
        {
            if (node->length == length)
                return node;

            bool goRight = getBit(prefix, node->length);
            Node* child = goRight ? node->right.load(std::memory_order_acquire) : node->left.load(std::memory_order_acquire);
            Node* res = insertRecursive(child, prefix, length);
            if (goRight)
                node->right.store(child, std::memory_order_release);
            else
                node->left.store(child, std::memory_order_release);
            return res;
        }

        Node* newParent = new Node(mask(prefix, common), common);
        bool bitPrefix = getBit(prefix, common);
        bool bitNode = getBit(node->prefix, common);

        if (bitNode)
            newParent->right.store(node, std::memory_order_release);
        else
            newParent->left.store(node, std::memory_order_release);

        Node* newChild = new Node(prefix, length);
        if (bitPrefix)
            newParent->right.store(newChild, std::memory_order_release);
        else
            newParent->left.store(newChild, std::memory_order_release);
        
        node = newParent;
        return newChild;
    }

    static bool clearRecursive(Node*& node, AddrType prefix, uint8_t len)
    {
        if (!node) return false;

        if (node->length == len && mask(prefix, len) == node->prefix)
        {
            node->entry.store(nullptr, std::memory_order_release);
        }
        else
        {
            bool goRight = getBit(prefix, node->length);
            Node* child = goRight ? node->right.load(std::memory_order_acquire)
                                  : node->left.load(std::memory_order_acquire);

            if (!child) return false;

            bool deleted = clearRecursive(child, prefix, len);
            if (deleted)
            {
                if (goRight)
                    node->right.store(nullptr, std::memory_order_release);
                else
                    node->left.store(nullptr, std::memory_order_release);
            }
        }

        RibEntry<AddrType>* entry = node->entry.load(std::memory_order_acquire);
        Node* left = node->left.load(std::memory_order_acquire);
        Node* right = node->right.load(std::memory_order_acquire);

        if (!entry && !left && !right)
        {
            delete node;
            node = nullptr;
            return true;
        }

        if (!entry && left && !right)
        {
            Node* tmp = left;
            delete node;
            node = tmp;
            return false;
        }
        else if (!entry && right && !left)
        {
            Node* tmp = right;
            delete node;
            node = tmp;
            return false;
        }

        return false;
    }

    static void destroyRecursive(Node* n) noexcept
    {
        if (!n) return;
        destroyRecursive(n->left.load());
        destroyRecursive(n->right.load());
        delete n;
    }
};

#endif // FIB_HPP
