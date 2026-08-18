/**
 * @file PrefixLeaseManager.h
 * @ingroup SERVICES_DHCP_V6
 */

// PrefixLeaseManager.h

#ifndef PREFIX_LEASE_MANAGER_H
#define PREFIX_LEASE_MANAGER_H

#include <unordered_map>
#include <mutex>
#include <TimeManager.h>

#include "PrefixPool.h"

namespace services::dhcp
{
struct Configs;
class Dhcpv6Server;

class PrefixLeaseManager {
public:
    PrefixLeaseManager(PrefixPool& pool, core::TimeManager& timeManager, Configs& configs);
    friend class Dhcpv6Server;

    struct StaticBinding
    {
        uint32_t preferred = 0;
        uint32_t valid = 0;
        types::IPv6Prefix prefix;
    };

    std::unordered_map<IAKey, StaticBinding> staticPDs;

    std::pair<Dhcpv6StatusMessage, std::unordered_set<types::IPv6Prefix>> createPrefix(const IAKey& key, uint32_t valid);
    std::pair<types::IPv6Prefix, Dhcpv6StatusMessage> createPrefix(const IAKey& key, uint8_t length, uint32_t valid);
    Dhcpv6StatusMessage createPrefixFromAdvertised(const types::IPv6Prefix& prefix, const IAKey& key, uint32_t valid);
    Dhcpv6StatusMessage createPrefixFromRequest(const types::IPv6Prefix& prefix, const IAKey& key, uint32_t valid);
    Dhcpv6StatusMessage renewPrefix(const types::IPv6Prefix& prefix, const IAKey& key, uint32_t leaseTime);
    Dhcpv6StatusMessage rebindPrefix(const types::IPv6Prefix& prefix, const IAKey& key, uint32_t valid);
    Dhcpv6StatusMessage releasePrefix(const types::IPv6Prefix& prefix, const IAKey& key);
    Dhcpv6StatusMessage declinePrefix(const types::IPv6Prefix& prefix, const IAKey& key);
    std::unordered_set<types::IPv6Prefix> getPrefixes(const IAKey& key) const;
    bool match(const IAKey& key, const types::IPv6Prefix& prefix) const;
    size_t size() const;

private:

    void expirePrefix(const types::IPv6Prefix& prefix, const IAKey& key);

    PrefixPool& pool;
    core::TimeManager& timeManager;
    Configs& configs;
    mutable std::mutex mutex;

    std::unordered_map<IAKey, std::unordered_set<types::IPv6Prefix>> prefixes;
    std::unordered_map<types::IPv6Prefix, IAKey> prefixKeys;
    std::unordered_map<types::IPv6Prefix, uint32_t> prefixTimerIDs;
};

} // namespace services::dhcp

#endif // PREFIX_LEASE_MANAGER_H

