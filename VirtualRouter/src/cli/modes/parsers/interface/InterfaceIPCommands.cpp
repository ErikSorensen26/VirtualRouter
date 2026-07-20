// InterfaceIPCommands.cpp

#include "InterfaceIPCommands.h"
#include "cli/parser/CliModeParser.hpp"
#include "cli/parser/CommandUtils.hpp"
#include "InterfaceIPOspfCommands.h"
#include "configs/registry/interface/InterfaceRegistry.h"
#include "configs/FieldAccessor.hpp"

#define INTERFACE_PARAMS DEFINE_PARAMS(config::InterfaceRegistry)
#define INTERFACE_SUB_PARAMS DEFINE_SUB_PARAMS(config::InterfaceRegistry)

namespace cli
{
bool InterfaceIP_AddressSet_Handler(INTERFACE_PARAMS)
{
    if (!ctx.negate && !ctx.defaulted)
    {
        auto dhcp = ctx.configs().get<config::Interface::IP_ADDRESS_DHCP>();
        if (dhcp.load())
            dhcp.set(false);
    }

    if (segs.size() == 1)
    {
        auto primary = ctx.configs().get<config::Interface::IP_ADDRESS>();
        return utils::setDoubleFieldValue(primary, ctx, segs[0] >> 1, segs[0] >> 2);
    }
    auto secondary = ctx.configs().get<config::Interface::IP_ADDRESS_SECONDARY>();
    config::DefType<decltype(secondary)::Field>::node tup;
    if (!utils::setDoubleTupleElement(std::get<0>(tup), segs[0] >> 1, segs[0] >> 2))
        return false;
    utils::setTupleElement(std::get<1>(tup), segs >> 1 >> 1);
    return utils::setListEntry(secondary, ctx, tup);
}

bool InterfaceIP_AddressDhcp_Handler(INTERFACE_PARAMS)
{
    UNUSED(segs);
    if (!ctx.negate && !ctx.defaulted)
    {
        auto ipAddr = ctx.configs().get<config::Interface::IP_ADDRESS>();
        if (ipAddr.hasValue())
            ipAddr.unset();
    }
    auto ipdhcp = ctx.configs().get<config::Interface::IP_ADDRESS_DHCP>();
    utils::setToggleValue(ipdhcp, ctx);
    return true;
}

bool InterfaceIP_AuthenticationKeyChain_Handler(INTERFACE_PARAMS)
{
    switch (segs[0][0])
    {
        case "eigrp"_tok:
        {
            uint16_t as;
            if (!utils::setValue(as, segs[0] >> 1))
                return false;
            auto authKey = ctx.configs().get<config::Interface::IP_EIGRP>().emplaceBack(as).get<config::EigrpInterface::AUTHENTICATION_KEYCHAIN>();
            return utils::setFieldValue(authKey, ctx, segs[0] >> 2);
        }
    }
    return false;
}

bool InterfaceIP_AuthenticationMode_Handler(INTERFACE_PARAMS)
{
    switch (segs[0][0])
    {
        case "eigrp"_tok:
        {
            uint16_t as;
            if (!utils::setValue(as, segs[0] >> 1))
                return false;
            auto& configs = ctx.configs().get<config::Interface::IP_EIGRP>()
                .emplaceBack(as);

            if (utils::handleValueReset(configs.get<config::EigrpInterface::AUTHENTICATION_MODE>(), ctx))
                return true;

            switch (segs[1][0])
            {
                case "md5"_tok:
                {
                    configs.get<config::EigrpInterface::AUTHENTICATION_MODE>().set(config::eigrp::AuthType::MD5);
                    return true;
                }
            }
        }
    }
    return false;
}

bool InterfaceIP_BandwidthPercentage_Handler(INTERFACE_PARAMS)
{
    switch (segs[0][0])
    {
        case "eigrp"_tok:
        {
            uint16_t as;
            if (!utils::setValue(as, segs[0] >> 1))
                return false;
            auto eigrpBw = ctx.configs().get<config::Interface::IP_EIGRP>()
                .emplaceBack(as).get<config::EigrpInterface::BANDWIDTH_PERCENTAGE>();
            return utils::setFieldValue(eigrpBw, ctx, segs[0] >> 2);
        }
    }
    return false;
}

bool InterfaceIP_DampeningChange_Handler(INTERFACE_PARAMS)
{
    switch (segs[0][0])
    {
        case "eigrp"_tok:
        {
            uint16_t as;
            if (!utils::setValue(as, segs[0] >> 1))
                return false;
            auto& eigrp = ctx.configs().get<config::Interface::IP_EIGRP>().emplaceBack(as);
            auto dampChange = eigrp.get<config::EigrpInterface::DAMPENING_CHANGE>();
            auto dampChangePercent = eigrp.get<config::EigrpInterface::DAMPENING_CHANGE_PERCENT>();
            utils::setToggleValue(dampChange, ctx);
            return utils::setFieldValue(dampChangePercent, ctx, segs[0] >> 2);
        }
    }
    return false;
}

bool InterfaceIP_DampeningInterval_Handler(INTERFACE_PARAMS)
{
    switch (segs[0][0])
    {
        case "eigrp"_tok:
        {
            uint16_t as;
            if (!utils::setValue(as, segs[0] >> 1))
                return false;
            auto& eigrp = ctx.configs().get<config::Interface::IP_EIGRP>().emplaceBack(as);
            auto dampInterval = eigrp.get<config::EigrpInterface::DAMPENING_INTERVAL>();
            auto dampIntervalTime = eigrp.get<config::EigrpInterface::DAMPENING_INTERVAL_TIME>();
            utils::setToggleValue(dampInterval, ctx);
            return utils::setFieldValue(dampIntervalTime, ctx, segs[0] >> 2);
        }
    }
    return false;
}

bool InterfaceIP_HelloInterval_Handler(INTERFACE_PARAMS)
{
    switch (segs[0][0])
    {
        case "eigrp"_tok:
        {
            uint16_t as;
            if (!utils::setValue(as, segs[0] >> 1))
                return false;
            auto helloTime = ctx.configs().get<config::Interface::IP_EIGRP>()
                .emplaceBack(as).get<config::EigrpInterface::HELLO_INTERVAL>();
            return utils::setFieldValue(helloTime, ctx, segs[0] >> 2);
        }
    }
    return false;
}

bool InterfaceIP_HoldTime_Handler(INTERFACE_PARAMS)
{
    switch (segs[0][0])
    {
        case "eigrp"_tok:
        {
            uint16_t as;
            if (!utils::setValue(as, segs[0] >> 1))
                return false;
            auto holdTime = ctx.configs().get<config::Interface::IP_EIGRP>()
                .emplaceBack(as).get<config::EigrpInterface::HOLD_TIME>();
            return utils::setFieldValue(holdTime, ctx, segs[0] >> 2);
        }
    }
    return false;
}

bool InterfaceIP_Mtu_Handler(INTERFACE_PARAMS)
{
    auto mtu = ctx.configs().get<config::Interface::IP_MTU>();
    return utils::setFieldValue(mtu, ctx, segs[0] >> 1);
}

bool InterfaceIP_NextHopSelf_Handler(INTERFACE_PARAMS)
{
    switch (segs[0][0])
    {
        case "eigrp"_tok:
        {
            uint16_t as;
            if (!utils::setValue(as, segs[0] >> 1))
                return false;
            auto nhs = ctx.configs().get<config::Interface::IP_EIGRP>()
                .emplaceBack(as).get<config::EigrpInterface::NEXT_HOP_SELF>();
            utils::setToggleValue(nhs, ctx);
            return true;
        }
    }
    return false;
}

bool InterfaceIP_SplitHorizon_Handler(INTERFACE_PARAMS)
{
    switch (segs[0][0])
    {
        case "eigrp"_tok:
        {
            uint16_t as;
            if (!utils::setValue(as, segs[0] >> 1))
                return false;
            auto sh = ctx.configs().get<config::Interface::IP_EIGRP>()
                .emplaceBack(as).get<config::EigrpInterface::SPLIT_HORIZON>();
            utils::setToggleValue(sh, ctx);
            return true;
        }
    }
    return true;
}

bool InterfaceIP_SummaryAddress_Handler(INTERFACE_PARAMS)
{
    switch (segs[0][0])
    {
        case "eigrp"_tok:
        {
            uint16_t as;
            if (!utils::setValue(as, segs[0] >> 1))
                return false;
            auto sum = ctx.configs().get<config::Interface::IP_EIGRP>()
                .emplaceBack(as).get<config::EigrpInterface::SUMMARY_ADDRESS>();
            config::DefType<decltype(sum)::Field>::node tup;
            if (!utils::setTupleElement(std::get<0>(tup), segs[0] >> 2) &&
                !utils::setDoubleTupleElement(std::get<0>(tup), segs[0] >> 2, segs[0] >> 3))
                return false;
            utils::setTupleElement(std::get<1>(tup), segs >> 1 >> 1);
            return utils::setListEntry(sum, ctx, tup);
        }
    }
    return false;
}

bool InterfaceIP_Ospf_SubHandler(INTERFACE_SUB_PARAMS)
{
    auto& ospf = ctx.configs().get<config::Interface::IP_OSPF>().get();
    Context<config::OspfGlobalInterfaceRegistry> newCtx(ctx.terminal, ospf);
    newCtx.negate = ctx.negate;
    newCtx.defaulted = ctx.defaulted;
    return InterfaceIPOspfCommands::execute(newCtx, toks, idx);
}

#define INTERFACE_IP_LIST(X, Y) \
    X(Y, (COMMAND, AddressSet, "address"_tok, P_IPV4)) \
    X(Y, (COMMAND, AddressDhcp, "address"_tok, "dhcp"_tok)) \
    X(Y, (COMMAND, AuthenticationKeyChain, "authentication"_tok, "key-chain"_tok)) \
    X(Y, (COMMAND, AuthenticationMode, "authentication"_tok, "mode"_tok)) \
    X(Y, (COMMAND, BandwidthPercentage, "bandwidth-percentage"_tok)) \
    X(Y, (COMMAND, DampeningChange, "dampening-change"_tok)) \
    X(Y, (COMMAND, DampeningInterval, "dampening-interval"_tok)) \
    X(Y, (COMMAND, HelloInterval, "hello-interval"_tok)) \
    X(Y, (COMMAND, HoldTime, "hold-time"_tok)) \
    X(Y, (COMMAND, Mtu, "mtu"_tok)) \
    X(Y, (COMMAND, NextHopSelf, "next-hop-self"_tok)) \
    X(Y, (SUBPRSR, Ospf, "ospf"_tok)) \
    X(Y, (COMMAND, SplitHorizon, "split-horizon"_tok)) \
    X(Y, (COMMAND, SummaryAddress, "summary-address"_tok))

DEFINE_CMD_MODE(InterfaceIP, config::InterfaceRegistry, INTERFACE_IP_LIST);
}
