// OspfOriginatorV3.cpp

#include "OspfOriginatorV3.h"
#include <OspfNeighbor.h>
#include <OspfInterface.h>
#include <OspfArea.h>
#include <OspfTopology.h>
#include <OspfProcess.h>
#include <Interface.h>
#include <Functions.h>
#include <SpfManager.h>

namespace OSPF
{
OspfOriginatorV3::OspfOriginatorV3(OspfArea& area) : OspfOriginator(area) {}

void OspfOriginatorV3::updateInterface(uint32_t ifaceId)
{
    addRouterLsa(ifaceId);
    area.flood();
}

void OspfOriginatorV3::addRouterLsa(uint32_t ifaceId)
{
    constexpr size_t MAX_LINKS_PER_LSA = 32;

    uint32_t selfRid = area.topology().process.getRouterId();

    LsaBody baseBody = RouterLsaV3{};
    std::get<RouterLsaV3>(baseBody).options = area.getFlags().getFlags();

    {
        auto& ifmgr = area.topology().process.getIfaceMgr();
        std::shared_lock<std::shared_mutex> lock(ifmgr.interfaceMutex);
        for (auto& [id, iface] : ifmgr.ospfInterfaceList)
        {
            if (id.area != area.areaId)
                continue;
            addRouterLink(baseBody, iface, id.interfaceId == ifaceId);
        }
    }

    std::vector<RouterLinkV3>& allLinks = std::get<RouterLsaV3>(baseBody).links;
    uniqueLinks(allLinks);

    std::set<RouterLinkV3> linkSet;
    for (const auto& l : allLinks)
        linkSet.insert(l);

    std::vector<std::pair<uint32_t, LsaBody>> newLsas;

    for (const auto& [lsid, oldBody] : lastRouterLsas)
    {
        auto& oldLsa = std::get<RouterLsaV3>(oldBody);

        LsaBody body = RouterLsaV3{};
        RouterLsaV3& newLsa = std::get<RouterLsaV3>(body);
        newLsa.options = area.getFlags().getFlags();

        for (const auto& oldLink : oldLsa.links)
        {
            auto it = linkSet.find(oldLink);
            if (it != linkSet.end())
            {
                newLsa.links.push_back(*it);
                linkSet.erase(it);
            }
        }

        if (!newLsa.links.empty())
            newLsas.emplace_back(lsid, std::move(newLsa));
    }

    for (const auto& link : linkSet)
    {
        bool placed = false;

        for (auto& [lsid, body] : newLsas)
        {
            auto& lsa = std::get<RouterLsaV3>(body);
            if (lsa.links.size() < MAX_LINKS_PER_LSA)
            {
                lsa.links.push_back(link);
                placed = true;
                break;
            }
        }

        if (placed) continue;

        RouterLsaV3& lsa = std::get<RouterLsaV3>(newLsas.emplace_back(findNextRouterLsid(), RouterLsaV3{}).second);
        lsa.options = area.getFlags().getFlags();
        lsa.links.push_back(link);
    }

    for (auto& [lsid, newBody] : newLsas)
    {
        auto it = std::find_if(lastRouterLsas.begin(), lastRouterLsas.end(), [&](auto& lsa) { return lsa.first == lsid; });
        if (it == lastRouterLsas.end()) continue;

        const auto& oldLsa = std::get<RouterLsaV3>(it->second);
        const auto& newLsa = std::get<RouterLsaV3>(newBody);

        if (oldLsa.options == newLsa.options &&
            oldLsa.links == newLsa.links)
            continue;

        LsaKey key(OSPFV3_LSA_ROUTER, lsid, selfRid);
        area.processReoriginatedLsa<PolicyV3>(key, std::forward<LsaBody>(newBody));
    }

    for (auto& [lsid, oldLsa] : lastRouterLsas)
    {
        auto it = std::find_if(newLsas.begin(), newLsas.end(), [&](const auto& l) { return l.first == lsid; });
        if (it == newLsas.end())
        {
            LsaKey key(OSPFV3_LSA_ROUTER, lsid, selfRid);
            expire(key, oldLsa);
        }
    }

    addRouterPrefixLsa(newLsas);

    lastRouterLsas.swap(newLsas);
}

void OspfOriginatorV3::addRouterPrefixLsa(std::vector<std::pair<uint32_t, LsaBody>>& routerLsas)
{
    constexpr size_t MAX_PREFIXES_PER_LSA = 32;

    uint32_t selfRid = area.topology().process.getRouterId();
    auto& ifaceMgr = area.topology().process.getIfaceMgr();

    std::unordered_map<uint32_t, std::unordered_map<IPPrefix, uint16_t>> prefixesByLsid;

    {
        std::shared_lock<std::shared_mutex> lock(ifaceMgr.interfaceMutex);

        // Iterate Router-LSAs directly, this preserves LSID ownership
        for (const auto& [lsid, body] : routerLsas)
        {
            const auto& routerLsa = std::get<RouterLsaV3>(body);
            auto& out = prefixesByLsid[lsid];

            // Each link corresponds to one local interface
            for (const auto& link : routerLsa.links)
            {
                if (link.type == OSPFV3_LINK_TRANSIT) continue;

                OspfInterfaceId ifaceId(link.interfaceId, area.areaId);

                auto it = ifaceMgr.ospfInterfaceList.find(ifaceId);
                if (it == ifaceMgr.ospfInterfaceList.end())
                    continue;

                uint16_t cost = it->second.configs->cost.load(std::memory_order_relaxed);
                auto prefixes = it->second.getIface().configs.ipv6.getRoutablePrefixSet(true);

                out.reserve(prefixes.size());

                for (auto pit = prefixes.begin(); pit != prefixes.end();)
                    out.emplace(std::move(prefixes.extract(pit++).value()), cost);
            }
        }
    }

    std::vector<std::pair<uint32_t, LsaBody>> newLsas;
    newLsas.reserve(lastRouterPrefixes.size() + 4);

    // Rebuild existing prefix lsas
    for (const auto& [prefixLsid, entry] : lastRouterPrefixes)
    {
        const auto& oldLsa = std::get<IntraAreaPrefixLsa>(entry);
        uint32_t refLsid = oldLsa.referencedLinkStateId;

        IntraAreaPrefixLsa newLsa;
        newLsa.referencedLsaType = OSPFV3_LSA_ROUTER;
        newLsa.referencedLinkStateId = refLsid;
        newLsa.referencedAdvRouter = selfRid;

        auto it = prefixesByLsid.find(refLsid);
        if (it != prefixesByLsid.end())
        {
            auto prefixMap = it->second;
            for (const auto& oldPrefix : oldLsa.prefixes)
            {
                auto pit = prefixMap.find(oldPrefix.prefix);
                if (pit != prefixMap.end())
                {
                    newLsa.prefixes.emplace_back(0, pit->second, pit->first);
                    prefixMap.erase(pit);
                }
            }

            if (!newLsa.prefixes.empty())
                newLsas.emplace_back(prefixLsid, std::move(newLsa));
        }
    }

    for (auto& [refLsid, prefixMap] : prefixesByLsid)
    {
        for (auto& [prefix, cost] : prefixMap)
        {
            bool placed = false;

            for (auto& [lsid, body] : newLsas)
            {
                auto& lsa = std::get<IntraAreaPrefixLsa>(body);
                if (lsa.referencedLinkStateId != refLsid || lsa.prefixes.size() < MAX_PREFIXES_PER_LSA) continue;
                lsa.prefixes.emplace_back(0, cost, prefix);
                placed = true;
                break;
            }
        
            if (placed) continue;

            IntraAreaPrefixLsa& lsa = std::get<IntraAreaPrefixLsa>(newLsas.emplace_back(findNextPrefixLsid(), IntraAreaPrefixLsa{}).second);
            lsa.referencedLsaType = OSPFV3_LSA_ROUTER;
            lsa.referencedLinkStateId = refLsid;
            lsa.referencedAdvRouter = selfRid;
        }
    }

    for (auto& [lsid, newBody] : newLsas)
    {
        auto it = std::find_if(lastRouterPrefixes.begin(), lastRouterPrefixes.end(), [&](auto& lsa) { return lsa.first == lsid; });
        if (it == lastRouterPrefixes.end()) continue;

        auto oldLsa = std::get<IntraAreaPrefixLsa>(it->second);
        auto newLsa = std::get<IntraAreaPrefixLsa>(newBody);

        if (oldLsa.referencedLinkStateId == newLsa.referencedLinkStateId &&
            oldLsa.prefixes == newLsa.prefixes)
            continue;

        LsaKey key(OSPFV3_LSA_INTRA_AREA_PREFIX, lsid, selfRid);
        area.processReoriginatedLsa<PolicyV3>(key, std::forward<LsaBody>(newBody));
    }

    for (auto& [lsid, oldLsa] : lastRouterPrefixes)
    {
        auto it = std::find_if(newLsas.begin(), newLsas.end(), [&](const auto& l) { return l.first == lsid; });
        if (it == newLsas.end())
        {
            LsaKey key(OSPFV3_LSA_INTRA_AREA_PREFIX, lsid, selfRid);
            expire(key, oldLsa);
        }
    }

    lastRouterPrefixes.swap(newLsas);
}

void OspfOriginatorV3::addNetworkLsa(const OspfInterface& iface)
{
    LsaBody lsa = NetworkLsaV3{};
    NetworkLsaV3& network = std::get<NetworkLsaV3>(lsa);

    uint32_t selfRid = area.topology().process.getRouterId();
    network.options = area.getFlags().getFlags();
    network.attachedRouters.push_back(selfRid);

    {
        auto& ntable = iface.getNTable();
        std::shared_lock<std::shared_mutex> lock(ntable.mu);
        for (const auto& [rid, nbr] : ntable.neighbors)
            if (nbr.getState() == Neighbor::State::FULL)
                network.attachedRouters.push_back(rid);
    }

    uniqueLinks(network.attachedRouters);

    LsaKey key(OSPFV3_LSA_NETWORK, readU32(iface.interfaceAddress.addr), selfRid);
    
    auto it = networkLsas.find(iface.id);
    if (it != networkLsas.end() && std::get<NetworkLsaV3>(it->second.lastLsa) == network && it->second.key == key)
        return;

    networkLsas[iface.id] = NetworkState{key, lsa};

    area.processReoriginatedLsa<PolicyV3>(key, std::move(lsa)); 

    addNetworkPrefixLsa(key, iface);
}

void OspfOriginatorV3::addExternal(uint32_t asbr, uint32_t lsid, bool remove)
{
    if (!asbrExternalRouters.contains(asbr))
    {
        if (remove) return;
        addAsbrLsa(asbr);
    }

    auto& external = asbrExternalRouters[asbr];

    bool found = std::find(external.second.begin(), external.second.end(), lsid) != external.second.end();

    if (!found && !remove)
    {
        external.second.push_back(lsid);
    }
    else if (found && remove)
    {
        external.second.erase(std::find(external.second.begin(), external.second.end(), lsid));
        if (external.second.empty())
        {
            LsaKey key{OSPFV3_LSA_INTER_AREA_ROUTER, asbr, area.topology().process.getRouterId()};
            expire(key, external.first);
            asbrExternalRouters.erase(asbr);
        }
    }
}

void OspfOriginatorV3::addNetworkPrefixLsa(LsaKey& key, const OspfInterface& iface)
{
    constexpr size_t MAX_NETWORKS_PER_LSA = 32;

    uint32_t selfRid = area.topology().process.getRouterId();

    std::unordered_set<IPPrefix> prefixSet = iface.getIface().configs.ipv6.getRoutablePrefixSet();
    uint32_t cost = iface.configs->cost.load(std::memory_order_relaxed);

    std::vector<std::pair<uint32_t, LsaBody>> newLsas;
    newLsas.reserve(lastNetworkPrefixes.size() + 4);

    // Rebuild existing prefix lsas
    auto lastIt = std::find_if(lastNetworkPrefixes.begin(), lastNetworkPrefixes.end(),
        [&](const std::pair<uint32_t, std::vector<std::pair<uint32_t, LsaBody>>>& p) { return p.first == iface.id.interfaceId; });

    if (lastIt != lastNetworkPrefixes.end())
    {
        for (const auto& [prefixLsid, entry] : lastIt->second)
        {
            const auto& oldLsa = std::get<IntraAreaPrefixLsa>(entry);

            IntraAreaPrefixLsa newLsa;
            newLsa.referencedLsaType = OSPFV3_LSA_NETWORK;
            newLsa.referencedLinkStateId = key.linkStateId;
            newLsa.referencedAdvRouter = selfRid;

            for (const auto& oldPrefix : oldLsa.prefixes)
            {
                auto pit = prefixSet.find(oldPrefix.prefix);
                if (pit != prefixSet.end())
                {
                    newLsa.prefixes.emplace_back(0, cost, *pit);
                    prefixSet.erase(pit);
                }
            }

            if (!newLsa.prefixes.empty())
                newLsas.emplace_back(prefixLsid, std::move(newLsa));
        }
    }

    for (auto& prefix : prefixSet)
    {
        bool placed = false;

        for (auto& [lsid, body] : newLsas)
        {
            auto& lsa = std::get<IntraAreaPrefixLsa>(body);
            if (lsa.prefixes.size() < MAX_NETWORKS_PER_LSA) continue;
            lsa.prefixes.emplace_back(0, cost, prefix);
            placed = true;
            break;
        }
    
        if (placed) continue;

        IntraAreaPrefixLsa& lsa = std::get<IntraAreaPrefixLsa>(newLsas.emplace_back(findNextPrefixLsid(), IntraAreaPrefixLsa{}).second);
        lsa.referencedLsaType = OSPFV3_LSA_NETWORK;
        lsa.referencedLinkStateId = key.linkStateId;
        lsa.referencedAdvRouter = selfRid;
    }

    for (auto& [lsid, newBody] : newLsas)
    {
        auto it = std::find_if(lastIt->second.begin(), lastIt->second.end(), [&](auto& lsa) { return lsa.first == lsid; });
        if (it == lastIt->second.end()) continue;

        auto oldLsa = std::get<IntraAreaPrefixLsa>(it->second);
        auto newLsa = std::get<IntraAreaPrefixLsa>(newBody);

        if (oldLsa.referencedLinkStateId == newLsa.referencedLinkStateId &&
            oldLsa.prefixes == newLsa.prefixes)
            continue;

        LsaKey intraKey(OSPFV3_LSA_INTRA_AREA_PREFIX, lsid, selfRid);
        area.processReoriginatedLsa<PolicyV3>(intraKey, std::forward<LsaBody>(newBody));
    }

    for (auto& [lsid, oldLsa] : lastIt->second)
    {
        auto it = std::find_if(newLsas.begin(), newLsas.end(), [&](const auto& l) { return l.first == lsid; });
        if (it == newLsas.end())
        {
            LsaKey oldKey(OSPFV3_LSA_INTRA_AREA_PREFIX, lsid, selfRid);
            expire(oldKey, oldLsa);
        }
    }

    lastIt->second.swap(newLsas);
}

void OspfOriginatorV3::expire(LsaKey& key, LsaBody& lsa)
{
    area.processReoriginatedLsa<PolicyV3>(key, std::move(lsa), true);
    if (std::holds_alternative<IntraAreaPrefixLsa>(lsa))
        prefixLsidQueue.push_back(key.linkStateId); // Set id available
    else if (std::holds_alternative<RouterLsaV3>(lsa))
        routerLsidQueue.push_back(key.linkStateId);
}

void OspfOriginatorV3::addAsbrLsa(uint32_t asbr)
{
    uint32_t metric = area.topology().table.lookupDistance(asbr);
    if (metric == 0) return;
    auto it = asbrExternalRouters.emplace(asbr, InterAreaRouterLsa{}, std::vector<uint32_t>{});
    if (!it.second) return;

    auto& lsa = std::get<InterAreaRouterLsa>(it.first->second.first);
    lsa.destinationRouterId = asbr;
    lsa.metric = metric;

    LsaKey key(OSPFV3_LSA_INTER_AREA_ROUTER, asbr, area.topology().process.getRouterId());

    area.processReoriginatedLsa<PolicyV3>(key, lsa);
}

void OspfOriginatorV3::removeNetworkLsa(uint32_t ifaceId)
{
    OspfInterfaceId id(ifaceId, area.areaId);
    uint32_t selfRid = area.topology().process.getRouterId();
    auto it = networkLsas.find(id);
    if (it == networkLsas.end()) return;
    area.processReoriginatedLsa<PolicyV3>(it->second.key, std::move(it->second.lastLsa), true);
    networkLsas.erase(it);

    auto pit = std::find_if(lastNetworkPrefixes.begin(), lastNetworkPrefixes.end(),
        [&](const std::pair<uint32_t, std::vector<std::pair<uint32_t, LsaBody>>>& i) { return i.first == ifaceId; });
    if (pit == lastNetworkPrefixes.end()) return;
    for (auto& [lsid, body] : pit->second)
    {
        LsaKey key(OSPFV3_LSA_INTRA_AREA_PREFIX, lsid, selfRid);
        expire(key, body);
    }
    lastNetworkPrefixes.erase(pit);
}

uint32_t OspfOriginatorV3::findNextRouterLsid()
{
    if (!routerLsidQueue.empty())
    {
        uint32_t id = routerLsidQueue.front();
        routerLsidQueue.pop_front();
        return id;
    }
    return ++maxRouterLsid;
}

uint32_t OspfOriginatorV3::findNextPrefixLsid()
{
    if (!prefixLsidQueue.empty())
    {
        uint32_t id = prefixLsidQueue.front();
        prefixLsidQueue.pop_front();
        return id;
    }
    return ++maxPrefixLsid;
}

void OspfOriginatorV3::addTransitLink(LsaBody& router, const OspfInterface& iface, const Neighbor* nbr)
{ 
    uint32_t intId = nbr ? nbr->neighborInterfaceId : iface.id.interfaceId;
    std::get<RouterLsaV3>(router).links.push_back(RouterLinkV3{
        .type = OSPFV3_LINK_TRANSIT,
        .metric = iface.configs->cost.load(std::memory_order_relaxed),
        .interfaceId = iface.id.interfaceId,
        .neighborInterfaceId = intId,
        .neighborRouterId = iface.dr.rid.load(std::memory_order_relaxed)
    });
}

void OspfOriginatorV3::addP2PLink(LsaBody& router, const OspfInterface& iface, const Neighbor& neighbor)
{
    std::get<RouterLsaV3>(router).links.push_back(RouterLinkV3{
        .type = OSPFV3_LINK_P2P,
        .metric = iface.configs->cost.load(std::memory_order_relaxed),
        .interfaceId = iface.getIface().configs.key,
        .neighborInterfaceId = neighbor.neighborInterfaceId,
        .neighborRouterId = neighbor.routerID
    });
}

void OspfOriginatorV3::addStubLink(LsaBody& router, const OspfInterface& iface)
{
    std::get<RouterLsaV3>(router).links.push_back(RouterLinkV3{
        .type = OSPFV3_LINK_STUB,
        .metric = iface.configs->cost.load(std::memory_order_relaxed),
        .interfaceId = iface.getIface().configs.key,
        .neighborInterfaceId = 0,
        .neighborRouterId = 0
    });
}

void OspfOriginatorV3::addVirtualLink(LsaBody& router, const OspfInterface& iface, const Neighbor& vNbr)
{
    std::get<RouterLsaV3>(router).links.push_back(RouterLinkV3{
        .type = OSPFV3_LINK_VIRTUAL,
        .metric = iface.configs->cost.load(std::memory_order_relaxed),
        .interfaceId = iface.getIface().configs.key,
        .neighborInterfaceId = vNbr.neighborInterfaceId,
        .neighborRouterId = vNbr.routerID
    });
}
}
