// OriginatorV3.cpp

#include <VirtualRouter.h>

#include "OriginatorV3.h"
#include "ospf/neighbor/Neighbor.h"
#include "ospf/interface/OspfInterface.h"
#include "ospf/area/Area.h"
#include "ospf/OspfProcess.h"
#include "interface/Interface.h"

namespace OSPF
{
OriginatorV3::OriginatorV3(Area& area) : Originator(area)
{
    initGroupPacing<PolicyV3>();
    auto& configs = area.getConfigs();
    auto type = configs.get<Config::OspfArea::AREA_TYPE>().load();
    if (type == AreaType::TOTALLY_STUB || type == AreaType::TOTALLY_STUB)
        addStubDefaultRoute(true);
    fullRefresh();
}

OriginatorV3::~OriginatorV3()
{
    addStubDefaultRoute(false);
}

void OriginatorV3::fullRefresh()
{
    addRouterLsa(std::nullopt, true, true);

    // Originate Link LSAs for all interfaces in this area
    auto& ifmgr = area.process().getIfaceMgr();
    for (auto& [id, iface] : ifmgr.ospfInterfaceList)
    {
        if (id.area == area.areaId)
            addLinkLsa(iface, true);
    }

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
            addAsbrLsa(asbr.first, true);
}

void OriginatorV3::updateInterface(uint32_t ifaceId)
{
    addRouterLsa(ifaceId, false);

    // Refresh the Link LSA for this specific interface
    auto& ifmgr = area.process().getIfaceMgr();
    for (auto& [id, iface] : ifmgr.ospfInterfaceList)
    {
        if (id.area == area.areaId && id.interfaceId == ifaceId)
        {
            addLinkLsa(iface, false);
            break;
        }
    }
}

void OriginatorV3::addLinkLsa(const OspfInterface& iface, bool refresh)
{
    // Link LSA is link-local scoped (type 0x0008), one per interface
    uint32_t selfRid = area.process().getRouterId();

    LsaKey key{OSPFV3_LSA_LINK, iface.interfaceId, selfRid};

    // Build Link LSA body
    LinkLsa lsa;
    lsa.priority = iface.getConfigs().get<Config::OspfInterface::PRIORITY>().load();
    lsa.options = area.getFlags().getFlags();

    // Use the interface's link-local IPv6 address
    const IPPrefix& ifAddr = iface.interfaceAddress;
    lsa.localLink = IPv6Address(ifAddr.v6());

    // Add all prefixes associated with this interface
    auto& ifcConfigs = iface.getIface().configs.ipv6;
    uint8_t pfxBuf[16];
    uint8_t pfxLen = ifcConfigs.getGlobalUnicastPrefix(pfxBuf);
    if (pfxLen > 0)
    {
        LinkLsaPrefix entry;
        entry.options = 0;
        entry.prefix = IPv6Prefix(pfxBuf, pfxLen);
        lsa.prefixes.push_back(entry);
    }

    LsaBody body = lsa;
    originateLsa<PolicyV3>(key, body, false);
}

void OriginatorV3::addRouterLsa(std::optional<uint32_t> ifaceId, bool refresh, bool fullRefresh)
{
    constexpr size_t MAX_LINKS_PER_LSA = 32;

    uint32_t selfRid = area.process().getRouterId();

    LsaBody baseBody = RouterLsaV3{};
    std::get<RouterLsaV3>(baseBody).options = area.getFlags().getFlags();

    auto& ifmgr = area.process().getIfaceMgr();
    for (auto& [id, iface] : ifmgr.ospfInterfaceList)
    {
        if (id.area != area.areaId)
            continue;
        addRouterLink(baseBody, iface, refresh, fullRefresh || (ifaceId.has_value() && ifaceId.value() == id.interfaceId));
    }

    std::vector<RouterLinkV3>& allLinks = std::get<RouterLsaV3>(baseBody).links;
    uniqueLinks(allLinks);

    std::set<RouterLinkV3> linkSet;
    for (const auto& l : allLinks)
        linkSet.insert(l);

    std::vector<std::pair<LsaKey, std::optional<bool>>> newLsas;
    uint32_t options = area.getFlags().getFlags();

    for (const auto& key : lastRouterLsas)
    {
        LsaBody& body = originationState[key].body;
        RouterLsaV3& oldLsa = std::get<RouterLsaV3>(body);

        std::optional<bool> expire = std::nullopt;

        std::vector<RouterLinkV3> links;
        links.reserve(oldLsa.links.size());
        for (const auto& oldLink : oldLsa.links)
        {
            auto it = linkSet.find(oldLink);
            if (it != linkSet.end())
                links.push_back(oldLink);
            else
                expire = false;
        }

        // If links are empty, mark the lsa as removal
        if (links.empty())
        {
            newLsas.emplace_back(key, true);
            continue;
        }

        if (!expire.value_or(false) && (oldLsa.options != options || oldLsa.links != links))
            expire = false;

        RouterLsaV3& newLsa = std::get<RouterLsaV3>(body);
        newLsa.links = std::move(links);
        newLsa.options = options;

        newLsas.emplace_back(key, expire);
    }

    for (const auto& link : linkSet)
    {
        bool placed = false;

        for (auto& [key, expire] : newLsas)
        {
            auto& lsa = std::get<RouterLsaV3>(originationState[key].body);
            if (lsa.links.size() < MAX_LINKS_PER_LSA)
            {
                expire = false;
                lsa.links.push_back(link);
                placed = true;
                break;
            }
        }

        if (placed) continue;

        LsaKey key = {OSPFV3_LSA_ROUTER, findNextRouterLsid(), selfRid};
        newLsas.emplace_back(key, false);

        auto& lsa = std::get<RouterLsaV3>(originationState[key].body);
        lsa.options = options;
        lsa.links.push_back(link);
    }

    for (const auto& [key, expire] : newLsas)
    {
        if (!refresh && !expire.has_value())
            continue;
        bool e = expire.has_value() ? expire.value() : false;
        if (e) originationState[key].expire = true;
        processOriginatedLsa<PolicyV3>(key);
    }

    addRouterPrefixLsa(newLsas, refresh);

    lastRouterLsas.clear();
    lastRouterLsas.reserve(newLsas.size());
    for (const auto& [k, e] : newLsas)
        if (!e.has_value() || e.value() != true)
            lastRouterLsas.push_back(k);
}

void OriginatorV3::addRouterPrefixLsa(std::vector<std::pair<LsaKey, std::optional<bool>>>& routerLsas, bool refresh)
{
    constexpr size_t MAX_PREFIXES_PER_LSA = 32;

    uint32_t selfRid = area.process().getRouterId();
    auto& ifaceMgr = area.process().getIfaceMgr();

    std::unordered_map<uint32_t, std::unordered_map<IPv6Prefix, uint16_t>> prefixesByLsid;

    // Iterate Router-LSAs directly, this preserves LSID ownership
    for (auto& [key, expire] : routerLsas)
    {
        if (!expire.has_value()) continue;

        const auto& routerLsa = std::get<RouterLsaV3>(originationState[key].body);
        auto& out = prefixesByLsid[key.linkStateId];

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
                    out.emplace(std::move(prefix.value()), cost);
                    prefix.value().prefixLength = 128;
                }
                out.emplace(std::move(prefix.value()), cost);
            }
        }
    }

    std::vector<std::pair<LsaKey, std::optional<bool>>> newLsas;
    newLsas.reserve(lastRouterPrefixes.size() + 4);

    // Rebuild existing prefix lsas
    for (const auto& key : lastRouterPrefixes)
    {
        auto& oldLsa = std::get<IntraAreaPrefixLsa>(originationState[key].body);
    
        std::optional<bool> expire = std::nullopt;

        auto it = prefixesByLsid.find(oldLsa.referencedLinkStateId);
        std::vector<IntraAreaPrefix> prefixes;
        if (it != prefixesByLsid.end())
        {
            auto prefixMap = it->second;
            for (const auto& oldPrefix : oldLsa.prefixes)
            {
                if (auto pit = prefixMap.find(oldPrefix.prefix); pit != prefixMap.end())
                {
                    prefixes.emplace_back(0, pit->second, pit->first);
                    prefixMap.erase(pit);
                }
                else expire = false;
            }
        }
        else
        {
            expire = true;
        }

        if (prefixes.empty())
        {
            expire = true;
            continue;
        }

        if (!expire.value_or(false) && oldLsa.prefixes != prefixes)
            expire = false;

        IntraAreaPrefixLsa& newLsa = oldLsa;
        newLsa.prefixes = std::move(prefixes);

        newLsas.emplace_back(key, expire);
    }

    // Compare current against old
    for (auto& [refLsid, prefixMap] : prefixesByLsid)
    {
        for (auto& [prefix, cost] : prefixMap)
        {
            bool placed = false;

            for (auto& [key, expire] : newLsas)
            {
                auto& lsa = std::get<IntraAreaPrefixLsa>(originationState[key].body);
                if (lsa.referencedLinkStateId != refLsid || lsa.prefixes.size() < MAX_PREFIXES_PER_LSA) continue;
                lsa.prefixes.emplace_back(0, cost, prefix);
                expire = false;
                placed = true;
                break;
            }
        
            if (placed) continue;

            LsaKey key = {OSPFV3_LSA_INTRA_AREA_PREFIX, findNextPrefixLsid(), selfRid};
            newLsas.emplace_back(key, false);

            IntraAreaPrefixLsa& lsa = std::get<IntraAreaPrefixLsa>(originationState[key].body);
            lsa.referencedLsaType = OSPFV3_LSA_ROUTER;
            lsa.referencedLinkStateId = refLsid;
            lsa.referencedAdvRouter = selfRid;

            auto& newLsa = std::get<IntraAreaPrefixLsa>(originationState[key].body);
            newLsa.prefixes.emplace_back(0, cost, prefix);
        }
    }

    for (const auto& [key, expire] : newLsas)
    {
        if (!refresh && !expire.has_value())
            continue;
        bool e = expire.has_value() ? expire.value() : false;
        if (e) originationState[key].expire = true;
        processOriginatedLsa<PolicyV3>(key);
    }

    lastRouterPrefixes.reserve(newLsas.size());
    for (const auto& [k, e] : newLsas)
        if (!e.has_value() || e.value() != true)
            lastRouterPrefixes.push_back(k);
}

