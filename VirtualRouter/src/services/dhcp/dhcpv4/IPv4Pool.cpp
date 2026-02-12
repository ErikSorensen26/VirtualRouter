// IPv4Pool.cpp

#include "IPv4Pool.h"
#include "IPv4LeaseManager.h"

//TODO add mac tracking

IPv4Pool::IPv4Pool(TimeManager& timeManager)
    : timeManager(timeManager)
{}

IPv4Pool::~IPv4Pool()
{
    // Cancel tiemrs
    for (const auto& [_, id] : quarantined)
        timeManager.cancelTimer(id.second);
    for (const auto& [_, id] : bad)
        timeManager.cancelTimer(id);
    for (const auto& [_, id] : temporary)
        timeManager.cancelTimer(id.second);
}

uint32_t IPv4Pool::allocateIP(const ClientID& clientId)
{
    std::lock_guard<std::mutex> lock(poolMutex);

    auto alIt = reverseAllocated.find(clientId);
    if (alIt != reverseAllocated.end())
    {
        return alIt->second;
    }

    if (auto rit = reverseQuarantined.find(clientId); rit != reverseQuarantined.end())
    {
        if (auto it = quarantined.find(rit->second); it != quarantined.end())
        {
            timeManager.cancelTimer(it->second.second);
            uint32_t ip = it->first;
            quarantined.erase(it);
            reverseQuarantined.erase(rit);
            allocated[ip] = clientId;
            reverseAllocated[clientId] = ip;
            return ip;
        }
        else reverseQuarantined.erase(rit);
    }

    if (!release.empty())
    {
        uint32_t newIP = release.front();
        release.pop_front();
        allocated[newIP] = clientId;
        reverseAllocated[clientId] = newIP;
        return newIP;
    }

    for (uint32_t i = 0; i < poolSize - 2; ++i)
    {
        uint32_t offset = (currentAddress - baseAddress - 1 + i) % (poolSize - 2);
        uint32_t ip = baseAddress + 1 + offset;

        if (ip == gateway || isReserved(ip)) continue;

        allocated[ip] = clientId;
        reverseAllocated[clientId] = ip;
        currentAddress = ip + 1;
        return ip;
    }

    return 0;
}

uint32_t IPv4Pool::allocateTemporaryIP(const ClientID& clientId, uint32_t timeout)
{
    std::lock_guard<std::mutex> lock(poolMutex);

    if (auto rit = reverseTemporary.find(clientId); rit != reverseTemporary.end())
    {
        if (auto it = temporary.find(rit->second); it != temporary.end())
        {
            timeManager.cancelTimer(it->second.second);
            it->second.second = timeManager.addTimer(
                std::chrono::steady_clock::now() + std::chrono::seconds(timeout),
                [this, ip = it->first, clientId](uint32_t) {
                    std::lock_guard<std::mutex> lock(poolMutex);
                    temporary.erase(ip);
                    reverseTemporary.erase(clientId);
                });
            return it->first;
        }
        else reverseTemporary.erase(rit);
    }

    if (auto rit = reverseQuarantined.find(clientId); rit != reverseQuarantined.end())
    {
        if (auto it = quarantined.find(rit->second); it != quarantined.end())
        {
            uint32_t ip = it->first;
            timeManager.cancelTimer(it->second.second);
            quarantined.erase(it);
            reverseQuarantined.erase(rit);
            temporary[ip] = {
                clientId,
                timeManager.addTimer(std::chrono::steady_clock::now() + std::chrono::seconds(timeout),
                [this, ip, clientId](uint32_t) {
                    std::lock_guard<std::mutex> lock(poolMutex);
                    temporary.erase(ip);
                    reverseTemporary.erase(clientId);
                })
            };
            reverseTemporary[clientId] = ip;
            return ip;
        }
        else reverseQuarantined.erase(rit);
    }

    if (!release.empty())
    {
        uint32_t ip = release.front();
        release.pop_front();
        temporary[ip] = {
            clientId,
            timeManager.addTimer(
            std::chrono::steady_clock::now() + std::chrono::seconds(timeout),
            [this, ip, clientId](uint32_t) {
                std::lock_guard<std::mutex> lock(poolMutex);
                temporary.erase(ip);
                reverseTemporary.erase(clientId);
            })
        };
        reverseTemporary[clientId] = ip;
        return ip;
    }

    for (uint32_t i = 0; i < poolSize; ++i)
    {
        uint32_t offset = (currentAddress - baseAddress - 1 + i) % (poolSize - 2);
        uint32_t ip = baseAddress + 1 + offset;

        if (ip == gateway || isReserved(ip) || isTemporarilyOffered(ip)) continue;

        temporary[ip] = {
            clientId,
            timeManager.addTimer(std::chrono::steady_clock::now() + std::chrono::seconds(timeout),
            [this, ip, clientId](uint32_t) {
                std::lock_guard<std::mutex> lock(poolMutex);
                temporary.erase(ip);
                reverseTemporary.erase(clientId);
            })
        };
        reverseTemporary[clientId] = ip;
        currentAddress = ip + 1;
        return ip;
    }

    return 0;
}

