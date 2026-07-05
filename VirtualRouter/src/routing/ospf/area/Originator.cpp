// OspfOriginator.cpp

#include <TimeManager.h>
#include <VirtualRouter.h>

#include "Originator.h"
#include "Area.h"
#include "interface/Interface.h"
#include "ospf/OspfProcess.h"
#include "ospf/interface/OspfInterface.h"
#include "ospf/neighbor/Neighbor.h"
#include "interface/Interface.h"
#include "interface/configs/InterfaceType.hpp"
#include "ospf/OspfTypes.hpp"

namespace routing::ospf
{
Originator::Originator(Area& a) : area(a)
{
    auto& configs = a.getConfigs();
    auto type = configs.get<config::OspfArea::AREA_TYPE>().load();

    if (type == config::ospf::AreaType::NSSA || type == config::ospf::AreaType::TOTALLY_NSSA)
        nssaDefaultOriginate(configs.get<config::OspfArea::NSSA_DEFAULT_ORIGINATE>().load());
}

Originator::~Originator()
{
    nssaDefaultOriginate(false);
    cancelGroupPacing();
}

void Originator::cancelGroupPacing()
{
    for (auto& b : refreshBuckets)
    {
        if (b.timerId != 0)
            area.getScheduler().cancel(b.timerId);
        b.timerId = 0;
        b.keys.clear();
    }
    refreshBuckets.clear();
    keyToBucket.clear();
}

void Originator::scheduleForGroupPacing(const LsaKey& key)
{
    // Only self-originated lsas that still exist should be refreshed
    if (originationState.find(key) == originationState.end())
        return;
    if (keyToBucket.find(key) != keyToBucket.end())
        return;
    if (refreshBuckets.empty())
        return;

    uint32_t idx = static_cast<uint32_t>(std::hash<LsaKey>{}(key) % refreshBuckets.size());

    keyToBucket.emplace(key, idx);
    refreshBuckets[idx].keys.push_back(key);
}

void Originator::unscheduleForGroupPacing(const LsaKey& key)
{
    auto it = keyToBucket.find(key);
    if (it == keyToBucket.end())
        return;

    uint32_t idx = it->second;
    keyToBucket.erase(it);

    if (idx >= refreshBuckets.size())
        return;

    auto& vec = refreshBuckets[idx].keys;
    vec.erase(std::remove(vec.begin(), vec.end(), key), vec.end());
}

template <typename Policy>
void Originator::initGroupPacing()
{
    cancelGroupPacing();

    uint32_t groupIntervalSec = area.process().getConfigs().get<config::Ospf::LSA_GROUP_PACING>().load();
    uint32_t bucketCount = (OSPF_REFRESH_AGE + groupIntervalSec - 1) / groupIntervalSec;

    if (bucketCount == 0) bucketCount = 1;

    refreshBuckets.resize(bucketCount);

    const auto now = std::chrono::steady_clock::now();

    for (uint32_t i = 0; i < bucketCount; ++i)
    {
        auto firstFire = now + std::chrono::seconds(i * groupIntervalSec);

        refreshBuckets[i].timerId = area.getScheduler().postAfter(firstFire, [this, i](uint32_t tid)
        {
            handleGroupPackingBucket<Policy>(tid, i);
        });
    }

    for (const auto& [key, info] : originationState)
    {
        if (!info.expire)
            scheduleForGroupPacing(key);
    }
}

template <typename Policy>
void Originator::handleGroupPackingBucket(uint32_t tid, uint32_t bucketIndex)
{
    if (bucketIndex >= refreshBuckets.size())
        return;

    auto& bucket = refreshBuckets[bucketIndex];
    if (bucket.timerId != tid)
        return;

    std::vector<LsaKey> keys = bucket.keys;

    auto& ifaceMgr = area.process().getIfaceMgr();

    bool needsRouterRefresh = false;

    for (const auto& key : keys)
    {
        auto it = originationState.find(key);
        if (it == originationState.end())
        {
            unscheduleForGroupPacing(key);
            continue;
        }

        if (it->second.expire)
        {
            continue;
        }

        switch (key.lsaType)
        {
            case Policy::RouterLsaType:
            {
                needsRouterRefresh = true;
                break;
            }
            case Policy::NetworkLsaType:
            {
                auto* iface = ifaceMgr.getInterface(OspfInterfaceId{key.linkStateId, area.areaId});
                if (!iface)
                    removeNetworkLsa(key.linkStateId);
                else
                    addNetworkLsa(*iface, true);
                break;
            }
            case Policy::InterRouterType:
            {
                addAsbrLsa(key.linkStateId, true);
                break;
            }
            default:
            {
                it->second.refresh = true;
                processOriginatedLsa<Policy>(key);
                it = originationState.find(key);
                if (it != originationState.end())
                    it->second.refresh = false;
                break;
            }
        }
    }

    if (needsRouterRefresh)
        addRouterLsa(std::nullopt, true, true);

    uint32_t groupIntervalSec = area.process().getConfigs().get<config::Ospf::LSA_GROUP_PACING>().load();
    uint32_t bucketCount = static_cast<uint32_t>(refreshBuckets.size());
    auto period = std::chrono::seconds(bucketCount * groupIntervalSec);

    auto nextFire = std::chrono::steady_clock::now() + period;
    bucket.timerId = area.getScheduler().postAfter(nextFire, [this, bucketIndex](uint32_t tid2)
    {
        handleGroupPackingBucket<Policy>(tid2, bucketIndex);
    });
}

void Originator::addRouterLink(LsaBody& router, const OspfInterface& iface, bool refresh, bool attemptNetLsa)
{
    if (iface.getAreaId() != area.areaId) return;

    auto& ifaceConfigs = iface.getConfigs();

    bool prefixSuppression = iface.getBaseConfigs().get<config::OspfInterfaceBase::PREFIX_SUPPRESSION>().load();
    auto ntype = ifaceConfigs.get<config::OspfInterface::NETWORK>().load();
    const auto& ntable = iface.getNTable();

    if (ifaceConfigs.get<config::OspfInterface::PASSIVE>().load() ||
        iface.getIface().configs.interfaceType == interface::InterfaceType::LOOPBACK)
    {
        addStubLink(router, iface);
        return;
    }

    if (ntype == config::ospf::NetworkType::POINT_TO_POINT)
    {
        bool isVirtual = iface.isVirtual.load(std::memory_order_relaxed);
        for (auto& [rid, nbr] : ntable.neighbors)
        {
            if (nbr.getState() != Neighbor::State::FULL)
                continue;

            if (isVirtual)
                addVirtualLink(router, iface, nbr);
            else
            {
                addP2PLink(router, iface, nbr);
                if (!prefixSuppression)
                    addStubLink(router, iface);
            }
        }
        return;
    }

    if (ntype == config::ospf::NetworkType::POINT_TO_MULTIPOINT)
    {
        for (auto& [rid, nbr] : ntable.neighbors)
        {
            if (nbr.getState() != Neighbor::State::FULL)
                continue;

            addP2PLink(router, iface, nbr);
            if (!prefixSuppression)
                addStubLink(router, iface, true);
        }
    }

    if (ntype == config::ospf::NetworkType::BROADCAST ||
        ntype == config::ospf::NetworkType::NON_BROADCAST)
    {
        bool isDr = iface.isDr.load(std::memory_order_relaxed);
        bool haveDr = isDr;

        const Neighbor* drNbr = nullptr;
        if (!haveDr)
        {
            drNbr = ntable.lookup(iface.dr.rid.load(std::memory_order_relaxed));
            if (drNbr && drNbr->getState() == Neighbor::State::FULL)
                haveDr = true;
        }

        // ALL routers advertise a transit link if the network is operational
        if (haveDr)
        {
            addTransitLink(router, iface, drNbr);
            if (isDr)
            {
                if (attemptNetLsa)
                {
                    addNetworkLsa(iface, refresh);
                    return;
                }
            }
        }

        return;
    }
}

template <typename Policy>
void Originator::requestReorigination(const LsaKey& key)
{
    auto& cfgs = area.process().getConfigs();
    auto& info = originationState[key];
    auto& state = info.throttleInfo;

    uint32_t delayMs = cfgs.get<config::Ospf::LSA_THROTTLE_DELAY>().load();
    uint32_t holdMs = cfgs.get<config::Ospf::LSA_THROTTLE_HOLD>().load();
    uint32_t maxMs = cfgs.get<config::Ospf::LSA_THROTTLE_MAX>().load();

    auto now = std::chrono::steady_clock::now();

    // If already pending, nothing to do
    if (state.pending.load(std::memory_order_relaxed))
        return;

    uint32_t fireDelay = 0;

    // First re-origination
    if (state.lastOriginate == std::chrono::steady_clock::time_point{})
    {
        state.backoffMs = delayMs;
        fireDelay = delayMs;
    }
    else
    {
        // Enforce hold timer
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.lastOriginate).count();

        uint32_t holdRemaining = elapsed < holdMs ? static_cast<uint32_t>(holdMs - elapsed) : 0;

        state.backoffMs = state.backoffMs == 0
            ? delayMs
            : std::min(state.backoffMs * 2,  maxMs);

        fireDelay = std::max(state.backoffMs, holdRemaining);
    }

