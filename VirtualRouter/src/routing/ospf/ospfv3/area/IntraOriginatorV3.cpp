// IntraOriginatorV3.cpp

#include <RCU.hpp>
#include <VirtualRouter.h>

#include "IntraOriginatorV3.h"
#include "ospf/neighbor/Neighbor.h"
#include "ospf/interface/OspfInterfaceBase.h"
#include "ospf/area/Area.h"
#include "ospf/OspfProcess.h"
#include "interface/Interface.h"

namespace routing::ospf
{
IntraOriginatorV3::IntraOriginatorV3(OriginatorContext& ctx) : IntraOriginator(ctx)
{
    ctx.initGroupPacing<PolicyV3>();
    ctx.getInterOriginator().refreshStubDefaultOriginate<PolicyV3>(ctx);
    fullRefresh();
}

IntraOriginatorV3::~IntraOriginatorV3()
{
    context.getInterOriginator().setStubDefaultOriginate<PolicyV3>(context, false);
}

void IntraOriginatorV3::fullRefresh()
{
    addRouterLsa(std::nullopt, true, true);

    auto& ifmgr = context.getIfaceMgr();
    ifmgr.forEach([&](OspfInterfaceId id, const OspfInterfaceBase& iface) {
        if (id.area == context.area.areaId && !iface.isVirtualLink())
            addLinkLsa(static_cast<const OspfInterface&>(iface), true);
    });

    auto& interOriginator = context.getInterOriginator();
    interOriginator.refreshStubDefaultOriginate<PolicyV3>(context);
    interOriginator.refreshNssaDefaultOriginate(context);
    interOriginator.refreshAsbrs<PolicyV3>(context);
}

void IntraOriginatorV3::updateInterface(uint32_t ifaceId)
{
    addRouterLsa(ifaceId, false);

    // Refresh the Link LSA for this specific interface (virtual links have
    // no Link-LSA; see fullRefresh()).
    auto& ifmgr = context.getIfaceMgr();
    ifmgr.forEach([&](OspfInterfaceId id, const OspfInterfaceBase& iface) {
        if (id.area == context.area.areaId && id.interfaceId == ifaceId && !iface.isVirtualLink())
        {
            addLinkLsa(static_cast<const OspfInterface&>(iface), false);
            return true;
        }
        return false;
    });
}

void IntraOriginatorV3::addRouterLsa(std::optional<uint32_t> ifaceId, bool refresh, bool fullRefresh)
{
    constexpr size_t MAX_LINKS_PER_LSA = 32;

    uint32_t selfRid = context.area.process.getRouterId();

    LsaBody baseBody = RouterLsaV3{};
    std::get<RouterLsaV3>(baseBody).options = context.getAreaFlags();

    auto& ifmgr = context.getIfaceMgr();
    ifmgr.forEach([&](OspfInterfaceId id, const OspfInterfaceBase& iface) {
        if (id.area != context.area.areaId)
            return;
        addRouterLink(baseBody, iface, refresh, fullRefresh || (ifaceId.has_value() && ifaceId.value() == id.interfaceId));
    });

    std::vector<RouterLinkV3>& allLinks = std::get<RouterLsaV3>(baseBody).links;
    uniqueLinks(allLinks);

    std::set<RouterLinkV3> linkSet;
    for (const auto& l : allLinks)
        linkSet.insert(l);

    std::vector<std::pair<LsaKey, std::optional<bool>>> newLsas;
    uint32_t options = context.getAreaFlags();

    for (const auto& key : lastRouterLsas)
    {
        LsaBody& body = context.originationState[key].body;
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

        if (links.empty())
        {
            oldLsa.links.clear();
            routerLsidQueue.push_back(key.linkStateId);
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
    std::set<RouterLinkV3> alreadyPlaced;
    for (const auto& [key, expire] : newLsas)
    {
        const auto& lsa = std::get<RouterLsaV3>(context.originationState[key].body);
        alreadyPlaced.insert(lsa.links.begin(), lsa.links.end());
    }

    for (const auto& link : linkSet)
    {
        if (alreadyPlaced.contains(link)) continue;

        bool placed = false;

        for (auto& [key, expire] : newLsas)
        {
            auto& lsa = std::get<RouterLsaV3>(context.originationState[key].body);
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

        context.originationState[key].body = RouterLsaV3{};
        auto& lsa = std::get<RouterLsaV3>(context.originationState[key].body);
        lsa.options = options;
        lsa.links.push_back(link);
    }

    for (const auto& [key, expire] : newLsas)
    {
        if (!refresh && !expire.has_value())
            continue;
        bool e = expire.has_value() ? expire.value() : false;
        context.originationState[key].expire = e;
        context.processOriginatedLsa<PolicyV3>(key);
    }

    addRouterPrefixLsa(newLsas, refresh);

    lastRouterLsas.clear();
    lastRouterLsas.reserve(newLsas.size());
    for (const auto& [k, e] : newLsas)
        if (!e.has_value() || e.value() != true)
            lastRouterLsas.push_back(k);
}

void IntraOriginatorV3::addNetworkLsa(const OspfInterfaceBase& iface, bool refresh)
{
    uint32_t selfRid = context.area.process.getRouterId();

    LsaKey key(OSPFV3_LSA_NETWORK, iface.interfaceId, selfRid);

    LsaBody& lsa = context.originationState[key].body;
    LsaBody lastLsa = lsa;
    lsa = NetworkLsaV3();
    NetworkLsaV3& network = std::get<NetworkLsaV3>(lsa);

    network.options = context.getAreaFlags();
    network.attachedRouters.push_back(selfRid);

    auto& ntable = context.getIfaceMgr().getNTable(iface);
    network.attachedRouters = ntable.getNeighborRIDs();

    uniqueLinks(network.attachedRouters);

    
    if (!refresh && std::holds_alternative<NetworkLsaV3>(lastLsa) && std::get<NetworkLsaV3>(lastLsa) == network)
        return;

    networkLsas.insert(iface.interfaceId);

    context.processOriginatedLsa<PolicyV3>(key);
    addNetworkPrefixLsa(static_cast<const OspfInterface&>(iface), refresh);
}

uint32_t IntraOriginatorV3::findNextPrefixLsid()
{
    if (!prefixLsidQueue.empty())
    {
        uint32_t id = prefixLsidQueue.front();
        prefixLsidQueue.pop_front();
        return id;
    }
    return maxPrefixLsid++;
}

uint32_t IntraOriginatorV3::findNextRouterLsid()
{
    if (!routerLsidQueue.empty())
    {
        uint32_t id = routerLsidQueue.front();
        routerLsidQueue.pop_front();
        return id;
    }
    return maxRouterLsid++;
}

void IntraOriginatorV3::expire(LsaKey& key)
{
    auto& lsa = context.originationState[key];
    if (std::holds_alternative<IntraAreaPrefixLsa>(lsa.body) || std::holds_alternative<IntraAreaPrefixLsaV4>(lsa.body))
        prefixLsidQueue.push_back(key.linkStateId); // Set id available
    else if (std::holds_alternative<RouterLsaV3>(lsa.body))
        routerLsidQueue.push_back(key.linkStateId);
    lsa.expire = true;
    context.processOriginatedLsa<PolicyV3>(key);
}

void IntraOriginatorV3::removeNetworkLsa(uint32_t ifaceId)
{
    OspfInterfaceId id(ifaceId, context.area.areaId);
    if (!networkLsas.contains(ifaceId))
        return;

    LsaKey netKey = {OSPFV3_LSA_NETWORK, ifaceId, context.area.process.getRouterId()};

    context.originationState[netKey].expire = true;
    context.processOriginatedLsa<PolicyV3>(netKey);
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

void IntraOriginatorV3::addRouterPrefixLsa(std::vector<std::pair<LsaKey, std::optional<bool>>>& routerLsas, bool refresh)
{
    if (context.area.process.af == types::AddressFamily::IPv4)
        addRouterPrefixLsaImpl<IntraAreaPrefixLsaV4, IntraAreaPrefixV4, types::IPv4Prefix>(routerLsas, refresh);
    else
        addRouterPrefixLsaImpl<IntraAreaPrefixLsa, IntraAreaPrefix, types::IPv6Prefix>(routerLsas, refresh);
}

template <typename PrefixLsaBody, typename PrefixEntry, typename PrefixType>
void IntraOriginatorV3::addRouterPrefixLsaImpl(std::vector<std::pair<LsaKey, std::optional<bool>>>& routerLsas, bool refresh)
{
    constexpr size_t MAX_PREFIXES_PER_LSA = 32;
    constexpr bool isV4 = std::is_same_v<PrefixType, types::IPv4Prefix>;

    uint32_t selfRid = context.area.process.getRouterId();
    auto& ifaceMgr = context.getIfaceMgr();

    std::unordered_map<uint32_t, std::unordered_map<PrefixType, uint16_t>> prefixesByLsid;

    for (auto& [key, expire] : routerLsas)
    {
        if (!refresh && !expire.has_value()) continue;

        const auto& routerLsa = std::get<RouterLsaV3>(context.originationState[key].body);
        auto& out = prefixesByLsid[key.linkStateId];

        // Each link corresponds to one local interface
        for (const auto& link : routerLsa.links)
        {
            if (link.type == OSPFV3_LINK_TRANSIT || link.type == OSPFV3_LINK_VIRTUAL) continue;

            OspfInterfaceId ifaceId(link.interfaceId, context.area.areaId);

            const OspfInterfaceBase* baseIface = ifaceMgr.getInterface(ifaceId);
            if (!baseIface) continue;
            const OspfInterface* iface = static_cast<const OspfInterface*>(baseIface);

            if (ifaceMgr.getGlobalInterfaceConfigs(*iface).get<config::OspfGlobalInterface::PREFIX_SUPPRESSION>().load())
                continue;

            auto& config = ifaceMgr.getGlobalInterfaceConfigs(*iface);

            bool isP2MP = config.get<config::OspfGlobalInterface::BASE>().get().get<config::OspfInterface::NETWORK>().load() == config::ospf::NetworkType::POINT_TO_MULTIPOINT;

            uint16_t cost = iface->getCost();

            if constexpr (isV4)
            {
                std::unordered_set<types::IPv4Prefix> prefixes;
                prefixes.insert(iface->iface.configs.ipv4.getPrimaryPrefix(true));
                for (auto& p : iface->iface.configs.ipv4.getSecondaryPrefixList(true))
                    prefixes.insert(p);

                out.reserve(prefixes.size());

                for (auto pit = prefixes.begin(); pit != prefixes.end();)
                {
                    auto prefix = prefixes.extract(pit++);
                    if (isP2MP)
                    {
                        out.emplace(prefix.value(), cost);
                        prefix.value().prefixLength = 32;
                    }
                    out.emplace(std::move(prefix.value()), cost);
                }
            }
            else
            {
                auto prefixes = iface->iface.configs.ipv6.getRoutablePrefixSet(true);

                out.reserve(prefixes.size());

                for (auto pit = prefixes.begin(); pit != prefixes.end();)
                {
                    auto prefix = prefixes.extract(pit++);
                    if (isP2MP)
                    {
                        out.emplace(prefix.value(), cost);
                        prefix.value().prefixLength = 128;
                    }
                    out.emplace(std::move(prefix.value()), cost);
                }
            }
        }
    }

    std::vector<std::pair<LsaKey, std::optional<bool>>> newLsas;
    newLsas.reserve(lastRouterPrefixes.size() + 4);

    // Rebuild existing prefix lsas
    for (const auto& key : lastRouterPrefixes)
    {
        auto& oldLsa = std::get<PrefixLsaBody>(context.originationState[key].body);

        std::optional<bool> expire = std::nullopt;

        auto it = prefixesByLsid.find(oldLsa.referencedLinkStateId);
        std::vector<PrefixEntry> prefixes;
        if (it != prefixesByLsid.end())
        {
            auto& prefixMap = it->second;
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
            prefixLsidQueue.push_back(key.linkStateId);
            newLsas.emplace_back(key, true);
            continue;
        }

        if (!expire.value_or(false) && oldLsa.prefixes != prefixes)
            expire = false;

        PrefixLsaBody& newLsa = oldLsa;
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
                auto& lsa = std::get<PrefixLsaBody>(context.originationState[key].body);
                if (lsa.referencedLinkStateId != refLsid || lsa.prefixes.size() >= MAX_PREFIXES_PER_LSA) continue;
                lsa.prefixes.emplace_back(0, cost, prefix);
                expire = false;
                placed = true;
                break;
            }

            if (placed) continue;

            LsaKey key = {OSPFV3_LSA_INTRA_AREA_PREFIX, findNextPrefixLsid(), selfRid};
            newLsas.emplace_back(key, false);

            context.originationState[key].body = PrefixLsaBody{};
            PrefixLsaBody& lsa = std::get<PrefixLsaBody>(context.originationState[key].body);
            lsa.referencedLsaType = OSPFV3_LSA_ROUTER;
            lsa.referencedLinkStateId = refLsid;
            lsa.referencedAdvRouter = selfRid;
            lsa.prefixes.emplace_back(0, cost, prefix);
        }
    }

    for (const auto& [key, expire] : newLsas)
    {
        if (!refresh && !expire.has_value())
            continue;
        bool e = expire.has_value() ? expire.value() : false;
        context.originationState[key].expire = e;
        context.processOriginatedLsa<PolicyV3>(key);
    }

    lastRouterPrefixes.clear();
    lastRouterPrefixes.reserve(newLsas.size());
    for (const auto& [k, e] : newLsas)
        if (!e.has_value() || e.value() != true)
            lastRouterPrefixes.push_back(k);
}

void IntraOriginatorV3::addNetworkPrefixLsa(const OspfInterface& iface, bool refresh)
{
    if (context.area.process.af == types::AddressFamily::IPv4)
        addNetworkPrefixLsaImpl<IntraAreaPrefixLsaV4, IntraAreaPrefixV4, types::IPv4Prefix>(iface, refresh);
    else
        addNetworkPrefixLsaImpl<IntraAreaPrefixLsa, IntraAreaPrefix, types::IPv6Prefix>(iface, refresh);
}

template <typename PrefixLsaBody, typename PrefixEntry, typename PrefixType>
void IntraOriginatorV3::addNetworkPrefixLsaImpl(const OspfInterface& iface, bool refresh)
{
    constexpr size_t MAX_NETWORKS_PER_LSA = 32;
    constexpr bool isV4 = std::is_same_v<PrefixType, types::IPv4Prefix>;

    uint32_t selfRid = context.area.process.getRouterId();

    std::unordered_set<PrefixType> prefixSet;
    if constexpr (isV4)
    {
        prefixSet.insert(iface.iface.configs.ipv4.getPrimaryPrefix());
        for (auto& p : iface.iface.configs.ipv4.getSecondaryPrefixList())
            prefixSet.insert(p);
    }
    else
    {
        prefixSet = iface.iface.configs.ipv6.getRoutablePrefixSet();
    }
    uint32_t cost = iface.getCost();

    std::vector<std::pair<LsaKey, std::optional<bool>>> newLsas;
    newLsas.reserve(lastNetworkPrefixes.size() + 4);

    // Rebuild existing prefix lsas
    auto lastIt = std::find_if(lastNetworkPrefixes.begin(), lastNetworkPrefixes.end(),
        [&](const std::pair<uint32_t, std::vector<LsaKey>>& p) { return p.first == iface.id.interfaceId; });

    const auto& configs = context.getIfaceMgr().getGlobalInterfaceConfigs(iface);

    bool prefixSuppression = configs.get<config::OspfGlobalInterface::PREFIX_SUPPRESSION>().load();

    if (lastIt == lastNetworkPrefixes.end())
    {
        if (prefixSuppression) return;
        lastNetworkPrefixes.emplace_back(iface.interfaceId, std::vector<LsaKey>{});
        lastIt = std::prev(lastNetworkPrefixes.end());
    }

    if (configs.get<config::OspfGlobalInterface::PREFIX_SUPPRESSION>().load())
    {
        for (auto& k : lastIt->second)
            expire(k);
        lastIt->second.clear();
    }

    if (lastIt != lastNetworkPrefixes.end())
    {
        for (const auto& key : lastIt->second)
        {
            auto& oldLsa = std::get<PrefixLsaBody>(context.originationState[key].body);

            std::optional<bool> expire = std::nullopt;

            std::vector<PrefixEntry> prefixes;
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


            PrefixLsaBody& newLsa = oldLsa;
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
            auto& lsa = std::get<PrefixLsaBody>(context.originationState[key].body);
            if (lsa.prefixes.size() >= MAX_NETWORKS_PER_LSA) continue;
            lsa.prefixes.emplace_back(0, cost, prefix);
            placed = true;
            break;
        }

        if (placed) continue;

        LsaKey key = {OSPFV3_LSA_INTRA_AREA_PREFIX, findNextPrefixLsid(), selfRid};
        auto& body = context.originationState[key].body;
        body = PrefixLsaBody{};
        auto& lsa = std::get<PrefixLsaBody>(body);

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
        if (e) context.originationState[key].expire = true;
        context.processOriginatedLsa<PolicyV3>(key);
    }

    lastIt->second.clear();
    lastIt->second.reserve(newLsas.size());
    for (const auto& [k, e] : newLsas)
        if (!e.has_value() || e.value() != true)
            lastIt->second.push_back(k);
}

void IntraOriginatorV3::addLinkLsa(const OspfInterface& iface, bool refresh)
{
    // Link LSA is link-local scoped (type 0x0008), one per interface
    uint32_t selfRid = context.area.process.getRouterId();

    LsaKey key{OSPFV3_LSA_LINK, iface.interfaceId, selfRid};

    // Build Link LSA body
    LinkLsa lsa;
    lsa.priority = iface.getPriority();
    lsa.options = context.getAreaFlags();

    // Use the interface's link-local IPv6 address
    const types::IPPrefix& ifAddr = iface.interfaceAddress;
    lsa.localLink = types::IPv6Address(ifAddr.v6());

    // Add all prefixes associated with this interface
    auto& ifcConfigs = iface.iface.configs.ipv6;
    uint8_t pfxBuf[16];
    uint8_t pfxLen = ifcConfigs.getGlobalUnicastPrefix(pfxBuf);
    if (pfxLen > 0)
    {
        LinkLsaPrefix entry;
        entry.options = 0;
        entry.prefix = types::IPv6Prefix(pfxBuf, pfxLen);
        lsa.prefixes.push_back(entry);
    }

    LsaBody body = lsa;
    context.originateLsa<PolicyV3>(key, body, false);
}

void IntraOriginatorV3::addTransitLink(LsaBody& router, const OspfInterfaceBase& baseIface, const Neighbor* nbr)
{
    const OspfInterface& iface = static_cast<const OspfInterface&>(baseIface);
    uint32_t intId = nbr ? nbr->neighborInterfaceId : iface.id.interfaceId;
    std::get<RouterLsaV3>(router).links.push_back(RouterLinkV3{
        .type = OSPFV3_LINK_TRANSIT,
        .metric = iface.getCost(),
        .interfaceId = iface.id.interfaceId,
        .neighborInterfaceId = intId,
        .neighborRouterId = iface.getDrRid()
    });
}

void IntraOriginatorV3::addP2PLink(LsaBody& router, const OspfInterfaceBase& iface, const Neighbor& neighbor)
{
    std::get<RouterLsaV3>(router).links.push_back(RouterLinkV3{
        .type = OSPFV3_LINK_P2P,
        .metric = iface.getCost(),
        .interfaceId = iface.id.interfaceId,
        .neighborInterfaceId = neighbor.neighborInterfaceId,
        .neighborRouterId = neighbor.routerID
    });
}

void IntraOriginatorV3::addStubLink(LsaBody& router, const OspfInterfaceBase& baseIface, bool fullMask)
{
    if (fullMask) return; // Full mask is only a v2 feature
    const OspfInterface& iface = static_cast<const OspfInterface&>(baseIface);
    auto cost = context.getIfaceMgr().getGlobalInterfaceConfigs(iface).get<config::OspfGlobalInterface::BASE>().get().get<config::OspfInterface::COST>();
    uint16_t metric = context.getProcessConfigs().get<config::Ospf::MAX_METRIC_INCLUDE_STUB>().load()
        ? 0xFFFF : cost.hasValue() ? cost.load() : iface.getCost();
    std::get<RouterLsaV3>(router).links.push_back(RouterLinkV3{
        .type = OSPFV3_LINK_STUB,
        .metric = metric,
        .interfaceId = iface.id.interfaceId,
        .neighborInterfaceId = 0,
        .neighborRouterId = 0
    });
}

void IntraOriginatorV3::addVirtualLink(LsaBody& router, const OspfInterfaceBase& iface, const Neighbor& vNbr)
{
    std::get<RouterLsaV3>(router).links.push_back(RouterLinkV3{
        .type = OSPFV3_LINK_VIRTUAL,
        .metric = iface.getCost(),
        .interfaceId = iface.id.interfaceId,
        .neighborInterfaceId = vNbr.neighborInterfaceId,
        .neighborRouterId = vNbr.routerID
    });
}    
} // namespace routing
