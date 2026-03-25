// RouterEigrpClassicVrfCommands.h

#include "RouterEigrpClassicVrfCommands.h"
#include "eigrp/core/Eigrp.h"
#include "eigrp/core/EigrpConfig.h"
#include "configs/registry/router/EigrpRegistry.h"
#include "cli/runtime/CliSession.h"
#include "cli/runtime/CliUtils.h"
#include "interface/configs/InterfaceType.hpp"
#include "interface/configs/InterfaceConfigs.h"

namespace cli
{
bool RouterEigrpClassicVrf_EigrpLogNeighborChanges_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.currentEigrp->getGlobalConfigMgr().getConfigs().get<config::Eigrp::LOG_NEIGHBOR_CHANGES>().set(!ctx.negate);
    return true;
}

bool RouterEigrpClassicVrf_EigrpLogNeighborWarnings_Handler(EIGRP_PARAMS)
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

bool RouterEigrpClassicVrf_EigrpRouterId_Handler(EIGRP_PARAMS)
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

bool RouterEigrpClassicVrf_EigrpStub_Handler(EIGRP_PARAMS)
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

bool RouterEigrpClassicVrf_Exit_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.terminal.exitMode<CliMode::RouterEigrpClassicV4>(ctx.tempEigrp, nullptr, nullptr);
    return true;
}

bool RouterEigrpClassicVrf_MetricWeights_Handler(EIGRP_PARAMS)
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

bool RouterEigrpClassicVrf_Neighbor_Handler(EIGRP_PARAMS)
{
    ctx.terminal.isList = true;
    types::IPAddress neighborIp; utils::extractIPAddress(args[0], neighborIp);
    interface::InterfaceType type = interface::getInterfaceType(args[1]);
    if (type == interface::InterfaceType::UNDEFINED)
    {
        ctx.terminal.controller.print("\r\n%EIGRP: Unknown interface type");
        return false;
    }
    uint32_t key = interface::calculateInterfaceKey(type, std::stof(args[2]));
    if (!ctx.negate)
        ctx.currentEigrp->getGlobalConfigMgr().enableUnicastPeer(neighborIp, key);
    else
        ctx.currentEigrp->getGlobalConfigMgr().disableUnicastPeer(neighborIp, key);
    return true;
}

bool RouterEigrpClassicVrf_Network_Handler(EIGRP_PARAMS)
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
        _plen = _ip.getDefaultMask();
    types::IPv4Prefix network(_ip.addr, _plen);

    if (!ctx.negate)
        ctx.currentEigrp->getGlobalConfigMgr().addNetworkRange(network);
    else
        ctx.currentEigrp->getGlobalConfigMgr().delNetworkRange(network);
    return true;
}

bool RouterEigrpClassicVrf_PassiveInterface_Handler(EIGRP_PARAMS)
{
    ctx.terminal.isList = true;
    uint32_t key = interface::calculateInterfaceKey(interface::getInterfaceType(args[0]), std::stof(args[1]));
    ctx.currentEigrp->getGlobalConfigMgr().setPassiveInterface(key, !ctx.negate);
    return true;
}

bool RouterEigrpClassicVrf_TimersGracefulRestart_Handler(EIGRP_PARAMS)
{
    ctx.currentEigrp->getGlobalConfigMgr().getConfigs().get<config::Eigrp::GRACEFUL_PURGE_TIME>().set(
        ctx.negate ? 240 : static_cast<uint16_t>(std::stoul(args[0])));
    return true;
}
}
