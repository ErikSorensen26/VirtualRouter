// RouterEigrpInterfaceCommands.cpp

#include "RouterEigrpInterfaceCommands.h"
#include "cli/parser/CliModeParser.hpp"
#include "configs/registry/router/EigrpInterfaceRegistry.h"
#include "cli/runtime/CliSession.h"
#include "cli/parser/CommandUtils.hpp"

#define EIGRP_PARAMS DEFINE_PARAMS(config::EigrpInterfaceRegistry)

namespace cli
{
bool RouterEigrpInterface_AuthenticationKeyChain_Handler(EIGRP_PARAMS)
{
    auto authKey = ctx.configs().reg.get<config::EigrpInterface::AUTHENTICATION_KEYCHAIN>();
    return utils::setFieldValue(authKey, ctx, segs[0] >> 2);
}

bool RouterEigrpInterface_AuthenticationMode_Handler(EIGRP_PARAMS)
{
    auto mode = ctx.configs().reg.get<config::EigrpInterface::AUTHENTICATION_MODE>();

    if (utils::handleValueReset(mode, ctx))
        return true;

    switch (segs[0][0])
    {
        case "md5"_tok:
        {
            mode.set(config::eigrp::AuthType::MD5);
            return true;
        }
        case "hmac-sha-256"_tok:
        {
            // TODO
        }
    }
    return false;
}

bool RouterEigrpInterface_BandwidthPercentage_Handler(EIGRP_PARAMS)
{
    auto eigrpBw = ctx.configs().reg.get<config::EigrpInterface::BANDWIDTH_PERCENTAGE>();
    return utils::setFieldValue(eigrpBw, ctx, segs[0] >> 1);
}

bool RouterEigrpInterface_DampeningChange_Handler(EIGRP_PARAMS)
{
    auto dampChange = ctx.configs().reg.get<config::EigrpInterface::DAMPENING_CHANGE>();
    auto dampChangePercent = ctx.configs().reg.get<config::EigrpInterface::DAMPENING_CHANGE_PERCENT>();
    utils::setToggleValue(dampChange, ctx);
    return utils::setFieldValue(dampChangePercent, ctx, segs[0] >> 1);
}

bool RouterEigrpInterface_DampeningInterval_Handler(EIGRP_PARAMS)
{
    auto dampInterval = ctx.configs().reg.get<config::EigrpInterface::DAMPENING_INTERVAL>();
    auto dampIntervalTime = ctx.configs().reg.get<config::EigrpInterface::DAMPENING_INTERVAL_TIME>();
    utils::setToggleValue(dampInterval, ctx);
    return utils::setFieldValue(dampIntervalTime, ctx, segs[0] >> 1);
}

bool RouterEigrpInterfaceV4_Exit_Handler(EIGRP_PARAMS)
{
    UNUSED(segs);
    return ctx.terminal.popMode();
}

bool RouterEigrpInterfaceV6_Exit_Handler(EIGRP_PARAMS)
{
    UNUSED(segs);
    return ctx.terminal.popMode();
}

bool RouterEigrpInterface_HelloInterval_Handler(EIGRP_PARAMS)
{
    auto hello = ctx.configs().reg.get<config::EigrpInterface::HELLO_INTERVAL>();
    return utils::setFieldValue(hello, ctx, segs[0] >> 1);
}

bool RouterEigrpInterface_HoldTime_Handler(EIGRP_PARAMS)
{
    auto holdTime = ctx.configs().reg.get<config::EigrpInterface::HOLD_TIME>();
    return utils::setFieldValue(holdTime, ctx, segs[0] >> 1);
}

bool RouterEigrpInterface_NextHopSelf_Handler(EIGRP_PARAMS)
{
    UNUSED(segs);
    auto nhs = ctx.configs().reg.get<config::EigrpInterface::NEXT_HOP_SELF>();
    utils::setToggleValue(nhs, ctx);
    return true;
}

bool RouterEigrpInterface_PassiveInterface_Handler(EIGRP_PARAMS)
{
    UNUSED(segs);
    auto passive = ctx.configs().reg.get<config::EigrpInterface::PASSIVE_INTERFACE>();
    utils::setToggleValue(passive, ctx);
    return true;
}

bool RouterEigrpInterface_SplitHorizon_Handler(EIGRP_PARAMS)
{
    UNUSED(segs);
    auto split = ctx.configs().reg.get<config::EigrpInterface::SPLIT_HORIZON>();
    utils::setToggleValue(split, ctx);
    return true;
}

bool RouterEigrpInterface_SummaryAddress_Handler(EIGRP_PARAMS)
{
    auto sum = ctx.configs().reg.get<config::EigrpInterface::SUMMARY_ADDRESS>();
    config::DefType<typename decltype(sum)::Field>::node tup;
    if (!utils::setTupleElement(std::get<0>(tup), segs[0] >> 2) &&
        !utils::setDoubleTupleElement(std::get<0>(tup), segs[0] >> 2, segs[0] >> 3))
        return false;
    utils::setTupleElement(std::get<1>(tup), segs >> 1 >> 1);
    return utils::setListEntry(sum, ctx, tup);
}

// bool RouterEigrpInterface_Shutdown_Handler(EIGRP_PARAMS); //TODO

#define ROUTER_EIGRP_INTERFACE_LIST(X, Y) \
    X(Y, (COMMAND, AuthenticationKeyChain, "authentication"_tok, "key-chain"_tok)) \
    X(Y, (COMMAND, AuthenticationMode, "authentication"_tok, "mode"_tok)) \
    X(Y, (COMMAND, BandwidthPercentage, "bandwidth-percentage"_tok)) \
    X(Y, (COMMAND, DampeningChange, "dampening-change"_tok)) \
    X(Y, (COMMAND, DampeningInterval, "dampening-interval"_tok)) \
    X(Y, (COMMAND, HelloInterval, "hello-interval"_tok)) \
    X(Y, (COMMAND, HoldTime, "hold-time"_tok)) \
    X(Y, (COMMAND, NextHopSelf, "next-hop-self"_tok)) \
    X(Y, (COMMAND, PassiveInterface, "passive-interface"_tok)) \
    X(Y, (COMMAND, SplitHorizon, "split-horizon"_tok)) \
    X(Y, (COMMAND, SummaryAddress, "summary-address"_tok))

/**
 * @brief Parser for EIGRPv4 interface-level configuration commands.
 * @ingroup CLI_MODE_PARSERS
 *
 * Configures per-interface EIGRP parameters including bandwidth, delay,
 * reliability, timers, and split horizon settings.
 */
DEFINE_CMD_MODE(RouterEigrpInterface, config::EigrpInterfaceRegistry, ROUTER_EIGRP_INTERFACE_LIST)

#define ROUTER_EIGRP_INTERFACE_LIST_V4(X, Y) \
    X(Y, (COMMAND, Exit, "exit-af-intervace"_tok)) \
    X(Y, (CMD_INHERIT, RouterEigrpInterfaceCommands))

/**
 * @brief IPv4 address-family interface mode parser.
 * @ingroup CLI_MODE_PARSERS
 */
DEFINE_CMD_MODE(RouterEigrpInterfaceV4, config::EigrpInterfaceRegistry, ROUTER_EIGRP_INTERFACE_LIST_V4)

#define ROUTER_EIGRP_INTERFACE_LIST_V6(X, Y) \
    X(Y, (COMMAND, Exit, "exit-af-intervace"_tok)) \
    X(Y, (CMD_INHERIT, RouterEigrpInterfaceCommands))

/**
 * @brief IPv6 address-family interface mode parser.
 * @ingroup CLI_MODE_PARSERS
 */
DEFINE_CMD_MODE(RouterEigrpInterfaceV6, config::EigrpInterfaceRegistry, ROUTER_EIGRP_INTERFACE_LIST_V6)
}

#undef EIGRP_PARAMS