void OriginatorV3::addNetworkLsa(const OspfInterface& iface, bool refresh)
{
    uint32_t selfRid = area.process().getRouterId();

    LsaKey key(OSPFV3_LSA_NETWORK, iface.interfaceId, selfRid);

    LsaBody& lsa = originationState[key].body;
    LsaBody lastLsa = lsa;
    lsa = NetworkLsaV3();
    NetworkLsaV3& network = std::get<NetworkLsaV3>(lsa);

    network.options = area.getFlags().getFlags();
    network.attachedRouters.push_back(selfRid);

    {
        auto& ntable = iface.getNTable();
        for (const auto& [rid, nbr] : ntable.neighbors)
            if (nbr.getState() == Neighbor::State::FULL)
                network.attachedRouters.push_back(rid);
    }

    uniqueLinks(network.attachedRouters);

    
    if (!refresh && std::get<NetworkLsaV3>(lastLsa) == network)
        return;

    networkLsas.insert(iface.interfaceId);

    processOriginatedLsa<PolicyV3>(key); 
    addNetworkPrefixLsa(iface, refresh);
}

void OriginatorV3::addExternal(uint32_t asbr, uint32_t lsid, bool remove)
{
    if (!remove)
    {
        addAsbrLsa(asbr);
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
            expire(asbrLsa);
            externalRoutes.erase(asbr);
            asbrLsas.erase(asbr);
        }
    }
}

