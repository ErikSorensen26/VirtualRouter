// GlobalCommands.cpp

#include <VirtualRouter.h>
#include <Global.h>
#include <vector>

#include "GlobalCommands.h"
#include "infrastructure/Arp.h"
#include "interface/configs/InterfaceType.hpp"
#include "cli/runtime/CliSession.h"
#include "cli/runtime/CliUtils.h"
#include "configs/RegistryDefaultTable.hpp"
#include "cli/modes/contexts/InterfaceContext.hpp"
#include "eigrp/core/Eigrp.h"
#include "hardware/HardwareManager.h"
#include "GlobalHelpers.hpp"

namespace cli
{
bool Global_Arp_Handler(GLOBAL_PARAMS)
{
    config::VrfRegistry* vrf = nullptr;
    if (!getVrfConfigs(vrf, ctx))
        return false;
    config::DefType<decltype(vrf->get<config::Vrf::ARP_STATIC_ENTRY>())>::type tup;

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
    return utils::addListEntry(entries, ctx, tup);
}

bool Global_Exit_Handler(GLOBAL_PARAMS)
{
    UNUSED(segs);
    ctx.terminal.exitMode<CliMode::PrivilegedExec>();
    return true;
}

bool Global_SetHostname_Handler(GLOBAL_PARAMS)
{
    auto& g = getGlobalConfigs(ctx.global);
    auto& host = g.get<config::Global::HOSTNAME>();
    return utils::setFieldValue(host, ctx, segs[0] >> 1);
}

bool Global_End_Handler(GLOBAL_PARAMS)
{
    UNUSED(segs);
    ctx.terminal.exitMode<CliMode::PrivilegedExec>();
    return true;
}

bool Global_Interface_Handler(GLOBAL_PARAMS)
{
    config::VrfRegistry* vrf = nullptr;
    if (!getVrfConfigs(vrf, ctx))
        return false;
    config::DefType<decltype(vrf->get<config::Vrf::ARP_STATIC_ENTRY>())>::type tup;
    interface::InterfaceKey ifaceKey;
    if (!utils::extractInterfaceId(segs[0][0], segs[0][1], ifaceKey)) 
        return false;
    if (!ctx.global.getInterface(ifaceKey))
    {
        if (ctx.negate || ctx.defaulted)
        {
            ctx.global.removeInterface(ifaceKey);
            ctx.vrf.getInterfaceManager().remove(ifaceKey);
        }
        else
        {
            const hardware::HwIfaceInfo* info = ctx.terminal.engine.hwManager.getHwInfo(ifaceKey);
            if (!info) return false;
            const hardware::HwIfaceInfo& hwInfo = *info;
            ctx.global.addInterface(ifaceKey, hwInfo);
            ctx.vrf.getInterfaceManager().add(ctx.global.getInterface(ifaceKey), ifaceKey);
        }
    }
    ctx.terminal.changeMode<CliMode::Interface>(*ctx.global.getInterface(ifaceKey));
    return true;
}

bool Global_RouterEIGRP_Handler(GLOBAL_PARAMS)
{
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
    return true;
}

bool Global_RouterOSPF_Handler(GLOBAL_PARAMS)
{
    return true;
}

bool Global_RouterBGP_Handler(GLOBAL_PARAMS)
{
    return true;
}

#undef ROUTER_CONFIG



}
