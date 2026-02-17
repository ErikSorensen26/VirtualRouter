// TcpBuffer.cpp

#include <cstring>
#include <utility>

#include "TcpBuffer.h"
#include "TcpBufferPool.h"

namespace TCP
{
TcpBufferPool::Block* TcpBuffer::newBlock() noexcept
{
    if (!pool) return nullptr;
    return pool->pop();
}

void TcpBuffer::appendNewBlock() noexcept
{
    auto* b = newBlock();
    if (!b) return;

    if (!head)
    {
        head = tail = b;
    }
    else
    {
        tail->next = b;
        tail = b;
    }

    capacityBytes += pool->blockSize();
}

void TcpBuffer::ensureTail() noexcept
{
    if (!tail)
        appendNewBlock();
}

std::span<uint8_t> TcpBuffer::reserveSpan(size_t minBytes) noexcept
{
    if (!pool) return {};

    const size_t blockSize = pool->blockSize();

    if (!tail) appendNewBlock();
    if (!tail) return {};

    size_t slack = blockSize - tail->wr;

    if (slack < minBytes)
    {
        appendNewBlock();
        if (!tail) return {};
        slack = blockSize - tail->wr;
    }

    return std::span<uint8_t>(
        tail->data() + tail->wr,
        slack
    );
}

void TcpBuffer::commit(size_t n) noexcept
{
    if (!pool || !tail || n == 0) return;

    const size_t slack = pool->blockSize() - tail->wr;
    if (n > slack)
        n = slack; // hard clamp (no exceptions)

    tail->wr += static_cast<uint32_t>(n);
    sizeBytes += n;
}

std::span<const uint8_t> TcpBuffer::peek(size_t offset) const noexcept
{
    if (!head) return {};
    if (offset >= sizeBytes) return {};

    size_t remaining = offset;
    
    for (TcpBufferPool::Block* b = head; b; b = b->next)
    {
        size_t readable = b->wr - b->rd;

        if (remaining < readable)
        {
            const uint8_t* ptr = b->data() + b->rd + remaining;
            size_t len = readable - remaining;

            return std::span<const uint8_t>(ptr, len);
        }

        remaining -= readable;
    }

    return {};
}

size_t TcpBuffer::consume(size_t n) noexcept
{
    if (!pool || !head || n == 0 || sizeBytes == 0) return 0;

    size_t remaining = std::min(n, sizeBytes);
    size_t consumed = 0;

    while (head && remaining > 0)
    {
        size_t avail = static_cast<size_t>(head->wr - head->rd);
        if (avail == 0)
        {
            TcpBufferPool::Block* dead = head;
            head = head->next;
            if (!head) tail = nullptr;
            capacityBytes -= pool->blockSize();
            pool->release(dead);
            continue;
        }

        size_t take = std::min(avail, remaining);
        head->rd += static_cast<uint32_t>(take);
        remaining -= take;
        consumed += take;
        sizeBytes -= take;

        if (head->rd == head->wr)
        {
            TcpBufferPool::Block* dead = head;
            head = head->next;
            if (!head) tail = nullptr;
            capacityBytes -= pool->blockSize();
            pool->release(dead);
        }
    }

    return consumed;
}

void TcpBuffer::spliceFrom(TcpBuffer* other) noexcept
{
    if (!other || other == this) return;
    if (!other->head) return;

    // must share same pool to avoid cross-pool block ownership bugs
    if (pool != other->pool)
        return;

    if (!head)
    {
        head = other->head;
        tail = other->tail;
    }
    else
    {
        tail->next = other->head;
        tail = other->tail;
    }

    sizeBytes += other->sizeBytes;
    capacityBytes += other->capacityBytes;

    other->head = nullptr;
    other->tail = nullptr;
    other->sizeBytes = 0;
    other->capacityBytes = 0;
}

void TcpBuffer::reset() noexcept
{
    if (!pool)
    {
        head = tail = nullptr;
        sizeBytes = 0;
        capacityBytes = 0;
        return;
    }

    auto* b = head;
    while (b)
    {
        auto* next = b->next;
        pool->release(b);
        b = next;
    }

    head = nullptr;
    tail = nullptr;
    sizeBytes = 0;
    capacityBytes = 0;
}
}
