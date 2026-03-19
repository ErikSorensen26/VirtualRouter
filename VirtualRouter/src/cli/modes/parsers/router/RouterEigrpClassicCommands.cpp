// RouterEigrpCommands.cpp

#include <Global.h>
#include <VirtualRouter.h>

#include "RouterEigrpClassicCommands.h"
#include "eigrp/core/Eigrp.h"
#include "eigrp/interface/EigrpInterface.h"
#include "cli/runtime/CliSession.h"
#include "interface/configs/InterfaceType.hpp"

namespace Cli
{
bool RouterEigrpClassic_AddressFamilyVrf_Handler(EIGRP_PARAMS)
{
    ctx.terminal.isList = true;
    auto* vrf = ctx.currentEigrp->routingInstance->getGlobal().getRoutingInstance(args[0]);
    if (!vrf)
    {
        ctx.terminal.iConsole->print(std::string("\r\n%") + "VRF" + args[0] + " does not exist or is not enalbed for IPv4");
        return false;
    }
    if (!vrf->enabledAddressFamilies.contains(AddressFamily::IPv4))
    {
        ctx.terminal.iConsole->print(std::string("\r\n%") + "VRF" + args[0] + " does exist but is not enabled for IPv4");
        return false;
    }

    uint16_t asNum = args.size() == 3 ? static_cast<uint16_t>(std::stoi(args[2])) : ctx.currentEigrp->getAS();

    Eigrp::EigrpAutonomousSystem* as = vrf->getEigrpAutonomousSystem(asNum);
    if (!ctx.negate)
    {
        if (as)
        {
            if (as->ipv4Named)
            {
                ctx.terminal.iConsole->print(std::string("\r\n%") + " ERROR: AS(" + std::to_string(asNum) + ") used by name mode");
                return false; // AS used in named mode.
            }
        }
        else
        {
            as = vrf->addEigrpAutonomousSystem(asNum);
        }

        if (!as->ipv4)
        {
            as->ipv4 = new Eigrp::Eigrp(asNum, AddressFamily::IPv4, vrf);
        }

        ctx.terminal.changeMode<CliMode::RouterEigrpClassicVRF>(as->ipv4, nullptr, nullptr, ctx.currentEigrp);
    }
    else
    {
        if (as)
        {
            if (!as->ipv4Named && as->ipv4)
            {
                delete as->ipv4;
                as->ipv4 = nullptr;
                if (!as->ipv6 && !as->ipv6Named)
                {
                    vrf->removeEigrpAutonomousSystem(asNum);
                }
            }
        }
    }
    return true;
}

bool RouterEigrpClassic_EigrpLogNeighborChanges_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.currentEigrp->getConfigs().logNeighborChanges.store(!ctx.negate, std::memory_order_release);
    return true;
}

bool RouterEigrpClassic_EigrpLogNeighborWarnings_Handler(EIGRP_PARAMS)
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

bool RouterEigrpClassic_EigrpRouterId_Handler(EIGRP_PARAMS)
{
    if (!ctx.negate)
        ctx.currentEigrp->routerID(Functions::getIPv4Address(args[0]).addr);
    else
        ctx.currentEigrp->clearRouterID();
    return true;
}

bool RouterEigrpClassic_EigrpStub_Handler(EIGRP_PARAMS)
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

bool RouterEigrpClassic_Exit_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.terminal.exitMode<CliMode::GlobalConfiguration>(ctx.currentEigrp->routingInstance->getGlobal(), *ctx.currentEigrp->routingInstance);
    return true;
}

bool RouterEigrpClassic_MetricWeights_Handler(EIGRP_PARAMS)
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

bool RouterEigrpClassic_Neighbor_Handler(EIGRP_PARAMS)
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

bool RouterEigrpClassic_Network_Handler(EIGRP_PARAMS)
{
    ctx.terminal.isList = true;
    EigrpConfigs::Network network(AddressFamily::IPv4);
    network.ip = Functions::getIPv4Address(args[0]);
    if (args.size() == 2)
        network.mask = 32 - Functions::prefixToPrefixLength(Functions::getIPv4Address(args[1]).addr);
    else
        network.mask = 32 - Functions::getDefaultMask(network.ip.addr);

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

bool RouterEigrpClassic_PassiveInterface_Handler(EIGRP_PARAMS)
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

bool RouterEigrpClassic_TimersGracefulRestart_Handler(EIGRP_PARAMS)
{
    auto& configs = ctx.currentEigrp->getConfigs();
    ctx.negate
        ? configs.purgeTime.store(240, std::memory_order_release)
        : configs.purgeTime.store(static_cast<uint16_t>(std::stoi(args[0])));
    return true;
}
}
