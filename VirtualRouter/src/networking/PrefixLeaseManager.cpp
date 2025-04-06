
#include "PrefixLeaseManager.h"
#include <Functions.h>

PrefixLeaseManager::PrefixLeaseManager(PrefixPool* p) : pool(p) {}

std::pair<ByteString, uint8_t> PrefixLeaseManager::allocatePrefix(const ByteString& duid, uint8_t requestedLength, double leaseTime, double t1Percent, double t2Percent, bool usePrefix)
{
    std::lock_guard<std::mutex> lock(leaseMutex);
    auto prefixPair = pool->allocatePrefix(duid, requestedLength, usePrefix);
    if (prefixPair.first.empty()) return {};

    uint32_t t1 = static_cast<uint32_t>(t1Percent * leaseTime);
    uint32_t t2 = static_cast<uint32_t>(t2Percent * leaseTime);

    leases[prefixPair.first] = { duid + (usePrefix ? prefixPair.first : ""), secondsSinceEpoch(), leaseTime, requestedLength, t1, t2 };
    //std::cout << prefixPair.first.toHex() << std::endl;
    return prefixPair;
}

bool PrefixLeaseManager::allocateRequestedPrefix(const ByteString& duid, const ByteString& prefix, uint8_t requestedLength, double leaseTime, double t1Percent, double t2Percent)
{
    std::lock_guard<std::mutex> lock(leaseMutex);

    if (isAllocated(prefix))
        return false;

    bool success = pool->allocateSpecificPrefix(prefix, requestedLength, duid);
    if (!success) return false;

    uint32_t t1 = static_cast<uint32_t>(t1Percent * leaseTime);
    uint32_t t2 = static_cast<uint32_t>(t2Percent * leaseTime);

    leases[prefix] = { duid, secondsSinceEpoch(), leaseTime, requestedLength, t1, t2 };
    return true;
}

std::pair<ByteString, uint8_t> PrefixLeaseManager::activateLeaseFromTemp(const ByteString& duid, double leaseTime, double t1Percent, double t2Percent)
{
    std::lock_guard<std::mutex> lock(leaseMutex);
    auto prefix = pool->acivateOfferedPrefix(duid);
    if (prefix.first.empty()) return {};

    uint32_t t1 = static_cast<uint32_t>(t1Percent * leaseTime);
    uint32_t t2 = static_cast<uint32_t>(t2Percent * leaseTime);

    leases[prefix.first] = { duid, secondsSinceEpoch(), leaseTime, prefix.second, t1, t2 };
    return prefix;
}

void PrefixLeaseManager::releasePrefix(const ByteString& prefix)
{
    std::lock_guard<std::mutex> lock(leaseMutex);
    pool->releasePrefix(prefix);
    leases.erase(prefix);
}

bool PrefixLeaseManager::renewPrefix(const ByteString& prefix)
{
    std::lock_guard<std::mutex> lock(leaseMutex);
    auto it = leases.find(prefix);
    if (it != leases.end())
    {
        it->second.leaseStart = secondsSinceEpoch();
        return true;
    }
    return false;
}

bool PrefixLeaseManager::isAllocated(const ByteString& prefix)
{
    std::lock_guard<std::mutex> lock(leaseMutex);
    return leases.contains(prefix);
}

void PrefixLeaseManager::cleanupExpiredLeases()
{
    std::lock_guard<std::mutex> lock(leaseMutex);
    double now = secondsSinceEpoch();

    for (auto it = leases.begin(); it != leases.end(); )
    {
        if (now - it->second.leaseStart >= it->second.leaseDuration)
        {
            pool->releasePrefix(it->first);
            it = leases.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

std::map<ByteString, PrefixLeaseManager::PrefixLease> PrefixLeaseManager::getActiveLeases() const
{
    std::lock_guard<std::mutex> lock(leaseMutex);
    return leases;
}
