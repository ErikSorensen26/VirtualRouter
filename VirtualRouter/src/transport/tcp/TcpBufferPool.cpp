// TcpBufferPool.cpp

#include "TcpBufferPool.h"
#include "TcpBuffer.h"
#include "TcpTypes.hpp"

namespace TCP
{
TcpBufferPool::TcpBufferPool(PoolConfig& c) noexcept : cfg(c)
{
    if (cfg.blockSize < 256) cfg.blockSize = 256;
    if (cfg.slabBlocks == 0) cfg.slabBlocks = 1;
}

TcpBufferPool::~TcpBufferPool()
{
    for (void* p : slabs)
        ::operator delete(p, std::align_val_t{alignof(std::max_align_t)});
    slabs.clear();
}

TcpBuffer TcpBufferPool::acquire() noexcept
{
    TcpBuffer b(this);
    return b;
}

size_t TcpBufferPool::blockSize() const noexcept { return cfg.blockSize; }

TcpBufferPool::Block* TcpBufferPool::atomicPop(std::atomic<TcpBufferPool::Block*>& head) noexcept
{
    TcpBufferPool::Block* h = head.load(std::memory_order_acquire);
    while (h)
    {
        TcpBufferPool::Block* next = h->next;
        if (head.compare_exchange_weak(h, next, std::memory_order_acq_rel, std::memory_order_acquire))
            return h;
    }
    return nullptr;
}

void TcpBufferPool::atomicPush(std::atomic<TcpBufferPool::Block*>& head, TcpBufferPool::Block* b) noexcept
{
    TcpBufferPool::Block* h = head.load(std::memory_order_relaxed);
    do
    {
        b->next = h;
    }
    while (!head.compare_exchange_weak(h, b, std::memory_order_release, std::memory_order_relaxed));
}

void TcpBufferPool::grow(size_t blocks) noexcept
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

TcpBufferPool::Block* TcpBufferPool::pop() noexcept
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

void TcpBufferPool::addRef(Block* b) noexcept
{
    if (!b) return;
    b->refs.fetch_add(1, std::memory_order_acq_rel);
}

void TcpBufferPool::release(Block* b) noexcept
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
