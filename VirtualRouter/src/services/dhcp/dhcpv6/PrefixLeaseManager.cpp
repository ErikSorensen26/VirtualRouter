// PrefixLeaseManager.cpp

#include "PrefixLeaseManager.h"
#include "Dhcpv6Server.h"

namespace services::dhcp
{

PrefixLeaseManager::PrefixLeaseManager(PrefixPool& pool, core::TimeManager& timeManager, Configs& configs)
    : pool(pool), timeManager(timeManager), configs(configs) {}

std::pair<types::IPv6Prefix, Dhcpv6StatusMessage> PrefixLeaseManager::createPrefix(const IAKey& key, uint8_t length, uint32_t valid)
{
    std::lock_guard<std::mutex> lock(mutex);

    auto prefix = pool.allocatePrefix(length);
    if (prefix.second.code != Dhcpv6StatusCode::Success) return prefix;

    auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(valid);
    prefixTimerIDs[prefix.first] = timeManager.addTimer(expiry, [this, prefix = prefix.first, key](uint32_t) {
        expirePrefix(prefix, key);
    });

    prefixKeys[prefix.first] = key;
    auto& group = prefixes[key];
    group.insert(prefix.first);

    return prefix;
}

std::pair<Dhcpv6StatusMessage, std::unordered_set<types::IPv6Prefix>> PrefixLeaseManager::createPrefix(const IAKey& key, uint32_t valid)
{
    std::lock_guard<std::mutex> lock(mutex);

    auto prefix = pool.allocatePrefix();
    if (prefix.second.code != Dhcpv6StatusCode::Success) return {prefix.second, { prefix.first } };

    auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(valid);
    prefixTimerIDs[prefix.first] = timeManager.addTimer(expiry, [this, prefix = prefix.first, key](uint32_t) {
        expirePrefix(prefix, key);
    });

    prefixKeys[prefix.first] = key;
    auto& group = prefixes[key];
    group.insert(prefix.first);

    return { { Dhcpv6StatusCode::Success }, group };
}

Dhcpv6StatusMessage PrefixLeaseManager::createPrefixFromAdvertised(const types::IPv6Prefix& prefix, const IAKey& key, uint32_t valid)
{
    auto startPrefixLease = [&]() {
        auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(valid);
        prefixTimerIDs[prefix] = timeManager.addTimer(expiry, [this, key, prefix](uint32_t) {
            expirePrefix(prefix, key);
        });
    };

    std::lock_guard<std::mutex> lock(mutex);
    if (auto it = prefixKeys.find(prefix); it != prefixKeys.end() && prefixTimerIDs.count(prefix))
    {
        if (it->second == key)
        {
            timeManager.cancelTimer(prefixTimerIDs.at(prefix));
            startPrefixLease();
            return { Dhcpv6StatusCode::Success };
        }
        else
        {
            return { Dhcpv6StatusCode::NotOnLink };
        }
    };

    if (!pool.activateAdvertisedPrefix({key.duid, key.iaid, prefix.addr, prefix.prefixLength}))
        return { Dhcpv6StatusCode::NotOnLink };

    startPrefixLease();

    prefixes[key].insert(prefix);
    prefixKeys[prefix] = key;
    return { Dhcpv6StatusCode::Success };
}

Dhcpv6StatusMessage PrefixLeaseManager::createPrefixFromRequest(const types::IPv6Prefix& prefix, const IAKey& key, uint32_t valid)
{
    auto startPrefixLease = [&]() {
        auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(valid);
        prefixTimerIDs[prefix] = timeManager.addTimer(expiry, [this, key, prefix](uint32_t) {
            expirePrefix(prefix, key);
        });
    };

    std::lock_guard<std::mutex> lock(mutex);
    if (auto it = prefixKeys.find(prefix); it != prefixKeys.end() && prefixTimerIDs.count(prefix))
    {
        if (it->second == key)
        {
            timeManager.cancelTimer(prefixTimerIDs.at(prefix));
            startPrefixLease();
            return { Dhcpv6StatusCode::Success };
        }
        else
        {
            return { Dhcpv6StatusCode::NotOnLink };
        }
    };

    if (auto code = pool.allocateRequestedPrefix({key.duid, key.iaid, prefix.addr, prefix.prefixLength}); code.second.code != Dhcpv6StatusCode::Success)
        return code.second;

    startPrefixLease();

    prefixes[key].insert(prefix);
    prefixKeys[prefix] = key;
    return { Dhcpv6StatusCode::Success };
}

Dhcpv6StatusMessage PrefixLeaseManager::renewPrefix(const types::IPv6Prefix& prefix, const IAKey& key, uint32_t valid)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto timerIt = prefixTimerIDs.find(prefix);
    if (auto it = prefixes.find(key); it == prefixes.end() || !it->second.count(prefix) || timerIt == prefixTimerIDs.end()) 
        return { Dhcpv6StatusCode::NoBinding };

