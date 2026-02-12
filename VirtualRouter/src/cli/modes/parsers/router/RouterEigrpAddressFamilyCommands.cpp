// RouterEigrpAddressFamilyCommands.cpp

#include <Functions.h>

#include "RouterEigrpAddressFamilyCommands.h"

#include "eigrp/core/Eigrp.h"
#include "eigrp/interface/EigrpInterface.h"
#include "cli/runtime/CliSession.h"
#include "interface/Interface.h"
#include "interface/configs/InterfaceType.hpp"

namespace Cli
{
bool RouterEigrpAddressFamily_AfInterface_Handler(EIGRP_PARAMS)
{
    ctx.terminal.isList = true;

    InterfaceType type = getInterfaceType(args[0]);
    float interfaceId = std::stof(args[1]);
    uint32_t key = calculateInterfaceKey(type, interfaceId);
    
    auto& ifaceMgr = ctx.currentEigrp->getIfaceMgr();
    std::unique_lock<std::shared_mutex> lock(ifaceMgr.interfaceMutex);
    auto intIt = ifaceMgr.eigrpInterfaceConfigList.find(key);

    if (!ctx.negate)
    {
        if (intIt != ifaceMgr.eigrpInterfaceConfigList.end())
        {
            ctx.currentEigrpInterface = &intIt->second;
            ctx.currentEigrpInterface->userMade = true;
        }
        else
        {
            auto it = ifaceMgr.eigrpInterfaceConfigList.emplace(key, key);
            it.first->second.userMade = true;
        }
        if (ctx.currentEigrp->getAF() == AddressFamily::IPv4)
            ctx.terminal.changeMode<CliMode::RouterEigrpInterfaceV4>(ctx.currentEigrp, ctx.currentEigrpNamed, ctx.currentEigrpInterface);
        else
            ctx.terminal.changeMode<CliMode::RouterEigrpInterfaceV6>(ctx.currentEigrp, ctx.currentEigrpNamed, ctx.currentEigrpInterface);
    }
    else
    {
        if (intIt != ifaceMgr.eigrpInterfaceConfigList.end())
        {
            ctx.currentEigrpInterface = &intIt->second;
            if (ctx.currentEigrpInterface->userMade)
            {
                ifaceMgr.eigrpInterfaceConfigList.erase(key);
            }
            else return true;
        }
        else
        {
            return true;
        }
    }
    ctx.currentEigrp->refreshInterfaceList();
    return true;
}

bool RouterEigrpAddressFamily_EigrpDefaultRouteTag_Handler(EIGRP_PARAMS)
{
    uint32_t routeTag;
    if (!ctx.negate)
    {
        if (Functions::isNumber(args[0]))
        {
            routeTag = static_cast<uint32_t>(std::stoi(args[0]));
        }
        else
        {
            routeTag = Functions::addressToIntv4(args[0]);
        }
    }
    else
    {

    }
    // TODO set the route tag
    return false;
}

bool RouterEigrpAddressFamily_EigrpEventLogSize_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.currentEigrp->getConfigs().eventLogSize.store(ctx.negate ? 500 : static_cast<uint32_t>(std::stoi(args[0])), std::memory_order_release); 
    return true;
}

bool RouterEigrpAddressFamily_EigrpLogNeighborChanges_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.currentEigrp->getConfigs().logNeighborChanges.store(!ctx.negate, std::memory_order_release);
    return true;
}

bool RouterEigrpAddressFamily_EigrpLogNeighborWarnings_Handler(EIGRP_PARAMS)
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

bool RouterEigrpAddressFamily_EigrpRouterId_Handler(EIGRP_PARAMS)
{
    if (!ctx.negate)
        ctx.currentEigrp->routerID(Functions::getAddress(args[0]).raw);
    else
        ctx.currentEigrp->clearRouterID();
    return true;
}

bool RouterEigrpAddressFamily_EigrpStub_Handler(EIGRP_PARAMS)
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

bool RouterEigrpAddressFamily_Exit_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.terminal.exitMode<CliMode::RouterEigrpNamed>(nullptr, ctx.currentEigrpNamed, nullptr);
    return true;
}

bool RouterEigrpAddressFamily_MaximumPrefix_Handler(EIGRP_PARAMS)
{
    auto& configs = ctx.currentEigrp->getConfigs();
    if (!ctx.negate)
    {
        configs.maximumPrefix.store(static_cast<uint32_t>(std::stoi(args[0])), std::memory_order_release);
        if (args.size() > 1)
        {
            for (size_t i = 1; i < args.size(); i++)
            {
                if (Functions::isNumber(args[i]))
                {
                    configs.dampeningInterval.store(static_cast<uint8_t>(std::stoul(args[i])));
                }
                else if (args[i] == "dampened")
                {
                    configs.dampening.store(true, std::memory_order_release);
                }
                else if (args[i] == "reset-time")
                {
                    configs.dampeningResetTime.store(static_cast<uint8_t>(std::stoul(args[i + 1]), std::memory_order_release));
                    i++;
                }
                else if (args[i] == "restart")
                {
                    configs.dampeningRestart.store(static_cast<uint8_t>(std::stoul(args[i + 1]), std::memory_order_release));
                    i++;
                }
                else if (args[i] == "restart-count")
                {
                    configs.dampeningRestartCount.store(static_cast<uint8_t>(std::stoul(args[i + 1]), std::memory_order_release));
                    i++;
                }
                else if (args[i] == "warning-only")
                {
                    configs.dampeningWarnings.store(true, std::memory_order_release);
                }
            }
        }
    }
    else
    {
        configs.maximumPrefix.store(0, std::memory_order_relaxed);
        configs.dampeningInterval.store(75, std::memory_order_release);
        configs.dampening.store(false, std::memory_order_release);
        configs.dampeningResetTime.store(0, std::memory_order_release);
        configs.dampeningRestart.store(0, std::memory_order_release);
        configs.dampeningRestartCount.store(1, std::memory_order_release);
        configs.dampeningWarnings.store(false, std::memory_order_release);
    }
    return true;
}

