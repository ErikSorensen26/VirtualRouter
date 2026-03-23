// RouterEigrpAddressFamilyCommands.cpp

#include "RouterEigrpAddressFamilyCommands.h"

#include "eigrp/core/Eigrp.h"
#include "eigrp/core/EigrpConfig.h"
#include "configs/registry/router/EigrpRegistry.h"
#include "cli/runtime/CliSession.h"
#include "cli/runtime/CliUtils.h"
#include "interface/configs/InterfaceType.hpp"
#include "interface/configs/InterfaceConfigs.h"

namespace cli
{
bool RouterEigrpAddressFamily_AfInterface_Handler(EIGRP_PARAMS)
{
    ctx.terminal.isList = true;

    interface::InterfaceType type = interface::getInterfaceType(args[0]);
    float interfaceId = std::stof(args[1]);
    uint32_t key = interface::calculateInterfaceKey(type, interfaceId);

    auto ref = ctx.currentEigrp->getIfaceMgr().getRegistryByKey(key);
    ctx.currentEigrpInterface = &ref.get();

    if (!ctx.negate)
    {
        if (ctx.currentEigrp->getAF() == types::AddressFamily::IPv4)
            ctx.terminal.changeMode<CliMode::RouterEigrpInterfaceV4>(ctx.currentEigrp, ctx.currentEigrpNamed, ctx.currentEigrpInterface);
        else
            ctx.terminal.changeMode<CliMode::RouterEigrpInterfaceV6>(ctx.currentEigrp, ctx.currentEigrpNamed, ctx.currentEigrpInterface);
    }
    else
    {
        // Mark the interface as shutdown so refreshInterfaceList removes it
        ctx.currentEigrpInterface->get<config::EigrpInterface::SHUTDOWN>().set(true);
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
    ctx.currentEigrp->getGlobalConfigMgr().getConfigs().get<config::Eigrp::MAX_EVENT_LOG_SIZE>().set(
        ctx.negate ? 500u : static_cast<uint32_t>(std::stoul(args[0])));
    return true;
}

bool RouterEigrpAddressFamily_EigrpLogNeighborChanges_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.currentEigrp->getGlobalConfigMgr().getConfigs().get<config::Eigrp::LOG_NEIGHBOR_CHANGES>().set(!ctx.negate);
    return true;
}

bool RouterEigrpAddressFamily_EigrpLogNeighborWarnings_Handler(EIGRP_PARAMS)
{
    auto& cfg = ctx.currentEigrp->getGlobalConfigMgr().getConfigs();
    if (!ctx.negate)
    {
        cfg.get<config::Eigrp::LOG_NEIGHBOR_WARNINGS>().set(true);
        if (!args.empty())
            cfg.get<config::Eigrp::LOG_NEIGHBOR_WARNINGS_INTERVAL>().set(static_cast<uint16_t>(std::stoul(args[0])));
    }
    else
    {
        cfg.get<config::Eigrp::LOG_NEIGHBOR_WARNINGS>().set(false);
        cfg.get<config::Eigrp::LOG_NEIGHBOR_WARNINGS_INTERVAL>().set(10);
    }
    return true;
}

bool RouterEigrpAddressFamily_EigrpRouterId_Handler(EIGRP_PARAMS)
{
    auto& ridField = ctx.currentEigrp->getGlobalConfigMgr().getConfigs().get<config::Eigrp::ROUTER_ID>();
    if (!ctx.negate)
    {
        types::IPv4Address _tmp; utils::extractIPv4Address(args[0], _tmp);
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
            cfgMgr.getConfigs().get<config::Eigrp::STUB_LEAK_MAP>().set(leakMap);
        else
            cfgMgr.getConfigs().get<config::Eigrp::STUB_LEAK_MAP>().unset();
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
        cfg.get<config::Eigrp::MAXIMUM_PREFIX>().set(static_cast<uint32_t>(std::stoul(args[0])));
        for (size_t i = 1; i < args.size(); i++)
        {
            if (utils::isNumber(args[i]))
                cfg.get<config::Eigrp::DAMPENING_INTERVAL>().set(static_cast<uint8_t>(std::stoul(args[i])));
            else if (args[i] == "dampened")
                cfg.get<config::Eigrp::DAMPENING>().set(true);
            else if (args[i] == "reset-time" && i + 1 < args.size())
                cfg.get<config::Eigrp::DAMPENING_RESET_TIME>().set(static_cast<uint16_t>(std::stoul(args[++i])));
            else if (args[i] == "restart" && i + 1 < args.size())
                cfg.get<config::Eigrp::DAMPENING_RESTART>().set(static_cast<uint16_t>(std::stoul(args[++i])));
            else if (args[i] == "restart-count" && i + 1 < args.size())
                cfg.get<config::Eigrp::DAMPENING_RESTART_COUNT>().set(static_cast<uint16_t>(std::stoul(args[++i])));
            else if (args[i] == "warning-only")
                cfg.get<config::Eigrp::DAMPENING_WARNINGS>().set(true);
        }
    }
    else
    {
        cfg.get<config::Eigrp::MAXIMUM_PREFIX>().set(0u);
        cfg.get<config::Eigrp::DAMPENING_INTERVAL>().set(75);
        cfg.get<config::Eigrp::DAMPENING>().set(false);
        cfg.get<config::Eigrp::DAMPENING_RESET_TIME>().set(0);
        cfg.get<config::Eigrp::DAMPENING_RESTART>().set(0);
        cfg.get<config::Eigrp::DAMPENING_RESTART_COUNT>().set(1);
        cfg.get<config::Eigrp::DAMPENING_WARNINGS>().set(false);
    }
    return true;
}

bool RouterEigrpAddressFamily_MetricRibScale_Handler(EIGRP_PARAMS)
{
    ctx.currentEigrp->getGlobalConfigMgr().getConfigs().get<config::Eigrp::RIB_SCALE>().set(
        ctx.negate ? 128 : static_cast<uint8_t>(std::stoul(args[0])));
    return true;
}

bool RouterEigrpAddressFamily_MetricWeights_Handler(EIGRP_PARAMS)
{
    auto& cfg = ctx.currentEigrp->getGlobalConfigMgr().getConfigs();
    if (!ctx.negate)
    {
        cfg.get<config::Eigrp::WEIGHT_TOS>().set(static_cast<uint8_t>(std::stoul(args[0])));
        cfg.get<config::Eigrp::WEIGTH_K1>().set(static_cast<uint8_t>(std::stoul(args[1])));
        cfg.get<config::Eigrp::WEIGHT_K3>().set(static_cast<uint8_t>(std::stoul(args[2])));
        cfg.get<config::Eigrp::WEIGHT_k4>().set(static_cast<uint8_t>(std::stoul(args[3])));
        cfg.get<config::Eigrp::WEIGHT_K2>().set(static_cast<uint8_t>(std::stoul(args[4])));
        cfg.get<config::Eigrp::WEIGHT_k5>().set(static_cast<uint8_t>(std::stoul(args[5])));
    }
    else
    {
        cfg.get<config::Eigrp::WEIGHT_TOS>().set(0);
        cfg.get<config::Eigrp::WEIGTH_K1>().set(1);
        cfg.get<config::Eigrp::WEIGHT_K3>().set(1);
        cfg.get<config::Eigrp::WEIGHT_k4>().set(0);
        cfg.get<config::Eigrp::WEIGHT_K2>().set(0);
        cfg.get<config::Eigrp::WEIGHT_k5>().set(0);
    }
    return true;
}

bool RouterEigrpAddressFamily_Neighbor_Handler(EIGRP_PARAMS)
{
    ctx.terminal.isList = true;
    types::IPAddress neighborIp; utils::extractIPAddress(args[0], neighborIp);
    interface::InterfaceType type = interface::getInterfaceType(args[1]);
    if (type == interface::InterfaceType::UNDEFINED)
    {
        ctx.terminal.iConsole->print("\r\n%EIGRP: Unknown interface type");
        return false;
    }
    uint32_t key = interface::calculateInterfaceKey(type, std::stof(args[2]));
    if (!ctx.negate)
        ctx.currentEigrp->getGlobalConfigMgr().enableUnicastPeer(neighborIp, key);
    else
        ctx.currentEigrp->getGlobalConfigMgr().disableUnicastPeer(neighborIp, key);
    return true;
}

bool RouterEigrpAddressFamily_Network_Handler(EIGRP_PARAMS)
{
    ctx.terminal.isList = true;
    types::IPv4Address _ip; utils::extractIPv4Address(args[0], _ip);
    uint8_t _plen;
    if (args.size() == 2)
    {
        types::IPv4Address _tmp; utils::extractIPv4Address(args[1], _tmp);
        utils::extractSubnetMask(_tmp.addr, _plen);
    }
    else
    {
        _plen = _ip.getDefaultMask();
    }
    types::IPv4Prefix network(_ip.addr, _plen);

    if (!ctx.negate)
        ctx.currentEigrp->getGlobalConfigMgr().addNetworkRange(network);
    else
        ctx.currentEigrp->getGlobalConfigMgr().delNetworkRange(network);
    return true;
}

bool RouterEigrpAddressFamily_SoftSia_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.currentEigrp->getGlobalConfigMgr().getConfigs().get<config::Eigrp::NON_STOP_FORWARDING>().set(!ctx.negate);
    return true;
}

bool RouterEigrpAddressFamily_TimersGracefulRestart_Handler(EIGRP_PARAMS)
{
    ctx.currentEigrp->getGlobalConfigMgr().getConfigs().get<config::Eigrp::GRACEFUL_PURGE_TIME>().set(
        ctx.negate ? 240 : static_cast<uint16_t>(std::stoul(args[0])));
    return true;
}

bool RouterEigrpAddressFamily_Topology_Handler(EIGRP_PARAMS)
{
    if (!ctx.negate)
    {
        if (args[0] == "base")
        {
            if (ctx.currentEigrp->getAF() == types::AddressFamily::IPv4)
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
