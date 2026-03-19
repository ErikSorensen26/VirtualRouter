// RouterEigrpInterfaceCommands.cpp

#include "RouterEigrpInterfaceCommands.h"
#include "eigrp/core/Eigrp.h"
#include "eigrp/interface/EigrpInterface.h"
#include "cli/runtime/CliSession.h"
#include "cli/runtime/CliUtils.h"
#include "interface/Interface.h"

namespace Cli
{
bool RouterEigrpInterface_AuthenticationKeyChain_Handler(EIGRP_PARAMS)
{
    std::string keychain = args[0];
    if (ctx.negate)
    {
        ctx.currentEigrpInterface->auth.fullyEnabled.store(false, std::memory_order_relaxed);
        ctx.currentEigrpInterface->auth.authType = EigrpConfigs::AuthType::NONE;
    }
    else
    {
        ctx.currentEigrpInterface->auth.fullyEnabled.store(false, std::memory_order_relaxed);
        ctx.currentEigrpInterface->auth.authType = EigrpConfigs::AuthType::MD5;
        if (std::holds_alternative<std::string>(ctx.currentEigrpInterface->auth.key) && !std::get<std::string>(ctx.currentEigrpInterface->auth.key).empty())
        {
            ctx.currentEigrpInterface->auth.fullyEnabled.store(true, std::memory_order_release);
        }
    }
    return true;
}

bool RouterEigrpInterface_AuthenticationMode_Handler(EIGRP_PARAMS)
{
    if (args[0] == "hmac-sha-256")
    {
        ctx.currentEigrpInterface->auth.authType = EigrpConfigs::AuthType::SHA256;
        if (args[1].size() > 32)
        {
            //TODO
            ctx.terminal.iConsole->print(std::string("\r\n%EIGRP: HMAC-SHA-256 password accepted but truncated, max length is 32 characters"));
            ctx.currentEigrpInterface->auth.key = std::string(args[1].substr(0, 32));
        }
        ctx.currentEigrpInterface->auth.key = std::string(args[1]);
    }
    else if (args[0] == "md5")
    {
        ctx.currentEigrpInterface->auth.authType = EigrpConfigs::AuthType::MD5;
    }
    return true;
}

bool RouterEigrpInterface_BandwidthPercentage_Handler(EIGRP_PARAMS)
{
    ctx.negate
        ? ctx.currentEigrpInterface->bandwidthPercentage.store(50, std::memory_order_release)
        : ctx.currentEigrpInterface->bandwidthPercentage.store(static_cast<uint32_t>(std::stoi(args[0])), std::memory_order_release);
    return true;
}

bool RouterEigrpInterface_DampeningChange_Handler(EIGRP_PARAMS)
{
    ctx.negate
        ? ctx.currentEigrpInterface->dampeningChange.store(0, std::memory_order_release)
        : ctx.currentEigrpInterface->dampeningChange.store(static_cast<uint8_t>(std::stoi(args[0])), std::memory_order_release);
    return true;
}

bool RouterEigrpInterface_DampeningInterval_Handler(EIGRP_PARAMS)
{
    ctx.negate
        ? ctx.currentEigrpInterface->dampeningInterval.store(5, std::memory_order_release)
        : ctx.currentEigrpInterface->dampeningInterval.store(static_cast<uint16_t>(std::stoi(args[0])), std::memory_order_release);
    return true;
}

bool RouterEigrpInterface_Exit_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    if (ctx.currentEigrp->getAF() == AddressFamily::IPv4)
        ctx.terminal.exitMode<CliMode::RouterEigrpAddressFamilyV4>(ctx.currentEigrp, ctx.currentEigrpNamed, nullptr);
    else
        ctx.terminal.exitMode<CliMode::RouterEigrpAddressFamilyV6>(ctx.currentEigrp, ctx.currentEigrpNamed, nullptr);
    return true;
}

bool RouterEigrpInterface_HelloInterval_Handler(EIGRP_PARAMS)
{
    ctx.negate
      ? ctx.currentEigrpInterface->helloTime.store(5, std::memory_order_release)
      : ctx.currentEigrpInterface->helloTime.store(static_cast<uint16_t>(std::stoi(args[0])), std::memory_order_release);
    return true;
}

bool RouterEigrpInterface_HoldTime_Handler(EIGRP_PARAMS)
{
    ctx.negate
        ? ctx.currentEigrpInterface->holdTime.store(15, std::memory_order_release)
        : ctx.currentEigrpInterface->holdTime.store(static_cast<uint16_t>(std::stoi(args[0])), std::memory_order_release);
    return true;
}

bool RouterEigrpInterface_NextHopSelf_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.currentEigrpInterface->nextHopSelf.store(!ctx.negate, std::memory_order_release);
    return true;
}

bool RouterEigrpInterface_PassiveInterface_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.currentEigrp->getGlobalConfigMgr().setPassiveInterface(ctx.currentEigrpInterface->key, !ctx.negate);
    return true;
}

bool RouterEigrpInterface_SplitHorizon_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.currentEigrpInterface->splitHorizon.store(!ctx.negate, std::memory_order_release);
    return true;
}

bool RouterEigrpInterface_SummaryAddress_Handler(EIGRP_PARAMS)
{
    uint8_t size = 1;
    IPPrefix network;
    Eigrp::EigrpInterface* iface = nullptr;
    iface = ctx.currentEigrp->getIfaceMgr().getInterface(ctx.currentEigrpInterface->key);

    AddressFamily af = ctx.currentEigrp->getAF();
    bool extracted = CliUtils::extractIPPrefix(args[0], network);

    if (!extracted && af == AddressFamily::IPv4)
    {
        if (af != AddressFamily::IPv4 || !CliUtils::extractIPv4Prefix(args[0], args[1], network))
            return false;
        size = 2;
    }

    if (args.size() != size && args[size] == "leak-map")
    {
        // XXX
    }

    if (iface)
    {
        ctx.negate
            ? iface->getAggregator().installSummary(network, false)
            : iface->getAggregator().withdrawSummary(network);
    }
    else
    {
        if (!ctx.negate)
        {
            std::shared_lock<std::shared_mutex> lock(ctx.currentEigrpInterface->configsMutex);
            if (!std::any_of(ctx.currentEigrpInterface->pendingSummaryRoutes.begin(), ctx.currentEigrpInterface->pendingSummaryRoutes.end(),
                [&](IPPrefix& pfx) { return pfx == network; }))
            {
                ctx.currentEigrpInterface->pendingSummaryRoutes.push_back(network);
            }
        }
        else
        {
            std::shared_lock<std::shared_mutex> lock(ctx.currentEigrpInterface->configsMutex);
            ctx.currentEigrpInterface->pendingSummaryRoutes.erase(std::remove(ctx.currentEigrpInterface->pendingSummaryRoutes.begin(),
                ctx.currentEigrpInterface->pendingSummaryRoutes.end(), network), ctx.currentEigrpInterface->pendingSummaryRoutes.end());
        }
    }
    return true;
}
}
