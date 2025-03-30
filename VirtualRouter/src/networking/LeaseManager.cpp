// LeaseManager.cpp

#include "LeaseManager.h"
#include <Functions.h>
#include <GlobalLeaseManager.h>

LeaseManager::LeaseManager(IPPool* pool) : ipPool(pool)
{
    ipPool->addLeaseManager(this);
}

void LeaseManager::addGlobalManager(ByteString& ID, GlobalLeaseManager* globalLm)
{
    if (globalLm)
    {
        // Add lease manager to the global table.
        globalLeaseManager = globalLm;
        networkID = ID;
        globalLeaseManager->addLeaseManager(networkID, this);
    }
}

ByteString LeaseManager::allocateIP(const ByteString& macAddress, double leaseTime, double t1Percent, double t2Percent)
{
    std::lock_guard<std::mutex> lock(leaseMutex);

    // Check if the MAC already has a lease
    for (const auto& [ip, lease] : leases) 
    {
        if (lease.clientID == macAddress)
        {
            return ip;
        }
    }

    // Get next available IP from the pool
    ByteString allocatedIP = ipPool->allocateIP(macAddress);
    if (allocatedIP.empty()) return {}; // No available IPs

    uint32_t t1 = static_cast<uint32_t>(leaseTime * t1Percent);
    uint32_t t2 = static_cast<uint32_t>(leaseTime * t2Percent);

    // Store lease info
    double currentTime = secondsSinceEpoch();
    leases[allocatedIP] = {allocatedIP, macAddress, currentTime, leaseTime, t1, t2};
    
    // Add lease to global table if able
    if (globalLeaseManager)
    {
        globalLeaseManager->updateLeaseRecord(macAddress, networkID, leases[allocatedIP]);
    }

    return allocatedIP;
}

bool LeaseManager::allocateRequestedIP(const ByteString& macAddress, const ByteString& requestedIP, double leaseTime, double t1Percent, double t2Percent)
{
    std::lock_guard<std::mutex> lock(leaseMutex);

    // Check if the MAC already has a lease
    for (const auto& [ip, lease] : leases) 
    {
        if (lease.clientID == macAddress)
        {
            return false;
        }
    }

    // Get next available IP from the pool
    if (!ipPool->excludeIP(requestedIP)) return false;

    uint32_t t1 = static_cast<uint32_t>(leaseTime * t1Percent);
    uint32_t t2 = static_cast<uint32_t>(leaseTime * t2Percent);

    // Store lease info
    double currentTime = secondsSinceEpoch();
    leases[requestedIP] = {requestedIP, macAddress, currentTime, leaseTime, t1, t2};
    
    // Add lease to global table if able
    if (globalLeaseManager)
    {
        globalLeaseManager->updateLeaseRecord(macAddress, networkID, leases[requestedIP]);
    }

    return true;
    
}

void LeaseManager::releaseIP(const ByteString& ipAddress)
{
    std::lock_guard<std::mutex> lock(leaseMutex);

    auto it = leases.find(ipAddress);
    if (it != leases.end())
    {
        // Remove address from global table if able to.
        if (globalLeaseManager)
        {
            globalLeaseManager->removeLeaseRecord(it->first);
        }
        
        ipPool->releaseIP(ipAddress); // Return IP to the pool
        leases.erase(it);
    }
}

bool LeaseManager::isAllocated(const ByteString& ipAddress)
{
    std::lock_guard<std::mutex> lock(leaseMutex);
    return leases.find(ipAddress) != leases.end();
}

bool LeaseManager::renewLease(const ByteString& ipAddress)
{
    std::lock_guard<std::mutex> lock(leaseMutex);
    auto leaseIt = leases.find(ipAddress);
    if (leaseIt != leases.end())
    {
        leaseIt->second.leaseStart = secondsSinceEpoch();
        return true;
    }
    return false;
}

void LeaseManager::cleanupExpiredLeases()
{
    std::lock_guard<std::mutex> lock(leaseMutex);

    double currentTime = secondsSinceEpoch();
    for (auto it = leases.begin(); it != leases.end();)
    {
        if (currentTime - it->second.leaseStart >= it->second.leaseDuration)
        {
            ipPool->releaseIP(it->second.ipAddress); // Return expired IP to the pool.
            it = leases.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

ByteString LeaseManager::activateLeaseFromTemp(const ByteString& id, double leaseTime, double t1Percent, double t2Percent)
{
    ByteString tempIP = ipPool->getTempIP(id);
    if (tempIP.empty()) return {};

    ByteString ip = ipPool->activateTempIP(id);
    ipPool->clearTempOffer(id);
    if (ip.empty())
    {
        return {};
    }

    uint32_t t1 = static_cast<uint32_t>(leaseTime * t1Percent);
    uint32_t t2 = static_cast<uint32_t>(leaseTime * t2Percent);

    // Store lease info
    double currentTime = secondsSinceEpoch();
    leases[ip] = {ip, id, currentTime, leaseTime, t1, t2};

    return {};
}

const std::unordered_map<ByteString, LeaseManager::Lease>& LeaseManager::getActiveLeases() const
{
    return leases;
}
