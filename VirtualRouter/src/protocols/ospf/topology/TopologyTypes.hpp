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

    bool operator==(const OspfPath& other)
    {
        return options == other.options &&
               type == other.type &&
               area == other.area &&
               cost == other.cost &&
               adminDistance == other.adminDistance;
    }

    bool operator==(const OspfRoute& other)
    {
        return type == other.type &&
               cost == other.cost &&
               options == other.options &&
               adminDistance == other.adminDistance &&
               area == other.area &&
               suppressed == other.suppressed;
    }
};

struct OspfRouteChange
{
    IPPrefix prefix{};
    uint8_t options{};
    uint64_t cost{};
    bool isRemoval{false};
};

struct ExternalOriginateContext
{
    uint32_t lsId;
    IPPrefix prefix;
    uint32_t metric;
    uint32_t tag;
    std::optional<IPAddress> nextHop;
    bool metricIsE2; // false = E1, true = E2
};
}

#endif // OSPF_TOPOLOGY_TYPES_HPP