bool IPv4Pool::allocateRequestedTemporaryIP(uint32_t ip, const ClientID& clientId, uint32_t timeout)
{
    std::lock_guard<std::mutex> lock(poolMutex);

    if (auto it = quarantined.find(ip); it != quarantined.end())
    {
        if (it->second.first != clientId) return false;
        timeManager.cancelTimer(it->second.second);
        quarantined.erase(ip);
        reverseQuarantined.erase(clientId);
    }

    if (!withinRange(ip) || isReserved(ip) || isTemporarilyOffered(ip)) return false;

    auto relIt = std::find(release.begin(), release.end(), ip);
    if (relIt != release.end())
        release.erase(relIt);
    temporary[ip] = {
        clientId,
        timeManager.addTimer(std::chrono::steady_clock::now() + std::chrono::seconds(timeout),
        [this, ip, clientId](uint32_t) {
            std::lock_guard<std::mutex> lock(poolMutex);
            temporary.erase(ip);
            reverseTemporary.erase(clientId);
        })
    };
    reverseTemporary[clientId] = ip;
    return true;
}

bool IPv4Pool::allocateRequestedIP(uint32_t ip, const ClientID& clientId)
{
    std::lock_guard<std::mutex> lock(poolMutex);

    auto it = quarantined.find(ip);
    if (it != quarantined.end())
    {
        if (it->second.first != clientId) return false;
        timeManager.cancelTimer(it->second.second);
        quarantined.erase(ip);
        reverseQuarantined.erase(clientId);
    }

    if (!withinRange(ip) || isReserved(ip) || isTemporarilyOffered(ip)) return false;

    auto relIt = std::find(release.begin(), release.end(), ip);
    if (relIt != release.end())
        release.erase(relIt);
    allocated[ip] = clientId;
    reverseAllocated[clientId] = ip;
    return true;
}

uint32_t IPv4Pool::getTemporaryIPForClient(const ClientID& clientId)
{
    std::lock_guard<std::mutex> lock(poolMutex);
    if (auto it = reverseTemporary.find(clientId); it != reverseTemporary.end())
        return it->second;
    return 0;
}

bool IPv4Pool::activateTemporaryIP(uint32_t ip, const ClientID& clientId)
{
    std::lock_guard<std::mutex> lock(poolMutex);
    auto it = temporary.find(ip);
    if (it == temporary.end() || clientId != it->second.first) return false;

    timeManager.cancelTimer(it->second.second);
    temporary.erase(it);
    reverseTemporary.erase(clientId);
    allocated[ip] = clientId;
    reverseAllocated[clientId] = ip;
    return true;
}

void IPv4Pool::clearTemporaryOffer(uint32_t ip)
{
    std::lock_guard<std::mutex> lock(poolMutex);
    auto it = temporary.find(ip);
    if (it != temporary.end())
    {
        timeManager.cancelTimer(it->second.second);
        reverseTemporary.erase(it->second.first);
        temporary.erase(ip);
    }
}

bool IPv4Pool::excludeIP(uint32_t ip)
{
    std::lock_guard<std::mutex> lock(poolMutex);
    if (!withinRange(ip) || isAllocated(ip)) return false;
    excluded.insert(ip);
    return true;
}

bool IPv4Pool::removeExclusion(uint32_t ip)
{
    std::lock_guard<std::mutex> lock(poolMutex);
    return excluded.erase(ip);
}

