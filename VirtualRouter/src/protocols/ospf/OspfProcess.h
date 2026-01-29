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
#include <type_traits>

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
    friend class Topology;

    using V3AfConfigs = Config::Reference<Config::OspfAddressFamilyV3Registry>;
    using V2AfConfigs = Config::Reference<Config::OspfAddressFamilyV2Registry>;

    OspfProcess(bool isV3, uint32_t procId, AddressFamily af, VirtualRouter* vrf);

    // Reorigination
    template <typename Policy>
    void distributeExternalLsa(const OspfArea& sourceArea, IncomingLsaContext& ctx, LsaBody& body);

    template <typename Policy>
    void originateExternal(Policy::ExternalLsa& lsa);

    template <typename Policy>
    void reoriginateSummaries(OspfArea& sourceArea, std::vector<OspfRouteChange>& pathList);

    template<typename Policy>
    void flood();

    std::atomic<bool> isABR = false;
    std::atomic<bool> isASBR = false;

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

    OspfArea* getArea(uint32_t areaId);
    OspfArea& insureArea(uint32_t areaId);

    const bool isV3;

    VirtualRouter* routingInstance = nullptr;

    TimeManager& tmgr;

    std::mutex externalMu;
    std::unordered_map<LsaKey, std::pair<LsaHeader, LsaBody>> externalDb;
    std::atomic<uint32_t> monotonicExternalId{0};

    TopologyTable table;

private:
    std::shared_mutex areaMu;
    std::unordered_map<uint32_t, OspfArea> areas;
    std::atomic<size_t> areaSize;

    OspfRib rib;

    std::atomic<uint32_t> rid;

    const uint32_t procId;
    const AddressFamily af;
    InterfaceManager ifaceMgr;

    std::variant<std::monostate, V3AfConfigs, V2AfConfigs> afConfigs;
    Config::Reference<Config::OspfRegistry> configs;
};
}

#endif // OSPF_H
