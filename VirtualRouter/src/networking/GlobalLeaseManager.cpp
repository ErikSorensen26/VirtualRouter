#include "GlobalLeaseManager.h"

void GlobalLeaseManager::addLeaseManager(const ByteString& networkID, LeaseManager* lm) 
{
    std::lock_guard<std::mutex> lock(mtx);
    leaseManagers[networkID] = lm;
}

void GlobalLeaseManager::removeLeaseManager(const ByteString& networkID) {
    std::lock_guard<std::mutex> lock(mtx);
    leaseManagers.erase(networkID);
    
    for (auto it = globalLookup.begin(); it != globalLookup.end(); )
    {
        if (it->second.networkID == networkID)
            it = globalLookup.erase(it);
        else
            ++it;
    }
}

void GlobalLeaseManager::updateLeaseRecord(const ByteString& mac, const ByteString& networkID, const LeaseManager::Lease& lease)
{
    std::lock_guard<std::mutex> lock(mtx);
    globalLookup[mac] = GlobalLeaseRecord{networkID, lease};
}

void GlobalLeaseManager::removeLeaseRecord(const ByteString& mac)
{
    std::lock_guard<std::mutex> lock(mtx);
    globalLookup.erase(mac);
}

std::optional<GlobalLeaseRecord> GlobalLeaseManager::findLease(const ByteString& mac) const
{
    std::lock_guard<std::mutex> lock(mtx);
    auto it = globalLookup.find(mac);
    if (it != globalLookup.end())
        return it->second;
    return std::nullopt;
}

bool GlobalLeaseManager::moveManager(const ByteString& oldKey, const ByteString& newKey)
{
    std::lock_guard<std::mutex> lock(mtx);
    if (leaseManagers.find(oldKey) != leaseManagers.end() && leaseManagers.find(newKey) == leaseManagers.end())
    {
        leaseManagers[newKey] = leaseManagers[oldKey];
        leaseManagers.erase(oldKey);
        return true;
    }
    return false;
}
