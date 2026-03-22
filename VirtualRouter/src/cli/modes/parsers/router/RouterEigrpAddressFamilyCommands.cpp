// RouterEigrpAddressFamilyCommands.cpp

#include "RouterEigrpAddressFamilyCommands.h"

#include "eigrp/core/Eigrp.h"
#include "eigrp/core/EigrpConfig.h"
#include "configs/registry/router/EigrpRegistry.h"
#include "cli/runtime/CliSession.h"
#include "cli/runtime/CliUtils.h"
#include "interface/configs/InterfaceType.hpp"
#include "interface/configs/InterfaceConfigs.h"

namespace Cli
{
bool RouterEigrpAddressFamily_AfInterface_Handler(EIGRP_PARAMS)
{
    ctx.terminal.isList = true;

    InterfaceType type = getInterfaceType(args[0]);
    float interfaceId = std::stof(args[1]);
    uint32_t key = calculateInterfaceKey(type, interfaceId);

    auto ref = ctx.currentEigrp->getIfaceMgr().getRegistryByKey(key);
    ctx.currentEigrpInterface = &ref.get();

    if (!ctx.negate)
    {
        if (ctx.currentEigrp->getAF() == AddressFamily::IPv4)
            ctx.terminal.changeMode<CliMode::RouterEigrpInterfaceV4>(ctx.currentEigrp, ctx.currentEigrpNamed, ctx.currentEigrpInterface);
        else
            ctx.terminal.changeMode<CliMode::RouterEigrpInterfaceV6>(ctx.currentEigrp, ctx.currentEigrpNamed, ctx.currentEigrpInterface);
    }
    else
    {
        // Mark the interface as shutdown so refreshInterfaceList removes it
        ctx.currentEigrpInterface->get<Config::EigrpInterface::SHUTDOWN>().set(true);
        ctx.currentEigrp->refreshInterfaceList();
    }
    return true;
}

bool RouterEigrpAddressFamily_EigrpDefaultRouteTag_Handler(EIGRP_PARAMS)
{
    // TODO: ROUTE_TAG not yet in registry
    UNUSED(args);
    return false;
}

bool RouterEigrpAddressFamily_EigrpEventLogSize_Handler(EIGRP_PARAMS)
{
    ctx.currentEigrp->getGlobalConfigMgr().getConfigs().get<Config::Eigrp::MAX_EVENT_LOG_SIZE>().set(
        ctx.negate ? 500u : static_cast<uint32_t>(std::stoul(args[0])));
    return true;
}

bool RouterEigrpAddressFamily_EigrpLogNeighborChanges_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.currentEigrp->getGlobalConfigMgr().getConfigs().get<Config::Eigrp::LOG_NEIGHBOR_CHANGES>().set(!ctx.negate);
    return true;
}

bool RouterEigrpAddressFamily_EigrpLogNeighborWarnings_Handler(EIGRP_PARAMS)
{
    auto& cfg = ctx.currentEigrp->getGlobalConfigMgr().getConfigs();
    if (!ctx.negate)
    {
        cfg.get<Config::Eigrp::LOG_NEIGHBOR_WARNINGS>().set(true);
        if (!args.empty())
            cfg.get<Config::Eigrp::LOG_NEIGHBOR_WARNINGS_INTERVAL>().set(static_cast<uint16_t>(std::stoul(args[0])));
    }
    else
    {
        cfg.get<Config::Eigrp::LOG_NEIGHBOR_WARNINGS>().set(false);
        cfg.get<Config::Eigrp::LOG_NEIGHBOR_WARNINGS_INTERVAL>().set(10);
    }
    return true;
}

bool RouterEigrpAddressFamily_EigrpRouterId_Handler(EIGRP_PARAMS)
{
    auto& ridField = ctx.currentEigrp->getGlobalConfigMgr().getConfigs().get<Config::Eigrp::ROUTER_ID>();
    if (!ctx.negate)
    {
        IPv4Address _tmp; CliUtils::extractIPv4Address(args[0], _tmp);
        ridField.set(_tmp.addr);
    }
    else
    {
        ridField.unset();
    }
    return true;
}

