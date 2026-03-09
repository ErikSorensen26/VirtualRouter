// Ospf.h

#ifndef OSPF_H
#define OSPF_H

#include <ControlScheduler.h>

#include "ospf/interface/InterfaceManager.h"
#include "ospf/area/Area.h"
#include "ospf/topology/RoutingTable.h"
#include "ospf/topology/TopologyTable.h"
#include "configs/registry/router/OspfRegistry.h"

class VirtualRouter;

namespace OSPF 
{
class Topology;
class OspfProcess;
class OspfInterface;
class Area;

struct OspfV3Instance
{
    OspfV3Instance(Config::Reference<Config::OspfAddressFamilyV3Registry> cfgs)
        : configs(std::move(cfgs)) {}

    OspfProcess* ipv4 = nullptr;
    OspfProcess* ipv6 = nullptr;
    
    Config::Reference<Config::OspfAddressFamilyV3Registry> configs;
};

struct OspfInterfaceInstance
{
    OspfInterface* IPv4; ///< Pointer to the IPv4 EIGRP interface.
    OspfInterface* IPv6; ///< Pointer to the IPv6 EIGRP interface.
};

class OspfProcess
{
public:
    using V3AfConfigs = Config::Reference<Config::OspfAddressFamilyV3Registry>;
    using V2AfConfigs = Config::Reference<Config::OspfAddressFamilyV2Registry>;

    OspfProcess(bool isV3, uint16_t procId, AddressFamily af, VirtualRouter* vrf);

    // External Origination
    template <typename Policy>
    void distributeExternalLsa(const Area& sourceArea, IncomingLsaContext& ctx, const LsaBody& body);

    template <typename Policy>
    void originateExternal(ExternalOriginateContext& ctx, bool expire);

    template <typename Policy>
    void originateExternals(std::vector<std::pair<ExternalOriginateContext, bool>>& ctxs);

    template <typename Policy>
    LsaKey buildExternalKey(ExternalOriginateContext& ctx, bool isNssa);

    template <typename Policy>
    void buildExternalBody(ExternalOriginateContext& ctx, Policy::ExternalLsa& body, bool isNssa);

    // Summary Origination
    template <typename Policy>
    void reoriginateSummaries(Area& sourceArea, std::vector<OspfRouteChange>& pathList);

    template <typename Policy>
    void reoriginateSummary(Area& sourceArea, OspfRouteChange& path);

    // ASBR Summarization
    void syncSummaryConfig();

    // Getters
    AddressFamily getAF() { return af; }
    InterfaceManager& getIfaceMgr() { return ifaceMgr; }
    const InterfaceManager& getIfaceMgr() const noexcept { return ifaceMgr; }
    Config::OspfRegistry& getConfigs() { return configs.get(); }
    __uint128_t getConfigKey() const { return readU128(configs.getKey().dataPtr()); }
    const Config::OspfRegistry& getConfigs() const noexcept { return configs.get(); }
    OspfRib& getRib() { return rib; }
    ProcessQueueRef getScheduler() { return scheduler.ref(); }
    const OspfRib& getRib() const { return rib; }
    uint16_t getProcId() const { return procId; }
    uint32_t getRouterId() const
    {
        const auto& id = configs.get().get<Config::Ospf::ROUTER_ID>();
        if (id.hasValue()) return id.load();
        return rid;
    }

    bool calculateRID();

    // Areas
    Area* getArea(uint32_t areaId);
    Area& insureArea(uint32_t areaId);

    // Router types
    void setASBR(bool val);
    void setABR(bool val);
    bool isASBR();
    bool isABR();

    void initiateReset();

    void addDefaultRoute(bool add);

    const bool isV3;

    VirtualRouter* routingInstance = nullptr;

    // External
    std::unordered_map<LsaKey, std::pair<LsaHeader, LsaBody>> externalDb;
    std::atomic<uint32_t> monotonicExternalId{0};

    // Summaries
    std::unordered_map<IPPrefix, uint32_t> intraLsids;
    std::atomic<uint32_t> monotonicIntraId{0};

    TopologyTable table;

private:
    struct OspfSummaryAddress {
        // Config
        bool notAdvertise = false;
        bool nssaOnly = false;
        std::optional<uint32_t> tag;

        // Runtime
        uint32_t contributorCount = 0;
        uint32_t computedMetric = 0;
        bool isType2;

        uint32_t lsId;
        bool discardPresent = false;
    };

    // Summaries
    std::unordered_map<IPPrefix, OspfSummaryAddress> summaries;
    template <typename Policy>
    void syncSummarySuppression(std::unordered_map<IPPrefix, OspfSummaryAddress>& activeSummaries);

    // Areas
    std::unordered_map<uint32_t, Area> areas;

    OspfRib rib;
    ProcessQueue scheduler;

    // Route type
    bool abr = false;
    bool asbr = false;
    uint32_t rid;

    std::optional<uint32_t> defaultRoute = std::nullopt;

    const uint16_t procId;
    const AddressFamily af;
    InterfaceManager ifaceMgr;

    // Configs
    std::variant<std::monostate, V3AfConfigs, V2AfConfigs> afConfigs;
    Config::Reference<Config::OspfRegistry> configs;
};
}

#endif // OSPF_H
