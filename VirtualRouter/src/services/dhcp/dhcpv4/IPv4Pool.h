// IPv4Pool.h

#ifndef IPV4_POOL_H
#define IPV4_POOL_H

#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <deque>

#include "dhcp/DhcpInfo.hpp"

class TimeManager;
class IPv4LeaseManager;

enum class IPState : uint8_t
{
    NONE = 0,
    ALLOCATED,
    TEMPORARY,
    QUARENTINED,
    BAD,
    EXCLUDED
};

struct IPEntry
{
    IPState state = IPState::NONE;
    ClientID client{};
    uint32_t timerID = 0;
};

class IPv4Pool
{
public:

    IPv4Pool(TimeManager& timeManager);
    ~IPv4Pool();
    
    friend class IPv4LeaseManager;

    uint32_t allocateIP(const ClientID& clientId);
    uint32_t allocateTemporaryIP(const ClientID& clientId, uint32_t timeout);
    bool allocateRequestedTemporaryIP(uint32_t ip, const ClientID& clientId, uint32_t timeout);
    bool allocateRequestedIP(uint32_t ip, const ClientID& clientId);
    uint32_t getTemporaryIPForClient(const ClientID& clientId);
    bool activateTemporaryIP(uint32_t ip, const ClientID& clientId);
    void clearTemporaryOffer(uint32_t ip);

    bool excludeIP(uint32_t ip);
    bool removeExclusion(uint32_t ip);
    void releaseIP(uint32_t ip, const ClientID& client);
    void expireIP(uint32_t ip, const ClientID& client, size_t timeout);
    bool setConflicted(uint32_t ip, uint32_t duration);
    void cleanupBadIPs();
        
    bool isAllocated(uint32_t ip) const;
    bool isExcluded(uint32_t ip) const;
    bool isConflicted(uint32_t ip) const;
    bool isTemporarilyOffered(uint32_t ip) const;
    bool isTemporarilyOffered(uint32_t ip, ClientID id) const;
    bool matchIPToClient(uint32_t ip, ClientID& clientId) const;

    bool adjustPool(uint32_t* network, uint8_t* prefixLen, uint32_t* newGateway);
    bool withinRange(uint32_t ip) const;
    void setLeaseManager(IPv4LeaseManager* leaseMgr);

    std::atomic<uint32_t> broadcast = 0;

    std::atomic<bool> init = false;

private:
    IPv4Pool(const IPv4Pool&) = delete;
    IPv4Pool& operator=(const IPv4Pool&) = delete;

    bool isAllocatedOrExcluded(uint32_t ip) const;
    bool isReserved(uint32_t ip) const;

    mutable std::mutex poolMutex;

    uint32_t baseAddress = 0;
    uint32_t lastAddress = 0;
    uint32_t currentAddress = 0;
    uint32_t poolSize = 0;
    uint32_t gateway = 0;

    std::unordered_map<uint32_t, ClientID> allocated;
    std::unordered_map<ClientID, uint32_t> reverseAllocated;
    std::unordered_map<uint32_t, std::pair<ClientID, uint32_t>> temporary;
    std::unordered_map<ClientID, uint32_t> reverseTemporary;
    std::unordered_map<uint32_t, std::pair<ClientID, uint32_t>> quarantined;
    std::unordered_map<ClientID, uint32_t> reverseQuarantined;
    std::unordered_map<uint32_t, uint32_t> bad; // Bad ip to timer ID
    std::unordered_set<uint32_t> excluded;
    std::deque<uint32_t> release;

    IPv4LeaseManager* leaseManager = nullptr;
    TimeManager& timeManager;
};

#endif // IPV4_POOL_H
