#include "DhcpServerBase.h"
#include <Global.h>

//bool Protocol::Dhcp::DhcpNetworkConfig::updateNetwork(ByteString* newNetwork, uint8_t* newPrefix, ByteString* newGateway, DhcpServerBase* server)
bool Protocol::Dhcp::DhcpNetworkConfig::updateNetwork(IPPrefix& prefix, IPAddress& gateway, DhcpServerBase* server)
{
    IPPrefix networkID = getNetworkID(prefix.af);
    {
        std::unique_lock<std::shared_mutex> lock(configMutex);
        if (prefix.af == networkID.af)
        {
            std::memcpy(network.raw, prefix.addr, static_cast<uint8_t>(prefix.af));
        }
        if (gateway.isV6 == defaultGateway.isV6)
        {
            gateway = defaultGateway;
        }
        if (prefix.prefixLength <= static_cast<uint8_t>(prefix.af) * 8)
        {
            subnetPrefix.store(prefix.prefixLength, std::memory_order_release);
        }
    }
    
    if (server)
    {
        IPPrefix newNetworkID = getNetworkID(prefix.af);
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

Protocol::DhcpServerBase::DhcpServerBase(Global& global) : global(global) {}

bool Protocol::DhcpServerBase::moveConfig(const IPPrefix& oldKey, const IPPrefix& newKey)
{
    if (dhcpNetworks.find(oldKey) != dhcpNetworks.end() && dhcpNetworks.find(newKey) == dhcpNetworks.end())
    {
        dhcpNetworks[newKey] = dhcpNetworks[oldKey];
        dhcpNetworks.erase(oldKey);
        return true;
    }
    return false;
}

void Protocol::DhcpServerBase::addNetwork(Dhcp::DhcpNetworkConfig* config, AddressFamily af)
{
    IPPrefix networkID = {config->getNetwork().raw, config->getPrefixLen(), af};
    {
        std::lock_guard<std::mutex> lock(configMutex);
        // Create a new pool and lease for the dhcp network.
        Dhcp::DhcpNetwork* dhcpNetwork = new Dhcp::DhcpNetwork();
        dhcpNetwork->pool = new IPPool(config->getNetwork(), config->getPrefixLen(), config->getGateway());
        dhcpNetwork->lease = new LeaseManager(dhcpNetwork->pool);
        dhcpNetwork->config = config;

        // Check if Network is IPv6
        if (config->getNetwork().isV6)
        {
            dhcpNetwork->prefixPool = new PrefixPool(config->getNetwork(), config->getPrefixLen());
            dhcpNetwork->prefixLease = new PrefixLeaseManager(dhcpNetwork->prefixPool);
            dhcpNetwork->config->defaultSubnetPrefix = std::min(64, dhcpNetwork->config->getPrefixLen() + 8);
        }

        dhcpNetwork->lease->addGlobalManager(networkID, &globalLeaseManager);
        dhcpNetworks[networkID] = dhcpNetwork;
    }
}

void Protocol::DhcpServerBase::removeNetwork(const IPPrefix& networkID)
{
    // Remove network config.
    auto it = dhcpNetworks.find(networkID);
    if (it != dhcpNetworks.end())
    {
        delete it->second;
        dhcpNetworks.erase(it);
    }
}

void Protocol::DhcpServerBase::scheduleTimeout(Dhcp::TimerType type, const Dhcp::TrackedTimer::ClientID& clientID, const IPAddress& offer, const IPPrefix& networkID, uint32_t timeout)
{
    Dhcp::TrackedTimer state;
    state.clientID = clientID;
    state.resource = offer;
    state.networkID = networkID;

    state.timerID = global.timeManager.addTimer(
        std::chrono::steady_clock::now() + std::chrono::seconds(timeout),
        [this, clientID, offer, networkID, type]() {
            std::lock_guard<std::mutex> lock(timerMutex);
            if (dhcpNetworks.find(networkID) == dhcpNetworks.end()) return;

            for (auto it = activeTimers[type].begin(); it != activeTimers[type].end(); ++it)
            {
                if (it->clientID == clientID && it->resource == offer)
                {
                    auto netIt = dhcpNetworks.find(it->networkID);
                    if (netIt == dhcpNetworks.end()) continue;
                    bool finished = false;

                    switch (type)
                    {
                    case Dhcp::TimerType::IP_OFFER_TIMEOUT:
                        if (netIt->second->pool->getTempIP(clientID) == offer)
                        {
                            netIt->second->pool->clearTempOffer(clientID);
                            finished = true;
                        }
                        break;
                    case Dhcp::TimerType::PREFIX_OFFER_TIMEOUT:
                        if (netIt->second->prefixPool->getTempPrefix(clientID).first == offer)
                        {
                            netIt->second->prefixPool->clearTempPrefix(clientID);
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

void Protocol::DhcpServerBase::cancelTimeout(Dhcp::TimerType type, const Dhcp::TrackedTimer::ClientID& clientID, const IPAddress& offer)
{
    std::lock_guard<std::mutex> lock(timerMutex);
    for (auto it = activeTimers[type].begin(); it != activeTimers[type].end(); ++it)
    {
        if (it->clientID == clientID && it->resource == offer)
        {
            global.timeManager.cancelTimer(it->timerID);
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
            global.timeManager.cancelTimer(timer.timerID);
        }
    }
}