bool RouterEigrpAddressFamily_EigrpStub_Handler(EIGRP_PARAMS)
{
    if (!ctx.negate)
    {
        bool connected = false, redistributed = false, stat = false, summary = false;
        std::string leakMap;
        for (size_t i = 0; i < args.size(); i++)
        {
            if (args[i] == "connected")          connected = true;
            else if (args[i] == "redistributed") redistributed = true;
            else if (args[i] == "static")        stat = true;
            else if (args[i] == "summary")       summary = true;
            else if (args[i] == "leak-map" && i + 1 < args.size())
                leakMap = args[++i];
        }
        auto& cfgMgr = ctx.currentEigrp->getGlobalConfigMgr();
        cfgMgr.enableStub(true, connected, stat, summary, redistributed);
        if (!leakMap.empty())
            cfgMgr.getConfigs().get<Config::Eigrp::STUB_LEAK_MAP>().set(leakMap);
        else
            cfgMgr.getConfigs().get<Config::Eigrp::STUB_LEAK_MAP>().unset();
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
    auto& cfg = ctx.currentEigrp->getGlobalConfigMgr().getConfigs();
    if (!ctx.negate)
    {
        cfg.get<Config::Eigrp::MAXIMUM_PREFIX>().set(static_cast<uint32_t>(std::stoul(args[0])));
        for (size_t i = 1; i < args.size(); i++)
        {
            if (CliUtils::isNumber(args[i]))
                cfg.get<Config::Eigrp::DAMPENING_INTERVAL>().set(static_cast<uint8_t>(std::stoul(args[i])));
            else if (args[i] == "dampened")
                cfg.get<Config::Eigrp::DAMPENING>().set(true);
            else if (args[i] == "reset-time" && i + 1 < args.size())
                cfg.get<Config::Eigrp::DAMPENING_RESET_TIME>().set(static_cast<uint16_t>(std::stoul(args[++i])));
            else if (args[i] == "restart" && i + 1 < args.size())
                cfg.get<Config::Eigrp::DAMPENING_RESTART>().set(static_cast<uint16_t>(std::stoul(args[++i])));
            else if (args[i] == "restart-count" && i + 1 < args.size())
                cfg.get<Config::Eigrp::DAMPENING_RESTART_COUNT>().set(static_cast<uint16_t>(std::stoul(args[++i])));
            else if (args[i] == "warning-only")
                cfg.get<Config::Eigrp::DAMPENING_WARNINGS>().set(true);
        }
    }
    else
    {
        cfg.get<Config::Eigrp::MAXIMUM_PREFIX>().set(0u);
        cfg.get<Config::Eigrp::DAMPENING_INTERVAL>().set(75);
        cfg.get<Config::Eigrp::DAMPENING>().set(false);
        cfg.get<Config::Eigrp::DAMPENING_RESET_TIME>().set(0);
        cfg.get<Config::Eigrp::DAMPENING_RESTART>().set(0);
        cfg.get<Config::Eigrp::DAMPENING_RESTART_COUNT>().set(1);
        cfg.get<Config::Eigrp::DAMPENING_WARNINGS>().set(false);
    }
    return true;
}

bool RouterEigrpAddressFamily_MetricRibScale_Handler(EIGRP_PARAMS)
{
    ctx.currentEigrp->getGlobalConfigMgr().getConfigs().get<Config::Eigrp::RIB_SCALE>().set(
        ctx.negate ? 128 : static_cast<uint8_t>(std::stoul(args[0])));
    return true;
}

bool RouterEigrpAddressFamily_MetricWeights_Handler(EIGRP_PARAMS)
{
    auto& cfg = ctx.currentEigrp->getGlobalConfigMgr().getConfigs();
    if (!ctx.negate)
    {
        cfg.get<Config::Eigrp::WEIGHT_TOS>().set(static_cast<uint8_t>(std::stoul(args[0])));
        cfg.get<Config::Eigrp::WEIGTH_K1>().set(static_cast<uint8_t>(std::stoul(args[1])));
        cfg.get<Config::Eigrp::WEIGHT_K3>().set(static_cast<uint8_t>(std::stoul(args[2])));
        cfg.get<Config::Eigrp::WEIGHT_k4>().set(static_cast<uint8_t>(std::stoul(args[3])));
        cfg.get<Config::Eigrp::WEIGHT_K2>().set(static_cast<uint8_t>(std::stoul(args[4])));
        cfg.get<Config::Eigrp::WEIGHT_k5>().set(static_cast<uint8_t>(std::stoul(args[5])));
    }
    else
    {
        cfg.get<Config::Eigrp::WEIGHT_TOS>().set(0);
        cfg.get<Config::Eigrp::WEIGTH_K1>().set(1);
        cfg.get<Config::Eigrp::WEIGHT_K3>().set(1);
        cfg.get<Config::Eigrp::WEIGHT_k4>().set(0);
        cfg.get<Config::Eigrp::WEIGHT_K2>().set(0);
        cfg.get<Config::Eigrp::WEIGHT_k5>().set(0);
    }
    return true;
}

bool RouterEigrpAddressFamily_Neighbor_Handler(EIGRP_PARAMS)
{
    ctx.terminal.isList = true;
    IPAddress neighborIp; CliUtils::extractIPAddress(args[0], neighborIp);
    InterfaceType type = getInterfaceType(args[1]);
    if (type == InterfaceType::UNDEFINED)
    {
        ctx.terminal.iConsole->print("\r\n%EIGRP: Unknown interface type");
        return false;
    }
    uint32_t key = calculateInterfaceKey(type, std::stof(args[2]));
    if (!ctx.negate)
        ctx.currentEigrp->getGlobalConfigMgr().enableUnicastPeer(neighborIp, key);
    else
        ctx.currentEigrp->getGlobalConfigMgr().disableUnicastPeer(neighborIp, key);
    return true;
}

bool RouterEigrpAddressFamily_Network_Handler(EIGRP_PARAMS)
{
    ctx.terminal.isList = true;
    IPv4Address _ip; CliUtils::extractIPv4Address(args[0], _ip);
    uint8_t _plen;
    if (args.size() == 2)
    {
        IPv4Address _tmp; CliUtils::extractIPv4Address(args[1], _tmp);
        CliUtils::extractSubnetMask(_tmp.addr, _plen);
    }
    else
    {
        _plen = _ip.getDefaultMask();
    }
    IPv4Prefix network(_ip.addr, _plen);

    if (!ctx.negate)
        ctx.currentEigrp->getGlobalConfigMgr().addNetworkRange(network);
    else
        ctx.currentEigrp->getGlobalConfigMgr().delNetworkRange(network);
    return true;
}

bool RouterEigrpAddressFamily_SoftSia_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.currentEigrp->getGlobalConfigMgr().getConfigs().get<Config::Eigrp::NON_STOP_FORWARDING>().set(!ctx.negate);
    return true;
}

bool RouterEigrpAddressFamily_TimersGracefulRestart_Handler(EIGRP_PARAMS)
{
    ctx.currentEigrp->getGlobalConfigMgr().getConfigs().get<Config::Eigrp::GRACEFUL_PURGE_TIME>().set(
        ctx.negate ? 240 : static_cast<uint16_t>(std::stoul(args[0])));
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
