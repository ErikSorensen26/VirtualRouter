// LeaseManager.h

#ifndef IPV4_LEASE_MANAGER_H
#define IPV4_LEASE_MANAGER_H

#include <unordered_map>
#include <mutex>
#include <chrono>
#include "IPv4Pool.h"
#include "TimeManager.h"

namespace Protocol
{
    namespace Dhcp
    {
        struct Configs;
    }
}

class IPv4LeaseManager
{
public:
    explicit IPv4LeaseManager(IPv4Pool& pool, Protocol::Dhcp::Configs& configs);

    bool createLeaseFromTemp(const ClientID& clientId, uint32_t tempIp, uint32_t leaseTime, uint32_t t1, uint32_t t2);
    uint32_t createLease(const ClientID& clientId, uint32_t leaseTime, uint32_t t1, uint32_t t2);
    bool createLeaseFromReq(const ClientID& clientId, uint32_t reqIp, uint32_t leaseTime, uint32_t t1, uint32_t t2);
    bool renewLease(const ClientID& clientId, uint32_t leaseTime, uint32_t t1, uint32_t t2);
    bool rebindLease(const ClientID& clientId, uint32_t ip, uint32_t leaseTime, uint32_t t1, uint32_t t2);
    bool releaseLease(const ClientID& clientId);
    bool declineLease(const ClientID& clientId, uint32_t ip);
    size_t size();
    
    bool hasLease(const ClientID& clientId, uint32_t ip) const;
    uint32_t getIP(const ClientID& clientId) const;
    bool match(const ClientID& clientId, uint32_t ip) const;

    struct Lease
    {
        uint32_t ip;
        uint32_t leaseTime;
        uint32_t t1;
        uint32_t t2;
        std::chrono::steady_clock::time_point expiry;
        uint32_t timerId;
    };

    std::optional<Lease> getLease(const ClientID& clientId, uint32_t ip) const;

private:

    mutable std::mutex leaseMutex;
    std::unordered_map<ClientID, Lease> leases;
    IPv4Pool& pool;
    TimeManager& timeManager;
    Protocol::Dhcp::Configs& configs;

    void expireLease(const ClientID& clientId);
};


#endif // IPV4_LEASE_MANAGER_H
