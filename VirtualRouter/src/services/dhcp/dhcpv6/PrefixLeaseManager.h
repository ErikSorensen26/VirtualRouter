// PrefixLeaseManager.h

#ifndef PREFIX_LEASE_MANAGER_H
#define PREFIX_LEASE_MANAGER_H

#include <unordered_map>
#include <mutex>
#include <TimeManager.h>

#include "PrefixPool.h"

namespace Protocol
{
    namespace Dhcpv6
    {
        struct Configs;
    }
    class Dhcpv6Server;
}

class PrefixLeaseManager {
public:
    PrefixLeaseManager(PrefixPool& pool, TimeManager& timeManager, Protocol::Dhcpv6::Configs& configs);
    friend class Protocol::Dhcpv6Server;

    struct StaticBinding
    {
        uint32_t preferred = 0;
        uint32_t valid = 0;
        IPv6Prefix prefix;
    };

    std::unordered_map<IAKey, StaticBinding> staticPDs;

    std::pair<Dhcpv6StatusMessage, std::unordered_set<IPv6Prefix>> createPrefix(const IAKey& key, uint32_t valid);
    std::pair<IPv6Prefix, Dhcpv6StatusMessage> createPrefix(const IAKey& key, uint8_t length, uint32_t valid);
    Dhcpv6StatusMessage createPrefixFromAdvertised(const IPv6Prefix& prefix, const IAKey& key, uint32_t valid);
    Dhcpv6StatusMessage createPrefixFromRequest(const IPv6Prefix& prefix, const IAKey& key, uint32_t valid);
    Dhcpv6StatusMessage renewPrefix(const IPv6Prefix& prefix, const IAKey& key, uint32_t leaseTime);
    Dhcpv6StatusMessage rebindPrefix(const IPv6Prefix& prefix, const IAKey& key, uint32_t valid);
    Dhcpv6StatusMessage releasePrefix(const IPv6Prefix& prefix, const IAKey& key);
    Dhcpv6StatusMessage declinePrefix(const IPv6Prefix& prefix, const IAKey& key);
    std::unordered_set<IPv6Prefix> getPrefixes(const IAKey& key) const;
    bool match(const IAKey& key, const IPv6Prefix& prefix) const;
    size_t size() const;

private:

    void expirePrefix(const IPv6Prefix& prefix, const IAKey& key);

    PrefixPool& pool;
    TimeManager& timeManager;
    Protocol::Dhcpv6::Configs& configs;
    mutable std::mutex mutex;

    std::unordered_map<IAKey, std::unordered_set<IPv6Prefix>> prefixes;
    std::unordered_map<IPv6Prefix, IAKey> prefixKeys;
    std::unordered_map<IPv6Prefix, uint32_t> prefixTimerIDs;
};

#endif // PREFIX_LEASE_MANAGER_H
