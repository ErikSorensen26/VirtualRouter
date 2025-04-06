
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

std::pair<ByteString, uint8_t> PrefixPool::allocatePrefix(const ByteString& duid, uint8_t requestedLength, bool usePrefix)
{
    //std::cout << basePrefix.toHex() << std::endl;
    __uint128_t count;
    ByteString tempBasePrefix;
    uint8_t tempBaseLength;
    {
        std::lock_guard<std::mutex> lock(mutex);
        tempBasePrefix = basePrefix;
        tempBaseLength = baseLength;
        count = __uint128_t(1) << (requestedLength - baseLength);
    }

    if (requestedLength < tempBaseLength || requestedLength > 128)
        return {};

    for (__uint128_t i = 1; i < count; ++i)
    {
        ByteString prefix = generatePrefix(i, requestedLength);
        if (!isAllocatedOrExcluded(prefix) && prefixMatches(tempBasePrefix, prefix, tempBaseLength))
        {
            allocated[prefix] = {duid + (usePrefix ? prefix : ""), requestedLength};
            return {prefix, requestedLength};
        }
    }
    return {};
}

bool PrefixPool::allocateSpecificPrefix(const ByteString& prefix, uint8_t length, const ByteString& duid)
{
    ByteString tempBasePrefix;
    uint8_t tempBaseLength;
    {
        std::lock_guard<std::mutex> lock(mutex);
        tempBasePrefix = basePrefix;
        tempBaseLength = baseLength;
    }

    if (prefixMatches(tempBasePrefix, prefix, tempBaseLength) && !isAllocatedOrExcluded(prefix))
    {
        std::lock_guard<std::mutex> lock(mutex);
        allocated[prefix] = {duid, length};
        return true;
    }
    return false;
}

std::pair<ByteString, uint8_t> PrefixPool::allocateTempPrefix(const ByteString& duid, uint8_t requestedLength, bool usePrefix)
{
    // Store temporary offers with ID for tracking
    ByteString tempBasePrefix;
    uint8_t tempBaseLength;
    __uint128_t count;
    {
        std::lock_guard<std::mutex> lock(mutex);
        tempBasePrefix = basePrefix;
        tempBaseLength = baseLength;
        count = __uint128_t(1) << (requestedLength - baseLength);
    }

    if (requestedLength < tempBaseLength || requestedLength > 128)
        return {};

    for (__uint128_t i = 1; i < count; ++i)
    {
        ByteString prefix = generatePrefix(i, requestedLength);
        if (!prefixMatches(tempBasePrefix, prefix, tempBaseLength))
            continue;
        temporaryOffers[duid + (usePrefix ? prefix : "")] = {prefix, requestedLength};
        //std::cout << prefix.toHex() << std::endl;
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
    return excluded.find(prefix) != excluded.end() || allExcluded;
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

ByteString PrefixPool::generatePrefix(__uint128_t index, uint8_t length) const
{
    __uint128_t base = Functions::byteToNum128(basePrefix);
    __uint128_t shift = 128 - length;
    __uint128_t step = (__uint128_t)1 << shift;

    __uint128_t value = base + (index * step);
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

    ByteString tempBasePrefix;
    __uint128_t count;
    {
        std::lock_guard<std::mutex> lock(mutex);
        tempBasePrefix = basePrefix;
        count = __uint128_t(1) << (requestedLength - baseLength);
    }


    if (requestedLength < baseLength || requestedLength > 128)
        return result;

    for (__uint128_t i = 1; i < count; ++i)
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
