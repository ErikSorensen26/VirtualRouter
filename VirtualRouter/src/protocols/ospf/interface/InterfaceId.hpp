// InterfaceId.hpp

#ifndef OSPF_INTERFACE_ID_HPP
#define OSPF_INTERFACE_ID_HPP

#include <functional>
#include <cstdint>

namespace OSPF
{
struct OspfInterfaceId
{
    OspfInterfaceId() = default;
    OspfInterfaceId(uint32_t id, uint32_t a)
        : interfaceId(id), area(a) {}

    const uint32_t interfaceId = 0;
    const uint32_t area = 0;
    
    bool operator==(const OspfInterfaceId& other) const {
        return interfaceId == other.interfaceId && area == other.area;
    }
};
}

namespace std
{
template <>
struct hash<OSPF::OspfInterfaceId>
{
    size_t operator()(const OSPF::OspfInterfaceId& k) const
    {
        return hash<uint32_t>{}(k.interfaceId) ^ (hash<uint32_t>{}(k.area));
    }
};
}

#endif // OSPF_INTERFACE_ID_HPP
