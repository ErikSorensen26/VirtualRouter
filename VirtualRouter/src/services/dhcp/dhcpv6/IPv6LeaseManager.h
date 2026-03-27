/**
 * @file IPv6LeaseManager.h
 */

// IPv6LeaseManager.h

#ifndef IPV6_LEASE_MANAGER_H
#define IPV6_LEASE_MANAGER_H

#include <unordered_map>
#include <mutex>
#include "IPv6Pool.h"
#include <TimeManager.h>

namespace services::dhcp
{

struct Configs;
class Dhcpv6Server;

class IPv6LeaseManager
{
public:
    IPv6LeaseManager(IPv6Pool& pool, Configs& configs);
    friend class Dhcpv6Server;

    struct StaticBinding
    {
        uint32_t preferred = 0;
        uint32_t valid = 0;
        __uint128_t address;
    };

    std::unordered_map<IAKey, StaticBinding> staticNAs;
    
    std::unordered_set<__uint128_t> createLease(const IAKey& key, uint32_t leaseTime);
    Dhcpv6StatusMessage createLeaseFromAdvertised(__uint128_t addr, const IAKey& key, uint32_t leaseTime);
    Dhcpv6StatusMessage createLeaseFromRequest(__uint128_t addr, const IAKey& key, uint32_t leaseTime);
    Dhcpv6StatusMessage renewLease(__uint128_t addr, const IAKey& key, uint32_t leaseTime);
    Dhcpv6StatusMessage rebindLease(__uint128_t addr, const IAKey& key, uint32_t leaseTime);
    Dhcpv6StatusMessage releaseLease(__uint128_t addr, const IAKey& key);
    Dhcpv6StatusMessage declineLease(__uint128_t addr, const IAKey& key);
    std::optional<std::vector<__uint128_t>> getIANA(const IAKey& key) const;

    bool isLeased(__uint128_t addr, const IAKey& key) const;

    size_t size() const;

private:

    void expireLease(__uint128_t addr, const IAKey& key);

    IPv6Pool& pool;
    core::TimeManager& timeManager;
    Configs& configs;
    mutable std::mutex mutex;

    std::unordered_map<IAKey, std::unordered_set<__uint128_t>> leases;
    std::unordered_map<__uint128_t, IAKey> leaseKeys;
    std::unordered_map<__uint128_t, uint32_t> leaseTimerIDs;

};

} // namespace services::dhcp

#endif //IPV6_LEASE_MANAGER_H

