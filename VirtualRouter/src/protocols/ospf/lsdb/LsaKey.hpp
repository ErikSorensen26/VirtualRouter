// LsaKey.hpp

#ifndef OSPF_LSA_KEY_HPP
#define OSPF_LSA_KEY_HPP

#include <cstdint>
#include <functional>

namespace OSPF
{
enum class OspfVersion : uint8_t
{
    V2 = 2,
    V3 = 3
};

enum class LsaScope : uint8_t
{
    LINK = 0,
    AREA = 1,
    AS = 2
};

using RouterId = uint32_t;
using AreaId = uint32_t;
using InstanceId = uint32_t;
using LinkScopeId = uint32_t;

struct LsaKey final
{
    OspfVersion version;
    InstanceId instanceID;

    LsaScope scope;
    AreaId areaID;
    LinkScopeId linkScopeID;

    uint16_t lsaType;
    uint32_t linkStateID;
    RouterId advertisingRouter;

    bool operator==(const LsaKey& o) const noexcept
    {
        return version == o.version &&
               instanceID == o.instanceID &&
               scope == o.scope &&
               areaID == o.areaID &&
               linkScopeID == o.linkScopeID &&
               lsaType == o.lsaType &&
               linkStateID == o.linkStateID &&
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
        x ^= static_cast<uint64_t>(k.version) << 60;
        x ^= static_cast<uint64_t>(k.instanceID) << 52;
        x ^= static_cast<uint64_t>(k.scope) << 48;
        x ^= static_cast<uint64_t>(k.lsaType) << 32;
        x ^= static_cast<uint64_t>(k.linkStateID);
        x ^= static_cast<uint64_t>(k.advertisingRouter) << 1;
        x ^= static_cast<uint64_t>(k.areaID);
        x ^= static_cast<uint64_t>(k.linkScopeID);
        return std::hash<uint64_t>{}(x);
    }
};
}

#endif // OSPF_LSA_KEY_HPP
