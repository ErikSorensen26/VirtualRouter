// EigrpConfigManager

#include <EigrpConfig.h>
#include <Eigrp.h>
#include <AddressFamily.hpp>
#include <VirtualRouter.h>
#include <EigrpInterface.h>
#include <Functions.h>

namespace Eigrp
{
void EigrpConfig::addNetworkRange(const EigrpConfigs::Network& newNetwork)
{
    if (base.getAF() != AddressFamily::IPv4) return;

    // Check for duplicate
    {
        std::unique_lock<std::shared_mutex> configsLock(configs.configsMutex);
        for (auto network : configs.networks)
        {
            if (network.ip == newNetwork.ip && network.mask == newNetwork.mask)
            {
                return; // Network already exists
            }
        }
        configs.networks.push_back(std::move(newNetwork));
    }

    // Update interfaces and routing table after adding the network
    base.getIfaceMgr().refreshInterfaceList();
}

void EigrpConfig::delNetworkRange(const EigrpConfigs::Network& newNetwork)
{
    if (base.getAF() != AddressFamily::IPv4) return;

    // Check for duplicate
    {
        std::unique_lock<std::shared_mutex> configsLock(configs.configsMutex);
        std::vector<EigrpConfigs::Network>::iterator it = std::find(configs.networks.begin(), configs.networks.end(), newNetwork);
        if (it != configs.networks.end())
        {
            configs.networks.erase(it);
        }
        else
        {
            return; // Network does not exist
        }
    }

    // Update interfaces and routing table after adding the network
    base.getIfaceMgr().refreshInterfaceList();
}

bool EigrpConfig::isInNetworkRange(const uint8_t* testIp)
{
    {
        std::shared_lock<std::shared_mutex> configMutex(configs.configsMutex);
        for (const auto& network : configs.networks)
        {
            if (Functions::compareNetworkWithIp(network.ip.raw, testIp, network.mask, base.getAF()))
            {
                return true;
            }
        }
    }

    return false; // No matches found
}

void EigrpConfig::clearNetworks()
{
    {
        std::unique_lock<std::shared_mutex> configsLock(configs.configsMutex);
        configs.networks.clear();
    }

    // Update interfaces and routing table after clearing networks
    base.getIfaceMgr().refreshInterfaceList();
}

void EigrpConfig::enableStub(bool isStub, bool advertiseConnected, bool advertiseLeakMap, bool advertiseStatic, bool advertiseSummary, bool advertiseRedistributed)
{
    {
        std::unique_lock<std::shared_mutex> configsLock(configs.configsMutex);
        configs.stubConfig.isStub = isStub;
        configs.stubConfig.advertiseConnected = advertiseConnected;
        configs.stubConfig.advertiseLeakMap = advertiseLeakMap;
        configs.stubConfig.advertiseStatic = advertiseStatic;
        configs.stubConfig.advertiseSummary = advertiseSummary;
        configs.stubConfig.advertiseRedistributed = advertiseRedistributed;
    }
}

void EigrpConfig::setPassiveInterface(uint32_t key, bool add)
{
    {
        std::shared_lock<std::shared_mutex> lock(configs.configsMutex);
        if (add)
        {
            configs.passiveInterfaces.insert(key);
        }
        else
        {
            configs.passiveInterfaces.erase(key);
        }
    }

    // Make the interface passive if it already exists
    auto& ifmgr = base.getIfaceMgr();
    std::shared_lock<std::shared_mutex> lock(ifmgr.interfaceMutex);
    auto intIt = ifmgr.eigrpInterfaceList.find(key);
    if (intIt != ifmgr.eigrpInterfaceList.end())
    {
        intIt->second.setPassiveMode(add);
    }
}

void EigrpConfig::enableUnicastPeer(const IPAddress& neighborIp, uint32_t key)
{
    // Add unicast neighbor to the unicast neighbor list
    {
        std::shared_lock<std::shared_mutex> lock(configs.configsMutex);
        configs.unicastNeighbors[key].emplace(neighborIp);
    }

    // Find the interface to add the neighbor
    {
        auto& iface = base.getIfaceMgr();
        std::shared_lock<std::shared_mutex> intLock(iface.interfaceMutex);
        auto intIt = iface.eigrpInterfaceList.find(key);
        if (intIt != iface.eigrpInterfaceList.end())
        {
            intIt->second.getNTable().createNeighbor(neighborIp);
        }
    }
}

void EigrpConfig::disableUnicastPeer(const IPAddress& neighborIp, uint32_t key)
{
    // Remove unicast neighbor from the unicast neighbor list
    {
        std::shared_lock<std::shared_mutex> lock(configs.configsMutex);
        configs.unicastNeighbors[key].erase(neighborIp);
    }

    // Find the interface to remove the neighbor from
    {
        auto& iface = base.getIfaceMgr();
        std::shared_lock<std::shared_mutex> intLock(iface.interfaceMutex);
        auto intIt = iface.eigrpInterfaceList.find(key);
        if (intIt != iface.eigrpInterfaceList.end())
        {
            intIt->second.getNTable().deleteNeighbor(neighborIp, true);
        }
    }
}
}
