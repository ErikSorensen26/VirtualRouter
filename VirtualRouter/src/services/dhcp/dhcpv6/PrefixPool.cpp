// PrefixPool.cpp

#include "PrefixPool.h"

namespace services::dhcp
{

PrefixPool::PrefixPool(const types::IPv6Prefix& prefix, core::TimeManager& tm, uint8_t dl)
    : base(prefix), delegationLength(dl), timeManager(tm)
{
    size = (__uint128_t(1) << (delegationLength - prefix.prefixLength));
}

PrefixPool::~PrefixPool()
{
    for (auto& [_, t] : bad) timeManager.cancelTimer(t);
    for (auto& [_, t] : advertised) timeManager.cancelTimer(t);
    for (auto& [_, t] : quarantined) timeManager.cancelTimer(t);
}

std::optional<std::unordered_set<types::IPv6Prefix>> PrefixPool::getIAID(const IAKey& key)
{
    if (auto pfxs = advertisedGroups.find(key); pfxs != advertisedGroups.end())
        return pfxs->second;
    return std::nullopt;
}

void PrefixPool::setLeaseManager(PrefixLeaseManager* mgr)
{
    leaseManager = mgr;
}

bool PrefixPool::withinRange(const types::IPv6Prefix& prefix) const
{
    return prefix.addr >= base.addr && prefix.addr < base.addr + size && prefix.prefixLength <= base.prefixLength;
}

bool PrefixPool::prefixMatches(__uint128_t a, __uint128_t b, uint8_t length) const
{
    __uint128_t mask = ~(__uint128_t(0)) << (128 - length);
    return (a & mask) == (b & mask);
}

types::IPv6Prefix PrefixPool::generatePrefix(uint64_t index, uint8_t length) const
{
    __uint128_t step = __uint128_t(1) << (128 - length);
    types::IPv6Prefix pfx;
    pfx.addr = base.addr + (index * step);
    pfx.prefixLength = length;
    return pfx;
}

std::pair<types::IPv6Prefix, Dhcpv6StatusMessage> PrefixPool::allocatePrefix(uint8_t requestedLength)
{
    std::lock_guard<std::mutex> lock(mutex);

    uint8_t effectiveLength = requestedLength > 64 ? delegationLength : std::max(requestedLength, delegationLength);
    if (effectiveLength < base.prefixLength) return {{}, { Dhcpv6StatusCode::UnspecFail } };

    __uint128_t count = (__uint128_t(1) << (effectiveLength - base.prefixLength));
    __uint128_t maxAttempts = std::min(count, __uint128_t(MAX_GENERATION_ATTEMPTS));

    for (__uint128_t i = 0; i < maxAttempts; ++i)
    {
        types::IPv6Prefix prefix = generatePrefix(i, effectiveLength);

        if (!withinRange(prefix) || isAllocated(prefix) || isExcluded(prefix) || isConflicted(prefix) || isAdvertised(prefix))
            continue;

        allocated.insert(prefix);
        return {prefix, { Dhcpv6StatusCode::Success } };
    }
    return { {}, { Dhcpv6StatusCode::NoPrefixAvail } };
}

std::pair<types::IPv6Prefix, Dhcpv6StatusMessage> PrefixPool::allocateAdvertisedPrefix(const IAKey& key, uint8_t requestedLength, uint32_t timeout)
{
    std::lock_guard<std::mutex> lock(mutex);

    uint8_t effectiveLength = requestedLength > 64 ? delegationLength : std::max(requestedLength, delegationLength);
    if (effectiveLength < base.prefixLength) return { {}, { Dhcpv6StatusCode::UnspecFail } };

    __uint128_t count = __uint128_t(1) << (effectiveLength - base.prefixLength);
    __uint128_t maxAttempts = std::min(count, __uint128_t(MAX_GENERATION_ATTEMPTS));

    for (__uint128_t i = 0; i < maxAttempts; ++i)
    {
        types::IPv6Prefix prefix = generatePrefix(i, effectiveLength);

        if (!withinRange(prefix) || isAllocated(prefix) || isExcluded(prefix) || isConflicted(prefix) || isAdvertised(prefix))
            continue;

        const IAPrefixKey prefixKey = {key.duid, key.iaid, prefix.addr, prefix.prefixLength};

        advertised[prefixKey] = {
            timeManager.addTimer(std::chrono::steady_clock::now() + std::chrono::seconds(timeout), [this, prefixKey, prefix](uint32_t) {
                std::lock_guard<std::mutex> lock(mutex);
                advertised.erase(prefixKey);
                advertisedPDs.erase(prefix);
                auto& group = advertisedGroups[{prefixKey.duid, prefixKey.iaid}];
                group.erase({prefixKey.address, prefixKey.prefixLength});
                if (group.empty()) advertisedGroups.erase({prefixKey.duid, prefixKey.iaid});
            })
        };

        advertisedPDs.insert(prefix);
        advertisedGroups[{prefixKey.duid, prefixKey.iaid}].insert({prefixKey.address, prefixKey.prefixLength});
        return { {prefix}, { Dhcpv6StatusCode::Success } };
    }

    return { {}, { Dhcpv6StatusCode::NoPrefixAvail } };
}

std::pair<uint8_t, Dhcpv6StatusMessage> PrefixPool::allocateRequestedAdvertisedPrefix(const IAPrefixKey& key, uint32_t timeout)
{
    std::lock_guard<std::mutex> lock(mutex);

    uint8_t effectiveLength = key.prefixLength > 64 ? delegationLength : std::max(key.prefixLength, delegationLength);
    if (effectiveLength < base.prefixLength) return { {}, { Dhcpv6StatusCode::UnspecFail } };
    const types::IPv6Prefix prefix = {key.address, effectiveLength};

    if (auto it = quarantined.find(key); it != quarantined.end())
    {
        timeManager.cancelTimer(it->second);
        quarantined.erase(it);
        quarantinedPDs.erase(prefix);
    }

    if (!withinRange(prefix))
        return { 0, { Dhcpv6StatusCode::NotOnLink } };

    if (!withinRange(prefix) || isAllocated(prefix) || isExcluded(prefix) || isConflicted(prefix))
        return { 0, { Dhcpv6StatusCode::NoPrefixAvail } };

    advertised[key] = {
        timeManager.addTimer(std::chrono::steady_clock::now() + std::chrono::seconds(timeout), [this, key](uint32_t) {
            std::lock_guard<std::mutex> lock(mutex);
            advertised.erase(key);
            advertisedPDs.erase({key.address, key.prefixLength});
        })
    };
    advertisedPDs.insert(prefix);
    return { effectiveLength, { Dhcpv6StatusCode::Success } };
}

std::pair<uint8_t, Dhcpv6StatusMessage> PrefixPool::allocateRequestedPrefix(const IAPrefixKey& key)
{
    std::lock_guard<std::mutex> lock(mutex);

    uint8_t effectiveLength = key.prefixLength > 64 ? delegationLength : std::max(key.prefixLength, delegationLength);
    if (effectiveLength < base.prefixLength) return { {}, { Dhcpv6StatusCode::UnspecFail } };
    const types::IPv6Prefix prefix = {key.address, key.prefixLength};

    if (auto it = quarantined.find(key); it != quarantined.end())
    {
        timeManager.cancelTimer(it->second);
        quarantined.erase(it);
        quarantinedPDs.erase(prefix);
    }

    if (!withinRange(prefix))
        return { 0, { Dhcpv6StatusCode::NotOnLink } };

    if (isAllocated(prefix) || isExcluded(prefix) || isConflicted(prefix) || isAdvertised(prefix))
        return { 0, { Dhcpv6StatusCode::NoPrefixAvail } };

    allocated.insert(prefix);
    return { effectiveLength, { Dhcpv6StatusCode::Success } };
}

bool PrefixPool::activateAdvertisedPrefix(const IAPrefixKey& key)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = advertised.find(key);
    if (it == advertised.end()) return false;

