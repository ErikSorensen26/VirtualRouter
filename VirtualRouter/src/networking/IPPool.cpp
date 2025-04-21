#include "IPPool.h"
#include <Functions.h>
#include <LeaseManager.h>

IPPool::IPPool(const ByteString& network, const uint8_t& subnetPrefix, const ByteString& gateway)
{
    // Determine if we're working with IPv4 (4 bytes) or IPv6 (16 bytes)
    __uint128_t networkInt = Functions::byteToNum128(network);
    __uint128_t maskInt;

    if (network.size() == 4)
    {
        maskInt = (subnetPrefix == 0) ? 0 : (~0U << (32 - subnetPrefix));
        poolSize = (subnetPrefix < 31) ? (1U << (32 - subnetPrefix)) - 2 : 0;
    }
    else if  (network.size() == 16)
    {
        firstSlaccAddress = Functions::byteToNum128(network.substr(0, 11) + ByteString("\xff\xfe\x00\x00\x00", 5));
        lastSlaccAddress = Functions::byteToNum128(network.substr(0, 11) + ByteString("\xff\xfe\xff\xff\xff", 5));
        __uint128_t slackAmmount = lastSlaccAddress - firstSlaccAddress;

        maskInt = subnetPrefix == 0 ? 0 : (~__uint128_t(0) << (128 - subnetPrefix));
        poolSize = (__uint128_t(1) << (128 - subnetPrefix)) - 4096 - slackAmmount;
    }
    else
    {
        throw std::invalid_argument("Unsupported network size");
    }

    __uint128_t lastUsableInt = 0;
    if (network.size() == 4)
    {
        lastUsableInt = static_cast<__uint128_t>(static_cast<uint32_t>(networkInt) | ~static_cast<uint32_t>(maskInt));
    }
    else if (network.size() == 16)
    {
        lastUsableInt = networkInt | ~maskInt;
    }


    baseAddress = Functions::numToByte128(networkInt);
    lastAddress = Functions::numToByte128(lastUsableInt);

    excludeIP(gateway);
    currentAddress = baseAddress;
}

void IPPool::addLeaseManager(LeaseManager* lease)
{
    leaseManager = lease;
}

ByteString IPPool::allocateIP(const ByteString* duid)
{
    // Check if MAC already is assigned an address
    {
        if (duid && baseAddress.size() == 4)
        {
            std::lock_guard<std::mutex> lock(poolMutex);
            for (auto& [ipAddress, mac] : allocatedIPs)
            {
                if (*duid == mac)
                {
                    return ipAddress;
                }
            }
        }
    }

    // First, check if there are any released IPs available
    if (!releasedIPs.empty())
    {
        // Choose the lowest available released IP
        ByteString canidateIP;
        {
            std::lock_guard<std::mutex> lock(poolMutex);
            canidateIP = *releasedIPs.begin();
            releasedIPs.erase(releasedIPs.begin());
            if (duid)
            {
                allocatedIPs[canidateIP] = *duid;
            }
            else
            {
                allocatedIPs[canidateIP] = "";
            }
        }
        return canidateIP;
    }

    // No released IP available, so  allocate dynamically from the pool
    __uint128_t start;
    __uint128_t end;
    {
        start = Functions::byteToNum128(currentAddress);
        end = Functions::byteToNum128(lastAddress) - 1;
    }

    // Allocate dynamically from the pool
    for (__uint128_t ip = start; ip < end; ++ip)
    {
        if (ip + 1 == firstSlaccAddress) ip = lastSlaccAddress;
        ByteString canidateIP = Functions::numToByte128(ip + 1);
        if (!isAllocatedOrExcluded(canidateIP))
        {
            std::lock_guard<std::mutex> lock(poolMutex);
            if (duid)
            {
                allocatedIPs[canidateIP] = *duid;
            }
            else
            {
                allocatedIPs[canidateIP] = "";
            }
            currentAddress = canidateIP;
            return canidateIP;
        }
    }

    return {}; // No available IPs
}

