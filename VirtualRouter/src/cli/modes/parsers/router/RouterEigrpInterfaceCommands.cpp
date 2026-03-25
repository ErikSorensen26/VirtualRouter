// RouterEigrpInterfaceCommands.cpp

#include "RouterEigrpInterfaceCommands.h"
#include "eigrp/core/Eigrp.h"
#include "configs/registry/router/EigrpInterfaceRegistry.h"
#include "cli/runtime/CliSession.h"
#include "cli/runtime/CliUtils.h"

namespace cli
{
bool RouterEigrpInterface_AuthenticationKeyChain_Handler(EIGRP_PARAMS)
{
    if (!ctx.negate)
    {
        ctx.currentEigrpInterface->get<config::EigrpInterface::AUTHENTICATION_KEYCHAIN>().set(args[0]);
        ctx.currentEigrpInterface->get<config::EigrpInterface::AUTHENTICATION_MODE>().set(config::eigrp::AuthType::MD5);
    }
    else
    {
        ctx.currentEigrpInterface->get<config::EigrpInterface::AUTHENTICATION_KEYCHAIN>().unset();
        ctx.currentEigrpInterface->get<config::EigrpInterface::AUTHENTICATION_MODE>().set(config::eigrp::AuthType::NONE);
    }
    return true;
}

bool RouterEigrpInterface_AuthenticationMode_Handler(EIGRP_PARAMS)
{
    if (ctx.negate)
    {
        ctx.currentEigrpInterface->get<config::EigrpInterface::AUTHENTICATION_MODE>().set(config::eigrp::AuthType::NONE);
        ctx.currentEigrpInterface->get<config::EigrpInterface::AUTHENTICATION_KEYCHAIN>().unset();
        return true;
    }

    if (args[0] == "hmac-sha-256")
    {
        std::string key = args[1];
        if (key.size() > 32)
        {
            ctx.terminal.controller.print("\r\n%EIGRP: HMAC-SHA-256 password truncated to 32 characters");
            key = key.substr(0, 32);
        }
        ctx.currentEigrpInterface->get<config::EigrpInterface::AUTHENTICATION_MODE>().set(config::eigrp::AuthType::SHA256);
        ctx.currentEigrpInterface->get<config::EigrpInterface::AUTHENTICATION_KEYCHAIN>().set(key);
    }
    else if (args[0] == "md5")
    {
        ctx.currentEigrpInterface->get<config::EigrpInterface::AUTHENTICATION_MODE>().set(config::eigrp::AuthType::MD5);
    }
    return true;
}

bool RouterEigrpInterface_BandwidthPercentage_Handler(EIGRP_PARAMS)
{
    ctx.currentEigrpInterface->get<config::EigrpInterface::BANDWIDTH_PERCENTAGE>().set(
        ctx.negate ? 50u : static_cast<uint32_t>(std::stoul(args[0])));
    return true;
}

bool RouterEigrpInterface_DampeningChange_Handler(EIGRP_PARAMS)
{
    if (ctx.negate)
    {
        ctx.currentEigrpInterface->get<config::EigrpInterface::DAMPENING_CHANGE>().set(false);
        ctx.currentEigrpInterface->get<config::EigrpInterface::DAMPENING_CHANGE_PERCENT>().set(1);
    }
    else
    {
        ctx.currentEigrpInterface->get<config::EigrpInterface::DAMPENING_CHANGE>().set(true);
        ctx.currentEigrpInterface->get<config::EigrpInterface::DAMPENING_CHANGE_PERCENT>().set(
            static_cast<uint8_t>(std::stoul(args[0])));
    }
    return true;
}

bool RouterEigrpInterface_DampeningInterval_Handler(EIGRP_PARAMS)
{
    if (ctx.negate)
    {
        ctx.currentEigrpInterface->get<config::EigrpInterface::DAMPENING_INTERVAL>().set(false);
        ctx.currentEigrpInterface->get<config::EigrpInterface::DAMPENING_INTERVAL_TIME>().set(5);
    }
    else
    {
        ctx.currentEigrpInterface->get<config::EigrpInterface::DAMPENING_INTERVAL>().set(true);
        ctx.currentEigrpInterface->get<config::EigrpInterface::DAMPENING_INTERVAL_TIME>().set(
            static_cast<uint16_t>(std::stoul(args[0])));
    }
    return true;
}

bool RouterEigrpInterface_Exit_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    if (ctx.currentEigrp->getAF() == types::AddressFamily::IPv4)
        ctx.terminal.exitMode<CliMode::RouterEigrpAddressFamilyV4>(ctx.currentEigrp, ctx.currentEigrpNamed, nullptr);
    else
        ctx.terminal.exitMode<CliMode::RouterEigrpAddressFamilyV6>(ctx.currentEigrp, ctx.currentEigrpNamed, nullptr);
    return true;
}

bool RouterEigrpInterface_HelloInterval_Handler(EIGRP_PARAMS)
{
    ctx.currentEigrpInterface->get<config::EigrpInterface::HELLO_INTERVAL>().set(
        ctx.negate ? 5 : static_cast<uint16_t>(std::stoul(args[0])));
    return true;
}

bool RouterEigrpInterface_HoldTime_Handler(EIGRP_PARAMS)
{
    ctx.currentEigrpInterface->get<config::EigrpInterface::HOLD_TIME>().set(
        ctx.negate ? 15 : static_cast<uint16_t>(std::stoul(args[0])));
    return true;
}

bool RouterEigrpInterface_NextHopSelf_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.currentEigrpInterface->get<config::EigrpInterface::NEXT_HOP_SELF>().set(!ctx.negate);
    return true;
}

bool RouterEigrpInterface_PassiveInterface_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.currentEigrpInterface->get<config::EigrpInterface::PASSIVE_INTERFACE>().set(!ctx.negate);
    return true;
}

bool RouterEigrpInterface_SplitHorizon_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.currentEigrpInterface->get<config::EigrpInterface::SPLIT_HORIZON>().set(!ctx.negate);
    return true;
}

bool RouterEigrpInterface_SummaryAddress_Handler(EIGRP_PARAMS)
{
    types::IPPrefix network;
    types::AddressFamily af = ctx.currentEigrp->getAF();

    bool extracted = utils::extractIPPrefix(args[0], network);
    if (!extracted && af == types::AddressFamily::IPv4)
    {
        if (!utils::extractIPv4Prefix(args[0], args[1], network))
        {
            ctx.terminal.controller.print("\r\n%EIGRP: Invalid summary address");
            return false;
        }
    }

    types::IPAddress netAddr = network; // uses operator types::IPAddress() for the network address
    uint8_t plen = network.prefixLength;

    ctx.currentEigrpInterface->get<config::EigrpInterface::SUMMARY_ADDRESS>().withWrite(
        [&](std::vector<std::tuple<types::IPAddress, uint8_t>>& v) {
            auto it = std::find_if(v.begin(), v.end(), [&](const auto& t) {
                return std::get<0>(t) == netAddr && std::get<1>(t) == plen;
            });
            if (!ctx.negate)
            {
                if (it == v.end())
                    v.emplace_back(netAddr, plen);
            }
            else
            {
                if (it != v.end())
                    v.erase(it);
            }
        });
    return true;
}
}