    timeManager.cancelTimer(it->second);
    advertised.erase(it);
    const types::IPv6Prefix prefix = {key.address, key.prefixLength};
    advertisedPDs.erase(prefix);
    allocated.insert(prefix);
    return true;
}

void PrefixPool::releasePrefix(const types::IPv6Prefix& prefix)
{
    std::lock_guard<std::mutex> lock(mutex);
    allocated.erase(prefix);
}

void PrefixPool::expirePrefix(const IAPrefixKey& key, uint32_t timeout)
{
    std::lock_guard<std::mutex> lock(mutex);
    const types::IPv6Prefix prefix = {key.address, key.prefixLength};
    if (!isAllocated(prefix)) return;
    allocated.erase(prefix);
    quarantined[key] = {
        timeManager.addTimer(std::chrono::steady_clock::now() + std::chrono::seconds(timeout),
        [this, key](uint32_t) {
            std::lock_guard<std::mutex> lock(mutex);
            quarantined.erase(key);
            quarantinedPDs.erase({key.address, key.prefixLength});
        })
    };
    quarantinedPDs.insert(prefix);
}

bool PrefixPool::setConflicted(const IAPrefixKey& key, uint32_t duration)
{
    std::lock_guard<std::mutex> lock(mutex);
    const types::IPv6Prefix prefix = {key.address, key.prefixLength};
    if (isAllocated(prefix)) return false;
    bad[prefix] = timeManager.addTimer(std::chrono::steady_clock::now() + std::chrono::seconds(duration),
        [this, prefix](uint32_t) {
            std::lock_guard<std::mutex> lock(mutex);
            bad.erase(prefix);
        });
    return true;
}