ByteString IPPool::allocateTempIP(const ByteString* duid, bool useIP)
{
    // Check if DUID already is assigned an address
    {
        if (duid && baseAddress.size() == 4)
        {
            std::lock_guard<std::mutex> lock(poolMutex);
            for (auto& [ipAddress, id] : allocatedIPs)
            {
                if (*duid == id)
                {
                    //std::cout << ipAddress.toHex() << std::endl;
                    return ipAddress;
                }
            }
            for (auto& [tempAddress, id] : temporaryOffers)
            {
                if (*duid == id)
                {
                    //std::cout << tempAddress.toHex() << std::endl;
                    return tempAddress;
                }
            }
        }
    }

    // Store temporary offers with ID for tracking
    if (!releasedIPs.empty())
    {
        // Choose the lowest available released IP
        ByteString canidateIP;
        {
            std::lock_guard<std::mutex> lock(poolMutex);
            canidateIP = *releasedIPs.begin();
            releasedIPs.erase(releasedIPs.begin());
            
            if (duid)
            {
                temporaryOffers[canidateIP] = *duid + (useIP ? canidateIP : "");
            }
            else
            {
                temporaryOffers[canidateIP] = "";
            }
        }
        //std::cout << canidateIP.toHex() << std::endl;
        return canidateIP;
    }
    
    __uint128_t start;
    __uint128_t end;
    {
        std::lock_guard<std::mutex> lock(poolMutex);
        start = Functions::byteToNum128(currentAddress);
        end = Functions::byteToNum128(lastAddress) - 1;
    }

    for (__uint128_t ip = start; ip < end; ++ip)
    {
        if (ip >= end) return {};

        if (ip + 1 == firstSlaccAddress) ip = lastSlaccAddress;
        ByteString canidate = Functions::numToByte128(ip + 1);
        if (!isAllocatedOrExcluded(canidate) && !isTemporarilyOffered(canidate))
        {
            if (duid)
            {
                temporaryOffers[canidate] = *duid + (useIP ? canidate : "");
            }
            else
            {
                temporaryOffers[canidate] = "";
            }
            //std::cout << canidate.toHex() << std::endl;
            return canidate;
        }
    }
    return {};
}

bool IPPool::allocateRequestedTempIP(const ByteString& requestedIP, const ByteString* duid)
{
    __uint128_t ipNum = Functions::byteToNum128(requestedIP);
    if (ipNum >= firstSlaccAddress || ipNum <= lastSlaccAddress) return false;

    __uint128_t start;
    __uint128_t end;
    {
        std::lock_guard<std::mutex> lock(poolMutex);
        start = Functions::byteToNum128(currentAddress);
        end = Functions::byteToNum128(lastAddress) - 1;
    }

    __uint128_t requested;
    requested = Functions::byteToNum128(requestedIP);

    if (requested >= end || requested <= start) return false;
    if (!isAllocatedOrExcluded(requestedIP) || isTemporarilyOffered(requestedIP))
    {
        temporaryOffers[requestedIP] = duid ? *duid : "";
        return true;
    }
    return false;
}

bool IPPool::excludeIP(const ByteString& ip)
{
    __uint128_t ipNum = Functions::byteToNum128(ip);
    if (ip.size() == 16 && (ipNum >= firstSlaccAddress || ipNum <= lastSlaccAddress)) return false;

    if (isExcluded(ip))
    {
        return false; // IP already or excluded
    }
    else
    {
        std::lock_guard<std::mutex> lock(poolMutex);
        excludedAddresses.insert(ip);
    }
    return true;
}

void IPPool::releaseIP(const ByteString& ip)
{
    __uint128_t ipNum = Functions::byteToNum128(ip);
    if (ipNum >= firstSlaccAddress || ipNum <= lastSlaccAddress) return;

    {
        std::lock_guard<std::mutex> lock(poolMutex);
        auto it = allocatedIPs.find(ip);
        if (it != allocatedIPs.end())
        {
            allocatedIPs.erase(it);
            // Store the released IP so it can be reused first.
            releasedIPs.insert(ip);
        }

        auto tempIt = allocatedTempIPs.find(ip);
        if (tempIt != allocatedTempIPs.end())
        {
            allocatedTempIPs.erase(tempIt);
            // Store the released IP so it can be reused first.
            releasedIPs.insert(ip);
        }
        // Check if IP is excluded
        if (!excludedAddresses.count(ip))
        {
            return;
        }
    }
    excludeIP(ip);
}

bool IPPool::removeExclusion(const ByteString& ip)
{
    std::lock_guard<std::mutex> lock(poolMutex);
    if (ip.size() == 4 && Functions::byteToNum(ip) <= Functions::byteToNum(currentAddress))
    {
        releasedIPs.insert(ip);
    }
    else if (ip.size() == 16 && Functions::byteToNum128(ip) <= Functions::byteToNum128(currentAddress))
    {
        releasedIPs.insert(ip);
    }
    return excludedAddresses.erase(ip) > 0;
}

bool IPPool::setConflicted(const ByteString& ip)
{
    std::lock_guard<std::mutex> lock(poolMutex);
    if (allocatedIPs.find(ip) == allocatedIPs.end())
    {
        return false;
    }
    allocatedIPs[ip].clear();
    return true;
}

