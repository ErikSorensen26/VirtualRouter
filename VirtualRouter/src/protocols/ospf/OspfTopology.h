// OspfTopology.h

#ifndef OSPF_TOPOLOGY_H
#define OSPF_TOPOLOGY_H

#include <cstdint>
#include <OspfArea.h>
#include <OspfTopologyTable.h>
#include <OspfRoutingTable.h>
#include <OspfRegistry.hpp>

namespace OSPF
{
struct OspfRouteChange;
class OspfProcess;
class OspfArea;
class Topology
{
public:
    Topology(OspfProcess& process, uint8_t tid, AddressFamily af);

    OspfArea* getArea(uint32_t areaId);
    OspfArea& insureArea(uint32_t areaId);

    template <typename Policy>
    void distributeExternalLsa(uint32_t areaId, const IncomingLsaContext& ctx, LsaBody& body);

    const uint8_t tid;
    OspfProcess& process;

    template<typename Policy>
    void flood();

    std::atomic<bool> isABR = false;

    Config::OspfTopologyRegistry& getConfigs() { return *configs; }
    const Config::OspfTopologyRegistry& getConfigs() const { return *configs; }
    OspfRib& getRib() { return rib; }
    const OspfRib& getRib() const { return rib; }
    const OspfProcess& getProcess() const noexcept { return process; }
    AddressFamily getAf() { return af; }

    // Reorigination
    template <typename Policy>
    void reoriginateSummaries(OspfArea& sourceArea, std::vector<OspfRouteChange>& pathList);

    std::shared_mutex& getAreaLock() { return areaMu; }
    std::unordered_map<uint32_t, OspfArea>& getAreas() { return areas; }
    const std::unordered_map<uint32_t, OspfArea>& getAreas() const { return areas; }

    std::mutex externalMu;
    std::unordered_map<LsaKey, std::pair<LsaHeader, LsaBody>> externalDb;

    TopologyTable table;

private:
    std::shared_mutex areaMu;
    std::unordered_map<uint32_t, OspfArea> areas;
    std::atomic<size_t> areaSize;
    AddressFamily af;

    OspfRib rib;

    Config::OspfRegistry* processConfigs{nullptr};

    Config::Bucket<Config::OspfTopologyRegistry>::Handle handle;
    Config::OspfTopologyRegistry& configs;
};
}

#endif // OSPF_TOPOLOGY_H
