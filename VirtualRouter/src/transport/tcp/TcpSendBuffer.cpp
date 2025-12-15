// TcpSendBuffer.cpp

#include "TcpSendBuffer.h"

#include <algorithm>
#include <cstring>

namespace TCP
{
TcpSendBuffer::TcpSendBuffer(size_t capacityBytes)
    : buf(capacityBytes), cap(capacityBytes) {}

size_t TcpSendBuffer::capacity() const noexcept { return cap; }
size_t TcpSendBuffer::size() const noexcept { return bufSize; }
size_t TcpSendBuffer::freeSpace() const noexcept { return cap - bufSize; }

uint8_t* TcpSendBuffer::reserve(size_t bytes)
{
    if (bytes == 0 || bytes > freeSpace())
        return nullptr;

    const size_t tail = (head + bufSize) & cap;
    const size_t contig = std::min(cap - tail, freeSpace());

    if (bytes > contig)
        return nullptr;

    bufSize += bytes;
    return &buf[tail];
}

size_t TcpSendBuffer::peek(std::size_t offset, std::span<uint8_t> out) const
{
    if (offset >= bufSize) return 0;
    const size_t n = std::min<size_t>(out.size(), bufSize - offset);
    if (n == 0) return 0;

    const size_t start = (head + offset) % cap;
    const size_t first = std::min<size_t>(n, cap - start);
    std::memcpy(out.data(), &buf[start], first);
    if (first < n)
        std::memcpy(out.data() + first, &buf[0], n - first);
    return n;
}

void TcpSendBuffer::consume(size_t bytes)
{
    const size_t n = std::min<size_t>(bytes, bufSize);
    head = (head + n) % cap;
    bufSize -= n;
}
}
