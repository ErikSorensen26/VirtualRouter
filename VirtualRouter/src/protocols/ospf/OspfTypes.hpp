// OspfTypes.hpp

#ifndef OSPF_TYPES_HPP
#define OSPF_TYPES_HPP

#include <AddressFamily.hpp>
#include <atomic>
#include <string>
#include <optional>
#include <IPAddress.hpp>
#include <shared_mutex>
#include <map>

#include <Ospfv2LSAHeader.hpp>
#include <Ospfv3LSAHeader.hpp>

#define OSPF_MAX_AGE 3600
#define OSPF_REFRESH_AGE 1800
#define OSPF_MAX_DIFF 900

namespace OSPF
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
 
    struct Nssa
    {
        std::atomic<bool> enabled;
        struct DefaultInformation
        {
            std::atomic<bool> enabled;
            std::atomic<uint32_t> metric;
            std::optional<std::string> routeMap = std::nullopt;

            enum class LinkStateType : uint8_t { ROUTER = 1, NETWORK = 2 };
            std::atomic<LinkStateType> metricType;
        } defaultInfoOriginate;

        std::atomic<bool> alwaysTranslateT7;
        std::atomic<bool> suppressForwardAddress;

        std::atomic<bool> noRedistribution;
        std::atomic<bool> noSummary;
    } nssa; //TODO

    struct Stub
    {
        std::atomic<bool> enabled;
        std::atomic<bool> noSummary;
    } stub; //TODO

    std::atomic<bool> deterministicParentOrder;
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

    std::atomic<bool> ribForwardingAddress; //TODO
    std::atomic<bool> ribInterAreaSummary; //TODO
    std::atomic<bool> ribNssaTranslation; //TODO
    std::atomic<bool> trafficShareMin; //TODO
    std::atomic<bool> trafficShareMinAllIface; //TODO

    std::atomic<uint8_t> distance; //TODO
    std::atomic<uint8_t> externalDistance; //TODO
    std::atomic<uint8_t> interAreaDistance; //TODO
    std::atomic<uint8_t> intraAreaDistance; //TODO
    std::atomic<uint8_t> maxPaths;
    std::atomic<uint8_t> priority; //TODO
    std::atomic<uint8_t> floodIntervalMsec; //TODO
    std::atomic<uint8_t> retransIntervalMsec; //TODO

    std::atomic<uint16_t> lsaGroupIntervalSec; //TODO

    std::atomic<uint32_t> defaultMetric; //TODO
    std::atomic<uint32_t> lsaArrivalTimer; //TODO
 
    std::map<uint32_t, AreaConfigs> areaInfo;

    AreaConfigs::Nssa::DefaultInformation defaultInformation; //TODO

    struct DiscardRoute
    {
        std::atomic<uint8_t> externalAdminDistance; //TODO
        std::atomic<uint8_t> internalAdminDistance; //TODO
    } discardRoute;

    struct MaxMetrics
    {
        std::atomic<bool> external; //TODO
        std::atomic<uint32_t> externalOverride = 16711680u; //TODO
        std::atomic<bool> summary; //TODO
        std::atomic<uint32_t> summaryOverride = 16711680u; //TODO
        std::atomic<bool> includeStub; //TODO
        std::atomic<bool> onStartup; //TODO
    } maxMetrics; //TODO
 
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
    AddressFamily af; 
    std::atomic<bool> multicast; //TODO
    std::atomic<bool> allInterfaceBfd{false}; //TODO
    std::atomic<bool> eventLogOneShot; //TODO
    std::atomic<bool> eventLogPause; //TODO
    std::atomic<bool> ignoreMofpf; //TODO
    std::atomic<bool> snmpIfIndex; //TODO
    std::atomic<bool> lspfEnabled; //TODO
    std::atomic<bool> logAdjacencyChange; //TODO
    std::atomic<bool> logAdjacencyChangeDetail; //TODO
    std::atomic<bool> nsfDisable; //TODO
    std::atomic<bool> strictLsaChecking; //TODO
    std::atomic<bool> prefixSuppression; //TODO

    std::atomic<std::optional<uint8_t>> dcRetransmissions; //TODO
    std::atomic<std::optional<uint8_t>> nonDcRetransmissions; //TODO
    std::atomic<std::optional<uint8_t>> ttlSecHops; //TODO
 
    std::atomic<uint16_t> numEventLogs; //TODO
    std::atomic<uint16_t> maxAge = 3600;
    std::atomic<uint16_t> maxAgeDiff = 900;

    std::atomic<uint32_t> referenceBandwidth; //TODO
    std::atomic<uint32_t> domainTag; //TODO
    std::atomic<uint32_t> maxLsa; //TODO
    std::atomic<uint32_t> maxHelloQueueDepth; //TODO
    std::atomic<uint32_t> maxUpdateQueueDepth; //TODO
    std::atomic<uint32_t> maxFloodQueueDepth = 4096;
    std::atomic<uint32_t> routerId; //TODO

    std::vector<uint32_t> passiveInterfaces; //TODO

    enum class Capability { LLS, OPAQUE, TRANSIT };
    std::atomic<Capability> capability; //TODO

    enum class Compatible { RFC_1583, RFC_1587 };
    std::atomic<Compatible> compatibility; //TODO

    struct PrefixPriority
    {
        std::atomic<bool> high; //TODO
        std::string routeMap; //TODO
    };

    struct Network
    {
        IPPrefix prefix;
        uint32_t area;
    };
    std::vector<Network> networks;

    struct Throttle
    {
        uint32_t initDelayMs;
        uint32_t holdTimeMs;
        uint32_t maxHoldTimeMs;
    };

    std::atomic<Throttle> lsaThrottle; //TODO
    std::atomic<Throttle> spfThrottle; //TODO

    struct DomainId
    {
        enum struct Type : uint16_t {
            STANDARD = 0x0005, AS = 0x0105, IP = 0x0205, LEGACY = 0x8005 };
        uint32_t id{0};
        Type type{Type::STANDARD};
        bool isNull{false};
    };
    std::atomic<DomainId> domainId; //TODO

    struct Mpls
    {
        // TODO
    };

    struct Snmp
    {
        // TODO
    };

    std::shared_mutex configsMutex;
};

