#include <IPv6Pool.h>
#include <TimeManager.h>

thread_local std::minstd_rand IPv6Pool::rng{std::random_device{}()};

IPv6Pool::IPv6Pool(TimeManager& timeManager) : timeManager(timeManager) {}

IPv6Pool::~IPv6Pool()
{
    for (auto& [_, t] : bad) timeManager.cancelTimer(t);
    for (auto& [_, t] : advertised) timeManager.cancelTimer(t);
    for (auto& [_, t] : quarantined) timeManager.cancelTimer(t);
}

std::optional<std::unordered_set<__uint128_t>> IPv6Pool::getIAID(const IAKey& key)
{
    if (auto it = advertisedGroups.find(key); it != advertisedGroups.end())
        return it->second;
    return std::nullopt;
}

__uint128_t IPv6Pool::allocate()
{
    std::lock_guard<std::mutex> lock(mutex);

    for (size_t i = 0; i < MAX_GENERATION_ATTEMPTS; ++i)
    {
        auto addr = generateRandomIP();
        if (isExcluded(addr) || isConflicted(addr) || isAllocated(addr) || isQuarantined(addr)) continue;
        allocated.insert(addr);
        return addr;
    }

    return 0;
}

__uint128_t IPv6Pool::allocateAdvertised(const IAKey& key, uint32_t timeout)
{
    std::lock_guard<std::mutex> lock(mutex);

    for (size_t i = 0; i < MAX_GENERATION_ATTEMPTS; ++i)
    {
        auto addr = generateRandomIP();
        if (isExcluded(addr) || isConflicted(addr) || isAllocated(addr) || isQuarantined(addr)) continue;

        const IALeaseKey leaseKey = {key.duid, key.iaid, addr};
        advertised[leaseKey] = {
            timeManager.addTimer(
            std::chrono::steady_clock::now() + std::chrono::seconds(timeout),
            [this, leaseKey](uint32_t) {
                std::lock_guard<std::mutex> lock(mutex);
                advertised.erase(leaseKey);
                advertisedIPs.erase(leaseKey.address);
                auto& group = advertisedGroups[{leaseKey.duid, leaseKey.iaid}];
                group.erase(leaseKey.address);
                if (group.empty()) advertisedGroups.erase({leaseKey.duid, leaseKey.iaid});
            })
        };
        advertisedIPs.insert(leaseKey.address);
        advertisedGroups[{leaseKey.duid, leaseKey.iaid}].insert(leaseKey.address);
        return addr;
    }

    return {};
}

Dhcpv6StatusMessage IPv6Pool::allocateRequestedAdvertised(const IALeaseKey& key, uint32_t timeout)
{
    std::lock_guard<std::mutex> lock(mutex);

    auto setAdvertisedTimer = [&]() {
        advertised[key] = {
            timeManager.addTimer(
            std::chrono::steady_clock::now() + std::chrono::seconds(timeout),
            [this, key](uint32_t) {
                std::lock_guard<std::mutex> lock(mutex);
                advertised.erase(key);
                advertisedIPs.erase(key.address);
                auto& group = advertisedGroups[{key.duid, key.iaid}];
                group.erase(key.address);
                if (group.empty()) advertisedGroups.erase({key.duid, key.iaid});
            })
        };
    };

    if (auto it = quarantined.find(key); it != quarantined.end())
    {
        timeManager.cancelTimer(it->second);
        quarantined.erase(it);
        quarantinedIPs.erase(it->first.address);
    }

    if (!withinRange(key.address))
        return { Dhcpv6StatusCode::NotOnLink };

    if (isExcluded(key.address) || isConflicted(key.address) || isAdvertised(key.address))
        return { Dhcpv6StatusCode::NoAddrsAvail };

    setAdvertisedTimer();
    advertisedIPs.insert(key.address);
    advertisedGroups[{key.duid, key.iaid}].insert(key.address);
    return { Dhcpv6StatusCode::Success };
}

Dhcpv6StatusMessage IPv6Pool::allocateRequested(const IALeaseKey& key)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (auto it = quarantined.find(key); it != quarantined.end())
    {
        timeManager.cancelTimer(it->second);
        quarantined.erase(it);
        quarantinedIPs.erase(it->first.address);
    }

    if (!withinRange(key.address))
        return { Dhcpv6StatusCode::NotOnLink };

    if (isExcluded(key.address) || isConflicted(key.address) || isAdvertised(key.address))
        return { Dhcpv6StatusCode::NoAddrsAvail };

    allocated.insert(key.address);
    return { Dhcpv6StatusCode::Success };
}

bool IPv6Pool::activateAdvertised(const IALeaseKey& key)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = advertised.find(key);
    if (it == advertised.end()) return false;

    timeManager.cancelTimer(it->second);
    advertised.erase(it);
    advertisedIPs.erase(key.address);
    auto& group = advertisedGroups[{key.duid, key.iaid}];
    group.erase(key.address);
    if (group.empty()) advertisedGroups.erase({key.duid, key.iaid});
    allocated.insert(key.address);
    return true;
}

