// OspfTopology.h

#ifndef OSPF_TOPOLOGY_H
#define OSPF_TOPOLOGY_H

#include <cstdint>
#include <map>
#include "OspfTypes.hpp"
#include <OspfArea.h>

namespace OSPF
{
struct OspfRouteChange;
class OspfProcess;
class OspfArea;
class Topology
{
public:
    Topology(OspfProcess& process, uint8_t tid);

    OspfArea* getArea(uint32_t areaId);
    OspfArea& insureArea(uint32_t areaId);

    OspfArea::Result processExternalLsa(uint32_t areaId, const IncomingLsaContext& ctx, LsaBody& body);

    const uint8_t tid;
    OspfProcess& process;

    void flood();

    std::atomic<bool> isABR = false;

    TopologyConfigs& getConfigs() { return configs; }
    const TopologyConfigs& getConfigs() const { return configs; }
    const OspfProcess& getProcess() const noexcept { return process; }

    // Reorigination
    template <typename SummaryNetwork, typename SummaryRouter>
    void reoriginateSummaries(OspfArea& sourceArea, std::vector<OspfRouteChange>& pathList);

private:
    std::shared_mutex areaMu;
    std::map<uint32_t, OspfArea> areas;
    std::atomic<size_t> areaSize;

    TopologyConfigs configs;
};
}

#endif // OSPF_TOPOLOGY_H
