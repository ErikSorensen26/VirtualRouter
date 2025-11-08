// RibSource.hpp

#ifndef RIB_SOURCE_HPP
#define RIB_SOURCE_HPP

#include <cstdint>

enum class RouteSource : uint8_t
{
    CONNECTED,
    STATIC,
    EIGRP,
    OSPF,
    BGP,
    RIP,
    UNKNOWN
};

#endif // RIB_SOURCE_HPP
