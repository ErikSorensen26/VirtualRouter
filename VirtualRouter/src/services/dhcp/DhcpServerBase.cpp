#include "DhcpServerBase.h"
#include <TimeManager.h>

bool Protocol::Dhcp::DhcpNetworkConfig::updateNetwork(ByteString* newNetwork, uint8_t* newPrefix, ByteString* newGateway, DhcpServerBase* server)
{
    ByteString networkID = getNetworkID();
    {
        std::unique_lock<std::shared_mutex> lock(configMutex);
        if (newNetwork && (network.size() == newNetwork->size() || network.empty()))
        {
            network = *newNetwork;
        }
        if (newGateway && network.size() == newGateway->size())
        {
            defaultGateway = *newGateway;
        }
        if (newPrefix && *newPrefix <= network.size() * 8)
        {
            subnetPrefix.store(*newPrefix, std::memory_order_release);
        }
    }
    
    if (server)
    {
        ByteString newNetworkID = getNetworkID();
        auto networkIt = server->dhcpNetworks.find(networkID);
        if (newNetworkID != networkID && networkIt != server->dhcpNetworks.end())
        {
            networkIt->second->pool->adjustPool(network, subnetPrefix.load(std::memory_order_relaxed), defaultGateway);
            // Move old Dhcp Network
            server->moveConfig(networkID, newNetworkID);
            return true;
        }
    }
    return false;
}

bool Protocol::DhcpServerBase::moveConfig(const ByteString& oldKey, const ByteString& newKey)
{
    if (dhcpNetworks.find(oldKey) != dhcpNetworks.end() && dhcpNetworks.find(newKey) == dhcpNetworks.end())
    {
        dhcpNetworks[newKey] = dhcpNetworks[oldKey];
        dhcpNetworks.erase(oldKey);
        return true;
    }
    return false;
}

void Protocol::DhcpServerBase::addNetwork(Dhcp::DhcpNetworkConfig* config)
{
    ByteString networkID = config->getNetwork() + "/" + std::to_string(config->getPrefixLen());
    {
        std::lock_guard<std::mutex> lock(configMutex);
        // Create a new pool and lease for the dhcp network.
        Dhcp::DhcpNetwork* dhcpNetwork = new Dhcp::DhcpNetwork();
        dhcpNetwork->pool = new IPPool(config->getNetwork(), config->getPrefixLen(), config->getGateway());
        dhcpNetwork->lease = new LeaseManager(dhcpNetwork->pool);
        dhcpNetwork->config = config;

        // Check if Network is IPv6
        if (config->getNetwork().size() == 16)
        {
            dhcpNetwork->prefixPool = new PrefixPool(config->getNetwork(), config->getPrefixLen());
            dhcpNetwork->prefixLease = new PrefixLeaseManager(dhcpNetwork->prefixPool);
            dhcpNetwork->config->defaultSubnetPrefix = std::min(64, dhcpNetwork->config->getPrefixLen() + 8);
        }

        dhcpNetwork->lease->addGlobalManager(networkID, &globalLeaseManager);
        dhcpNetworks[networkID] = dhcpNetwork;
    }
}

void Protocol::DhcpServerBase::removeNetwork(const ByteString& networkID)
{
    // Remove network config.
    auto it = dhcpNetworks.find(networkID);
    if (it != dhcpNetworks.end())
    {
        delete it->second;
        dhcpNetworks.erase(it);
    }
}

void Protocol::DhcpServerBase::scheduleTimeout(Dhcp::TimerType type, const ByteString& id, const ByteString& offer, const ByteString& networkID, uint32_t timeout)
{
    Dhcp::TrackedTimer state;
    state.clientID = id;
    state.resource = offer;
    state.networkID = networkID;

    state.timerID = TimeManager::getInstance().addTimer(
        std::chrono::steady_clock::now() + std::chrono::seconds(timeout),
        [this, id, offer, networkID, type]() {
            std::lock_guard<std::mutex> lock(timerMutex);
            if (dhcpNetworks.find(networkID) == dhcpNetworks.end()) return;

            for (auto it = activeTimers[type].begin(); it != activeTimers[type].end(); ++it)
            {
                if (it->clientID == id && it->resource == offer)
                {
                    auto netIt = dhcpNetworks.find(it->networkID);
                    if (netIt == dhcpNetworks.end()) continue;
                    bool finished = false;

                    switch (type)
                    {
                    case Dhcp::TimerType::IP_OFFER_TIMEOUT:
                        if (netIt->second->pool->getTempIP(id) == offer)
                        {
                            netIt->second->pool->clearTempOffer(id);
                            finished = true;
                        }
                        break;
                    case Dhcp::TimerType::PREFIX_OFFER_TIMEOUT:
                        if (netIt->second->prefixPool->getTempPrefix(id).first == offer)
                        {
                            netIt->second->prefixPool->clearTempPrefix(id);
                            finished = true;
                        }
                        break;
                    case Dhcp::TimerType::CLIENT_REQUEST_TIMEOUT:
                        if (netIt->second->pool->isTemporarilyOffered(offer) || (netIt->second->prefixPool && netIt->second->prefixPool->isTemporarilyOffered(offer)))
                        {
                            outgoingRequests.erase(offer);
                        }
                        break;
                    case Dhcp::TimerType::DECLINE_HOLD:
                        {
                            if (netIt->second->lease->isAllocated(offer))
                            {
                                netIt->second->lease->releaseIP(offer);
                            }
                            else if (netIt->second->prefixLease && netIt->second->prefixLease->isAllocated(offer))
                            {
                                netIt->second->prefixLease->releasePrefix(offer);
                            }
                        }
                        break;
                    case Dhcp::TimerType::RELEASE_HOLD:
                        {
                            if (netIt->second->lease->isAllocated(offer))
                            {
                                netIt->second->lease->releaseIP(offer);
                            }
                            else if (netIt->second->prefixLease && netIt->second->prefixLease->isAllocated(offer))
                            {
                                netIt->second->prefixLease->releasePrefix(offer);
                            }
                        }
                        break;
                    }

                    if (finished) break;
                    {
                        activeTimers[type].erase(it);
                    }
                }
            }
        }
    );

    std::lock_guard<std::mutex> lock(timerMutex);
    activeTimers[type].push_back(state);
}

void Protocol::DhcpServerBase::cancelTimeout(Dhcp::TimerType type, const ByteString& id, const ByteString& offer)
{
    std::lock_guard<std::mutex> lock(timerMutex);
    for (auto it = activeTimers[type].begin(); it != activeTimers[type].end(); ++it)
    {
        if (it->clientID == id && it->resource == offer)
        {
            TimeManager::getInstance().cancelTimer(it->timerID);
            activeTimers[type].erase(it);
            break;
        }
    }
}

void Protocol::DhcpServerBase::clearOfferTimeouts()
{
    for (const auto& type : activeTimers)
{
        for (const auto& timer : type.second)
        {
            TimeManager::getInstance().cancelTimer(timer.timerID);
        }
    }
}
