#include <IPv6LeaseManager.h>
#include "Dhcpv6Server2.h"

IPv6LeaseManager::IPv6LeaseManager(IPv6Pool& pool, Protocol::Dhcpv6::Configs& cgs)
    : pool(pool), timeManager(pool.timeManager), configs(cgs) {}

std::unordered_set<__uint128_t> IPv6LeaseManager::createLease(const IAKey& key, uint32_t leaseTime)
{
    std::lock_guard<std::mutex> lock(mutex);

    __uint128_t addr = pool.allocate();
    if (addr == 0) return {};

    auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(leaseTime);
    leaseTimerIDs[addr] = timeManager.addTimer(expiry, [this, addr, key]() {
        expireLease(addr, key);
    });

    leaseKeys[addr] = key;
    auto& group = leases[key];
    group.insert(addr);

    return group;
}

Dhcpv6StatusMessage IPv6LeaseManager::createLeaseFromAdvertised(__uint128_t addr, const IAKey& key, uint32_t leaseTime)
{
    auto startLeaseTimer = [&]() {
        auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(leaseTime);
        leaseTimerIDs[addr] = timeManager.addTimer(expiry, [this, key, addr]() {
            expireLease(addr, key);
        });
    };

    std::lock_guard<std::mutex> lock(mutex);
    if (auto it = leaseKeys.find(addr); it != leaseKeys.end() && leaseTimerIDs.count(addr))
    {
        if (it->second == key)
        {
            timeManager.cancelTimer(leaseTimerIDs.at(addr));
            startLeaseTimer();
            return { Dhcpv6StatusCode::Success };
        }
        else
            return { Dhcpv6StatusCode::NotOnLink };
    }

    if (!pool.activateAdvertised({key.duid, key.iaid, addr}))
        return { Dhcpv6StatusCode::NoAddrsAvail };

    startLeaseTimer();

    leases[key].insert(addr);
    leaseKeys[addr] = key;
    return { Dhcpv6StatusCode::Success };
}

Dhcpv6StatusMessage IPv6LeaseManager::createLeaseFromRequest(__uint128_t addr, const IAKey& key, uint32_t leaseTime)
{
    auto startLeaseTimer = [&]() {
        auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(leaseTime);
        leaseTimerIDs[addr] = timeManager.addTimer(expiry, [this, key, addr]() {
            expireLease(addr, key);
        });
    };

    std::lock_guard<std::mutex> lock(mutex);
    if (auto it = leaseKeys.find(addr); it != leaseKeys.end() && leaseTimerIDs.count(addr))
    {
        if (it->second == key)
        {
            timeManager.cancelTimer(leaseTimerIDs.at(addr));
            startLeaseTimer();
            return { Dhcpv6StatusCode::Success };
        }
        else
            return { Dhcpv6StatusCode::NotOnLink };
    }

    if (auto code = pool.allocateRequested({key.duid, key.iaid, addr}); code.code != Dhcpv6StatusCode::Success) return { code };

    startLeaseTimer();

    leases[key].insert(addr);
    leaseKeys[addr] = key;
    return { Dhcpv6StatusCode::Success };
}

Dhcpv6StatusMessage IPv6LeaseManager::renewLease(__uint128_t addr, const IAKey& key, uint32_t leaseTime)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto timerIt = leaseTimerIDs.find(addr);
    if (auto it = leases.find(key); it == leases.end() || !it->second.count(addr) || timerIt == leaseTimerIDs.end()) return { Dhcpv6StatusCode::NoBinding };

    timeManager.cancelTimer(timerIt->second);
    auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(leaseTime);
    leaseTimerIDs[addr] = timeManager.addTimer(expiry, [this, key, addr]() {
        expireLease(addr, key);
    });

    return { Dhcpv6StatusCode::Success };
}

Dhcpv6StatusMessage IPv6LeaseManager::rebindLease(__uint128_t addr, const IAKey& key, uint32_t leaseTime)
{
    return renewLease(addr, key, leaseTime);
}

Dhcpv6StatusMessage IPv6LeaseManager::releaseLease(__uint128_t addr, const IAKey& key)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto timerIt = leaseTimerIDs.find(addr);
    auto it = leases.find(key);
    if (it == leases.end() || !it->second.count(addr) || timerIt == leaseTimerIDs.end()) return { Dhcpv6StatusCode::NoBinding };

    timeManager.cancelTimer(timerIt->second);
    pool.release(addr);

    it->second.erase(addr);
    if (it->second.empty()) leases.erase(it);

    leaseTimerIDs.erase(addr);
    leaseKeys.erase(addr);

    return { Dhcpv6StatusCode::Success };
}

Dhcpv6StatusMessage IPv6LeaseManager::declineLease(__uint128_t addr, const IAKey& key)
{
    std::lock_guard<std::mutex> lock(mutex);
    bool success = pool.setConflicted({key.duid, key.iaid, addr}, configs.declineHoldTime.load(std::memory_order_relaxed));
    return { success ? Dhcpv6StatusCode::Success : Dhcpv6StatusCode::NotOnLink };
}

std::optional<std::vector<__uint128_t>> IPv6LeaseManager::getIANA(const IAKey& key) const
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = leases.find(key);
    if (it == leases.end()) return std::nullopt;
    return std::vector<__uint128_t>{it->second.begin(), it->second.end()};
}

bool IPv6LeaseManager::isLeased(__uint128_t addr, const IAKey& key) const
{
    if (auto it = leases.find(key); it != leases.end() && it->second.count(addr))
        return true;
    return false;
}

size_t IPv6LeaseManager::size() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return leaseKeys.size();
}

void IPv6LeaseManager::expireLease(__uint128_t addr, const IAKey& key)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = leases.find(key);
    auto timerIt = leaseTimerIDs.find(addr);
    if (it == leases.end() || timerIt == leaseTimerIDs.end()) return;

    timeManager.cancelTimer(timerIt->second);
    pool.expire({key.duid, key.iaid, addr}, configs.expiredHoldTime.load(std::memory_order_relaxed));

    it->second.erase(addr);
    if (it->second.empty()) leases.erase(it);

    leaseTimerIDs.erase(timerIt);
    leaseKeys.erase(addr);
}
