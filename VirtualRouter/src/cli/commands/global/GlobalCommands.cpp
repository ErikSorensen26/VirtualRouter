// GlobalCommands.cpp

#include <VirtualRouter.h>
#include <Global.h>
#include "configs/registry/global/GlobalRegistry.h"
#include <vector>

#include "GlobalCommands.h"
#include "cli/grammar/CliModeParser.hpp"
#include "cli/grammar/CommandUtils.hpp"
#include "configs/RegistryDefaultTable.hpp"
#include "configs/FieldAccessor.hpp"
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
    config::DefType<config::VrfRegistry::FieldTypeAt<config::Vrf::ARP_STATIC_ENTRY>>::node tup;

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

    auto entries = vrf->get<config::Vrf::ARP_STATIC_ENTRY>();
    return utils::setListEntry(entries, ctx, tup);
}

bool Global_Exit_Handler(GLOBAL_PARAMS)
{
    UNUSED(segs);
    return ctx.terminal.popMode();
}

bool Global_SetHostname_Handler(GLOBAL_PARAMS)
{
    auto host = ctx.configs().get<config::Global::HOSTNAME>();
    return utils::setFieldValue(host, ctx, segs[0] >> 1);
}

bool Global_End_Handler(GLOBAL_PARAMS)
{
    UNUSED(segs);
    return ctx.terminal.resetAndChangeMode<CliMode::PrivilegedExec>(ctx.terminal.engine.global.getConfigs());
}

bool Global_Interface_Handler(GLOBAL_PARAMS)
{
    auto interfaceCfgs = ctx.configs().get<config::Global::INTERFACE>();
    interface::InterfaceKey ifaceKey;
    if (!utils::extractInterfaceId(segs[0][0], segs[0][1], ifaceKey)) 
        return false;
    utils::setOwnedField(interfaceCfgs, ctx, ifaceKey);
    ctx.terminal.changeMode<CliMode::Interface>(*interfaceCfgs.get().at(ifaceKey));
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
        auto eigrpList = vrf->get<config::Vrf::ROUTER_EIGRP_V4>();
        if (ctx.negate || ctx.defaulted)
        {
            eigrpList.erase(id);
            return true;
        }
        // TODO handle vrf
        return ctx.terminal.changeMode<CliMode::RouterEigrpClassicV4>(eigrpList.emplaceBack(id));
    }
    else
    {
        auto namedList = ctx.configs().get<config::Global::ROUTER_EIGRP_NAMED>();
        std::string nameStr(name);
        if (ctx.negate || ctx.defaulted)
        {
            namedList.erase(nameStr);
            return true;
        }
        return ctx.terminal.changeMode<CliMode::RouterEigrpNamed>(namedList.emplaceBack(nameStr));
    }
}

bool Global_RouterOSPF_Handler(GLOBAL_PARAMS)
{
    // TODO
    return false;
}

bool Global_RouterBGP_Handler(GLOBAL_PARAMS)
{
    // TODO
    return false;
}

bool Global_IP_SubHandler(GLOBAL_SUB_PARAMS)
{
    return GlobalIPCommands::execute(ctx, toks, idx);
}

bool Global_IPv6_SubHandler(GLOBAL_SUB_PARAMS)
{
    return GlobalIPv6Commands::execute(ctx, toks, idx);
}

#define GLOBAL_LIST(X, Y) \
    X(Y, (COMMAND, Arp, "arp"_tok)) \
    X(Y, (COMMAND, Exit, "exit"_tok)) \
    X(Y, (COMMAND, SetHostname, "hostname"_tok)) \
    X(Y, (COMMAND, End, "end"_tok)) \
    X(Y, (SUBPRSR, IP, "ip"_tok)) \
    X(Y, (SUBPRSR, IPv6, "ipv6"_tok)) \
    X(Y, (COMMAND, Interface, "interface"_tok)) \
    X(Y, (COMMAND, RouterEIGRP, "router"_tok, "eigrp"_tok)) \
    X(Y, (COMMAND, RouterOSPF, "router"_tok, "ospf"_tok)) \
    X(Y, (COMMAND, RouterBGP, "router"_tok, "bgp"_tok))

DEFINE_CMD_MODE(Global, config::GlobalRegistry, GLOBAL_LIST);
}
