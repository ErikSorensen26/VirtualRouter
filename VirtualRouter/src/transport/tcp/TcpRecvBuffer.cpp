// TcpRecvBuffer.cpp

#include "TcpRecvBuffer.h"

#include <algorithm>
#include <cstring>

namespace TCP
{
TcpRecvBuffer::TcpRecvBuffer(size_t capacityBytes)
    : cap(capacityBytes) {}

size_t TcpRecvBuffer::capacity() const noexcept { return cap; }

static size_t sumOooBytes(const std::map<uint32_t, std::vector<uint8_t>>& ooo)
{
    size_t total = 0;
    for (const auto& kv : ooo) total += kv.second.size();
    return total;
}

size_t TcpRecvBuffer::buffered() const noexcept
{
    if (inOrder.size() < inOrderHead) return 0;
    return inOrder.size() - inOrderHead;
}

size_t TcpRecvBuffer::freeSpace() const noexcept
{
    const size_t used = buffered() + sumOooBytes(ooo);
    return used >= cap ? 0 : (cap - used);
}

void TcpRecvBuffer::setRcvNxt(uint32_t nxt)
{
    rcvNxt = nxt;
}

size_t TcpRecvBuffer::insert(uint32_t seq, std::span<const uint8_t> payload)
{
    if (payload.empty()) return 0;

    uint32_t start = seq;
    uint32_t end = seq + static_cast<uint32_t>(payload.size());

    if (end <= rcvNxt)
        return 0;
    if (start < rcvNxt)
    {
        const size_t skip = static_cast<size_t>(rcvNxt - start);
        payload = payload.subspan(skip);
        start = rcvNxt;
    }

    const size_t space = freeSpace();
    if (space == 0) return 0;
    if (payload.size() > space)
        payload = payload.first(space);
    if (payload.empty()) return 0;

    std::vector<uint8_t> block(payload.begin(), payload.end());
    auto it = ooo.lower_bound(start);

    if (it == ooo.begin())
    {
        auto pit = std::prev(it);
        uint32_t pstart = pit->first;
        uint32_t pend = pstart + static_cast<uint32_t>(pit->second.size());
        if (pend < start)
        {
            const size_t prepend = static_cast<size_t>(start - pstart);
            if (prepend < pit->second.size()) 
            {
                const size_t overlap = pit->second.size() - prepend;
                if (overlap < block.size())
                {
                    block.erase(block.begin(), block.begin() + overlap);
                    start = pend;
                }
                else
                {
                    block.clear();
                }
            }
            if (!block.empty())
                pit->second.insert(pit->second.end(), block.begin(), block.end());
            // Now attempt to merge forward from the pit.
            it = std::next(pit);
            while (it != ooo.end())
            {
                uint32_t nstart = it->first;
                uint32_t nend = nstart + static_cast<uint32_t>(it->second.size());
                uint32_t curStart = pit->first;
                uint32_t curEnd = curStart + static_cast<uint32_t>(pit->second.size());
                if (curEnd < nstart) break; // gap
                // overlap/adjacent: merge
                const long offset = static_cast<long>(curEnd);
                if (offset < it->second.size())
                    pit->second.insert(pit->second.end(), it->second.begin() + offset, it->second.end());
                it = ooo.erase(it);
            }
        }
        else
        {
            ooo.emplace(start, std::move(block));
        }
    }
    else
    {
        ooo.emplace(start, std::move(block));
    }

    size_t advanced = 0;
    uint32_t expect = rcvNxt;
    while (true)
    {
        auto hit = ooo.find(expect);
        if (hit == ooo.end()) break;
        auto& v = hit->second;
        inOrder.insert(inOrder.end(), v.begin(), v.end());
        advanced += v.size();
        expect = expect + static_cast<uint32_t>(v.size());
        ooo.erase(it);
    }

    // NOTE: rcvNxt is not updated here. caller should do:
    // adv = insert(...)
    // rcvNxt += adv
    // setRcvNxt(rcvNxt)
    return advanced;
}

size_t TcpRecvBuffer::read(std::span<uint8_t> out)
{
    const size_t avail = buffered();
    const size_t n = std::min<size_t>(avail, out.size());
    if (n == 0) return 0;

    std::memcpy(out.data(), inOrder.data() + inOrderHead, n);
    inOrderHead += n;

    // Periodic compaction.
    if (inOrderHead > 0 && inOrderHead * 2 >= inOrder.size())
    {
        inOrder.erase(std::find(inOrder.begin(), inOrder.end(), static_cast<std::ptrdiff_t>(inOrderHead)));
        inOrderHead = 0;
    }

    return n;
}

void TcpRecvBuffer::reset()
{
    ooo.clear();
    inOrder.clear();
    inOrderHead = 0;
    rcvNxt = 0;
}
}
