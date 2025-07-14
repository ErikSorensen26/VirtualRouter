// IPv6Pool.h

#ifndef IPV6_POOL_H
#define IPV6_POOL_H

#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <DhcpInfo.hpp>
#include <Dhcpv6.h>

class TimeManager;
class IPv6LeaseManager;
namespace Protocol
{
    class Dhcpv6Server;
}

class IPv6Pool
{
public:
    enum class IAType{ IATA, IANA };

    IPv6Pool(TimeManager& timeManager);
    ~IPv6Pool();

    friend class IPv6LeaseManager;
    friend class Protocol::Dhcpv6Server;

    bool adjustPool(__uint128_t network, uint8_t prefixLen);
    void setLeaseManager(IPv6LeaseManager* leaseMgr);
    bool withinRange(__uint128_t addr) const;

    void clearAdvertisedIP(const IALeaseKey& key);

    __uint128_t allocate();
    __uint128_t allocateAdvertised(const IAKey& key, uint32_t timeout);
    Dhcpv6StatusMessage allocateRequestedAdvertised(const IALeaseKey& key, uint32_t timeout);
    Dhcpv6StatusMessage allocateRequested(const IALeaseKey& key);
    bool activateAdvertised(const IALeaseKey& key);
    void release(__uint128_t addr);
    void expire(const IALeaseKey& key, uint32_t timeout);
    std::optional<std::unordered_set<__uint128_t>> getIAID(const IAKey& key);

    bool excludeIP(__uint128_t addr);
    bool removeExclusion(__uint128_t addr);
    bool setConflicted(const IALeaseKey& key, uint32_t duration);
    void cleanupBadIPs();
        
    bool isAllocated(__uint128_t addr) const;
    bool isExcluded(__uint128_t addr) const;
    bool isQuarantined(__uint128_t addr) const;
    bool isConflicted(__uint128_t addr) const;
    bool isAdvertised(__uint128_t addr) const;
    bool isAdvertised(const IALeaseKey& key) const;
    bool isAllocatedOrExcluded(__uint128_t addr) const;

    std::atomic<bool> init = false;

private:
    mutable std::mutex mutex;

    IPv6Pool(const IPv6Pool&) = delete;
    IPv6Pool& operator=(const IPv6Pool&) = delete;

    __uint128_t base{};
    __uint128_t size = 0;
    static constexpr __uint128_t SLAAC_SAFE_OFFSET = 0x1000;
    static constexpr size_t MAX_GENERATION_ATTEMPTS = 10000;

    IPv6LeaseManager* leaseManager = nullptr;
    TimeManager& timeManager;

    std::unordered_set<__uint128_t> allocated;

    std::unordered_map<IALeaseKey, uint32_t> advertised;
    std::unordered_set<__uint128_t> advertisedIPs;
    std::unordered_map<IAKey, std::unordered_set<__uint128_t>> advertisedGroups;

    std::unordered_map<IALeaseKey, uint32_t> quarantined;
    std::unordered_set<__uint128_t> quarantinedIPs;

    std::unordered_set<__uint128_t> excluded;
    std::unordered_map<__uint128_t, uint32_t> bad;

    static thread_local std::minstd_rand rng;

    __uint128_t generateRandomIP();
    bool isEUI64(__uint128_t ip) const;
};

#endif // IPV6_POOL_H
