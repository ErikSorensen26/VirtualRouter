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

struct LsaAdvKey
{
    LsaAdvKey() = default;
    LsaAdvKey(uint16_t type, RouterId advRtr)
        : lsaType(type), advertisingRouter(advRtr) {}

    uint16_t lsaType{0};
    RouterId advertisingRouter;
};

struct LsaKey : LsaAdvKey
{
    LsaKey() = default;
    LsaKey(uint16_t type, uint32_t id, RouterId advRtr)
        : LsaAdvKey(type, advRtr), linkStateId(id) {}

    uint32_t linkStateId{0};
    
    bool operator==(const LsaAdvKey& o) const noexcept
    {
        return lsaType == o.lsaType &&
               advertisingRouter == o.advertisingRouter;
    }

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
struct hash<OSPF::LsaAdvKey>
{
    size_t operator()(const OSPF::LsaAdvKey& k) const noexcept
    {
        uint64_t x = 0;
        x ^= static_cast<uint64_t>(k.lsaType) << 32;
        x ^= static_cast<uint64_t>(k.advertisingRouter) << 1;
        return std::hash<uint64_t>{}(x);
    }
};

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
