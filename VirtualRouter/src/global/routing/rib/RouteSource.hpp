// RibSource.hpp

#ifndef RIB_SOURCE_HPP
#define RIB_SOURCE_HPP

#include <cstdint>

enum class RouteSource : uint8_t
{
    CONNECTED,
    STATIC,
    EIGRP_INTERNAL,
    EIGRP_EXTERNAL,
    OSPF_INTRA,
    OSPF_INTER,
    OSPF_EXTERNAL,
    OSPF_NSSA,
    BGP,
    RIP,
    UNKNOWN
};

#endif // RIB_SOURCE_HPP
