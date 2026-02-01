// Ospf.h

#ifndef OSPF_H
#define OSPF_H

#include <Registry.hpp>
#include <OspfTypes.hpp>
#include <OspfInterfaceManager.h>
#include <OspfRoutingTable.h>
#include <RegistryTypes.hpp>
#include <variant>
#include <OspfArea.h>
#include <OspfTopologyTable.h>
#include <OspfRoutingTable.h>
#include <OspfRegistry.hpp>

class VirtualRouter;
class TimeManager;

namespace OSPF 
{
class Topology;
class OspfProcess;
class OspfInterface;
class OspfArea;
struct OspfV3Instance
{
    OspfProcess* ipv4 = nullptr;
    OspfProcess* ipv6 = nullptr;
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

    OspfProcess(bool isV3, uint32_t procId, AddressFamily af, VirtualRouter* vrf);

    // External Origination
    template <typename Policy>
    void distributeExternalLsa(const OspfArea& sourceArea, IncomingLsaContext& ctx, LsaBody& body);

    template <typename Policy>
    void originateExternal(ExternalOriginateContext& ctx, bool expire);

    template <typename Policy>
    void originateExternals(std::vector<std::pair<ExternalOriginateContext, bool>>& ctxs);

    template <typename Policy>
    std::pair<LsaKey, LsaBody> buildExternal(ExternalOriginateContext& ctx, bool isNssa);

    // Summary Origination
    template <typename Policy>
    void reoriginateSummaries(OspfArea& sourceArea, std::vector<OspfRouteChange>& pathList);

    // ASBR Summarization
    void syncSummaryConfig();

    // Flooding
    template<typename Policy>
    void flood();

    // Getters
    AddressFamily getAF() { return af; }
    InterfaceManager& getIfaceMgr() { return ifaceMgr; }
    const InterfaceManager& getIfaceMgr() const noexcept { return ifaceMgr; }
    Config::OspfRegistry& getConfigs() { return configs.get(); }
    __uint128_t getConfigKey() const { return configs.getKey(); }
    const Config::OspfRegistry& getConfigs() const noexcept { return configs.get(); }
    OspfRib& getRib() { return rib; }
    const OspfRib& getRib() const { return rib; }
    uint32_t getProcId() const { return procId; }
    uint32_t getRouterId() const
    {
        const auto& id = configs.get().get<Config::Ospf::ROUTER_ID>();
        if (id.hasValue()) return id.load();
        return rid.load(std::memory_order_relaxed);
    }

    bool calculateRID();

    // Areas
    OspfArea* getArea(uint32_t areaId);
    OspfArea& insureArea(uint32_t areaId);

    // Router types
    void setASBR(bool val);
    void setABR(bool val);
    bool isASBR();
    bool isABR();

    void addDefaultRoute(bool add);

    const bool isV3;

    VirtualRouter* routingInstance = nullptr;

    TimeManager& tmgr;

    // External
    std::mutex externalMu;
    std::unordered_map<LsaKey, std::pair<LsaHeader, LsaBody>> externalDb;
    std::atomic<uint32_t> monotonicExternalId{0};

    // Summaries
    std::mutex intraMu;
    std::unordered_map<IPPrefix, uint32_t> intraLsids;
    std::atomic<uint32_t> monotonicIntraId{0};

    TopologyTable table;

private:
    struct OspfSummaryAddress
    {
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
    std::mutex asbrSummaryMu;
    std::unordered_map<IPPrefix, OspfSummaryAddress> summaries;
    template <typename Policy>
    void syncSummarySuppression(std::unordered_map<IPPrefix, OspfSummaryAddress>& activeSummaries);

    // Areas
    std::shared_mutex areaMu;
    std::unordered_map<uint32_t, OspfArea> areas;
    std::atomic<size_t> areaSize;

    OspfRib rib;

    // Route type
    std::atomic<bool> abr = false;
    std::atomic<bool> asbr = false;
    std::atomic<uint32_t> rid;

    std::optional<uint32_t> defaultRoute = std::nullopt;

    const uint32_t procId;
    const AddressFamily af;
    InterfaceManager ifaceMgr;

    // Configs
    std::variant<std::monostate, V3AfConfigs, V2AfConfigs> afConfigs;
    Config::Reference<Config::OspfRegistry> configs;
};
}

#endif // OSPF_H
