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

ByteString LeaseManager::allocateIP(double leaseTime, double t1Percent, double t2Percent, const ByteString* duid, bool useIP)
{
    std::lock_guard<std::mutex> lock(leaseMutex);

    // Check if the MAC already has a lease
    {
        for (const auto& [ip, lease] : leases) 
        {
            if (ip.size() != 4) break;

            if (lease.clientID == *duid)
            {
                //std::cout << ip.toHex() << std::endl;
                return ip;
            }
        }
    }

    // Get next available IP from the pool
    ByteString allocatedIP = ipPool->allocateIP(duid);
    if (allocatedIP.empty()) return {}; // No available IPs

    uint32_t t1 = static_cast<uint32_t>(leaseTime * t1Percent);
    uint32_t t2 = static_cast<uint32_t>(leaseTime * t2Percent);

    // Store lease info
    double currentTime = secondsSinceEpoch();
    leases[allocatedIP] = {allocatedIP, duid ? *duid + (useIP ? allocatedIP : "") : "", currentTime, leaseTime, t1, t2};
    
    // Add lease to global table if able
    if (globalLeaseManager)
    {
        globalLeaseManager->updateLeaseRecord(duid ? *duid + (useIP ? allocatedIP : "") : "", networkID, leases[allocatedIP]);
    }

    //std::cout << allocatedIP.toHex() << std::endl;
    return allocatedIP;
}

bool LeaseManager::allocateRequestedIP(const ByteString& requestedIP, double leaseTime, double t1Percent, double t2Percent, const ByteString* duid)
{
    std::lock_guard<std::mutex> lock(leaseMutex);

    // Check if the MAC already has a lease
    if (duid && requestedIP.size() == 4)
    {
        for (const auto& [ip, lease] : leases) 
        {
            if (lease.clientID == *duid)
            {
                return false;
            }
        }
    }

    // Get next available IP from the pool
    if (!ipPool->excludeIP(requestedIP)) return false;

    uint32_t t1 = static_cast<uint32_t>(leaseTime * t1Percent);
    uint32_t t2 = static_cast<uint32_t>(leaseTime * t2Percent);

    // Store lease info
    double currentTime = secondsSinceEpoch();
    leases[requestedIP] = {requestedIP, duid ? *duid : "", currentTime, leaseTime, t1, t2};
    
    // Add lease to global table if able
    if (globalLeaseManager)
    {
        globalLeaseManager->updateLeaseRecord(duid ? *duid : "", networkID, leases[requestedIP]);
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

ByteString LeaseManager::activateLeaseFromTemp(const ByteString& ip, double leaseTime, double t1Percent, double t2Percent, const ByteString* duid)
{
    if (duid)
    {
        ByteString tempIP = ipPool->getTempIP(*duid);
        if (tempIP.empty() || tempIP != ip) return {};
    }

    ByteString newIp = ipPool->activateTempIP(ip, duid);
    ipPool->clearTempOffer(ip);
    if (newIp.empty() || newIp != ip)
    {
        return {};
    }

    uint32_t t1 = static_cast<uint32_t>(leaseTime * t1Percent);
    uint32_t t2 = static_cast<uint32_t>(leaseTime * t2Percent);

    // Store lease info
    double currentTime = secondsSinceEpoch();
    leases[ip] = {ip, duid ? *duid : "", currentTime, leaseTime, t1, t2};

    return {};
}

const std::unordered_map<ByteString, LeaseManager::Lease>& LeaseManager::getActiveLeases() const
{
    return leases;
}
