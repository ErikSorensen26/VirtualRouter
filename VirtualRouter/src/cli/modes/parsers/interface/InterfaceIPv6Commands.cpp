// InterfaceIPv6Commands.cpp

#include <VirtualRouter.h>

#include "InterfaceIPv6Commands.h"
#include "cli/runtime/CliSession.h"
#include "cli/parser/CommandUtils.hpp"
#include "configs/registry/router/EigrpInterfaceRegistry.h"
#include "InterfaceIPv6NDCommands.h"
#include "cli/modes/parsers/interface/InterfaceIPv6OspfCommands.h"
#include <infrastructure/Ndp.h>

#define INTERFACE_PARAMS DEFINE_PARAMS(config::InterfaceRegistry)
#define INTERFACE_SUB_PARAMS DEFINE_SUB_PARAMS(config::InterfaceRegistry)

namespace cli
{
bool InterfaceIPv6_AddressSet_Handler(INTERFACE_PARAMS)
{
    auto& ips = ctx.configs.get<config::Interface::IPV6_ADDRESS>();
    config::DefType<decltype(ips)>::node tup;
    for (const auto& seg : segs)
    {
        switch (seg[0])
        {
            case "address"_tok:
            {
                if (!utils::setTupleElement(std::get<0>(tup), seg >> 1))
                    return false;
                if (auto& addr = std::get<0>(tup); !addr.isGlobalUnicast() && !addr.isLocalUnicast())
                {
		    ctx.terminal.controller.print("\r\n% Invalid local-link address");
                    return false;
                }
                break;
            }
            case "anycast"_tok:
            {
                std::get<2>(tup) = true;
                break;
            }
            case "eui-64"_tok:
            {
                std::get<3>(tup) = true;
                break;
            }
        }
    }

    return utils::setListEntry(ips, ctx, tup);
}

bool InterfaceIPv6_AddressNamed_Handler(INTERFACE_PARAMS)
{
    auto& ips = ctx.configs.get<config::Interface::IPV6_ADDRESS>();
    config::DefType<decltype(ips)>::node tup;
    for (const auto& seg : segs)
    {
        switch (seg[0])
        {
            case "address"_tok:
            {
                if (!utils::setTupleElement(std::get<1>(tup), seg >> 1))
                    return false;
                if (!utils::setTupleElement(std::get<0>(tup), seg >> 2))
                    return false;
                if (auto& addr = std::get<0>(tup); !addr.isGlobalUnicast() && !addr.isLocalUnicast())
                {
		    ctx.terminal.controller.print("\r\n% Invalid local-link address");
                    return false;
                }
                break;
            }
            case "anycast"_tok:
            {
                std::get<2>(tup) = true;
                break;
            }
            case "eui-64"_tok:
            {
                std::get<3>(tup) = true;
                break;
            }
        }
    }

    return utils::setListEntry(ips, ctx, tup);
}

bool InterfaceIPv6_AddressLinkLocal_Handler(INTERFACE_PARAMS)
{
    types::IPv6Address ll;
    if (!utils::setValue(ll, segs[0] >> 1))
        return false;
    if (!ll.isLocalLink())
    {
        ctx.terminal.controller.print("\r\n% Invalid link-local address");
        return false;
    }
    ctx.configs.get<config::Interface::IPV6_ADDRESS_LL>().set(ll);
    return true;
}

bool InterfaceIPv6_AddressAuto_Handler(INTERFACE_PARAMS)
{
    auto& autocfg = ctx.configs.get<config::Interface::IPV6_ADDRESS_AUTOCONFIG>();
    auto& autodf = ctx.configs.get<config::Interface::IPV6_ADDRESS_AUTOCONFIG_DEFAULT>();
    utils::setToggleValue(autocfg, ctx);
    utils::setFieldValueWithFallback(autodf, ctx, segs >> 0 >> 0);
    return true;
}

bool InterfaceIPv6_AuthenticationKeyChain_Handler(INTERFACE_PARAMS)
{
    switch (segs[0][0])
    {
        case "eigrp"_tok:
        {
            uint16_t as;
            if (!utils::setValue(as, segs[0] >> 1))
                return false;
            auto& authKey = ctx.configs.get<config::Interface::IPV6_EIGRP>().emplaceBack(as).get<config::EigrpInterface::AUTHENTICATION_KEYCHAIN>();
            return utils::setFieldValue(authKey, ctx, segs[0] >> 2);
        }
    }
    return true;
}

bool InterfaceIPv6_AuthenticationMode_Handler(INTERFACE_PARAMS)
{
    switch (segs[0][0])
    {
        case "eigrp"_tok:
        {
            uint16_t as;
            if (!utils::setValue(as, segs[0] >> 1))
                return false;
            auto& mode = ctx.configs.get<config::Interface::IPV6_EIGRP>()
                .emplaceBack(as).get<config::EigrpInterface::AUTHENTICATION_MODE>();

            if (utils::handleValueReset(mode, ctx))
                return true;

            switch (segs[1][0])
            {
                case "md5"_tok:
                {
                    mode.set(config::eigrp::AuthType::MD5);
                    return true;
                }
            }
        }
    }
    return false;
}

bool InterfaceIPv6_BandwidthPercent_Handler(INTERFACE_PARAMS)
{
    switch (segs[0][0])
    {
        case "eigrp"_tok:
        {
            uint16_t as;
            if (!utils::setValue(as, segs[0] >> 1))
                return false;
            auto& eigrpBw = ctx.configs.get<config::Interface::IPV6_EIGRP>()
                .emplaceBack(as).get<config::EigrpInterface::BANDWIDTH_PERCENTAGE>();
            return utils::setFieldValue(eigrpBw, ctx, segs[0] >> 2);
        }
    }
    return false;
}

bool InterfaceIPv6_DampeningChange_Handler(INTERFACE_PARAMS)
{
    switch (segs[0][0])
    {
        case "eigrp"_tok:
        {
            uint16_t as;
            if (!utils::setValue(as, segs[0] >> 1))
                return false;
            auto& eigrp = ctx.configs.get<config::Interface::IPV6_EIGRP>().emplaceBack(as);
            auto& dampChange = eigrp.get<config::EigrpInterface::DAMPENING_CHANGE>();
            auto& dampChangePercent = eigrp.get<config::EigrpInterface::DAMPENING_CHANGE_PERCENT>();
            utils::setToggleValue(dampChange, ctx);
            utils::setFieldValue(dampChangePercent, ctx, segs[0] >> 2);
            return true;
        }
    }
    return false;
}

bool InterfaceIPv6_DampeningInterval_Handler(INTERFACE_PARAMS)
{
    switch (segs[0][0])
    {
        case "eigrp"_tok:
        {
            uint16_t as;
            if (!utils::setValue(as, segs[0] >> 1))
                return false;
            auto& eigrp = ctx.configs.get<config::Interface::IPV6_EIGRP>().emplaceBack(as);
            auto& dampInterval = eigrp.get<config::EigrpInterface::DAMPENING_INTERVAL>();
            auto& dampIntervalTime = eigrp.get<config::EigrpInterface::DAMPENING_INTERVAL_TIME>();
            utils::setToggleValue(dampInterval, ctx);
            utils::setFieldValue(dampIntervalTime, ctx, segs[0] >> 2);
            return true;
        }
    }
    return false;
}

bool InterfaceIPv6_EigrpAs_Handler(INTERFACE_PARAMS)
{
    auto& eigrpList = ctx.configs.get<config::Interface::IPV6_EIGRP_ENABLED>();
    uint16_t as;
    if (!utils::setValue(as, segs[0] >> 1))
        return false;
    return utils::setListEntry(eigrpList, ctx, as);
}

bool InterfaceIPv6_HelloInterval_Handler(INTERFACE_PARAMS)
{
    switch (segs[0][0])
    {
        case "eigrp"_tok:
        {
            uint16_t as;
            if (!utils::setValue(as, segs[0] >> 1))
                return false;
            auto& helloTime = ctx.configs.get<config::Interface::IPV6_EIGRP>()
                .emplaceBack(as).get<config::EigrpInterface::HELLO_INTERVAL>();
            return utils::setFieldValue(helloTime, ctx, segs[0] >> 2);
        }
    }
    return false;
}

bool InterfaceIPv6_HoldTime_Handler(INTERFACE_PARAMS)
{
    switch (segs[0][0])
    {
        case "eigrp"_tok:
        {
            uint16_t as;
            if (!utils::setValue(as, segs[0] >> 1))
                return false;
            auto& holdTime = ctx.configs.get<config::Interface::IPV6_EIGRP>()
                .emplaceBack(as).get<config::EigrpInterface::HOLD_TIME>();
            return utils::setFieldValue(holdTime, ctx, segs[0] >> 2);
        }
    }
    return false;
}

bool InterfaceIPv6_Mtu_Handler(INTERFACE_PARAMS)
{
    auto& mtu = ctx.configs.get<config::Interface::IPV6_MTU>();
    return utils::setFieldValue(mtu, ctx, segs[0] >> 1);
}

bool InterfaceIPv6_NextHopSelf_Handler(INTERFACE_PARAMS)
{
    switch (segs[0][0])
    {
        case "eigrp"_tok:
        {
            uint16_t as; 
            if (!utils::setValue(as, segs[0] >> 1))
                return false;
            auto& nhs = ctx.configs.get<config::Interface::IPV6_EIGRP>()
                .emplaceBack(as).get<config::EigrpInterface::NEXT_HOP_SELF>();
            utils::setToggleValue(nhs, ctx);
            return true;
        }
    }
    return false;
}

bool InterfaceIPv6_NdpRedirects_Handler(INTERFACE_PARAMS)
{
    UNUSED(segs);
    auto& redirects = ctx.configs.get<config::Interface::IPV6_REDIRECTS>();
    utils::setToggleValue(redirects, ctx);
    return true;
}

bool InterfaceIPv6_SplitHorizon_Handler(INTERFACE_PARAMS)
{
    switch (segs[0][0])
    {
        case "eigrp"_tok:
        {
            uint16_t as; 
            if (!utils::setValue(as, segs[0] >> 1))
                return false;
            auto& sh = ctx.configs.get<config::Interface::IPV6_EIGRP>()
                .emplaceBack(as).get<config::EigrpInterface::SPLIT_HORIZON>();
            utils::setToggleValue(sh, ctx);
            return true;
        }
    }
    return true;
}

bool InterfaceIPv6_SummaryAddress_Handler(INTERFACE_PARAMS)
{
    switch (segs[0][0])
    {
        case "eigrp"_tok:
        {
            uint16_t as; 
            if (!utils::setValue(as, segs[0] >> 1))
                return false;
            auto& sum = ctx.configs.get<config::Interface::IPV6_EIGRP>()
                .emplaceBack(as).get<config::EigrpInterface::SUMMARY_ADDRESS>();
            config::DefType<decltype(sum)>::node tup;
            if (!utils::setTupleElement(std::get<0>(tup), segs[0] >> 2))
                return false;
            utils::setTupleElement(std::get<1>(tup), segs >> 1 >> 1);
            return utils::setListEntry(sum, ctx, tup);
        }
    }
    return false;
}

bool InterfaceIPv6_ND_SubHandler(INTERFACE_SUB_PARAMS)
{
    auto& nd = ctx.configs.get<config::Interface::IPV6_ND>().get();
    Context<config::NdpRegistry> newCtx(ctx.terminal, nd);
    newCtx.negate = ctx.negate;
    newCtx.defaulted = ctx.defaulted;
    return InterfaceIPv6NDCommands::execute(newCtx, toks, idx);
}

bool InterfaceIPv6_Ospf_SubHandler(INTERFACE_SUB_PARAMS)
{
    auto& ospf = ctx.configs.get<config::Interface::IPV6_OSPF>().get();
    Context<config::OspfInterfaceBaseRegistry> newCtx(ctx.terminal, ospf);
    newCtx.negate = ctx.negate;
    newCtx.defaulted = ctx.defaulted;
    return InterfaceIPv6OspfCommands::execute(newCtx, toks, idx);
}
}
