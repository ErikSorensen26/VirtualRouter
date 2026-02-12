
#ifndef PREFIX_POOL_H
#define PREFIX_POOL_H

#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <cstdint>
#include <TimeManager.h>

#include "Dhcpv6.h"

class PrefixLeaseManager;
namespace Protocol
{
    class Dhcpv6Server;
}

class PrefixPool
{
public:
    PrefixPool(const IPv6Prefix& base, TimeManager& tm, uint8_t delegationLength);
    ~PrefixPool();
    friend class Protocol::Dhcpv6Server;

    bool adjustPool(const IPv6Prefix& base, size_t newDelegationLength);
    void setLeaseManager(PrefixLeaseManager* mgr);
    bool withinRange(const IPv6Prefix& prefix) const;

    std::pair<IPv6Prefix, Dhcpv6StatusMessage> allocatePrefix(uint8_t requestedLength = 0);
    std::pair<IPv6Prefix, Dhcpv6StatusMessage> allocateAdvertisedPrefix(const IAKey& key, uint8_t requestedLength, uint32_t timeout);
    std::pair<uint8_t, Dhcpv6StatusMessage> allocateRequestedAdvertisedPrefix(const IAPrefixKey& key, uint32_t timeout);
    std::pair<uint8_t, Dhcpv6StatusMessage> allocateRequestedPrefix(const IAPrefixKey& key);
    bool activateAdvertisedPrefix(const IAPrefixKey& key);
    void releasePrefix(const IPv6Prefix& key);
    void expirePrefix(const IAPrefixKey& key, uint32_t timeout);
    std::optional<std::unordered_set<IPv6Prefix>> getIAID(const IAKey& key);

    bool excludePrefix(const IPv6Prefix& prefix);
    bool removeExclusion(const IPv6Prefix& prefix);
    bool setConflicted(const IAPrefixKey& key, uint32_t duration);

    bool isAllocated(const IPv6Prefix& prefix) const;
    bool isAdvertised(const IPv6Prefix& prefix) const;
    bool isQuarantined(const IPv6Prefix& prefix) const;
    bool isExcluded(const IPv6Prefix& prefix) const;
    bool isConflicted(const IPv6Prefix& prefix) const;

    bool isFull();

    IPv6Prefix base;
    uint8_t delegationLength;

private:
    __uint128_t size;
    TimeManager& timeManager;

    mutable std::mutex mutex;

    std::unordered_set<IPv6Prefix> allocated;

    std::unordered_map<IAPrefixKey, uint32_t> advertised;
    std::unordered_set<IPv6Prefix> advertisedPDs;
    std::unordered_map<IAKey, std::unordered_set<IPv6Prefix>> advertisedGroups;
    std::unordered_map<IAPrefixKey, uint32_t> quarantined;
    std::unordered_set<IPv6Prefix> quarantinedPDs;

    static constexpr size_t MAX_GENERATION_ATTEMPTS = 1000;

    std::unordered_set<IPv6Prefix> excluded;
    std::unordered_map<IPv6Prefix, uint32_t> bad;

    PrefixLeaseManager* leaseManager = nullptr;

    IPv6Prefix generatePrefix(uint64_t index, uint8_t length) const;
    bool prefixMatches(__uint128_t a, __uint128_t b, uint8_t length) const;
};

#endif // PREFIX_POOL_H
