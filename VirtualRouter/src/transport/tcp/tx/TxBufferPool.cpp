// TxBufferPool.cpp

#include "TxBufferPool.h"
#include "TxBuffer.h"
#include "tcp/TcpTypes.hpp"

namespace TCP
{
TxBufferPool::TxBufferPool(PoolConfig& c) noexcept : cfg(c)
{
    if (cfg.blockSize < 256) cfg.blockSize = 256;
    if (cfg.slabBlocks == 0) cfg.slabBlocks = 1;
}

TxBufferPool::~TxBufferPool()
{
    for (void* p : slabs)
        ::operator delete(p, std::align_val_t{alignof(std::max_align_t)});
    slabs.clear();
}

TxBuffer TxBufferPool::acquire() noexcept
{
    TxBuffer b(this);
    return b;
}

size_t TxBufferPool::blockSize() const noexcept { return cfg.blockSize; }

TxBufferPool::Block* TxBufferPool::atomicPop(std::atomic<TxBufferPool::Block*>& head) noexcept
{
    TxBufferPool::Block* h = head.load(std::memory_order_acquire);
    while (h)
    {
        TxBufferPool::Block* next = h->next;
        if (head.compare_exchange_weak(h, next, std::memory_order_acq_rel, std::memory_order_acquire))
            return h;
    }
    return nullptr;
}

void TxBufferPool::atomicPush(std::atomic<TxBufferPool::Block*>& head, TxBufferPool::Block* b) noexcept
{
    TxBufferPool::Block* h = head.load(std::memory_order_relaxed);
    do
    {
        b->next = h;
    }
    while (!head.compare_exchange_weak(h, b, std::memory_order_release, std::memory_order_relaxed));
}

void TxBufferPool::grow(size_t blocks) noexcept
{
    if (blocks == 0) return;

    std::lock_guard<std::mutex> lk(growMtx);

    if (cfg.maxBlocks != 0)
    {
        size_t cur = totalBlocks.load(std::memory_order_relaxed);
        if (cur >= cfg.maxBlocks) return;
        blocks = std::min(blocks, cfg.maxBlocks - cur);
        if (blocks == 0) return;
    }

    const size_t stride = sizeof(Block) + cfg.blockSize;
    const size_t slabBytes = stride * blocks;

    void* slab = ::operator new(slabBytes, std::align_val_t{alignof(std::max_align_t)}, std::nothrow);
    if (!slab) return;

    slabs.push_back(slab);

    uint8_t* p = reinterpret_cast<uint8_t*>(slab);
    for (size_t i = 0; i < blocks; ++i)
    {
        auto* b = reinterpret_cast<Block*>(p + i * stride);
        b->refs.store(0, std::memory_order_relaxed);
        b->next = nullptr;
        b->rd = 0;
        b->wr = 0;
        atomicPush(freeList, b);
    }

    totalBlocks.fetch_add(blocks, std::memory_order_relaxed);
}

TxBufferPool::Block* TxBufferPool::pop() noexcept
{
    Block* b = atomicPop(freeList);
    if (!b)
    {
        grow(cfg.slabBlocks);
        b = atomicPop(freeList);
        if (!b) return nullptr;
    }

    b->refs.store(1, std::memory_order_release);
    b->next = nullptr;
    b->rd = 0;
    b->wr = 0;
    return atomicPop(freeList);
}

void TxBufferPool::addRef(Block* b) noexcept
{
    if (!b) return;
    b->refs.fetch_add(1, std::memory_order_acq_rel);
}

void TxBufferPool::release(Block* b) noexcept
{
    if (!b) return;

    uint32_t prev = b->refs.fetch_sub(1, std::memory_order_acq_rel);
    if (prev == 1)
    {
        b->rd = 0;
        b->wr = 0;
        b->next = nullptr;
        atomicPush(freeList, b);
    }
}
}