bool RouterEigrpAddressFamily_MetricRibScale_Handler(EIGRP_PARAMS)
{
    if (!ctx.negate)
    {
        if (Functions::isNumber(args[0]))
            ctx.currentEigrp->getConfigs().ribScale.store(static_cast<uint8_t>(std::stoi(args[0])), std::memory_order_release);
    }
    else
    {
        ctx.currentEigrp->getConfigs().ribScale.store(128, std::memory_order_release);
    }
    return true;
}

bool RouterEigrpAddressFamily_MetricWeights_Handler(EIGRP_PARAMS)
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

bool RouterEigrpAddressFamily_Neighbor_Handler(EIGRP_PARAMS)
{
    ctx.terminal.isList = true;
    IPAddress neighborIp = Functions::getAddress(args[0]);
    InterfaceType type = getInterfaceType(args[1]);
    float interfaceId = std::stof(args[2]);
    uint32_t key = calculateInterfaceKey(type, interfaceId);
    auto* iface = ctx.currentEigrp->getIfaceMgr().getInterface(key);

    if (iface)
    {
        if (!ctx.negate)
            iface->getNTable().createNeighbor(neighborIp, Eigrp::Neighbor::Version::UNKNOWN, true);
        else
            iface->getNTable().deleteNeighbor(neighborIp, true);
    }
    else
    {
        auto& configs = ctx.currentEigrp->getConfigs();
        std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
        if (ctx.negate)
            configs.unicastNeighbors[key].erase(neighborIp);
        else
            configs.unicastNeighbors[key].insert(neighborIp);
    }
    return true;
}

bool RouterEigrpAddressFamily_Network_Handler(EIGRP_PARAMS)
{
    ctx.terminal.isList = true;
    EigrpConfigs::Network network(AddressFamily::IPv4);
    network.ip = Functions::getAddress(args[0]);
    if (args.size() == 2)
    {
        network.mask = Functions::prefixToPrefixLength(Functions::addressToIntv4(args[1]));
    }
    else
    {
        network.mask = Functions::getDefaultMask(readU32(network.ip.raw));
    }

    if (!ctx.negate)
    {
        ctx.currentEigrp->getGlobalConfigMgr().addNetworkRange(network);
    }
    else
    {
        auto& configs = ctx.currentEigrp->getConfigs();
        if (args.size() == 1)
        {
            std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
            auto& networks = configs.networks;
            std::erase_if(networks, [&](const EigrpConfigs::Network& net) -> bool { return net.ip == network.ip; });
        }
        else
        {
            std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
            auto& networks = configs.networks;
            std::erase_if(networks, [&](EigrpConfigs::Network net) {
                return net.ip == network.ip && (args.size() > 1 ? net.mask == network.mask : true);
            });
        }

        ctx.currentEigrp->refreshInterfaceList();
    }
    return true;
}

bool RouterEigrpAddressFamily_SoftSia_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.currentEigrp->getConfigs().nonStopForwarding.store(!ctx.negate, std::memory_order_release);
    return true;
}

bool RouterEigrpAddressFamily_TimersGracefulRestart_Handler(EIGRP_PARAMS)
{
    ctx.negate
      ? ctx.currentEigrp->getConfigs().purgeTime.store(240, std::memory_order_release)
      : ctx.currentEigrp->getConfigs().purgeTime.store(static_cast<uint16_t>(std::stoi(args[0])), std::memory_order_release);
    return true;
}

bool RouterEigrpAddressFamily_Topology_Handler(EIGRP_PARAMS)
{
    if (!ctx.negate)
    {
        if (args[0] == "base")
        {
            if (ctx.currentEigrp->getAF() == AddressFamily::IPv4)
                ctx.terminal.changeMode<CliMode::RouterEigrpTopologyV4>(ctx.currentEigrp, ctx.currentEigrpNamed, nullptr);
            else
                ctx.terminal.changeMode<CliMode::RouterEigrpTopologyV6>(ctx.currentEigrp, ctx.currentEigrpNamed, nullptr);
        }
        else
        {
            //TODO ADD named topologies based on route distinguishers - maybe
        }
    }
    else
    {
        if (args[0] != "base")
        {
            //TODO ADD named topologies based on route distinguishers - maybe
        }
    }
    return true;
}
}
