// OspfTypes.hpp

#ifndef OSPF_TYPES_HPP
#define OSPF_TYPES_HPP

#include <AddressFamily.hpp>
#include <atomic>
#include <string>
#include <optional>
#include <IPAddress.hpp>
#include <shared_mutex>

namespace OSPF
{

struct TopologyConfigs
{
    struct Neighbor
    {
        uint16_t cost;
        bool databaseFilterAll;
        bool databaseFilterOut;
        uint16_t pollInterval;
        uint8_t priority;
    };

    std::atomic<bool> ribForwardingAddress;
    std::atomic<bool> ribInterAreaSummary;
    std::atomic<bool> ribNssaTranslation;
    std::atomic<bool> trafficShareMin;
    std::atomic<bool> trafficShareMinAllIface;

    std::atomic<uint8_t> distance;
    std::atomic<uint8_t> externalDistance;
    std::atomic<uint8_t> interAreaDistance;
    std::atomic<uint8_t> intraAreaDistance;
    std::atomic<uint8_t> maxPaths;
    std::atomic<uint8_t> priority;
    std::atomic<uint8_t> floodIntervalMsec;
    std::atomic<uint8_t> retransIntervalMsec;

    std::atomic<uint16_t> lsaGroupIntervalSec;

    std::atomic<uint32_t> defaultMetric;
    std::atomic<uint32_t> lsaArrivalTimer;

    struct DefaultInformation
    {
        std::atomic<uint32_t> metric;
        std::atomic<bool> always;
        std::optional<std::string> routeMap = std::nullopt;

        enum class LinkStateType : uint8_t { ROUTER = 1, NETWORK = 2 };
        std::atomic<LinkStateType> metricType;
    } defaultInformation;

    struct DiscardRoute
    {
        std::atomic<uint8_t> externalAdminDistance;
        std::atomic<uint8_t> internalAdminDistance;
    } discardRoute;

    struct MaxMetrics
    {
        std::atomic<bool> external;
        std::atomic<uint32_t> externalOverride = 16711680u;
        std::atomic<bool> summary;
        std::atomic<uint32_t> summaryOverride = 16711680u;
        std::atomic<bool> includeStub;
        std::atomic<bool> onStartup;
    } maxMetrics;

    std::unordered_map<IPAddress, Neighbor> neighbors;

    struct Summary
    {
        IPPrefix prefix;
        struct SummaryOpts { bool nssaOnly{false}; uint32_t tag; };
        std::optional<SummaryOpts> opts;
    };
    std::vector<Summary> summaries;
};

struct OspfConfigs
{
    AddressFamily af; 
    std::atomic<bool> multicast;
    std::atomic<bool> allInterfaceBfd{false};
    std::atomic<bool> eventLogOneShot;
    std::atomic<bool> eventLogPause;
    std::atomic<bool> ignoreMofpf;
    std::atomic<bool> snmpIfIndex;
    std::atomic<bool> lspfEnabled;
    std::atomic<bool> logAdjacencyChange;
    std::atomic<bool> logAdjacencyChangeDetail;
    std::atomic<bool> nsfDisable;
    std::atomic<bool> strictLsaChecking;
    std::atomic<bool> prefixSuppression;

    std::atomic<std::optional<uint8_t>> dcRetransmissions;
    std::atomic<std::optional<uint8_t>> nonDcRetransmissions;
    std::atomic<std::optional<uint8_t>> ttlSecHops;

    std::atomic<uint16_t> numEventLogs;
    std::atomic<uint16_t> maxAge = 3600;
    std::atomic<uint16_t> maxAgeDiff = 900;

    std::atomic<uint32_t> referenceBandwidth;
    std::atomic<uint32_t> domainTag;
    std::atomic<uint32_t> maxLsa;
    std::atomic<uint32_t> maxHelloQueueDepth;
    std::atomic<uint32_t> maxUpdateQueueDepth;
    std::atomic<uint32_t> maxFloodQueueDepth = 4096;
    std::atomic<uint32_t> routerId;

    std::vector<uint32_t> passiveInterfaces;

    enum class Capability { LLS, OPAQUE, TRANSIT };
    std::atomic<Capability> capability;

    enum class Compatible { RFC_1583, RFC_1587 };
    std::atomic<Compatible> compatibility;

    struct PrefixPriority
    {
        std::atomic<bool> high;
        std::string routeMap;
    };

    struct Network
    {
        IPPrefix prefix;
        uint32_t area;
    };
    std::vector<Network> networks;

    struct Throttle
    {
        uint32_t generateDelayMsec;
        uint32_t minBetweenOrigDelayMsec;
        uint32_t maxBetweenOrigDelayMsec;
    };

    std::atomic<Throttle> lsaThrottle;
    std::atomic<Throttle> spfThrottle;

    struct DomainId
    {
        enum struct Type : uint16_t {
            STANDARD = 0x0005, AS = 0x0105, IP = 0x0205, LEGACY = 0x8005 };
        uint32_t id{0};
        Type type{Type::STANDARD};
        bool isNull{false};
    };
    std::atomic<DomainId> domainId;

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
    
    std::atomic<bool> includeSecondaries;
    std::atomic<bool> bfd;
    std::atomic<bool> databaseFilterAll;
    std::atomic<bool> databaseFilterOut;
    std::atomic<bool> demandCircuit;
    std::atomic<bool> demandCircuitIgnore;
    std::atomic<bool> floodReduction;
    std::atomic<bool> lls;
    std::atomic<bool> mtuIgnore;
    std::atomic<bool> prefixSuppression;
    std::atomic<bool> shutdown;
    std::atomic<bool> isPassive;

    std::atomic<uint8_t> priority;
    std::atomic<uint8_t> helloMultiplier;
    std::atomic<std::optional<uint8_t>> ttlSecHops;

    std::atomic<uint16_t> area;
    std::atomic<uint16_t> cost;
    std::atomic<uint16_t> deadInterval;
    std::atomic<uint16_t> helloInterval;
    std::atomic<uint16_t> resyncTimeout;
    std::atomic<uint16_t> retransmitInterval;
    std::atomic<uint16_t> transmitDelay;

    enum class NetworkType { BROADCAST, NON_BROADCAST, POINT_TO_MULTIPOINT, POINT_TO_POINT };
    std::atomic<NetworkType> networkType;

    std::unordered_map<IPAddress, TopologyConfigs::Neighbor> neighbors;

    struct auth
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
