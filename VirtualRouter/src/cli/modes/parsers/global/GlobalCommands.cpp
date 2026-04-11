// GlobalCommands.cpp

#include <VirtualRouter.h>
#include <Global.h>
#include <vector>

#include "GlobalCommands.h"
#include "interface/configs/InterfaceType.hpp"
#include "cli/parser/CommandUtils.hpp"
#include "configs/registry/router/EigrpRegistry.h"
#include "configs/RegistryDefaultTable.hpp"
#include "GlobalIPCommands.h"
#include "GlobalIPv6Commands.h"
#include "GlobalHelpers.hpp"

#define GLOBAL_PARAMS DEFINE_PARAMS(config::GlobalRegistry)
#define GLOBAL_SUB_PARAMS DEFINE_SUB_PARAMS(config::GlobalRegistry)

namespace cli
{
bool Global_Arp_Handler(GLOBAL_PARAMS)
{
    config::VrfRegistry* vrf = nullptr;
    if (!getVrfConfigs(vrf, ctx))
        return false;
    config::DefType<decltype(vrf->get<config::Vrf::ARP_STATIC_ENTRY>())>::node tup;

    for (const auto& seg : segs)
    {
        switch (seg[0])
        {
            case "arp"_tok:
            {
                if (!utils::setTupleElement(std::get<1>(tup), seg >> 1))
                    return false;
                if (!ctx.negate && ctx.defaulted && !utils::setTupleElement(std::get<2>(tup), seg >> 2))
                    return false;
                break;
            }
            case "vrf"_tok:
            {
                if (!getVrfConfigs(vrf, ctx, seg[0]))
                    return false;
                if (!utils::setTupleElement(std::get<1>(tup), seg >> 2))
                    return false;
                if (!ctx.negate && ctx.defaulted && !utils::setTupleElement(std::get<2>(tup), seg >> 3))
                    return false;
                break;
            }
            default: return false;
        }
    }

    auto& entries = vrf->get<config::Vrf::ARP_STATIC_ENTRY>();
    return utils::setListEntry(entries, ctx, tup);
}

bool Global_Exit_Handler(GLOBAL_PARAMS)
{
    UNUSED(segs);
    ctx.terminal.changeMode<CliMode::PrivilegedExec>(ctx.configs);
    return true;
}

bool Global_SetHostname_Handler(GLOBAL_PARAMS)
{
    auto& host = ctx.configs.get<config::Global::HOSTNAME>();
    return utils::setFieldValue(host, ctx, segs[0] >> 1);
}

bool Global_End_Handler(GLOBAL_PARAMS)
{
    UNUSED(segs);
    ctx.terminal.changeMode<CliMode::PrivilegedExec>(ctx.configs);
    return true;
}

bool Global_Interface_Handler(GLOBAL_PARAMS)
{
    auto& interfaceCfgs = ctx.configs.get<config::Global::INTERFACE>();
    interface::InterfaceKey ifaceKey;
    if (!utils::extractInterfaceId(segs[0][0], segs[0][1], ifaceKey)) 
        return false;
    utils::setOwnedField(interfaceCfgs, ctx, ifaceKey);
    ctx.terminal.changeMode<CliMode::Interface>(interfaceCfgs.get().at(ifaceKey));
    return true;
}

bool Global_RouterEIGRP_Handler(GLOBAL_PARAMS)
{
    config::VrfRegistry* vrf;
    if (!getVrfConfigs(vrf, ctx))
        return false;
    std::string_view name = segs[0][1];
    uint16_t id;
    if (utils::stouint(id, name))
    {
        auto& eigrpList = vrf->get<config::Vrf::ROUTER_EIGRP_V4>();
        utils::setOwnedField(eigrpList, ctx, id);
        // TODO handle vrf
        return ctx.terminal.changeMode<CliMode::RouterEigrpClassicV4>(eigrpList.get().at(id));
    }
    else
    {
        // TODO named
    }
    


/*

{
    ctx.terminal.isList = true;
    auto* vrf = ctx.currentEigrp->routingInstance->getGlobal().getRoutingInstance(args[0]);
    if (!vrf)
    {
        ctx.terminal.controller.print(std::string("\r\n%") + "VRF" + args[0] + " does not exist or is not enalbed for IPv4");
        return false;
    }
    if (!vrf->enabledAddressFamilies.contains(types::AddressFamily::IPv4))
    {
        ctx.terminal.controller.print(std::string("\r\n%") + "VRF" + args[0] + " does exist but is not enabled for IPv4");
        return false;
    }

    uint16_t asNum = args.size() == 3 ? static_cast<uint16_t>(std::stoi(args[2])) : ctx.currentEigrp->getAS();

    routing::eigrp::EigrpAutonomousSystem* as = vrf->getEigrpAutonomousSystem(asNum);
    if (!ctx.negate)
    {
        if (as)
        {
            if (as->ipv4Named)
            {
                ctx.terminal.controller.print(std::string("\r\n%") + " ERROR: AS(" + std::to_string(asNum) + ") used by name mode");
                return false; // AS used in named mode.
            }
        }
        else
        {
            as = vrf->addEigrpAutonomousSystem(asNum);
        }

        if (!as->ipv4)
        {
            as->ipv4 = new routing::eigrp::Eigrp(asNum, types::AddressFamily::IPv4, vrf);
        }

        ctx.terminal.changeMode<CliMode::RouterEigrpClassicVRF>(as->ipv4, nullptr, nullptr, ctx.currentEigrp);
    }
    else
    {
        if (as)
        {
            if (!as->ipv4Named && as->ipv4)
            {
                delete as->ipv4;
                as->ipv4 = nullptr;
                if (!as->ipv6 && !as->ipv6Named)
                {
                    vrf->removeEigrpAutonomousSystem(asNum);
                }
            }
        }
    }
    return true;
}

    std::string id = args[0];

    if (utils::isNumber(id))
    {
        uint16_t asNum = static_cast<uint16_t>(std::stoi(id));
        routing::eigrp::EigrpAutonomousSystem* as = ctx.vrf.getEigrpAutonomousSystem(asNum);
        if (ctx.negate || ctx.defaulted)
        {
            if (as)
            {
                if (!as->ipv4Named && as->ipv4)
                {
                    delete as->ipv4;
                    as->ipv4 = nullptr;
                    if (!as->ipv6)
                    {
                        ctx.vrf.removeEigrpAutonomousSystem(asNum);
                    }
                }
            }
        }
        else
        {
            if (as)
            {
                if (as->ipv4Named)
                {
                    ctx.terminal.controller.print(std::string("\r\n%" + std::string(" ERROR: AS(" + id + ") used by named mode")));
                    return false; // AS used in named mode. }
                }
            }
            else
            {
                as = ctx.vrf.addEigrpAutonomousSystem(asNum);
            }
            if (!as->ipv4)
            {
                as->ipv4 = new routing::eigrp::Eigrp(asNum, types::AddressFamily::IPv4, ctx.global.getRoutingInstance("default"));
            }
            ctx.terminal.changeMode<CliMode::RouterEigrpClassicV4>(as->ipv4, nullptr, nullptr);
        }
}
    else
    {
        if (ctx.negate || ctx.defaulted)
        {
            ctx.vrf.removeEigrpNamed(id);
        }
        else
        {
            if (!ctx.vrf.getEigrpNamed(id))
            {
                ctx.vrf.addEigrpNamed(id);
            }
            ctx.terminal.changeMode<CliMode::RouterEigrpNamed>(nullptr, ctx.vrf.getEigrpNamed(id), nullptr);
        }
    }
    */
    return false;
}

bool Global_RouterOSPF_Handler(GLOBAL_PARAMS)
{
    return true;
}

bool Global_RouterBGP_Handler(GLOBAL_PARAMS)
{
    return true;
}

bool Global_IP_SubHandler(GLOBAL_SUB_PARAMS)
{
    return GlobalIPCommands::execute(ctx, toks, idx);
}

bool Global_IPv6_SubHandler(GLOBAL_SUB_PARAMS)
{
    return GlobalIPv6Commands::execute(ctx, toks, idx);
}
}

#undef GLOBAL_PARAMS
#undef GLOBAL_SUB_PARAMS