bool IPPool::isAllocated(const ByteString& ip) const
{
    bool allocated = false;
    {
        std::lock_guard<std::mutex> lock(poolMutex);
        allocated = allocatedIPs.find(ip) != allocatedIPs.end();
    }
    return allocated;
}

bool IPPool::isExcluded(const ByteString& ip) const
{
    bool excluded = false;
    {
        std::lock_guard<std::mutex> lock(poolMutex);
        excluded = excludedAddresses.find(ip) != excludedAddresses.end() || allExcluded;
    }
    return excluded;
}

bool IPPool::isAllocatedOrExcluded(const ByteString& ip) const
{
    return isAllocated(ip) || isExcluded(ip);
}

bool IPPool::matchMacToIP(const ByteString ip, const ByteString& mac)
{
    std::lock_guard<std::mutex> lock(poolMutex);
    auto it = allocatedIPs.find(ip);
    return it != allocatedIPs.end() && it->second == mac;
}

void IPPool::adjustPool(const ByteString& network, const uint8_t subnetPrefix, const ByteString& newGatway)
{
    // Determine if we're working with IPv4 (4 bytes) or IPv6 (16 bytes)
    __uint128_t networkInt = Functions::byteToNum(network);
    __uint128_t maskInt;

    if (network.size() == 4)
    {
        maskInt = (subnetPrefix == 0) ? 0 : (~0U << (32 - subnetPrefix));
        poolSize = (subnetPrefix < 31) ? (1U << (32 - subnetPrefix)) - 2 : 0;
    }
    else if  (network.size() == 16)
    {
        firstSlaccAddress = Functions::byteToNum128(network.substr(0, 11) + ByteString("\xff\xfe\x00\x00\x00", 5));
        lastSlaccAddress = Functions::byteToNum128(network.substr(0, 11) + ByteString("\xff\xfe\xff\xff\xff", 5));
        __uint128_t slackAmmount = lastSlaccAddress - firstSlaccAddress;

        maskInt = subnetPrefix == 0 ? 0 : (~__uint128_t(0) << (128 - subnetPrefix));
        poolSize = (__uint128_t(1) << (128 - subnetPrefix)) - 4096 - slackAmmount;
    }
    else
    {
        throw std::invalid_argument("Unsupported network size");
    }

    __uint128_t lastUsableInt = 0;
    if (network.size() == 4)
    {
        lastUsableInt = static_cast<__uint128_t>(static_cast<uint32_t>(networkInt) | ~static_cast<uint32_t>(maskInt));
    }
    else if (network.size() == 16)
    {
        lastUsableInt = networkInt | ~maskInt;
    }
    
    {
        std::lock_guard<std::mutex> lock(poolMutex);
        baseAddress = Functions::numToByte128(networkInt);
        lastAddress = Functions::numToByte128(lastUsableInt);
    }

    if (!gateway.empty() && gateway != newGatway)
    {
        releaseIP(gateway);
        excludeIP(newGatway);
        {
            std::lock_guard<std::mutex> lock(poolMutex);
            gateway = newGatway;
        }
    }

    currentAddress = baseAddress;

    // Collect keys for allocated IPs that vall outside the new scope
    std::vector<ByteString> ipsToRemove;
    {
        std::lock_guard<std::mutex> lock(poolMutex);
        for (const auto& [ip, mac] : allocatedIPs)
        {
            __uint128_t ipInt = Functions::byteToNum128(ip);
            if (ipInt < networkInt + 1 || ipInt > lastUsableInt)
            {
                ipsToRemove.push_back(ip);
            }
        }
    }

    // Now, remove these ips
    for (const auto& ip : ipsToRemove)
    {
        if (leaseManager)
        {
            leaseManager->releaseIP(ip);
        }
        else
        {
            releaseIP(ip);
        }
    }
}

bool IPPool::isTemporarilyOffered(const ByteString& ip)
{
    std::lock_guard<std::mutex> lock(poolMutex);
    for (const auto& [offered, id] : temporaryOffers)
        if (offered == ip) return true;
    return false;
}

ByteString IPPool::getTempIP(const ByteString& duid)
{
    for (const auto& [ip, id] : temporaryOffers)
    {
        if (duid == id)
        {
            return ip;
        }
    }
    return {};
}

void IPPool::clearTempOffer(const ByteString& ip)
{
    std::lock_guard<std::mutex> lock(poolMutex);
    temporaryOffers.erase(ip);
}

ByteString IPPool::activateTempIP(const ByteString& ip, const ByteString* duid)
{
    if (ip.empty() || isAllocatedOrExcluded(ip)) return {};
        allocatedIPs[ip] = duid ? *duid : "";
    return ip;
}
