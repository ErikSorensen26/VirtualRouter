// RibEntry.hpp

#ifndef RIB_ENTRY_HPP
#define RIB_ENTRY_HPP

#include "RouteSource.hpp"
#include <IPAddress.hpp>

template <typename AddrType>
struct RibEntry
{
    AddrType prefix;
    uint8_t length;
    AddrType nextHop;
    uint32_t iface;
    uint32_t processId;
    uint64_t metric;
    uint8_t adminDistance;
    RouteSource source;
    uint32_t weight;
    void* topInfo = nullptr;
};

#endif // RIB_ENTRY_HPP
