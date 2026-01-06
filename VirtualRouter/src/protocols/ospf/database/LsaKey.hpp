// LsaKey.hpp

#ifndef OSPF_LSA_KEY_HPP
#define OSPF_LSA_KEY_HPP

#include <cstdint>
#include <functional>

#include <Ospfv2LSAHeader.hpp>
#include <Ospfv3LSAHeader.hpp>

namespace OSPF
{
using RouterId = uint32_t;
using AreaId = uint32_t;
using InstanceId = uint32_t;
using LinkStateId = uint32_t;

struct LsaKey
{
    LsaKey() = default;
    LsaKey(uint16_t type, uint32_t id, RouterId advRtr)
        : lsaType(type), linkStateId(id), advertisingRouter(advRtr) {}

    uint16_t lsaType{0};
    uint32_t linkStateId{0};
    RouterId advertisingRouter{0};

    bool operator==(const LsaKey& o) const noexcept
    {
        return lsaType == o.lsaType &&
               linkStateId == o.linkStateId &&
               advertisingRouter == o.advertisingRouter;
    }
};
}

namespace std
{
template <>
struct hash<OSPF::LsaKey>
{
    size_t operator()(const OSPF::LsaKey& k) const noexcept
    {
        uint64_t x = 0;
        x ^= static_cast<uint64_t>(k.lsaType) << 32;
        x ^= static_cast<uint64_t>(k.linkStateId);
        x ^= static_cast<uint64_t>(k.advertisingRouter) << 1;
        return std::hash<uint64_t>{}(x);
    }
};
}

#endif // OSPF_LSA_KEY_HPP
