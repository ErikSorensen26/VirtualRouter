// RouterEigrpNamedCommands.cpp

#include "RouterEigrpNamedCommands.h"

#include <Global.h>
#include <VirtualRouter.h>

#include "eigrp/core/Eigrp.h"
#include "cli/runtime/CliSession.h"

namespace cli
{
bool RouterEigrpNamed_AddressFamilyIPv4_Handler(EIGRP_PARAMS)
{
    ctx.terminal.isList = true;
    ctx.terminal.isModeChanged = true;
    uint16_t asNum = static_cast<uint16_t>(std::stoi(args[0]));

    if (ctx.currentEigrpNamed->ipv4)
    {
        if (ctx.currentEigrpNamed->ipv4->getAS() != asNum && !ctx.negate)
        {
            ctx.terminal.controller.print("\r\nChanging from AS(" + std::to_string(ctx.currentEigrpNamed->ipv4->getAS()) + ") to AS(" + std::to_string(asNum) + ") is not allowed");
            return false;
        }
        ctx.currentEigrp = ctx.currentEigrpNamed->ipv4;
    }

    core::VirtualRouter* vrf = ctx.terminal.engine.global.getRoutingInstance("default");
    routing::eigrp::EigrpAutonomousSystem* eigrpAs = vrf->getEigrpAutonomousSystem(asNum);

    if (!eigrpAs && !ctx.negate)
    {
        eigrpAs = vrf->addEigrpAutonomousSystem(asNum);
    }
    else if (!eigrpAs && ctx.negate) return true; // Already removed
    
    if (ctx.negate)
    {
        if (!eigrpAs->ipv4) return false;

        delete eigrpAs->ipv4;
        eigrpAs->ipv4 = nullptr;
        eigrpAs->ipv4Named = false;
        ctx.currentEigrpNamed->ipv4 = nullptr;
        if (!eigrpAs->ipv6)
        {
            vrf->removeEigrpAutonomousSystem(asNum);
        }
    }
    else if (!eigrpAs->ipv4)
    {
        eigrpAs->ipv4 = new routing::eigrp::Eigrp(asNum, types::AddressFamily::IPv4, vrf, true);
        eigrpAs->ipv4Named = true;
        ctx.currentEigrpNamed->ipv4 = eigrpAs->ipv4;
        ctx.terminal.changeMode<CliMode::RouterEigrpAddressFamilyV4>(eigrpAs->ipv4, ctx.currentEigrpNamed, nullptr);
    }
    else if (ctx.currentEigrpNamed && ctx.currentEigrpNamed->ipv4)
    {
        ctx.terminal.changeMode<CliMode::RouterEigrpAddressFamilyV4>(ctx.currentEigrpNamed->ipv4, ctx.currentEigrpNamed, nullptr);
    }
    else
    {
        ctx.terminal.controller.print(std::string("\r\n%") + " ERROR: AS(" + std::to_string(asNum) + ") in use by classic router");
        return false;
    }
    return true;
}

bool RouterEigrpNamed_AddressFamilyIPv4Vrf_Handler(EIGRP_PARAMS)
{
    ctx.terminal.isList = true;
    ctx.terminal.isModeChanged = true;
    uint16_t asNum = static_cast<uint16_t>(std::stoi(args[1]));

    auto* vrf = ctx.terminal.engine.global.getRoutingInstance(args[0]);
    if (!vrf)
    {
        ctx.terminal.controller.print(std::string("\r\n%") + "VRF " + args[0] + " does not exist or is not enabled for IPv4");
        return false;
    }
    if (!vrf->enabledAddressFamilies.contains(types::AddressFamily::IPv4))
    {
        ctx.terminal.controller.print(std::string("\r\n%") + "VRF " + args[0] + " exists but is not enalbed for IPv4");
        return false;
    }

    routing::eigrp::EigrpAutonomousSystem* eigrpAs = vrf->getEigrpAutonomousSystem(asNum);

    if (!eigrpAs && !ctx.negate)
    {
        eigrpAs = vrf->addEigrpAutonomousSystem(asNum);
    }
    else if (!eigrpAs && ctx.negate) return true; // Already removed
    
    if (ctx.negate)
    {
        if (!eigrpAs->ipv4) return false;

        delete eigrpAs->ipv4;
        eigrpAs->ipv4 = nullptr;
        eigrpAs->ipv4Named = false;
        ctx.currentEigrpNamed->ipv4 = nullptr;
        if (!eigrpAs->ipv6)
        {
            vrf->removeEigrpAutonomousSystem(asNum);
        }
    }
    else if (!eigrpAs->ipv4)
    {
        eigrpAs->ipv4 = new routing::eigrp::Eigrp(asNum, types::AddressFamily::IPv4, vrf, true);
        eigrpAs->ipv4Named = true;
        ctx.currentEigrpNamed->ipv4 = eigrpAs->ipv4;
        ctx.terminal.changeMode<CliMode::RouterEigrpAddressFamilyV4>(eigrpAs->ipv4, ctx.currentEigrpNamed, nullptr);
    }
    else if (ctx.currentEigrpNamed && ctx.currentEigrpNamed->ipv4)
    {
        ctx.terminal.changeMode<CliMode::RouterEigrpAddressFamilyV4>(ctx.currentEigrpNamed->ipv4, ctx.currentEigrpNamed, nullptr);
    }
    else
    {
        ctx.terminal.controller.print(std::string("\r\n%") + " ERROR: AS(" + std::to_string(asNum) + ") in use by classic router");
        return false;
    }
    return true;
}

bool RouterEigrpNamed_AddressFamilyIPv6_Handler(EIGRP_PARAMS)
{
    ctx.terminal.isList = true;
    ctx.terminal.isModeChanged = true;
    uint16_t asNum = static_cast<uint16_t>(std::stoi(args[0]));

    if (ctx.currentEigrpNamed->ipv6)
    {
        if (ctx.currentEigrpNamed->ipv6->getAS() != asNum && !ctx.negate)
        {
            ctx.terminal.controller.print("\r\nChanging from AS(" + std::to_string(ctx.currentEigrpNamed->ipv6->getAS()) + ") to AS(" + std::to_string(asNum) + ") is not allowed");
            return false;
        }
        ctx.currentEigrp = ctx.currentEigrpNamed->ipv6;
    }

    core::VirtualRouter* vrf = ctx.terminal.engine.global.getRoutingInstance("default");
    routing::eigrp::EigrpAutonomousSystem* eigrpAs = vrf->getEigrpAutonomousSystem(asNum);
    if (!eigrpAs && !ctx.negate)
    {
        eigrpAs = vrf->addEigrpAutonomousSystem(asNum);
    }
    else if (!eigrpAs && ctx.negate) return true; // Already removed
    
    if (ctx.negate)
    {
        if (!eigrpAs->ipv6)
            return false;

        delete eigrpAs->ipv6;
        eigrpAs->ipv6 = nullptr;
        eigrpAs->ipv6Named = false;
        ctx.currentEigrpNamed->ipv6 = nullptr;
        if (!eigrpAs->ipv4)
        {
            vrf->removeEigrpAutonomousSystem(asNum);
        }
    }
    else if (!eigrpAs->ipv6)
    {
        eigrpAs->ipv6 = new routing::eigrp::Eigrp(asNum, types::AddressFamily::IPv6, vrf, true);
        eigrpAs->ipv6Named = true;
        ctx.currentEigrpNamed->ipv6 = eigrpAs->ipv6;
        ctx.terminal.changeMode<CliMode::RouterEigrpAddressFamilyV6>(eigrpAs->ipv6, ctx.currentEigrpNamed, nullptr);
    }
    else if (ctx.currentEigrpNamed && ctx.currentEigrpNamed->ipv6)
    {
        ctx.terminal.changeMode<CliMode::RouterEigrpAddressFamilyV6>(ctx.currentEigrpNamed->ipv6, ctx.currentEigrpNamed, nullptr);
    }
    else
    {
        ctx.terminal.controller.print(std::string("\r\n%") + " ERROR: AS(" + std::to_string(asNum) + ") in use by classic router");
        return false;
    }
    return true;
}

bool RouterEigrpNamed_AddressFamilyIPv6Vrf_Handler(EIGRP_PARAMS)
{
    ctx.terminal.isList = true;
    ctx.terminal.isModeChanged = true;
    uint16_t asNum = static_cast<uint16_t>(std::stoi(args[1]));

    auto* vrf = ctx.terminal.engine.global.getRoutingInstance(args[0]);
    if (!vrf)
    {
        ctx.terminal.controller.print(std::string("\r\n%") + "VRF " + args[0] + " does not exist or is not enabled for IPv6");
        return false;
    }
    if (!vrf->enabledAddressFamilies.contains(types::AddressFamily::IPv6))
    {
        ctx.terminal.controller.print(std::string("\r\n%") + "VRF " + args[0] + " exists but is not enalbed for IPv6");
        return false;
    }

    routing::eigrp::EigrpAutonomousSystem* eigrpAs = vrf->getEigrpAutonomousSystem(asNum);

    if (!eigrpAs && !ctx.negate)
    {
        eigrpAs = vrf->addEigrpAutonomousSystem(asNum);
    }
    else if (!eigrpAs && ctx.negate) return true; // Already removed
    
    if (ctx.negate)
    {
        if (!eigrpAs->ipv6)
            return false;

        delete eigrpAs->ipv6;
        eigrpAs->ipv6 = nullptr;
        eigrpAs->ipv6Named = false;
        ctx.currentEigrpNamed->ipv6 = nullptr;
        if (!eigrpAs->ipv4)
        {
            vrf->removeEigrpAutonomousSystem(asNum);
        }
    }
    else if (!eigrpAs->ipv6)
    {
        eigrpAs->ipv6 = new routing::eigrp::Eigrp(asNum, types::AddressFamily::IPv6, vrf, true);
        eigrpAs->ipv6Named = true;
        ctx.currentEigrpNamed->ipv6 = eigrpAs->ipv6;
        ctx.terminal.changeMode<CliMode::RouterEigrpAddressFamilyV6>(eigrpAs->ipv6, ctx.currentEigrpNamed, nullptr);
    }
    else if (ctx.currentEigrpNamed && ctx.currentEigrpNamed->ipv6)
    {
        ctx.terminal.changeMode<CliMode::RouterEigrpAddressFamilyV6>(ctx.currentEigrpNamed->ipv6, ctx.currentEigrpNamed, nullptr);
    }
    else
    {
        ctx.terminal.controller.print(std::string("\r\n%") + " ERROR: AS(" + std::to_string(asNum) + ") in use by classic router");
        return false;
    }
    return true;
}

bool RouterEigrpNamed_Exit_Handler(EIGRP_PARAMS)
{
    UNUSED(args);
    ctx.terminal.exitMode<CliMode::GlobalConfiguration>(ctx.terminal.engine.global, *ctx.terminal.engine.global.getRoutingInstance("default"));
    return true;
}
}
