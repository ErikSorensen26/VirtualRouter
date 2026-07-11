// OriginatorContext.cpp

#include "OriginatorContext.h"
#include "ospf/OspfTypes.hpp"
#include "Area.h"
#include "IntraOriginator.h"
#include "configs/registry/router/OspfRegistry.h"
#include "ospf/OspfProcess.h"

namespace routing::ospf
{
OriginatorContext::OriginatorContext(Area& a)
    : area(a)
{}

OriginatorContext::~OriginatorContext()
{
    cancelGroupPacing();

    for (auto& [key, info] : originationState)
    {
        if (info.throttleInfo.timerId != 0)
            area.scheduler.cancel(info.throttleInfo.timerId);
    }
}

template <typename Policy>
void OriginatorContext::originateLsa(const LsaKey& key, const LsaBody& body, bool expire)
{
    auto& info = originationState[key];
    info.body = body;
    info.expire = expire;
    processOriginatedLsa<Policy>(key);
}

template <typename Policy>
void OriginatorContext::initGroupPacing()
{
    cancelGroupPacing();

    uint32_t groupIntervalSec = area.getProcessConfigs().get<config::Ospf::LSA_GROUP_PACING>().load();
    uint32_t bucketCount = (OSPF_REFRESH_AGE + groupIntervalSec - 1) / groupIntervalSec;

    if (bucketCount == 0) bucketCount = 1;

    refreshBuckets.resize(bucketCount);

    const auto now = std::chrono::steady_clock::now();

    for (uint32_t i = 0; i < bucketCount; ++i)
    {
        auto firstFire = now + std::chrono::seconds(i * groupIntervalSec);

        refreshBuckets[i].timerId = area.scheduler.postAfter(firstFire, [this, i](uint32_t tid)
        {
            handleGroupPacingBucket<Policy>(tid, i);
        });
    }

    for (const auto& [key, info] : originationState)
    {
        if (!info.expire)
            scheduleForGroupPacing(key);
    }
}

void OriginatorContext::cancelGroupPacing()
{
    for (auto& b : refreshBuckets)
    {
        if (b.timerId != 0)
            area.scheduler.cancel(b.timerId);
        b.timerId = 0;
        b.keys.clear();
    }
    refreshBuckets.clear();
    keyToBucket.clear();
}

void OriginatorContext::scheduleForGroupPacing(const LsaKey& key)
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

void OriginatorContext::unscheduleForGroupPacing(const LsaKey& key)
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
void OriginatorContext::handleGroupPacingBucket(uint32_t tid, uint32_t bucketIndex)
{
    if (bucketIndex >= refreshBuckets.size())
        return;

    auto& bucket = refreshBuckets[bucketIndex];
    if (bucket.timerId != tid)
        return;

    std::vector<LsaKey> keys = bucket.keys;

    bool needsRouterRefresh = false;

    InterOriginator& interOriginator = area.getInterOriginator();

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
                const auto* iface = area.getIfaceMgr().getInterface(OspfInterfaceId{key.linkStateId, area.areaId});
                if (!iface)
                    area.originator.removeNetworkLsa(key.linkStateId);
                else
                    area.originator.addNetworkLsa(*iface, true);
                break;
            }
            case Policy::InterRouterType:
            {
                interOriginator.addAsbrLsa<Policy>(*this, key.linkStateId, true);
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
        area.originator.addRouterLsa(std::nullopt, true, true);

    uint32_t groupIntervalSec = area.getProcessConfigs().get<config::Ospf::LSA_GROUP_PACING>().load();
    uint32_t bucketCount = static_cast<uint32_t>(refreshBuckets.size());
    auto period = std::chrono::seconds(bucketCount * groupIntervalSec);

    auto nextFire = std::chrono::steady_clock::now() + period;
    bucket.timerId = area.scheduler.postAfter(nextFire, [this, bucketIndex](uint32_t tid2)
    {
        handleGroupPacingBucket<Policy>(tid2, bucketIndex);
    });
}

template <typename Policy>
void OriginatorContext::requestReorigination(const LsaKey& key)
{
    auto& cfgs = area.getProcessConfigs();
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

    state.timerId = area.scheduler.postAfter(state.nextFire, [this, key](uint32_t)
    {
        this->runReorigination<Policy>(key);
    });
}

template <typename Policy>
void OriginatorContext::runReorigination(const LsaKey& key)
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
void OriginatorContext::processReoriginatedLsa(const LsaKey& key, const OriginationInfo& info)
{
    LsaHeader hdr{};
    IncomingLsaContext ctx = {
        .key = key,
        .header = hdr
    };

    LsaRecord* existing = area.lsdb.find(ctx.key);

    if constexpr (std::is_same_v<Policy, PolicyV2>)
    {
        uint32_t options = area.flags.getFlags();
        if (key.lsaType == OSPFV2_LSA_NSSA)
            InterfaceFlagManager::setPropagate(options, true);
        InterfaceFlagManager::setDemandCircuits(options, area.isDcCompatible());
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
        area.scheduler.postAfter(
            std::chrono::steady_clock::now() + std::chrono::seconds(5),
            [&area = area](uint32_t) { area.originator.fullRefresh(); }
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
void OriginatorContext::processOriginatedLsa(const LsaKey& key)
{
    scheduleForGroupPacing(key);

    requestReorigination<Policy>(key);
}

template <typename Policy>
void OriginatorContext::processExternalLsa(IncomingLsaContext& ctx, const LsaBody& body)
{
    return area.processExternalLsa<Policy>(ctx, body);
}

uint32_t OriginatorContext::getAreaFlags() const
{
    return area.flags.getFlags();
}

IntraOriginator& OriginatorContext::getIntraOriginator()
{
    return area.originator;
}

InterOriginator& OriginatorContext::getInterOriginator()
{
    return area.getInterOriginator();
}

ExternalOriginator& OriginatorContext::getExternalOriginator()
{
    return area.getExternalOriginator();
}

const config::OspfAreaRegistry& OriginatorContext::getConfigs() const
{
    return area.configs;
}

const config::OspfRegistry& OriginatorContext::getProcessConfigs() const
{
    return area.getProcessConfigs();
}

bool OriginatorContext::isValidForwardAddress(const types::IPAddress& addr) const
{
    return area.isValidForwardAddress(addr);
}

const InterfaceManager& OriginatorContext::getIfaceMgr() const
{
    return area.getIfaceMgr(); 
}

template void OriginatorContext::originateLsa<PolicyV2>(const LsaKey&, const LsaBody&, bool);
template void OriginatorContext::originateLsa<PolicyV3>(const LsaKey&, const LsaBody&, bool);

template void OriginatorContext::initGroupPacing<PolicyV2>();
template void OriginatorContext::initGroupPacing<PolicyV3>();

template void OriginatorContext::requestReorigination<PolicyV2>(const LsaKey&);
template void OriginatorContext::requestReorigination<PolicyV3>(const LsaKey&);

template void OriginatorContext::runReorigination<PolicyV2>(const LsaKey&);
template void OriginatorContext::runReorigination<PolicyV3>(const LsaKey&);

template void OriginatorContext::processReoriginatedLsa<PolicyV2>(const LsaKey&, const OriginationInfo&);
template void OriginatorContext::processReoriginatedLsa<PolicyV3>(const LsaKey&, const OriginationInfo&);

template void OriginatorContext::processOriginatedLsa<PolicyV2>(const LsaKey&);
template void OriginatorContext::processOriginatedLsa<PolicyV3>(const LsaKey&);

template void OriginatorContext::processExternalLsa<PolicyV2>(IncomingLsaContext&, const LsaBody&);
template void OriginatorContext::processExternalLsa<PolicyV3>(IncomingLsaContext&, const LsaBody&);
}