void IPv6Pool::clearAdvertisedIP(const IALeaseKey& key)
{
    std::lock_guard<std::mutex> lock(mutex);
    advertised.erase(key);
    advertisedIPs.erase(key.address);
    auto& group = advertisedGroups[{key.duid, key.iaid}];
    group.erase(key.address);
    if (group.empty()) advertisedGroups.erase({key.duid, key.iaid});
}

void IPv6Pool::release(__uint128_t addr)
{
    std::lock_guard<std::mutex> lock(mutex);
    allocated.erase(addr);
}

void IPv6Pool::expire(const IALeaseKey& key, uint32_t duration)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!isAllocated(key.address)) return;
    allocated.erase(key.address);
    quarantined[key] = {
        timeManager.addTimer(
        std::chrono::steady_clock::now() + std::chrono::seconds(duration),
        [this, key](uint32_t) {
            std::lock_guard<std::mutex> lock(mutex);
            quarantined.erase(key);
            quarantinedIPs.erase(key.address);
        })
    };
    quarantinedIPs.insert(key.address);
}

bool IPv6Pool::excludeIP(__uint128_t addr)
{
    std::lock_guard<std::mutex> lock(mutex);
    excluded.insert(addr);
    return true;
}

bool IPv6Pool::removeExclusion(__uint128_t addr)
{
    std::lock_guard<std::mutex> lock(mutex);
    return excluded.erase(addr) > 0;
}

bool IPv6Pool::setConflicted(const IALeaseKey& key, uint32_t duration)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!isAdvertised(key)) return false;

    advertised.erase(key);
    advertisedIPs.erase(key.address);
    auto& group = advertisedGroups[{key.duid, key.iaid}];
    group.erase(key.address);
    if (group.empty()) advertisedGroups.erase({key.duid, key.iaid});

    bad[key.address] = timeManager.addTimer(
        std::chrono::steady_clock::now() + std::chrono::seconds(duration),
        [this, addr = key.address](uint32_t) {
            std::lock_guard<std::mutex> lock(mutex);
            bad.erase(addr);
        });
    return true;
}

void IPv6Pool::cleanupBadIPs()
{
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& [_, t] : bad) timeManager.cancelTimer(t);
    bad.clear();
}
    
bool IPv6Pool::isAllocated(__uint128_t addr) const
{
    std::lock_guard<std::mutex> lock(mutex);
    return allocated.count(addr);
}

bool IPv6Pool::isQuarantined(__uint128_t addr) const
{
    std::lock_guard<std::mutex> lock(mutex);
    return quarantinedIPs.count(addr);
}

bool IPv6Pool::isExcluded(__uint128_t addr) const
{
    std::lock_guard<std::mutex> lock(mutex);
    return excluded.count(addr) || isEUI64(addr);
}

bool IPv6Pool::isConflicted(__uint128_t addr) const
{
    std::lock_guard<std::mutex> lock(mutex);
    return bad.count(addr);
}

bool IPv6Pool::isAdvertised(__uint128_t addr) const
{
    std::lock_guard<std::mutex> lock(mutex);
    return advertisedIPs.count(addr);
}

bool IPv6Pool::isAdvertised(const IALeaseKey& key) const
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = advertised.find(key);
    return it != advertised.end();
}

bool IPv6Pool::isAllocatedOrExcluded(__uint128_t addr) const
{
    return isAllocated(addr) || isExcluded(addr);
}

bool IPv6Pool::adjustPool(__uint128_t network, uint8_t prefixLen)
{
    if (prefixLen > 128) return false;
    std::lock_guard<std::mutex> lock(mutex);

    size = (__uint128_t(1) << (128 - prefixLen));
    if (size <= SLAAC_SAFE_OFFSET + 2) return false;

    base = network;

    for (const auto& [_, t] : advertised) timeManager.cancelTimer(t);
    for (const auto& [_, t] : bad) timeManager.cancelTimer(t);
    for (const auto& [_, t] : quarantined) timeManager.cancelTimer(t);

    allocated.clear();
    advertised.clear();
    advertisedIPs.clear();
    advertisedGroups.clear();
    quarantined.clear();
    quarantinedIPs.clear();

    excluded.clear();
    bad.clear();

    init.store(true, std::memory_order_release);
    return true;
}

bool IPv6Pool::withinRange(__uint128_t addr) const
{
    return addr > base && addr < base + size;
}

void IPv6Pool::setLeaseManager(IPv6LeaseManager* mgr)
{
    leaseManager = mgr;
}

bool IPv6Pool::isEUI64(__uint128_t addr) const
{
    return ((addr >> 40) & 0xFFFF) == 0xFFFE;
}

__uint128_t IPv6Pool::generateRandomIP()
{
    __uint128_t offset = 0;
    for (int i = 0; i < 2; ++i)
        offset = (offset << 64) | rng();
    offset = SLAAC_SAFE_OFFSET + 1 + (offset % (size - SLAAC_SAFE_OFFSET - 2));
    return base + offset;
}
