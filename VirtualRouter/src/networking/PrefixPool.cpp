
#include "PrefixPool.h"
#include <Functions.h>

PrefixPool::PrefixPool(const ByteString& base, uint8_t length)
    : basePrefix(base), baseLength(length)
{
}

void PrefixPool::addLeaseManager(PrefixLeaseManager* lease)
{
    leaseManager = lease;
}

std::pair<ByteString, uint8_t> PrefixPool::allocatePrefix(const ByteString& duid, uint8_t requestedLength)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (requestedLength < baseLength || requestedLength > 128)
        return {};

    uint64_t count = 1ULL << (requestedLength - baseLength);
    for (uint64_t i = 0; i < count; ++i)
    {
        ByteString prefix = generatePrefix(i, requestedLength);
        if (!isAllocatedOrExcluded(prefix) && prefixMatches(basePrefix, prefix, baseLength))
        {
            allocated[prefix] = {duid, requestedLength};
            return {prefix, requestedLength};
        }
    }
    return {};
}

bool PrefixPool::allocateSpecificPrefix(const ByteString& prefix, uint8_t length, const ByteString& duid)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (prefixMatches(basePrefix, prefix, baseLength) && !isAllocatedOrExcluded(prefix))
    {
        allocated[prefix] = {duid, length};
        return true;
    }
    return false;
}

std::pair<ByteString, uint8_t> PrefixPool::allocateTempPrefix(const ByteString& duid, uint8_t requestedLength)
{
    // Store temporary offers with ID for tracking
    std::lock_guard<std::mutex> lock(mutex);

    if (requestedLength < baseLength || requestedLength > 128)
        return {};

    uint64_t count = 1ULL << (requestedLength - baseLength);
    for (uint64_t i = 0; i < count; ++i)
    {
        ByteString prefix = generatePrefix(i, requestedLength);
        if (!prefixMatches(basePrefix, prefix, baseLength))
            continue;
        temporaryOffers[duid] = {prefix, requestedLength};
        return {prefix, requestedLength};
    }
    return {};
}

bool PrefixPool::excludePrefix(const ByteString& prefix)
{
    if (isExcluded(prefix)) return false;
    
    std::lock_guard<std::mutex> lock(mutex);
    excluded.insert(prefix);
    return true;
}

void PrefixPool::releasePrefix(const ByteString& prefix)
{
    std::lock_guard<std::mutex> lock(mutex);
    allocated.erase(prefix);
    // Also clear temp offers just in case
    for (auto it = temporaryOffers.begin(); it != temporaryOffers.end();)
    {
        if (it->second.first == prefix)
            it = temporaryOffers.erase(it);
        else
            ++it;
    }
}

bool PrefixPool::removeExclusion(const ByteString& prefix)
{
    std::lock_guard<std::mutex> lock(mutex);
    return excluded.erase(prefix) > 0;
}

bool PrefixPool::isAllocated(const ByteString& prefix) const
{
    std::lock_guard<std::mutex> lock(mutex);
    return allocated.count(prefix) > 0;
}

bool PrefixPool::isExcluded(const ByteString& prefix) const
{
    std::lock_guard<std::mutex> lock(mutex);
    return excluded.count(prefix) > 0;
}

bool PrefixPool::isAllocatedOrExcluded(const ByteString& prefix) const
{
    std::lock_guard<std::mutex> lock(mutex);
    return allocated.count(prefix) > 0 || excluded.count(prefix) > 0;
}

bool PrefixPool::isTemporarilyOffered(const ByteString& prefix)
{
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& [_, temp] : temporaryOffers)
    {
        if (temp.first == prefix)
            return true;
    }
    return false;
}

std::map<ByteString, std::pair<ByteString, uint8_t>> PrefixPool::getAllocations() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return allocated;
}

std::pair<ByteString, uint8_t> PrefixPool::acivateOfferedPrefix(const ByteString& duid)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = temporaryOffers.find(duid);
    if (it != temporaryOffers.end())
    {
        ByteString prefix = it->second.first;
        allocated[prefix] = {duid, it->second.second};
        temporaryOffers.erase(it);
        return allocated[prefix];
    }
    return {};
}

ByteString PrefixPool::generatePrefix(uint64_t index, uint8_t length) const
{
    __uint128_t base = Functions::byteToNum128(basePrefix);
    __uint128_t value = base | (__uint128_t(index) << (128 - length));
    return Functions::numToByte128(value);
}

bool PrefixPool::prefixMatches(const ByteString& a, const ByteString& b, uint8_t length) const
{
    __uint128_t aInt = Functions::byteToNum128(a);
    __uint128_t bInt = Functions::byteToNum128(b);
    __uint128_t mask = ~(__uint128_t(0)) << (128 - length);
    return (aInt & mask) == (bInt & mask);
}

uint8_t PrefixPool::getPrefixLength(const ByteString& prefix) const
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = allocated.find(prefix);
    if (it != allocated.end())
        return it->second.second;
    return 0;
}

std::vector<std::pair<ByteString, uint8_t>> PrefixPool::getAvailablePrefixes(uint8_t requestedLength) const
{
    std::vector<std::pair<ByteString, uint8_t>> result;
    std::lock_guard<std::mutex> lock(mutex);

    if (requestedLength < baseLength || requestedLength > 128)
        return result;

    uint64_t count = 1ULL << (requestedLength - baseLength);
    for (uint64_t i = 0; i < count; ++i)
    {
        ByteString prefix = generatePrefix(i, requestedLength);
        if (prefixMatches(basePrefix, prefix, baseLength) && !isAllocatedOrExcluded(prefix))
            result.emplace_back(prefix, requestedLength);
    }
    return result;
}

std::pair<ByteString, uint8_t> PrefixPool::getTempPrefix(const ByteString& id)
{
    auto it = temporaryOffers.find(id);
    if (it != temporaryOffers.end())
    {
        return it->second;
    }
    return {};
}

void PrefixPool::clearTempPrefix(const ByteString& id)
{
    auto it = temporaryOffers.find(id);
    if (it != temporaryOffers.end())
    {
        temporaryOffers.erase(it);
    }
}