void OriginatorV3::addNetworkPrefixLsa(const OspfInterface& iface, bool refresh)
{
    constexpr size_t MAX_NETWORKS_PER_LSA = 32;

    uint32_t selfRid = area.process().getRouterId();

    std::unordered_set<IPv6Prefix> prefixSet = iface.getIface().configs.ipv6.getRoutablePrefixSet();
    uint32_t cost = iface.getConfigs().get<Config::OspfInterface::COST>().load();

    std::vector<std::pair<LsaKey, std::optional<bool>>> newLsas;
    newLsas.reserve(lastNetworkPrefixes.size() + 4);

    // Rebuild existing prefix lsas
    auto lastIt = std::find_if(lastNetworkPrefixes.begin(), lastNetworkPrefixes.end(),
        [&](const std::pair<uint32_t, std::vector<LsaKey>>& p) { return p.first == iface.id.interfaceId; });

    bool prefixSuppression = iface.getBaseConfigs().get<Config::OspfInterfaceBase::PREFIX_SUPPRESSION>().load();

    if (lastIt == lastNetworkPrefixes.end())
    {
        if (prefixSuppression) return;
        lastNetworkPrefixes.emplace_back(iface.interfaceId, std::vector<LsaKey>{});
        lastIt = std::prev(lastNetworkPrefixes.end());
    }

    if (iface.getBaseConfigs().get<Config::OspfInterfaceBase::PREFIX_SUPPRESSION>().load())
    {
        for (auto& k : lastIt->second)
            expire(k);
        lastIt->second.clear();
    }

    if (lastIt != lastNetworkPrefixes.end())
    {
        for (const auto& key : lastIt->second)
        {
            auto& oldLsa = std::get<IntraAreaPrefixLsa>(originationState[key].body);

            std::optional<bool> expire = std::nullopt;

            std::vector<IntraAreaPrefix> prefixes;
            for (const auto& oldPrefix : oldLsa.prefixes)
            {
                auto pit = prefixSet.find(oldPrefix.prefix);
                if (pit != prefixSet.end())
                {
                    prefixes.emplace_back(0, cost, *pit);
                    prefixSet.erase(pit);
                }
                else expire = false;
            }

            if (prefixes.empty())
            {
                expire = true;
                continue;
            }

            if (!expire.value_or(false) && oldLsa.prefixes != prefixes)
                expire = false;


            IntraAreaPrefixLsa& newLsa = oldLsa;
            newLsa.referencedLsaType = OSPFV3_LSA_NETWORK;
            newLsa.referencedLinkStateId = iface.interfaceId;
            newLsa.referencedAdvRouter = selfRid;
            newLsa.prefixes = std::move(prefixes);

            newLsas.emplace_back(key, expire);
        }
    }

    for (auto& prefix : prefixSet)
    {
        bool placed = false;

        for (auto& [key, expire] : newLsas)
        {
            auto& lsa = std::get<IntraAreaPrefixLsa>(originationState[key].body);
            if (lsa.prefixes.size() >= MAX_NETWORKS_PER_LSA) continue;
            lsa.prefixes.emplace_back(0, cost, prefix);
            placed = true;
            break;
        }
    
        if (placed) continue;

        LsaKey key = {OSPFV3_LSA_INTRA_AREA_PREFIX, findNextPrefixLsid(), selfRid};
        auto& body = originationState[key].body;
        body = IntraAreaPrefixLsa{};
        auto& lsa = std::get<IntraAreaPrefixLsa>(body);

        lsa.referencedLsaType = OSPFV3_LSA_NETWORK;
        lsa.referencedLinkStateId = key.linkStateId;
        lsa.referencedAdvRouter = selfRid;

        newLsas.emplace_back(key, false);
    }

    for (const auto& [key, expire] : newLsas)
    {
        if (!refresh && !expire.has_value())
            continue;
        bool e = expire.has_value() ? expire.value() : false;
        if (e) originationState[key].expire = true;
        processOriginatedLsa<PolicyV3>(key);
    }

    lastIt->second.clear();
    lastIt->second.reserve(newLsas.size());
    for (const auto& [k, e] : newLsas)
        if (!e.has_value() || e.value() != true)
            lastIt->second.push_back(k);
}

