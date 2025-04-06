#include "DhcpServerBase.h"
#include <TimeManager.h>

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

void Protocol::DhcpServerBase::addNetwork(DhcpNetworkConfig* config)
{
    ByteString networkID = config->network + "/" + std::to_string(config->subnetPrefix);
    {
        std::lock_guard<std::mutex> lock(configMutex);
        // Create a new pool and lease for the dhcp network.
        DhcpNetwork* dhcpNetwork = new DhcpNetwork();
        dhcpNetwork->pool = new IPPool(config->network, config->subnetPrefix, config->defaultGateway);
        dhcpNetwork->lease = new LeaseManager(dhcpNetwork->pool);
        dhcpNetwork->config = config;

        // Check if Network is IPv6
        if (config->network.size() == 16)
        {
            dhcpNetwork->prefixPool = new PrefixPool(config->network, config->subnetPrefix);
            dhcpNetwork->prefixLease = new PrefixLeaseManager(dhcpNetwork->prefixPool);
            dhcpNetwork->config->defaultSubnetPrefix = std::min(64, dhcpNetwork->config->subnetPrefix + 8);
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

bool Protocol::DhcpServerBase::updateNetworkConfig(const ByteString& networkID, const DhcpNetworkConfig& newConfig, const std::vector<ByteString>& dnsToRemove, const std::vector<ByteString>& winsToRemove, const std::vector<ByteString>& helperAddressesToRemove)
{
    std::lock_guard<std::mutex> lock(configMutex);
    
    auto configIt = dhcpNetworks.find(networkID);
    if (configIt == dhcpNetworks.end()) return false;

    // Update the current network configuration
    DhcpNetworkConfig& currentConfig = *configIt->second->config;

    // Update individual fields only if they are provided in the new configuration
    if (!newConfig.network.empty()) { currentConfig.network = newConfig.network; }
    if (newConfig.subnetPrefix != 0) { currentConfig.subnetPrefix = newConfig.subnetPrefix; }
    if (!newConfig.defaultGateway.empty()) { currentConfig.defaultGateway = newConfig.defaultGateway; }
    if (!newConfig.renewalTime.empty()) { currentConfig.renewalTime = newConfig.renewalTime; }
    if (!newConfig.rebindingTime.empty()) { currentConfig.rebindingTime = newConfig.rebindingTime; }
    if (!newConfig.domainName.empty()) { currentConfig.domainName = newConfig.domainName; }
    if (!newConfig.netbiosName.empty()) { currentConfig.netbiosName = newConfig.netbiosName; }
    if (!newConfig.ntpServer.empty()) { currentConfig.ntpServer = newConfig.ntpServer; }
    if (!newConfig.tftpServer.empty()) { currentConfig.tftpServer = newConfig.tftpServer; }
    if (!newConfig.broadcastAddress.empty()) { currentConfig.broadcastAddress = newConfig.broadcastAddress; }
    if (!newConfig.arpTimeout.empty()) { currentConfig.arpTimeout = newConfig.arpTimeout; }

    // Update non-default values
    if (newConfig.leaseTime > 0.0) { currentConfig.leaseTime = newConfig.leaseTime; }
    if (newConfig.interface) { currentConfig.interface = newConfig.interface; }
    if (newConfig.mtu > 0) { currentConfig.mtu = newConfig.mtu; }
    if (newConfig.allowDynamicUpdates) {currentConfig.allowDynamicUpdates = newConfig.allowDynamicUpdates;}

    // Append to vector fields (avoid duplication)
    currentConfig.dnsServer.insert(currentConfig.dnsServer.end(), newConfig.dnsServer.begin(), newConfig.dnsServer.end());
    currentConfig.winsServer.insert(currentConfig.winsServer.end(), newConfig.winsServer.begin(), newConfig.winsServer.end());
    currentConfig.helperAddresses.insert(currentConfig.helperAddresses.end(), newConfig.helperAddresses.begin(), newConfig.helperAddresses.end());
    currentConfig.staticRoutes.insert(currentConfig.staticRoutes.end(), newConfig.staticRoutes.begin(), newConfig.staticRoutes.end());

    // Remove entries from vectors based on optional parameters
    if (!dnsToRemove.empty())
    {
        for (const auto& dns : dnsToRemove)
        {
            currentConfig.dnsServer.erase(
                std::remove(currentConfig.dnsServer.begin(), currentConfig.dnsServer.end(), dns),
                currentConfig.dnsServer.end()
        );
    }
    }

    if (!winsToRemove.empty())
    {
        for (const auto& wins : winsToRemove)
        {
            currentConfig.winsServer.erase(
                std::remove(currentConfig.winsServer.begin(), currentConfig.winsServer.end(), wins),
                currentConfig.winsServer.end()
            );
        }
    }

    if (!helperAddressesToRemove.empty())
    {
        for (const auto& helper : helperAddressesToRemove)
        {
            currentConfig.helperAddresses.erase(
                std::remove(currentConfig.helperAddresses.begin(), currentConfig.helperAddresses.end(), helper),
                currentConfig.helperAddresses.end()
            );
        }
    }

    ByteString newKey = currentConfig.network + "/" + std::to_string(currentConfig.subnetPrefix);

    // Replace or merge metadata fields
    if (!newConfig.description.empty()) { currentConfig.description = newConfig.description; }
    currentConfig.isPrivate = newConfig.isPrivate;
    currentConfig.isEnabled = newConfig.isEnabled;

    // Update the IPPool/LeaseManager for this network if needed
    if (newKey != networkID)
    {
        configIt->second->pool->adjustPool(currentConfig.network, currentConfig.subnetPrefix, currentConfig.defaultGateway);
        // Move old Dhcp Network
        moveConfig(networkID, newKey);
    }
    return true;
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
