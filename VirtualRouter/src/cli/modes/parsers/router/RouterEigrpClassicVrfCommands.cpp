// RouterEigrpClassicVrfCommands.h

#include "RouterEigrpClassicVrfCommands.h"
#include "eigrp/core/Eigrp.h"
#include "eigrp/core/EigrpConfig.h"
#include "configs/registry/router/EigrpRegistry.h"
#include "cli/runtime/CliSession.h"
#include "cli/runtime/CliUtils.h"
#include "interface/configs/InterfaceType.hpp"
#include "interface/configs/InterfaceConfigs.h"

namespace Cli
{
bool RouterEigrpClassicVrf_EigrpLogNeighborChanges_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.currentEigrp->getGlobalConfigMgr().getConfigs().get<Config::Eigrp::LOG_NEIGHBOR_CHANGES>().set(!ctx.negate);
    return true;
}

bool RouterEigrpClassicVrf_EigrpLogNeighborWarnings_Handler(EIGRP_PARAMS)
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

bool RouterEigrpClassicVrf_EigrpRouterId_Handler(EIGRP_PARAMS)
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

bool RouterEigrpClassicVrf_Neighbor_Handler(EIGRP_PARAMS)
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

bool RouterEigrpClassicVrf_Network_Handler(EIGRP_PARAMS)
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
        _plen = _ip.getDefaultMask();
    IPv4Prefix network(_ip.addr, _plen);

    if (!ctx.negate)
        ctx.currentEigrp->getGlobalConfigMgr().addNetworkRange(network);
    else
        ctx.currentEigrp->getGlobalConfigMgr().delNetworkRange(network);
    return true;
}

bool RouterEigrpClassicVrf_PassiveInterface_Handler(EIGRP_PARAMS)
{
    ctx.terminal.isList = true;
    uint32_t key = calculateInterfaceKey(getInterfaceType(args[0]), std::stof(args[1]));
    ctx.currentEigrp->getGlobalConfigMgr().setPassiveInterface(key, !ctx.negate);
    return true;
}

bool RouterEigrpClassicVrf_TimersGracefulRestart_Handler(EIGRP_PARAMS)
{
    ctx.currentEigrp->getGlobalConfigMgr().getConfigs().get<Config::Eigrp::GRACEFUL_PURGE_TIME>().set(
        ctx.negate ? 240 : static_cast<uint16_t>(std::stoul(args[0])));
    return true;
}
}