    state.pending = true;
    state.nextFire = now + std::chrono::milliseconds(fireDelay);

    state.timerId = area.getScheduler().postAfter(state.nextFire, [this, key](uint32_t)
    {
        this->runReorigination<Policy>(key);
    });
}

template <typename Policy>
void Originator::runReorigination(const LsaKey& key)
{
    auto it = originationState.find(key);
    if (it == originationState.end())
        return;

    processReoriginatedLsa<Policy>(key, it->second);

    // Re-lookup: processReoriginatedLsa may have erased the entry (expire or rollover)
    it = originationState.find(key);
    if (it == originationState.end())
        return;

    auto& throttle = it->second.throttleInfo;
    throttle.lastOriginate = std::chrono::steady_clock::now();
    throttle.pending.store(false, std::memory_order_release);
}

template <typename Policy>
void Originator::processReoriginatedLsa(const LsaKey& key, const OriginationInfo& info)
{
    LsaHeader hdr{};
    IncomingLsaContext ctx = {
        .key = key,
        .header = hdr
    };

    LsaRecord* existing = area.lsdb().find(ctx.key);

    if constexpr (std::is_same_v<Policy, PolicyV2>)
    {
        uint32_t options = area.getFlags().getFlags();
        if (key.lsaType == OSPFV2_LSA_NSSA)
            InterfaceFlagManager::setPropagate(options, true);
        InterfaceFlagManager::setDemandCircuits(options, area.dcCompatible.load(std::memory_order_relaxed));
        ctx.header.options = static_cast<uint8_t>(options);
    }
    ctx.selfOriginatedKey = true;
    ctx.checksumValid = true; // We compute the checksum ourselves; it is always valid

    // RFC 2328 §12.1.6: detect sequence number rollover before normal processing
    if (!info.expire && existing && existing->header.sequence == OSPF_MAX_SEQUENCE)
    {
        // Flush with MaxSequenceNumber, then schedule a full refresh to restart
        ctx.header.age      = OSPF_MAX_AGE;
        ctx.header.sequence = OSPF_MAX_SEQUENCE;
        ctx.info            = {.reason = FloodReason::FLUSH};
        LsaBody flushBody   = info.body;

        auto results = runLsaCalculations<Policy>(ctx.header, key, flushBody);
        ctx.header.length   = results.size;
        ctx.header.checksum = results.checksum;
        (void)area.processLsa<Policy>(ctx, flushBody);

        unscheduleForGroupPacing(key);
        originationState.erase(key);

        // Re-originate after a short delay; the LSDB will have no existing entry
        // so the new instance starts from InitialSequenceNumber.
        area.getScheduler().postAfter(
            std::chrono::steady_clock::now() + std::chrono::seconds(5),
            [this](uint32_t) { this->fullRefresh(); }
        );
        return;
    }

    ctx.header.age = info.expire ? OSPF_MAX_AGE : 0;
    FloodReason reason = info.expire ? FloodReason::FLUSH : info.refresh ? FloodReason::REFRESH : FloodReason::UPDATE;
    ctx.info = {.reason = reason};
    ctx.header.sequence = existing
        ? existing->header.sequence + 1 : OSPF_INITIAL_SEQUENCE;

    auto results = runLsaCalculations<Policy>(ctx.header, key, info.body);

    ctx.header.length = results.size;
    ctx.header.checksum = results.checksum;

    (void)area.processLsa<Policy>(ctx, info.body);

    if (info.expire)
    {
        unscheduleForGroupPacing(key);
        originationState.erase(key);
    }
}