void OriginatorV3::originateSummary(uint32_t lsid, const IPPrefix& prefix, uint32_t cost, bool expire)
{
    LsaKey key;

    key.lsaType = OSPFV3_LSA_INTRA_AREA_PREFIX;
    key.linkStateId = lsid;
    key.advertisingRouter = area.process().getRouterId();

    auto& info = originationState[key];
    auto& body = info.body;
    body = InterAreaPrefixLsa();
    auto& summary = std::get<InterAreaPrefixLsa>(body);

    summary.metric = cost;
    summary.options = 0;
    summary.prefix = IPv6Prefix(prefix.v6(), prefix.prefixLength, true);

    info.expire = expire;

    requestReorigination<PolicyV3>(key);
}

void OriginatorV3::translateNssaToExternal(const LsaKey& key7, const LsaBody& body7, bool expire)
{
    if (std::get<ExternalLsaV3>(body7).prefix.prefixLength == 0 && std::get<ExternalLsaV3>(body7).prefix.addr == 0 &&
        !area.getConfigs().get<Config::OspfArea::NSSA_DEFAULT_ONLY>().load())
        return;

    if (area.process().getConfigs().get<Config::Ospf::LRC_NSSA_TRANSLATION>().load())
    {
        auto& ext7 = std::get<ExternalLsaV3>(body7);
        auto& base = area.process();

        __uint128_t lookupAddr = (!ext7.forwardingAddress.has_value() || ext7.forwardingAddress->addr == 0)
            ? ext7.prefix.addr : ext7.forwardingAddress->addr;

        if (!base.routingInstance->getRib().lookup(lookupAddr))
            return;
    }

    LsaKey key5;
    
    key5.lsaType = OSPFV3_LSA_AS_EXTERNAL;
    key5.linkStateId = key7.linkStateId;
    key5.advertisingRouter = area.process().getRouterId();

    auto& info = originationState[key5];
    auto& body5 = info.body;
    body5 = body7;
    auto& ext5 = std::get<ExternalLsaV3>(body5);

    ext5.options &= ~0x08;
    if (ext5.forwardingAddress.has_value() && !area.isValidForwardAddress(ext5.forwardingAddress.value()))
        ext5.forwardingAddress.reset();

    info.expire = expire;

    processOriginatedLsa<PolicyV3>(key5);
}