    timeManager.cancelTimer(timerIt->second);
    auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(valid);
    prefixTimerIDs[prefix] = timeManager.addTimer(expiry, [this, key, prefix](uint32_t) {
        expirePrefix(prefix, key);
    });

    return { Dhcpv6StatusCode::Success };
}

Dhcpv6StatusMessage PrefixLeaseManager::rebindPrefix(const types::IPv6Prefix& prefix, const IAKey& key, uint32_t valid) {
    return renewPrefix(prefix, key, valid);
}

Dhcpv6StatusMessage PrefixLeaseManager::releasePrefix(const types::IPv6Prefix& prefix, const IAKey& key)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto timerIt = prefixTimerIDs.find(prefix);
    auto it = prefixes.find(key);
    if (it == prefixes.end() || !it->second.count(prefix) || timerIt == prefixTimerIDs.end()) 
        return { Dhcpv6StatusCode::NoBinding };

    timeManager.cancelTimer(timerIt->second);
    pool.releasePrefix(prefix);

    it->second.erase(prefix);
    if (it->second.empty()) prefixes.erase(it);

    prefixTimerIDs.erase(prefix);
    prefixKeys.erase(prefix);

    return { Dhcpv6StatusCode::Success };
}

Dhcpv6StatusMessage PrefixLeaseManager::declinePrefix(const types::IPv6Prefix& prefix, const IAKey& key)
{
    std::lock_guard<std::mutex> lock(mutex);
    bool success = pool.setConflicted({key.duid, key.iaid, prefix.addr, prefix.prefixLength}, configs.declineHoldTime.load(std::memory_order_relaxed));
    return { success ? Dhcpv6StatusCode::Success : Dhcpv6StatusCode::NotOnLink };
}

std::unordered_set<types::IPv6Prefix> PrefixLeaseManager::getPrefixes(const IAKey& key) const
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = prefixes.find(key);
    if (it == prefixes.end()) return {};
    return {it->second.begin(), it->second.end()};
}

bool PrefixLeaseManager::match(const IAKey& key, const types::IPv6Prefix& prefix) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = prefixKeys.find(prefix);
    return it != prefixKeys.end() && it->second == key;
}

size_t PrefixLeaseManager::size() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return prefixKeys.size();
}

void PrefixLeaseManager::expirePrefix(const types::IPv6Prefix& prefix, const IAKey& key)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = prefixes.find(key);
    auto timerIt = prefixTimerIDs.find(prefix);
    if (it == prefixes.end() || timerIt == prefixTimerIDs.end()) return;

    timeManager.cancelTimer(timerIt->second);
    pool.expirePrefix({key.duid, key.iaid, prefix.addr, prefix.prefixLength}, configs.expiredHoldTime.load(std::memory_order_relaxed));

    it->second.erase(prefix);
    if (it->second.empty()) prefixes.erase(it);

    prefixTimerIDs.erase(timerIt);
    prefixKeys.erase(prefix);
}

} // namespace services::dhcp