struct InterfaceConfigs
{
    InterfaceConfigs(uint32_t k) : key(k) {}

    const uint32_t key;
    
    std::atomic<bool> includeSecondaries = false;
    std::atomic<bool> bfd = false; //TODO
    std::atomic<bool> databaseFilterAll = false;
    std::atomic<bool> databaseFilterOut = false;
    std::atomic<bool> demandCircuit = false;
    std::atomic<bool> demandCircuitIgnore = false;
    std::atomic<bool> floodReduction = false;
    std::atomic<bool> lls = true;
    std::atomic<bool> mtuIgnore = false;
    std::atomic<bool> prefixSuppression; //TODO
    std::atomic<bool> shutdown; //TODO
    std::atomic<bool> isPassive;

    std::atomic<uint8_t> priority;
    std::atomic<uint8_t> helloMultiplier; //TODO
    std::atomic<std::optional<uint8_t>> ttlSecHops; //TODO

    std::atomic<uint16_t> area; //TODO
    std::atomic<uint16_t> cost; //TODO
    std::atomic<uint16_t> deadInterval;
    std::atomic<uint16_t> helloInterval;
    std::atomic<uint16_t> resyncTimeout; //TODO
    std::atomic<uint16_t> retransmitInterval;
    std::atomic<uint16_t> transmitDelay; //TODO

    enum class NetworkType : uint8_t { BROADCAST, NON_BROADCAST, POINT_TO_MULTIPOINT, MULTIPOINT_NON_BROADCAST, POINT_TO_POINT };
    std::atomic<NetworkType> networkType; // TODO NEED TO SET UNICAST/MULTICAST BOOL BASED ON THIS

    std::unordered_map<IPAddress, TopologyConfigs::Neighbor> neighbors; //TODO

    struct auth //TODO
    {
        // v2
        std::atomic<bool> messageDigestAuth;
        uint16_t authType;
        uint8_t digestKey;

        // v3
        enum class Esp { _3DES, AES128, AES192, AES256, DES, NONE };
        std::optional<Esp> esp;
        std::atomic<bool> ipSec;

        std::string password;
    } auth;

    std::unordered_map<uint16_t, InterfaceConfigs> processSpecific;
};

}

#endif // OSPF_TYPES_HPP