void OriginatorV3::addStubDefaultRoute(bool add)
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

    if (!stubDefaultRoute.has_value())
    {
        LsaKey key;
        key.lsaType = OSPFV3_LSA_INTER_AREA_PREFIX;
        key.linkStateId = 0;
        key.advertisingRouter = area.process().getRouterId();
        stubDefaultRoute = key;
    }

    auto& info = originationState[stubDefaultRoute.value()];
    LsaBody& body = info.body;
    body = InterAreaPrefixLsa();

    auto& summary = std::get<InterAreaPrefixLsa>(body);

    summary.metric = area.getConfigs().get<Config::OspfArea::DEFAULT_COST>().load();
    summary.options = 0;
    summary.prefix = IPv6Prefix{};

    info.expire = !add;

    processOriginatedLsa<PolicyV3>(stubDefaultRoute.value());

    if (!add) stubDefaultRoute.reset();
}

void OriginatorV3::expire(LsaKey& key)
{
    auto& lsa = originationState[key];
    if (std::holds_alternative<IntraAreaPrefixLsa>(lsa.body))
        prefixLsidQueue.push_back(key.linkStateId); // Set id available
    else if (std::holds_alternative<RouterLsaV3>(lsa.body))
        routerLsidQueue.push_back(key.linkStateId);
    lsa.expire = true;
    processOriginatedLsa<PolicyV3>(key);
}

void OriginatorV3::addAsbrLsa(uint32_t asbr, bool refresh)
{
    uint32_t metric = area.process().table.lookupDistance(asbr);
    if (metric == 0) return;
    auto it = asbrLsas.find(asbr);

    LsaKey key(OSPFV3_LSA_INTER_AREA_ROUTER, asbr, area.process().getRouterId());

    auto& info = originationState[key];
    LsaBody& lsa = info.body;
    LsaBody lastLsa = lsa;
    lsa = InterAreaRouterLsa{};
    auto& asbrLsa = std::get<InterAreaRouterLsa>(lsa);
    asbrLsa.destinationRouterId = asbr;
    asbrLsa.metric = metric;

    if (!refresh && it != asbrLsas.end() && std::get<InterAreaRouterLsa>(lastLsa) == asbrLsa && key == it->second)
        return;

    asbrLsas[asbr] = key;
    processOriginatedLsa<PolicyV3>(key);
}

