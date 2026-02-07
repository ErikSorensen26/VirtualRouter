// RouterEigrpClassicVrfCommands.h

#include "RouterEigrpClassicVrfCommands.h"

#include <Eigrp.h>
#include <EigrpTypes.hpp>
#include <CliSession.h>
#include <Global.h>
#include <VirtualRouter.h>
#include <InterfaceType.hpp>
#include <EigrpInterface.h>

namespace Cli
{
bool RouterEigrpClassicVrf_EigrpLogNeighborChanges_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.currentEigrp->getConfigs().logNeighborChanges.store(!ctx.negate, std::memory_order_release);
    return true;
}

bool RouterEigrpClassicVrf_EigrpLogNeighborWarnings_Handler(EIGRP_PARAMS)
{
    if (!ctx.negate)
    {
        ctx.currentEigrp->getConfigs().logNeighborWarnings.store(true, std::memory_order_release); 
        if (args.size() > 0)
            ctx.currentEigrp->getConfigs().warningInterval.store(static_cast<uint16_t>(std::stoi(args[0])), std::memory_order_release);
    }
    else
    {
        ctx.currentEigrp->getConfigs().logNeighborWarnings.store(false, std::memory_order_release); 
        ctx.currentEigrp->getConfigs().warningInterval.store(10, std::memory_order_release);
    }
    return true;
}

bool RouterEigrpClassicVrf_EigrpRouterId_Handler(EIGRP_PARAMS)
{
    if (!ctx.negate)
        ctx.currentEigrp->setRouterID(Functions::getAddress(args[0]).raw);
    else
        ctx.currentEigrp->clearRouterID();
    return true;
}

bool RouterEigrpClassicVrf_EigrpStub_Handler(EIGRP_PARAMS)
{
    auto& configs = ctx.currentEigrp->getConfigs();
    std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
    if (!ctx.negate)
    {
        bool connected = false;
        bool leakMap = false;
        bool redistributed = false;
        bool stat = false;
        bool summary = false;
        for (size_t i = 0; i <= args.size(); i++)
        {
            if (args[i] == "connected")
                { connected = true; }
            else if (args[i] == "leak-map")
                { leakMap = true; }
            else if (args[i] == "redistrubuted")
                { redistributed = true; }
            else if (args[i] == "static")
                { stat = true; }
            else if (args[i] == "summary") 
                { summary = true; }
        }
        ctx.currentEigrp->getGlobalConfigMgr().enableStub(
            true,
            connected,
            leakMap,
            stat,
            summary,
            redistributed
        );
    }
    else
    {
        ctx.currentEigrp->getGlobalConfigMgr().enableStub(false);
    }
    return true;
}

bool RouterEigrpClassicVrf_Exit_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.terminal.exitMode<CliMode::RouterEigrpClassicV4>(ctx.tempEigrp, nullptr, nullptr);
    return true;
}

bool RouterEigrpClassicVrf_MetricWeights_Handler(EIGRP_PARAMS)
{
    auto& configs = ctx.currentEigrp->getConfigs();
    EigrpConfigs::KValue kvalue;
    if (!ctx.negate)
    {
        configs.TOS.store(static_cast<uint8_t>(std::stoi(args[0])), std::memory_order_release);
        kvalue.k1_Bandwidth = static_cast<uint8_t>(std::stoi(args[1]));
        kvalue.k3_Delay = static_cast<uint8_t>(std::stoi(args[2]));
        kvalue.k4_Reliability = static_cast<uint8_t>(std::stoi(args[3]));
        kvalue.k2_Load = static_cast<uint8_t>(std::stoi(args[4]));
        kvalue.k5_MTU = static_cast<uint8_t>(std::stoi(args[5]));
    }
    else
    {
        configs.TOS.store(0, std::memory_order_release);
        kvalue.k1_Bandwidth = 1;
        kvalue.k3_Delay = 0;
        kvalue.k4_Reliability = 1;
        kvalue.k2_Load = 0;
        kvalue.k5_MTU = 0;
    }
    std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
    configs.kvalue = kvalue;
    return true;
}

bool RouterEigrpClassicVrf_Neighbor_Handler(EIGRP_PARAMS)
{
    ctx.terminal.isList = true;
    IPAddress neighborIp = Functions::getAddress(args[0]);
    InterfaceType type = getInterfaceType(args[1]);
    if (type != InterfaceType::UNDEFINED)
    {
        uint32_t key = calculateInterfaceKey(type, std::stof(args[2]));
        Eigrp::EigrpInterface* iface = ctx.currentEigrp->getIfaceMgr().getInterface(key);
        if (!iface) return false;

        if (!ctx.negate)
            iface->getNTable().createNeighbor(neighborIp, Eigrp::Neighbor::Version::UNKNOWN, true);
        else
            iface->getNTable().deleteNeighbor(neighborIp, true);
    }
    return true;
}

bool RouterEigrpClassicVrf_Network_Handler(EIGRP_PARAMS)
{
    ctx.terminal.isList = true;
    EigrpConfigs::Network network(AddressFamily::IPv4);
    network.ip = Functions::getAddress(args[0]);
    if (args.size() == 2)
        network.mask = 32 - Functions::prefixToPrefixLength(Functions::addressToIntv4(args[1]));
    else
        network.mask = 32 - Functions::getDefaultMask(readU32(network.ip.raw));

    if (!ctx.negate)
    {
        ctx.currentEigrp->getGlobalConfigMgr().addNetworkRange(network);
    }
    else
    {
        {
            auto& configs = ctx.currentEigrp->getConfigs();
            std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
            auto& networks = configs.networks;
            networks.erase(std::remove(networks.begin(), networks.end(), network), networks.end());
        }

        ctx.currentEigrp->refreshInterfaceList();
    }
    return true;
}

bool RouterEigrpClassicVrf_PassiveInterface_Handler(EIGRP_PARAMS)
{
    ctx.terminal.isList = true;
    uint32_t key = calculateInterfaceKey(getInterfaceType(args[0]), std::stof(args[0]));
    auto* iface = ctx.currentEigrp->getIfaceMgr().getInterface(key);

    if (iface)
    {
        iface->setPassiveMode(!ctx.negate);
    }
    else
    {
        ctx.currentEigrp->getConfigs().passiveInterfaces.insert(key);
    }
    return true;
}

bool RouterEigrpClassicVrf_TimersGracefulRestart_Handler(EIGRP_PARAMS)
{
    auto& configs = ctx.currentEigrp->getConfigs();
    ctx.negate
        ? configs.purgeTime.store(240, std::memory_order_release)
        : configs.purgeTime.store(static_cast<uint16_t>(std::stoi(args[0])));
    return true;
}
}
