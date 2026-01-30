// OspfOriginatorV3.cpp

#include "OspfOriginatorV3.h"
#include <OspfNeighbor.h>
#include <OspfInterface.h>
#include <OspfArea.h>
#include <OspfProcess.h>
#include <Interface.h>
#include <Functions.h>
#include <SpfManager.h>

namespace OSPF
{
OspfOriginatorV3::OspfOriginatorV3(OspfArea& area) : OspfOriginator(area)
{
    fullRefresh();
}

void OspfOriginatorV3::fullRefresh()
{
    RefreshInfo info{.isRefresh = true};
    addRouterLsa(std::nullopt, info, true);

    if (area.type == AreaType::NSSA || area.type == AreaType::TOTALLY_NSSA)
        nssaDefaultOriginate(area.getConfigs().get<Config::OspfArea::NSSA_DEFAULT_ORIGINATE>().load());
    else
        nssaDefaultOriginate(false);
    if (area.type == AreaType::STUB || area.type == AreaType::TOTALLY_STUB)
        addStubDefaultRoute(true);
    else
        addStubDefaultRoute(false);
    
    if (area.type == AreaType::NORMAL)
        for (const auto& asbr : asbrLsas)
            addAsbrLsa(asbr.first, info);

    startRefresh<PolicyV3>(info);
    area.flood<PolicyV3>();
}

void OspfOriginatorV3::updateInterface(uint32_t ifaceId)
{
    RefreshInfo info{};
    addRouterLsa(ifaceId, info);
    startRefresh<PolicyV3>(info);
    area.flood<PolicyV3>();
}

void OspfOriginatorV3::addRouterLsa(std::optional<uint32_t> ifaceId, RefreshInfo& refresh, bool fullRefresh)
{
    constexpr size_t MAX_LINKS_PER_LSA = 32;

    bool isRefresh = refresh.isRefresh;
    uint32_t selfRid = area.process().getRouterId();

    LsaBody baseBody = RouterLsaV3{};
    std::get<RouterLsaV3>(baseBody).options = area.getFlags().getFlags();

    {
        auto& ifmgr = area.process().getIfaceMgr();
        std::shared_lock<std::shared_mutex> lock(ifmgr.interfaceMutex);
        for (auto& [id, iface] : ifmgr.ospfInterfaceList)
        {
            if (id.area != area.areaId)
                continue;
            addRouterLink(baseBody, iface, refresh, fullRefresh || (ifaceId.has_value() && ifaceId.value() == id.interfaceId));
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

    for (const auto& [lsid, newBody] : newLsas)
    {
        auto it = std::find_if(lastRouterLsas.begin(), lastRouterLsas.end(), [&](auto& lsa) { return lsa.first == lsid; });
        if (it == lastRouterLsas.end()) continue;

        const auto& oldLsa = std::get<RouterLsaV3>(it->second);
        const auto& newLsa = std::get<RouterLsaV3>(newBody);

        LsaKey key(OSPFV3_LSA_ROUTER, lsid, selfRid);

        bool needsRefresh = (isRefresh && !refresh.activeTimerId.has_value()) || (isRefresh && lsaRefreshes[key] == refresh.activeTimerId.value());

        if (!needsRefresh && oldLsa.options == newLsa.options &&
            oldLsa.links == newLsa.links)
            continue;
    
        LsaBody bodyCpy = newBody;
        processOriginatedLsa<PolicyV3>(key, bodyCpy, false, &refresh);
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

    addRouterPrefixLsa(newLsas, refresh);

    lastRouterLsas.swap(newLsas);
}

void OspfOriginatorV3::addRouterPrefixLsa(std::vector<std::pair<uint32_t, LsaBody>>& routerLsas, RefreshInfo& refresh)
{
    constexpr size_t MAX_PREFIXES_PER_LSA = 32;

    bool isRefresh = refresh.isRefresh;
    uint32_t selfRid = area.process().getRouterId();
    auto& ifaceMgr = area.process().getIfaceMgr();

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
                if (it == ifaceMgr.ospfInterfaceList.end() || it->second.getBaseConfigs().get<Config::OspfInterfaceBase::PREFIX_SUPPRESSION>().load())
                    continue;

                auto& config = it->second.getConfigs();

                bool isP2MP = config.get<Config::OspfInterface::NETWORK>().load() == NetworkType::POINT_TO_MULTIPOINT;

                uint16_t cost = config.get<Config::OspfInterface::COST>().load();
                auto prefixes = it->second.getIface().configs.ipv6.getRoutablePrefixSet(true);

                out.reserve(prefixes.size());

                for (auto pit = prefixes.begin(); pit != prefixes.end();)
                {
                    auto prefix = prefixes.extract(pit++);
                    if (isP2MP) 
                    {
                        out.emplace(std::move(prefix), cost);
                        prefix.value().prefixLength = 128;
                    }
                    out.emplace(std::move(prefix), cost);
                }
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

    // Compare current against old
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

        LsaKey key(OSPFV3_LSA_INTRA_AREA_PREFIX, lsid, selfRid);

        bool needsRefresh = (isRefresh && !refresh.activeTimerId.has_value()) || (isRefresh && lsaRefreshes[key] == refresh.activeTimerId.value());

        if (!needsRefresh && oldLsa.referencedLinkStateId == newLsa.referencedLinkStateId &&
            oldLsa.prefixes == newLsa.prefixes)
            continue;

        processOriginatedLsa<PolicyV3>(key, newBody, false, &refresh);
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

void OspfOriginatorV3::addNetworkLsa(const OspfInterface& iface, RefreshInfo& refresh)
{
    LsaBody lsa = NetworkLsaV3{};
    NetworkLsaV3& network = std::get<NetworkLsaV3>(lsa);

    uint32_t selfRid = area.process().getRouterId();
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
    if (!refresh.isRefresh && it != networkLsas.end() && std::get<NetworkLsaV3>(it->second.lastLsa) == network && it->second.key == key)
        return;

    networkLsas[iface.id] = LsaState{key, lsa};

    processOriginatedLsa<PolicyV3>(key, lsa, false, &refresh); 
    addNetworkPrefixLsa(key, iface, refresh);
}

void OspfOriginatorV3::addExternal(uint32_t asbr, uint32_t lsid, bool remove)
{
    if (!remove)
    {
        RefreshInfo info{};
        addAsbrLsa(asbr, info);
        startRefresh<PolicyV3>(info);
    }

    auto& external = externalRoutes[asbr];

    bool found = std::find(external.begin(), external.end(), lsid) != external.end();

    if (!found && !remove)
    {
        external.push_back(lsid);
    }
    else if (found && remove)
    {
        external.erase(std::find(external.begin(), external.end(), lsid));
        if (external.empty())
        {
            auto& asbrLsa = asbrLsas[asbr];
            expire(asbrLsa.key, asbrLsa.lastLsa);
            externalRoutes.erase(asbr);
            asbrLsas.erase(asbr);
        }
    }
}

void OspfOriginatorV3::addNetworkPrefixLsa(LsaKey& key, const OspfInterface& iface, RefreshInfo& refresh)
{
    constexpr size_t MAX_NETWORKS_PER_LSA = 32;

    bool isRefresh = refresh.isRefresh;
    uint32_t selfRid = area.process().getRouterId();

    std::unordered_set<IPPrefix> prefixSet = iface.getIface().configs.ipv6.getRoutablePrefixSet();
    uint32_t cost = iface.getConfigs().get<Config::OspfInterface::COST>().load();

    std::vector<std::pair<uint32_t, LsaBody>> newLsas;
    newLsas.reserve(lastNetworkPrefixes.size() + 4);

    // Rebuild existing prefix lsas
    auto lastIt = std::find_if(lastNetworkPrefixes.begin(), lastNetworkPrefixes.end(),
        [&](const std::pair<uint32_t, std::vector<std::pair<uint32_t, LsaBody>>>& p) { return p.first == iface.id.interfaceId; });

    if (iface.getBaseConfigs().get<Config::OspfInterfaceBase::PREFIX_SUPPRESSION>().load())
    {
        // No network prefixes sent when suppressed, clear previous state.
        lastIt->second.clear();
        return;
    }

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

        LsaKey intraKey(OSPFV3_LSA_INTRA_AREA_PREFIX, lsid, selfRid);

        bool needsRefresh = (isRefresh && !refresh.activeTimerId.has_value()) || (isRefresh && lsaRefreshes[key] == refresh.activeTimerId.value());

        if (!needsRefresh && oldLsa.referencedLinkStateId == newLsa.referencedLinkStateId &&
            oldLsa.prefixes == newLsa.prefixes)
            continue;

        LsaBody bodyCpy = newBody;
        processOriginatedLsa<PolicyV3>(intraKey, bodyCpy, false, &refresh);
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

void OspfOriginatorV3::originateExternal(uint32_t lsid, ExternalOriginateContext& ctx, bool expire)
{
    LsaKey key;
    LsaBody body = ExternalLsaV3();

    auto& external = std::get<ExternalLsaV3>(body);
    external.options = 0;

    if (area.type == AreaType::NORMAL)
        key.lsaType = OSPFV3_LSA_AS_EXTERNAL;
    else if (area.type == AreaType::NSSA || area.type == AreaType::TOTALLY_NSSA)
    {
        key.lsaType = OSPFV3_LSA_NSSA_EXTERNAL;
        external.options ^= 0x08; // Set P-bit
    }
    else return; // Stub not supported

    key.advertisingRouter = area.process().getRouterId();
    key.linkStateId = lsid;

    external.isType2 = ctx.metricIsE2;
    external.prefix = ctx.prefix;
    external.referencedLsType = 0;
    if (isValidForwardAddress(ctx.nextHop))
        external.forwardingAddress = ctx.nextHop.value();
    if (ctx.tag != 0)
        external.routeTag = ctx.tag;

    processOriginatedLsa<PolicyV3>(key, body, expire);
}

void OspfOriginatorV3::originateSummary(uint32_t lsid, const IPPrefix& prefix, uint32_t cost, bool expire)
{
    LsaKey key;
    LsaBody body = IntraAreaPrefixLsa();

    auto& summary = std::get<InterAreaPrefixLsa>(body);

    key.lsaType = OSPFV2_LSA_SUM_NET;
    key.linkStateId = lsid;
    key.advertisingRouter = area.process().getRouterId();

    summary.metric = cost;
    summary.options = 0;
    summary.prefix = prefix;

    processOriginatedLsa<PolicyV3>(key, body, expire);
}

void OspfOriginatorV3::translateNssaToExternal(const LsaKey& key7, const LsaBody& body7, bool expire)
{
    if (std::get<ExternalLsaV3>(body7).prefix.prefixLength == 0 && std::get<ExternalLsaV3>(body7).prefix.v6 == 0 &&
        !area.getConfigs().get<Config::OspfArea::NSSA_DEFAULT_ONLY>().load())
        return;

    LsaKey key5;
    LsaBody body5 = body7;

    auto& ext5 = std::get<ExternalLsaV3>(body5);

    key5.lsaType = OSPFV3_LSA_AS_EXTERNAL;
    key5.linkStateId = key7.linkStateId;
    key5.advertisingRouter = area.process().getRouterId();

    ext5.options &= ~0x08;
    if (ext5.forwardingAddress.has_value() && !isValidForwardAddress(ext5.forwardingAddress))
        ext5.forwardingAddress.reset();

    processOriginatedLsa<PolicyV3>(key5, body5, expire);
}

void OspfOriginatorV3::addStubDefaultRoute(bool add)
{
    if (area.type != AreaType::STUB && area.type != AreaType::TOTALLY_STUB)
        return;
    if (stubDefaultRoute.has_value() == add)
        return;
    if (!area.process().isABR() && add)
    {
        if (stubDefaultRoute)
            addStubDefaultRoute(false);
        return;
    }

    LsaBody body = InterAreaPrefixLsa();

    auto& summary = std::get<InterAreaPrefixLsa>(body);

    if (!stubDefaultRoute.has_value())
    {
        LsaKey key;
        key.lsaType = OSPFV2_LSA_SUM_NET;
        key.linkStateId = 0;
        key.advertisingRouter = area.process().getRouterId();
        stubDefaultRoute = key;
    }

    summary.metric = area.getConfigs().get<Config::OspfArea::DEFAULT_COST>().load();
    summary.options = 0;
    summary.prefix = IPPrefix{area.process().getAF()};

    processOriginatedLsa<PolicyV3>(stubDefaultRoute.value(), body, !add);

    if (!add) stubDefaultRoute.reset();
}

void OspfOriginatorV3::expire(LsaKey& key, LsaBody& lsa)
{
    processOriginatedLsa<PolicyV3>(key, lsa, true);
    if (std::holds_alternative<IntraAreaPrefixLsa>(lsa))
        prefixLsidQueue.push_back(key.linkStateId); // Set id available
    else if (std::holds_alternative<RouterLsaV3>(lsa))
        routerLsidQueue.push_back(key.linkStateId);
}

void OspfOriginatorV3::addAsbrLsa(uint32_t asbr, RefreshInfo& refresh)
{
    uint32_t metric = area.process().table.lookupDistance(asbr);
    if (metric == 0) return;
    auto it = asbrLsas.find(asbr);

    LsaBody lsa = InterAreaPrefixLsa{};
    auto& asbrLsa = std::get<InterAreaRouterLsa>(lsa);
    asbrLsa.destinationRouterId = asbr;
    asbrLsa.metric = metric;

    LsaKey key(OSPFV3_LSA_INTER_AREA_ROUTER, asbr, area.process().getRouterId());

    if (!refresh.isRefresh && it != asbrLsas.end() && std::get<InterAreaRouterLsa>(it->second.lastLsa) == asbrLsa && key == it->second.key)
        return;

    asbrLsas[asbr] = LsaState{key, lsa};
    processOriginatedLsa<PolicyV3>(key, lsa, false, &refresh);
}

void OspfOriginatorV3::removeNetworkLsa(uint32_t ifaceId)
{
    OspfInterfaceId id(ifaceId, area.areaId);
    uint32_t selfRid = area.process().getRouterId();
    auto it = networkLsas.find(id);
    if (it == networkLsas.end()) return;
    processOriginatedLsa<PolicyV3>(it->second.key, it->second.lastLsa, true, nullptr);
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
        .metric = iface.cost.load(std::memory_order_relaxed),
        .interfaceId = iface.id.interfaceId,
        .neighborInterfaceId = intId,
        .neighborRouterId = iface.dr.rid.load(std::memory_order_relaxed)
    });
}

void OspfOriginatorV3::addP2PLink(LsaBody& router, const OspfInterface& iface, const Neighbor& neighbor)
{
    std::get<RouterLsaV3>(router).links.push_back(RouterLinkV3{
        .type = OSPFV3_LINK_P2P,
        .metric = iface.cost.load(std::memory_order_relaxed),
        .interfaceId = iface.getIface().configs.key,
        .neighborInterfaceId = neighbor.neighborInterfaceId,
        .neighborRouterId = neighbor.routerID
    });
}

void OspfOriginatorV3::addStubLink(LsaBody& router, const OspfInterface& iface, bool fullMask)
{
    if (fullMask) return; // Full mask is only a v2 feature
    std::get<RouterLsaV3>(router).links.push_back(RouterLinkV3{
        .type = OSPFV3_LINK_STUB,
        .metric = iface.cost.load(std::memory_order_relaxed),
        .interfaceId = iface.getIface().configs.key,
        .neighborInterfaceId = 0,
        .neighborRouterId = 0
    });
}

void OspfOriginatorV3::addVirtualLink(LsaBody& router, const OspfInterface& iface, const Neighbor& vNbr)
{
    std::get<RouterLsaV3>(router).links.push_back(RouterLinkV3{
        .type = OSPFV3_LINK_VIRTUAL,
        .metric = iface.cost.load(std::memory_order_relaxed),
        .interfaceId = iface.getIface().configs.key,
        .neighborInterfaceId = vNbr.neighborInterfaceId,
        .neighborRouterId = vNbr.routerID
    });
}
}