bool PrefixPool::excludePrefix(const types::IPv6Prefix& prefix)
{
    std::lock_guard<std::mutex> lock(mutex);
    return excluded.insert(prefix).second;
}

bool PrefixPool::removeExclusion(const types::IPv6Prefix& prefix)
{
    std::lock_guard<std::mutex> lock(mutex);
    return excluded.erase(prefix);
}

bool PrefixPool::isAllocated(const types::IPv6Prefix& prefix) const
{
    std::lock_guard<std::mutex> lock(mutex);
    return allocated.count(prefix);
}

bool PrefixPool::isAdvertised(const types::IPv6Prefix& prefix) const
{
    std::lock_guard<std::mutex> lock(mutex);
    return advertisedPDs.count(prefix);
}

bool PrefixPool::isQuarantined(const types::IPv6Prefix& prefix) const
{
    std::lock_guard<std::mutex> lock(mutex);
    return quarantinedPDs.count(prefix);
}

bool PrefixPool::isExcluded(const types::IPv6Prefix& prefix) const
{
    std::lock_guard<std::mutex> lock(mutex);
    return excluded.count(prefix);
}

bool PrefixPool::isConflicted(const types::IPv6Prefix& prefix) const
{
    std::lock_guard<std::mutex> lock(mutex);
    return bad.count(prefix);
}

bool PrefixPool::adjustPool(const types::IPv6Prefix& newBase, size_t newDelegationLength)
{
    if (newBase.prefixLength > 128 || newDelegationLength > 128 || newDelegationLength < newBase.prefixLength)
        return false;

    std::lock_guard<std::mutex> lock(mutex);
    base = newBase;
    delegationLength = newDelegationLength;
    size = (__uint128_t(1) << (delegationLength - newBase.prefixLength));

    for (auto& [_, t] : bad) timeManager.cancelTimer(t);
    for (auto& [_, t] : advertised) timeManager.cancelTimer(t);
    for (auto& [_, t] : quarantined) timeManager.cancelTimer(t);

    allocated.clear();
    advertised.clear();
    advertisedPDs.clear();
    quarantined.clear();
    quarantinedPDs.clear();
    excluded.clear();
    bad.clear();
    return true;
}

bool PrefixPool::isFull()
{
    std::lock_guard<std::mutex> lock(mutex);

    size_t used = allocated.size()
                + advertisedPDs.size()
                + quarantinedPDs.size()
                + bad.size()
                + excluded.size();

    return used >= size;
}

} // namespace services::dhcp
