// RouterEigrpInterfaceCommands.cpp

#include "RouterEigrpInterfaceCommands.h"
#include "configs/registry/router/EigrpInterfaceRegistry.h"
#include "cli/runtime/CliSession.h"
#include "cli/parser/CommandUtils.hpp".h"

#define EIGRP_PARAMS DEFINE_PARAMS(config::EigrpInterfaceRegistry)

namespace cli
{
bool RouterEigrpInterface_AuthenticationKeyChain_Handler(EIGRP_PARAMS)
{
    auto& authKey = ctx.configs.get<config::EigrpInterface::AUTHENTICATION_KEYCHAIN>();
    return utils::setFieldValue(authKey, ctx, segs[0] >> 2);
}

bool RouterEigrpInterface_AuthenticationMode_Handler(EIGRP_PARAMS)
{
    auto& mode = ctx.configs.get<config::EigrpInterface::AUTHENTICATION_MODE>();

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
    auto& eigrpBw = ctx.configs.get<config::EigrpInterface::BANDWIDTH_PERCENTAGE>();
    return utils::setFieldValue(eigrpBw, ctx, segs[0] >> 1);
}

bool RouterEigrpInterface_DampeningChange_Handler(EIGRP_PARAMS)
{
    auto& dampChange = ctx.configs.get<config::EigrpInterface::DAMPENING_CHANGE>();
    auto& dampChangePercent = ctx.configs.get<config::EigrpInterface::DAMPENING_CHANGE_PERCENT>();
    utils::setToggleValue(dampChange, ctx);
    return utils::setFieldValue(dampChangePercent, ctx, segs[0] >> 1);
}

bool RouterEigrpInterface_DampeningInterval_Handler(EIGRP_PARAMS)
{
    auto& dampInterval = ctx.configs.get<config::EigrpInterface::DAMPENING_INTERVAL>();
    auto& dampIntervalTime = ctx.configs.get<config::EigrpInterface::DAMPENING_INTERVAL_TIME>();
    utils::setToggleValue(dampInterval, ctx);
    return utils::setFieldValue(dampIntervalTime, ctx, segs[0] >> 1);
}

bool RouterEigrpInterfaceV4_Exit_Handler(EIGRP_PARAMS)
{
    UNUSED(segs);
    return ctx.terminal.exitMode<CliMode::RouterEigrpAddressFamilyV6, config::EigrpRegistry>(ctx.configs);
}

bool RouterEigrpInterfaceV6_Exit_Handler(EIGRP_PARAMS)
{
    UNUSED(segs);
    return ctx.terminal.exitMode<CliMode::RouterEigrpAddressFamilyV4, config::EigrpRegistry>(ctx.configs);
}

bool RouterEigrpInterface_HelloInterval_Handler(EIGRP_PARAMS)
{
    auto& hello = ctx.configs.get<config::EigrpInterface::HELLO_INTERVAL>();
    return utils::setFieldValue(hello, ctx, segs[0] >> 1);
}

bool RouterEigrpInterface_HoldTime_Handler(EIGRP_PARAMS)
{
    auto& holdTime = ctx.configs.get<config::EigrpInterface::HOLD_TIME>();
    return utils::setFieldValue(holdTime, ctx, segs[0] >> 1);
}

bool RouterEigrpInterface_NextHopSelf_Handler(EIGRP_PARAMS)
{
    UNUSED(segs);
    auto& nhs = ctx.configs.get<config::EigrpInterface::NEXT_HOP_SELF>();
    utils::setToggleValue(nhs, ctx);
    return true;
}

bool RouterEigrpInterface_PassiveInterface_Handler(EIGRP_PARAMS)
{
    UNUSED(segs);
    auto& passive = ctx.configs.get<config::EigrpInterface::PASSIVE_INTERFACE>();
    utils::setToggleValue(passive, ctx);
    return true;
}

bool RouterEigrpInterface_SplitHorizon_Handler(EIGRP_PARAMS)
{
    UNUSED(segs);
    auto& split = ctx.configs.get<config::EigrpInterface::SPLIT_HORIZON>();
    utils::setToggleValue(split, ctx);
    return true;
}

bool RouterEigrpInterface_SummaryAddress_Handler(EIGRP_PARAMS)
{
    auto& sum = ctx.configs.get<config::EigrpInterface::SUMMARY_ADDRESS>();
    config::DefType<decltype(sum)>::node tup;
    if (!utils::setTupleElement(std::get<0>(tup), segs[0] >> 2) &&
        !utils::setDoubleTupleElement(std::get<0>(tup), segs[0] >> 2, segs[0] >> 3))
        return false;
    utils::setTupleElement(std::get<1>(tup), segs >> 1 >> 1);
    return utils::setListEntry(sum, ctx, tup);
}
}

#undef EIGRP_PARAMS
