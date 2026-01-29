// OspfTypes.hpp

#ifndef OSPF_TYPES_HPP
#define OSPF_TYPES_HPP

#include <AddressFamily.hpp>
#include <optional>
#include <IPAddress.hpp>
#include <shared_mutex>
#include <map>

#include <Ospfv2LSAHeader.hpp>
#include <Ospfv3LSAHeader.hpp>

static constexpr uint16_t OSPF_MAX_AGE = 3600;
static constexpr uint16_t OSPF_REFRESH_AGE = 1800;
static constexpr uint16_t OSPF_MAX_DIFF = 900;

static constexpr uint16_t OSPF_HELLO_TIME = 10;
static constexpr uint16_t OSPF_MU_HELLO_TIME = 30;

/*namespace OSPF
{
struct AreaConfigs
{
    std::shared_mutex areaMu;

    struct Range
    {
        IPPrefix range;
        bool advertise = true;
        uint32_t cost;
    };
    std::vector<Range> ranges; //TODO
};

struct TopologyConfigs
{
    struct Neighbor
    {
        uint16_t cost; //TODO
        bool databaseFilterAll; //TODO
        bool databaseFilterOut; //TODO
        uint16_t pollInterval; //TODO
        uint8_t priority; //TODO
    };

    std::map<uint32_t, AreaConfigs> areaInfo;
    std::unordered_map<IPAddress, Neighbor> neighbors; //TODO

    struct Summary
    {
        IPPrefix prefix; //TODO
        struct SummaryOpts { bool nssaOnly{false}; uint32_t tag; }; //TODO
        std::optional<SummaryOpts> opts; //TODO
    };
    std::vector<Summary> summaries; //TODO
};

struct OspfConfigs
{
    std::shared_mutex configsMutex;

    std::vector<uint32_t> passiveInterfaces; //TODO

    struct Network
    {
        IPPrefix prefix;
        uint32_t area;
    };
    std::vector<Network> networks;
};

struct InterfaceConfigs
{
    InterfaceConfigs(uint32_t k) : key(k) {}

    const uint32_t key;

    std::unordered_map<IPAddress, TopologyConfigs::Neighbor> neighbors; //TODO

    std::unordered_map<uint16_t, InterfaceConfigs> processSpecific;
};

}*/

#endif // OSPF_TYPES_HPP