template <typename Policy>
void Originator::originateLsa(const LsaKey& key, const LsaBody& body, bool expire)
{
    auto& info = originationState[key];
    info.body = body;
    info.expire = expire;
    processOriginatedLsa<Policy>(key);
}

template <typename Policy>
void Originator::processOriginatedLsa(const LsaKey& key)
{
    scheduleForGroupPacing(key);

    requestReorigination<Policy>(key);
}

void Originator::nssaDefaultOriginate(bool add)
{
    if (area.type != config::ospf::AreaType::NSSA && area.type != config::ospf::AreaType::TOTALLY_NSSA)
        return;
    if (nssaDefaultRoute.has_value() == add)
        return;
    if (!area.process().isABR() && add)
    {
        if (nssaDefaultRoute)
            nssaDefaultOriginate(false);
        return;
    }

    auto& configs = area.getConfigs();

    if (!nssaDefaultRoute.has_value())
        nssaDefaultRoute = area.process().monotonicIntraId.fetch_add(1, std::memory_order_release);

    ExternalOriginateContext ctx = {
        .lsId = nssaDefaultRoute.value(),
        .prefix = types::IPPrefix(area.process().getAF()),
        .metric = configs.get<config::OspfArea::NSSA_DEFAULT_METRIC>().load(),
        .tag = 0,
        .nextHop = std::nullopt,
        .metricIsE2 = configs.get<config::OspfArea::NSSA_DEFAULT_METRIC_TYPE>().load()
    };

    auto& process = area.process();
    if (process.isV3)
    {
        LsaKey key = process.buildExternalKey<PolicyV3>(ctx, true);
        auto& ext = originationState[key];
        ext.body = ExternalLsaV3();
        ext.expire = !add;
        process.buildExternalBody<PolicyV3>(ctx, std::get<ExternalLsaV3>(ext.body), true);
        processOriginatedLsa<PolicyV3>(key);
    }
    else
    {
        LsaKey key = process.buildExternalKey<PolicyV2>(ctx, true);
        auto& ext = originationState[key];
        ext.body = ExternalLsaV2();
        ext.expire = !add;
        process.buildExternalBody<PolicyV2>(ctx, std::get<ExternalLsaV2>(ext.body), true);
        processOriginatedLsa<PolicyV2>(key);
    }

    if (!add) nssaDefaultRoute.reset();
}

template void Originator::initGroupPacing<PolicyV2>();
template void Originator::initGroupPacing<PolicyV3>();

template void Originator::handleGroupPackingBucket<PolicyV2>(uint32_t, uint32_t);
template void Originator::handleGroupPackingBucket<PolicyV3>(uint32_t, uint32_t);

template void Originator::requestReorigination<PolicyV2>(const LsaKey&);
template void Originator::requestReorigination<PolicyV3>(const LsaKey&);

template void Originator::runReorigination<PolicyV2>(const LsaKey&);
template void Originator::runReorigination<PolicyV3>(const LsaKey&);

template void Originator::processReoriginatedLsa<PolicyV2>(const LsaKey&, const OriginationInfo&);
template void Originator::processReoriginatedLsa<PolicyV3>(const LsaKey&, const OriginationInfo&);

template void Originator::originateLsa<PolicyV2>(const LsaKey&, const LsaBody&, bool);
template void Originator::originateLsa<PolicyV3>(const LsaKey&, const LsaBody&, bool);

template void Originator::processOriginatedLsa<PolicyV2>(const LsaKey&);
template void Originator::processOriginatedLsa<PolicyV3>(const LsaKey&);
} // namespace routing
