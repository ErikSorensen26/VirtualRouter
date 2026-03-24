// Ospf.h

#ifndef OSPF_H
#define OSPF_H

#include <ControlScheduler.h>

#include "interface/InterfaceManager.h"
#include "ospf/interface/InterfaceManager.h"
#include "ospf/area/Area.h"
#include "ospf/topology/RoutingTable.h"
#include "ospf/topology/TopologyTable.h"
#include "configs/registry/router/OspfRegistry.h"

namespace core { class VirtualRouter; }

namespace routing::ospf
{
class Topology;
class OspfProcess;
class OspfInterface;
class Area;

struct OspfV3Instance
{
    OspfV3Instance(config::Reference<config::OspfAddressFamilyV3Registry> cfgs)
        : configs(std::move(cfgs)) {}

    OspfProcess* ipv4 = nullptr;
    OspfProcess* ipv6 = nullptr;
    
    config::Reference<config::OspfAddressFamilyV3Registry> configs;
};

struct OspfInterfaceInstance
{
    OspfInterface* IPv4; ///< Pointer to the IPv4 EIGRP interface.
    OspfInterface* IPv6; ///< Pointer to the IPv6 EIGRP interface.
};

class OspfProcess
{
public:
    using V3AfConfigs = config::Reference<config::OspfAddressFamilyV3Registry>;
    using V2AfConfigs = config::Reference<config::OspfAddressFamilyV2Registry>;

    OspfProcess(bool isV3, uint16_t procId, types::AddressFamily af, core::VirtualRouter* vrf);
    ~OspfProcess();

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
    types::AddressFamily getAF() { return af; }
    InterfaceManager& getIfaceMgr() { return ifaceMgr; }
    const InterfaceManager& getIfaceMgr() const noexcept { return ifaceMgr; }
    config::OspfRegistry& getConfigs() { return configs.get(); }
    uint64_t getConfigKey() const { return configs.getKey(); }
    const config::OspfRegistry& getConfigs() const noexcept { return configs.get(); }
    OspfRib& getRib() { return rib; }
    core::ProcessQueueRef getScheduler() { return scheduler.ref(); }
    const OspfRib& getRib() const { return rib; }
    uint16_t getProcId() const { return procId; }
    uint32_t getRouterId() const
    {
        const auto& id = configs.get().get<config::Ospf::ROUTER_ID>();
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

    core::VirtualRouter* routingInstance = nullptr;

    // External
    std::unordered_map<LsaKey, std::pair<LsaHeader, LsaBody>> externalDb;
    std::atomic<uint32_t> monotonicExternalId{0};

    // Summaries
    std::unordered_map<types::IPPrefix, uint32_t> intraLsids;
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
    std::unordered_map<types::IPPrefix, OspfSummaryAddress> summaries;
    template <typename Policy>
    void syncSummarySuppression(std::unordered_map<types::IPPrefix, OspfSummaryAddress>& activeSummaries);

    // Areas
    std::unordered_map<uint32_t, Area> areas;

    OspfRib rib;
    core::ProcessQueue scheduler;

    // Route type
    bool abr = false;
    bool asbr = false;
    uint32_t rid;

    std::optional<uint32_t> defaultRoute = std::nullopt;

    const uint16_t procId;
    const types::AddressFamily af;
    InterfaceManager ifaceMgr;

    // Interface event subscriptions
    uint32_t ifUpId, ifDownId, ipReadyId, ipDelId;

    // Configs
    std::variant<std::monostate, V3AfConfigs, V2AfConfigs> afConfigs;
    config::Reference<config::OspfRegistry> configs;
};
} // namespace routing

#endif // OSPF_H