void OriginatorV3::removeNetworkLsa(uint32_t ifaceId)
{
    OspfInterfaceId id(ifaceId, area.areaId);
    if (!networkLsas.contains(ifaceId))
        return;

    LsaKey netKey = {OSPFV3_LSA_NETWORK, ifaceId, area.process().getRouterId()};

    originationState[netKey].expire = true;
    processOriginatedLsa<PolicyV3>(netKey);
    networkLsas.erase(ifaceId);

    auto pit = std::find_if(lastNetworkPrefixes.begin(), lastNetworkPrefixes.end(),
        [&](const std::pair<uint32_t, std::vector<LsaKey>>& i) { return i.first == ifaceId; });
    if (pit == lastNetworkPrefixes.end()) return;
    for (auto& key : pit->second)
    {
        expire(key);
    }
    lastNetworkPrefixes.erase(pit);
}

uint32_t OriginatorV3::findNextRouterLsid()
{
    if (!routerLsidQueue.empty())
    {
        uint32_t id = routerLsidQueue.front();
        routerLsidQueue.pop_front();
        return id;
    }
    return ++maxRouterLsid;
}

uint32_t OriginatorV3::findNextPrefixLsid()
{
    if (!prefixLsidQueue.empty())
    {
        uint32_t id = prefixLsidQueue.front();
        prefixLsidQueue.pop_front();
        return id;
    }
    return ++maxPrefixLsid;
}

void OriginatorV3::addTransitLink(LsaBody& router, const OspfInterface& iface, const Neighbor* nbr)
{ 
    uint32_t intId = nbr ? nbr->neighborInterfaceId : iface.id.interfaceId;
    std::get<RouterLsaV3>(router).links.push_back(RouterLinkV3{
        .type = OSPFV3_LINK_TRANSIT,
        .metric = iface.cost,
        .interfaceId = iface.id.interfaceId,
        .neighborInterfaceId = intId,
        .neighborRouterId = iface.dr.rid.load(std::memory_order_relaxed)
    });
}

void OriginatorV3::addP2PLink(LsaBody& router, const OspfInterface& iface, const Neighbor& neighbor)
{
    std::get<RouterLsaV3>(router).links.push_back(RouterLinkV3{
        .type = OSPFV3_LINK_P2P,
        .metric = iface.cost,
        .interfaceId = iface.getIface().configs.key,
        .neighborInterfaceId = neighbor.neighborInterfaceId,
        .neighborRouterId = neighbor.routerID
    });
}

void OriginatorV3::addStubLink(LsaBody& router, const OspfInterface& iface, bool fullMask)
{
    if (fullMask) return; // Full mask is only a v2 feature
    auto& cost = iface.getConfigs().get<Config::OspfInterface::COST>();
    uint16_t metric = area.process().getConfigs().get<Config::Ospf::MAX_METRIC_INCLUDE_STUB>().load()
        ? 0xFFFF : cost.hasValue() ? cost.load() : iface.cost;
    std::get<RouterLsaV3>(router).links.push_back(RouterLinkV3{
        .type = OSPFV3_LINK_STUB,
        .metric = metric,
        .interfaceId = iface.getIface().configs.key,
        .neighborInterfaceId = 0,
        .neighborRouterId = 0
    });
}

void OriginatorV3::addVirtualLink(LsaBody& router, const OspfInterface& iface, const Neighbor& vNbr)
{
    std::get<RouterLsaV3>(router).links.push_back(RouterLinkV3{
        .type = OSPFV3_LINK_VIRTUAL,
        .metric = iface.cost,
        .interfaceId = iface.getIface().configs.key,
        .neighborInterfaceId = vNbr.neighborInterfaceId,
        .neighborRouterId = vNbr.routerID
    });
}
}
