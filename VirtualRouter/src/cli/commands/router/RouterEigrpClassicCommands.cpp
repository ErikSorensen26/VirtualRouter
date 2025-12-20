// RouterEigrpCommands.cpp

#include "RouterEigrpClassicCommands.h"

#include <Eigrp.h>
#include <EigrpTypes.hpp>
#include <CliSession.h>
#include <Global.h>
#include <VirtualRouter.h>
#include <InterfaceType.hpp>
#include <EigrpInterface.h>
#include <string>

namespace Cli
{
bool RouterEigrpClassic_AddressFamilyVrf_Handler(EIGRP_PARAMS)
{
    ctx.terminal.isList = true;
    auto* vrf = ctx.currentEigrp->routingInstance->global.getRoutingInstance(args[0]);
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

        ctx.currentEigrp = as->ipv4;
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

bool RouterEigrpClassic_AutoSummary_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
	ctx.currentEigrp->getAggregator().enableAutoSummary(!ctx.negate);
    return true;
}

bool RouterEigrpClassic_DefaultMetric_Handler(EIGRP_PARAMS)
{
    auto& configs = ctx.currentEigrp->getConfigs();
    if (!ctx.negate)
    {
        std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
        auto& defaultMetric = configs.defaultMetrics;
        defaultMetric.k1_Bandwidth = static_cast<uint8_t>(std::stoi(args[0]));
        defaultMetric.k3_Delay = static_cast<uint8_t>(std::stoi(args[1]));
        defaultMetric.k4_Reliability = static_cast<uint8_t>(std::stoi(args[2]));
        defaultMetric.k2_Load = static_cast<uint8_t>(std::stoi(args[3]));
        defaultMetric.k5_MTU = static_cast<uint8_t>(std::stoi(args[4]));
    }
    else
    {
        std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
        auto& defaultMetric = configs.defaultMetrics;
        defaultMetric.k1_Bandwidth = 1;
        defaultMetric.k2_Load = 0;
        defaultMetric.k3_Delay = 1;
        defaultMetric.k4_Reliability = 0;
        defaultMetric.k5_MTU = 0;
        defaultMetric.k6_Power = 0;
    }
    return true;
}

bool RouterEigrpClassic_Distance_Handler(EIGRP_PARAMS)
{
    if (Functions::isNumber(args[0]))
    {
        // XXX
    }
    else if (args[0] == "eigrp")
    {
        auto& configs = ctx.currentEigrp->getConfigs();
        if (!ctx.negate)
        {
            configs.adminDistance.store(static_cast<uint8_t>(std::stoi(args[1])), std::memory_order_release);
            configs.externalAdminDistance.store(static_cast<uint8_t>(std::stoi(args[2])), std::memory_order_release);
        }
        else
        {
            configs.adminDistance.store(90, std::memory_order_release);
            configs.externalAdminDistance.store(170, std::memory_order_release);
        }
    }
    return true;
}

bool RouterEigrpClassic_EigrpEventLogSize_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.currentEigrp->getConfigs().eventLogSize.store(ctx.negate ? 500 : static_cast<uint32_t>(std::stoi(args[0])), std::memory_order_release); 
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
        ctx.currentEigrp->setRouterID(Functions::getAddress(args[0]).raw);
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
    ctx.terminal.exitMode(CliMode::GlobalConfiguration);
    return true;
}

bool RouterEigrpClassic_MaximumPaths_Handler(EIGRP_PARAMS)
{
    auto& configs = ctx.currentEigrp->getConfigs();
    if (!ctx.negate)
    {
        ctx.currentEigrp->getConfigs().maxPaths.store(static_cast<uint8_t>(std::stoi(args[0])));
    }
    else
    {
        configs.maxPaths.store(static_cast<uint8_t>(4));
    }
    return true;
}

bool RouterEigrpClassic_MetricMaximumHops_Handler(EIGRP_PARAMS)
{
    ctx.currentEigrp->getConfigs().maxHops.store(static_cast<uint8_t>(
        ctx.negate ? 100 : std::stoi(args[0])), 
        std::memory_order_release
    );
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

bool RouterEigrpClassic_TimersActive_Handler(EIGRP_PARAMS)
{
    auto& configs = ctx.currentEigrp->getConfigs();
    if (!ctx.negate)
    {
        if (args[0] == "disabled")
        {
            configs.activeDisabled.store(true, std::memory_order_release);
        }
        else
        {
            configs.stuckInActiveTime.store(static_cast<uint16_t>(std::stoi(args[0]) / 2), std::memory_order_release);
            configs.activeDisabled.store(false, std::memory_order_release);
        }
    }
    else
    {
        configs.stuckInActiveTime.store(90, std::memory_order_relaxed);
        configs.activeDisabled.store(false, std::memory_order_release);
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

bool RouterEigrpClassic_TrafficShare_Handler(EIGRP_PARAMS)
{
    auto& configs = ctx.currentEigrp->getConfigs();
    if (!ctx.negate)
    {
        if (args[0] == "balanced")
        {
            configs.trafficShareMode.store(EigrpConfigs::TrafficShareMode::Balanced, std::memory_order_release);
        }
        else if (args[0] == "min")
        {
            if (args.size() == 2 && args[1] == "across-interfaces")
                configs.trafficShareMode.store(EigrpConfigs::TrafficShareMode::MinimumAcrossInterfaces, std::memory_order_release);
            else
                configs.trafficShareMode.store(EigrpConfigs::TrafficShareMode::Minimum, std::memory_order_release);
        }
    }
    else
    {
        configs.trafficShareMode.store(EigrpConfigs::TrafficShareMode::Balanced, std::memory_order_release);
    }
    return true;
}

bool RouterEigrpClassic_Variance_Handler(EIGRP_PARAMS)
{
    ctx.negate
        ? ctx.currentEigrp->getGlobalConfigMgr().setVariance(1)
        : ctx.currentEigrp->getGlobalConfigMgr().setVariance(static_cast<uint8_t>(std::stoul(args[0])));
    return true;
}
}
