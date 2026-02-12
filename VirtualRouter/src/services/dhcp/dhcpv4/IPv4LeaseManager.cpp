// IPv4Leasemanager.cpp

#include <TimeManager.h>
#include "IPv4LeaseManager.h"
#include "DhcpServer.h"

IPv4LeaseManager::IPv4LeaseManager(IPv4Pool& pool, Protocol::Dhcp::Configs& cgs)
    : pool(pool), timeManager(pool.timeManager), configs(cgs) {}

bool IPv4LeaseManager::createLeaseFromTemp(const ClientID& clientId, uint32_t tempIp, uint32_t leaseTime, uint32_t t1, uint32_t t2)
{
    std::lock_guard<std::mutex> lock(leaseMutex);
    if (leases.count(clientId)) return false;

    if (!pool.activateTemporaryIP(tempIp, clientId))
        return false;

    auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(leaseTime);
    uint32_t timerId = timeManager.addTimer(expiry, [this, clientId](uint32_t) {
        expireLease(clientId);
    });

    leases[clientId] = Lease{ tempIp, leaseTime, t1, t2, expiry, timerId };
    return true;
}

uint32_t IPv4LeaseManager::createLease(const ClientID& clientId, uint32_t leaseTime, uint32_t t1, uint32_t t2)
{
    std::lock_guard<std::mutex> lock(leaseMutex);
    if (leases.count(clientId)) return false;

    uint32_t finalIP = pool.allocateIP(clientId);
    if (finalIP == 0) return 0;

    auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(leaseTime);
    uint32_t timerId = timeManager.addTimer(expiry, [this, clientId](uint32_t) {
        expireLease(clientId);
    });

    leases[clientId] = Lease{ finalIP, leaseTime, t1, t2, expiry, timerId };
    return finalIP;
}

bool IPv4LeaseManager::createLeaseFromReq(const ClientID& clientId, uint32_t reqIp, uint32_t leaseTime, uint32_t t1, uint32_t t2)
{
    std::lock_guard<std::mutex> lock(leaseMutex);
    if (leases.count(clientId)) return false;

    if (!pool.allocateRequestedIP(reqIp, clientId))
        return false;

    auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(leaseTime);
    uint32_t timerId = timeManager.addTimer(expiry, [this, clientId](uint32_t) {
        expireLease(clientId);
    });

    leases[clientId] = Lease{ reqIp, leaseTime, t1, t2, expiry, timerId };
    return true;
}

bool IPv4LeaseManager::renewLease(const ClientID& clientId, uint32_t leaseTime, uint32_t t1, uint32_t t2)
{
    std::lock_guard<std::mutex> lock(leaseMutex);
    auto it = leases.find(clientId);
    if (it == leases.end()) return false;

    timeManager.cancelTimer(it->second.timerId);
    auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(leaseTime);
    uint64_t timerId = timeManager.addTimer(expiry, [this, clientId](uint32_t) {
        expireLease(clientId);
    });

    it->second.leaseTime = leaseTime;
    it->second.t1 = t1;
    it->second.t2 = t2;
    it->second.expiry = expiry;
    it->second.timerId = timerId;
    return true;
}

bool IPv4LeaseManager::rebindLease(const ClientID& clientId, uint32_t ip, uint32_t leaseTime, uint32_t t1, uint32_t t2)
{
    std::lock_guard<std::mutex> lock(leaseMutex);
    auto it = leases.find(clientId);
    if (it == leases.end()) return false;
    if (it->second.ip != ip) return false;

    return renewLease(clientId, leaseTime, t1, t2);
}

bool IPv4LeaseManager::releaseLease(const ClientID& clientId)
{
    std::lock_guard<std::mutex> lock(leaseMutex);
    auto it = leases.find(clientId);
    if (it == leases.end()) return false;

    timeManager.cancelTimer(it->second.timerId);
    pool.releaseIP(it->second.ip, clientId);
    leases.erase(it);
    return true;
}

bool IPv4LeaseManager::declineLease(const ClientID& clientId, uint32_t ip)
{
    std::lock_guard<std::mutex> lock(leaseMutex);
    auto it = leases.find(clientId);
    if (it == leases.end() && it->second.ip != ip)
        return false;
    timeManager.cancelTimer(it->second.timerId);
    leases.erase(it);

    return pool.setConflicted(ip, configs.declineQuarintine.load(std::memory_order_relaxed));
}

bool IPv4LeaseManager::hasLease(const ClientID& clientId, uint32_t ip) const
{
    std::lock_guard<std::mutex> lock(leaseMutex);
    const auto it = leases.find(clientId);
    return it != leases.end() && it->second.ip == ip;
}

uint32_t IPv4LeaseManager::getIP(const ClientID& clientId) const
{
    std::lock_guard<std::mutex> lock(leaseMutex);
    auto it = leases.find(clientId);
    return (it != leases.end()) ? it->second.ip : 0;
}

bool IPv4LeaseManager::match(const ClientID& clientId, uint32_t ip) const
{
    std::lock_guard<std::mutex> lock(leaseMutex);
    auto it = leases.find(clientId);
    return (it != leases.end() && it->second.ip == ip);
}

void IPv4LeaseManager::expireLease(const ClientID& clientId)
{
    std::lock_guard<std::mutex> lock(leaseMutex);
    auto it = leases.find(clientId);
    if (it == leases.end()) return;

    pool.expireIP(it->second.ip, clientId, configs.leaseExpirationOffset.load(std::memory_order_release));
    leases.erase(it);
}

size_t IPv4LeaseManager::size()
{
    std::lock_guard<std::mutex> lock(leaseMutex);
    return leases.size();
}

std::optional<IPv4LeaseManager::Lease> IPv4LeaseManager::getLease(const ClientID& clientId, uint32_t ip) const
{
    std::lock_guard<std::mutex> lock(leaseMutex);
    auto it = leases.find(clientId);
    if (it == leases.end() || it->second.ip != ip) return std::nullopt;
    return it->second;
}
