// Finish auth and hash stuff
// add 0003 and 0002 tlv for classic
#include <EigrpCore.h>
#include <AddressFamily.hpp>
#include <TopologyTable.h>

namespace Protocol
{
Eigrp::Eigrp(uint32_t as, AddressFamily af, VirtualRouter* vrf, bool named)
    : routingInstance(vrf), namedMode(named), asNumber(as), addressFamily(af), configMgr(*this)
{
    start();
}

Eigrp::~Eigrp()
{
    shutdown();
}

void Eigrp::start()
{
    calculateRouterID();

    interfaceMgr.updateInterfaceList();
    topology.initialize();

    Logger::getInstance().info() << "EIGRP process initiated." << std::endl;
}

void Eigrp::shutdown()
{
    interfaceMgr.shutdown();
    topology.shutdown();

    Logger::getInstance().info() << "EIGRP shutdown complete." << std::endl;
}

void Eigrp::restart()
{
    // Shutdown current state
    shutdown();

    // Reinitialize the EIGRP process
    initializeEigrp();

    Logger::getInstance().info() << "EIGRP process restarted successfully." << std::endl;
}

void Eigrp::cleanup()
{
    {
        std::unique_lock<std::shared_mutex> lock(interfaceMutex);
        for (const auto& [_, eigrpInterfacePtr] : eigrpInterfaceList)
        {
            eigrpInterfacePtr->stopHello();
            delete eigrpInterfacePtr;
        };
        eigrpInterfaceList.clear();
    }

    {
        std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
        configs.networks.clear();
    }
    Logger::getInstance().info() << "EIGRP Cleanup complete" << std::endl;
}

void Eigrp::runMaintenance()
{
    topologyTable->pruneStaleRoutes();
}

void Eigrp::calculateRID()
{
    uint32_t highestIP = 0;
    uint32_t tempIp;
    if (!routerID.isStatic)
    {
        auto processID = [&](Interface* interface)
        {
            if (interface->shutdownFlag.load(std::memory_order_relaxed)) return;
            auto& interfaceInfo = interface->configs;
            tempIp = interfaceInfo.ipv4.getAddress();
            if (tempIp == 0) return;
            if (tempIp < highestIP) return;
            highestIP = tempIp;
        };
        
        {
            std::shared_lock<std::shared_mutex> lock(routingInstance->interfaceMutex);
            for (const auto& [id, interface] : routingInstance->interfaceList)
            {
                if (interface->configs.interfaceType != InterfaceType::LOOPBACK) continue;
                processID(interface);
            }
            if (highestIP == 0)
            {
                for (const auto& [id, interface] : routingInstance->interfaceList)
                {
                    processID(interface);
                }
            }
        }
        writeU32(routerID.ID, highestIP);
    }
}

bool Eigrp::isInNetworkRange(const uint8_t* testIp)
{
    {
        std::shared_lock<std::shared_mutex> configMutex(configs.configsMutex);
        for (const auto& network : configs.networks)
        {
            if (Functions::compareNetworkWithIp(network.ip.raw, testIp, network.mask, addressFamily))
            {
                return true;
            }
        }
    }

    return false; // No matches found
}

}

