// OspfTopologyTypes.hpp

#ifndef OSPF_TOPOLOGY_TYPES_HPP
#define OSPF_TOPOLOGY_TYPES_HPP

#include <IPAddress.hpp>
#include <optional>

namespace OSPF
{
enum class OspfRouteType : uint8_t
{
    INTRA_AREA = 0,
    INTER_AREA = 1,
    EXTERNAL = 2,
    NSSA = 3
};

struct OspfNextHop
{
    uint32_t interfaceId{};
    IPAddress nextHop{};

    bool operator==(const OspfNextHop& o) const
    {
        return interfaceId == o.interfaceId &&
               nextHop == o.nextHop;
    }

    bool operator<(const OspfNextHop& o) const
    {
        return std::tie(interfaceId, nextHop)
             < std::tie(o.interfaceId, o.nextHop);
    }
};

struct OspfRouter
{
    uint32_t rid;
    uint64_t cost;
    std::vector<OspfNextHop> nextHops;
};

struct RouterReach
{
    uint64_t cost;
    std::vector<OspfNextHop> nextHops;
};

struct OspfPath
{
    OspfRouteType type{};
    std::optional<uint32_t> area{};
    uint64_t cost{};
    uint8_t adminDistance{};
    uint8_t options{};
    bool discard{false};
    bool suppressed{false};
    std::vector<OspfNextHop> nextHops{};

    friend bool operator==(const OspfPath& a, const OspfPath& b)
    {
        return a.options == b.options && a.area == b.area &&
               a.type == b.type && a.cost == b.cost &&
               a.adminDistance == b.adminDistance &&
               a.nextHops == b.nextHops;
    }
};

struct OspfRoute
{
    IPPrefix prefix{};
    uint8_t options{};
    uint64_t cost{};
    uint8_t adminDistance{};
    OspfRouteType type{};
    std::optional<uint32_t> area{};
    bool suppressed{false};
    std::vector<OspfPath> paths{};
};

struct OspfRouteChange
{
    IPPrefix prefix{};
    uint8_t options{};
    uint64_t cost{};
    bool isRemoval{false};
};
}

#endif // OSPF_TOPOLOGY_TYPES_HPP