void IPv4Pool::expireIP(uint32_t ip, const ClientID& client, size_t timeout)
{
    std::lock_guard<std::mutex> lock(poolMutex);
    if (!isAllocated(ip)) return;
    allocated.erase(ip);
    reverseAllocated.erase(client);
    quarantined[ip] = {
        client,
        timeManager.addTimer(
        std::chrono::steady_clock::now() + std::chrono::seconds(timeout),
        [this, ip, client](uint32_t) {
            std::lock_guard<std::mutex> lock(poolMutex);
            if (!isExcluded(ip) && !isConflicted(ip))
                release.push_back(ip); // For tracking if needed
            quarantined.erase(ip);
            reverseQuarantined.erase(client);
        })
    };
    reverseQuarantined[client] = ip;
}

void IPv4Pool::releaseIP(uint32_t ip, const ClientID& client)
{
    std::lock_guard<std::mutex> lock(poolMutex);
    if (!isAllocated(ip)) return;
    allocated.erase(ip);
    reverseAllocated.erase(client);
}

bool IPv4Pool::setConflicted(uint32_t ip, uint32_t duration)
{
    std::lock_guard<std::mutex> lock(poolMutex);
    if (!isAllocated(ip)) return false;
    auto client = allocated[ip];
    allocated.erase(ip);
    reverseAllocated.erase(client);
    bad[ip] = timeManager.addTimer(
        std::chrono::steady_clock::now() + std::chrono::minutes(duration),
        [this, ip](uint32_t) {
            std::lock_guard<std::mutex> lock(poolMutex);
            bad.erase(ip);
        });
    return true;
}

bool IPv4Pool::isAllocated(uint32_t ip) const
{
    std::lock_guard<std::mutex> lock(poolMutex);
    return allocated.find(ip) != allocated.end();
}

bool IPv4Pool::isExcluded(uint32_t ip) const
{
    std::lock_guard<std::mutex> lock(poolMutex);
    return excluded.find(ip) != excluded.end();
}

bool IPv4Pool::isConflicted(uint32_t ip) const
{
    std::lock_guard<std::mutex> lock(poolMutex);
    return bad.find(ip) != bad.end();
}

bool IPv4Pool::isTemporarilyOffered(uint32_t ip) const
{
    std::lock_guard<std::mutex> lock(poolMutex);
    return temporary.find(ip) != temporary.end();
}

bool IPv4Pool::isTemporarilyOffered(uint32_t ip, ClientID id) const
{
    std::lock_guard<std::mutex> lock(poolMutex);
    const auto it = temporary.find(ip);
    return it != temporary.end() && it->second.first == id;
}

bool IPv4Pool::matchIPToClient(uint32_t ip, ClientID& clientId) const
{
    std::lock_guard<std::mutex> lock(poolMutex);
    auto it = allocated.find(ip);
    return it != allocated.end() && it->second == clientId;
}

bool IPv4Pool::adjustPool(uint32_t* net, uint8_t* len, uint32_t* gw)
{
    std::lock_guard<std::mutex> lock(poolMutex);

    if (len && *len <= 32)
    {
        uint32_t newSize = 1U << (32 - *len);
        poolSize = newSize;
    }

    if (net) baseAddress = *net;
    if (gw) gateway = *gw;

    lastAddress = baseAddress + poolSize - 1;
    broadcast.store(lastAddress + 1);

    if (leaseManager)
    {
        allocated.clear();
        temporary.clear();
        release.clear();
        bad.clear();
    }

    bool ready = poolSize > 0 && leaseManager;
    init.store(ready, std::memory_order_release);
    return true;
}

void IPv4Pool::setLeaseManager(IPv4LeaseManager* leaseMgr)
{
    leaseManager = leaseMgr;
}

bool IPv4Pool::withinRange(uint32_t ip) const
{
    return ip > baseAddress && ip < lastAddress;
}

bool IPv4Pool::isReserved(uint32_t ip) const
{
    return isAllocated(ip) || isExcluded(ip) || isConflicted(ip);
}

bool IPv4Pool::isAllocatedOrExcluded(uint32_t ip) const
{
    return allocated.count(ip) || excluded.count(ip) || bad.find(ip) != bad.end() || quarantined.find(ip) != quarantined.end();
}
