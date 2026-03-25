// InterfaceIPv6Commands.cpp

#include <VirtualRouter.h>

#include "InterfaceIPv6Commands.h"
#include "cli/runtime/CliSession.h"
#include "cli/runtime/CliUtils.h"
#include "eigrp/core/Eigrp.h"
#include "eigrp/interface/EigrpInterface.h"
#include "interface/Interface.h"
#include "configs/registry/router/EigrpInterfaceRegistry.h"
#include <infrastructure/Ndp.h>

namespace cli
{
bool InterfaceIPv6_AddressSet_Handler(INTERFACE_PARAMS)
{
    if (ctx.negate && args.empty())
    {
	ctx.currentInterface.removeAllIPv6();
	return true;
    }

    if (args.empty()) return false;

    if (utils::isIPv6Address(args[0]))
    {
	types::IPv6Address ipv6Address; utils::extractIPv6Address(args[0], ipv6Address);
	if (!ctx.negate)
	{
	    if (!ipv6Address.isLocalLink())
	    {
			ctx.terminal.controller.print(std::string("\r\n%") + std::string(" Invalid local-link address"));
			return false;
	    }

	    ctx.currentInterface.setIPv6(types::IPv6Prefix(ipv6Address.addr, 64), true);
	}
	else
	{
	    ctx.currentInterface.removeIPv6();
	}
    }
    else if (utils::isIPv6AddressWithMask(args[0]))
    {
	types::IPv6Prefix pfx;
	if (utils::extractIPv6Prefix(args[0], pfx))
	{
	    types::IPv6Address ipv6Address(pfx.addr);
	    //TODO anycast
	    if (!ctx.negate)
	    {
		ctx.currentInterface.setIPv6(pfx, false);
	    }
	    else
	    {
		ctx.currentInterface.removeIPv6(&pfx);
	    }
	}
    }
    else if (args[0] == "dhcp")
    {
	if (!ctx.currentInterface.dhcp)
	{
	    //ctx.currentInterface->dhcp = new services::dhcp::Dhcpv6Client();
	}
	else
	{
	    //delete ctx.currentInterface->dhcp;
	}
    }
    return true;
}

bool InterfaceIPv6_AuthenticationKeyChain_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
        uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
        auto cfg = ctx.currentInterface.getEigrpConfig(as);
        if (ctx.negate)
        {
            cfg->get<config::EigrpInterface::AUTHENTICATION_KEYCHAIN>().unset();
            cfg->get<config::EigrpInterface::AUTHENTICATION_MODE>().set(config::eigrp::AuthType::NONE);
        }
        else
        {
            cfg->get<config::EigrpInterface::AUTHENTICATION_KEYCHAIN>().set(args[2]);
            cfg->get<config::EigrpInterface::AUTHENTICATION_MODE>().set(config::eigrp::AuthType::MD5);
        }
    }
    return true;
}

bool InterfaceIPv6_AuthenticationMode_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
        uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
        auto cfg = ctx.currentInterface.getEigrpConfig(as);
        if (ctx.negate)
        {
            cfg->get<config::EigrpInterface::AUTHENTICATION_MODE>().set(config::eigrp::AuthType::NONE);
            cfg->get<config::EigrpInterface::AUTHENTICATION_KEYCHAIN>().unset();
        }
        else
        {
            cfg->get<config::EigrpInterface::AUTHENTICATION_MODE>().set(config::eigrp::AuthType::MD5);
        }
    }
    return true;
}

bool InterfaceIPv6_BandwidthPercent_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
        uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
        ctx.currentInterface.getEigrpConfig(as)->get<config::EigrpInterface::BANDWIDTH_PERCENTAGE>().set(
            ctx.negate ? 50u : static_cast<uint32_t>(std::stoul(args[2])));
    }
    return true;
}

bool InterfaceIPv6_DampeningChange_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
        uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
        auto cfg = ctx.currentInterface.getEigrpConfig(as);
        if (ctx.negate)
        {
            cfg->get<config::EigrpInterface::DAMPENING_CHANGE>().set(false);
            cfg->get<config::EigrpInterface::DAMPENING_CHANGE_PERCENT>().set(static_cast<uint8_t>(1));
        }
        else
        {
            cfg->get<config::EigrpInterface::DAMPENING_CHANGE>().set(true);
            cfg->get<config::EigrpInterface::DAMPENING_CHANGE_PERCENT>().set(
                static_cast<uint8_t>(std::stoul(args[2])));
        }
    }
    return true;
}

bool InterfaceIPv6_DampeningInterval_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
        uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
        auto cfg = ctx.currentInterface.getEigrpConfig(as);
        if (ctx.negate)
        {
            cfg->get<config::EigrpInterface::DAMPENING_INTERVAL>().set(false);
            cfg->get<config::EigrpInterface::DAMPENING_INTERVAL_TIME>().set(static_cast<uint16_t>(5));
        }
        else
        {
            cfg->get<config::EigrpInterface::DAMPENING_INTERVAL>().set(true);
            cfg->get<config::EigrpInterface::DAMPENING_INTERVAL_TIME>().set(
                static_cast<uint16_t>(std::stoul(args[2])));
        }
    }
    return true;
}

bool InterfaceIPv6_EigrpAs_Handler(INTERFACE_PARAMS)
{
    ctx.terminal.isList = true;
    uint32_t as = static_cast<uint32_t>(std::stoi(args[0]));
    auto eigrpAs = ctx.currentInterface.getVRF()->getEigrpAutonomousSystem(as);
    if (!ctx.negate)
    {
	if (eigrpAs && eigrpAs->ipv6)
	{
	    eigrpAs->ipv6->getIfaceMgr().createInterface(&ctx.currentInterface);
	}
	{
	    std::unique_lock<std::shared_mutex> lock(ctx.currentInterface.configs.ipMutex);
	    ctx.currentInterface.configs.eigrp.ipv6AutonomousSystems.insert(as);
	}
    }
    else
    {
	ctx.currentInterface.configs.eigrp.ipv6AutonomousSystems.erase(static_cast<uint32_t>(std::stoul(args[2])));
    }

    if (eigrpAs && eigrpAs->ipv6)
    {
	    eigrpAs->ipv6->refreshInterfaceList();
    }
    return true;
}

bool InterfaceIPv6_HelloInterval_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
        uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
        ctx.currentInterface.getEigrpConfig(as)->get<config::EigrpInterface::HELLO_INTERVAL>().set(
            ctx.negate ? static_cast<uint16_t>(5) : static_cast<uint16_t>(std::stoul(args[2])));
    }
    return true;
}

bool InterfaceIPv6_HoldTime_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
        uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
        ctx.currentInterface.getEigrpConfig(as)->get<config::EigrpInterface::HOLD_TIME>().set(
            ctx.negate ? static_cast<uint16_t>(15) : static_cast<uint16_t>(std::stoul(args[2])));
    }
    return true;
}

bool InterfaceIPv6_Mtu_Handler(INTERFACE_PARAMS)
{
    ctx.currentInterface.configs.ipv6.mtu.store(ctx.negate ? 1500 : static_cast<uint16_t>(std::stoul(args[0]), std::memory_order_release));
    return true;
}

bool InterfaceIPv6_NextHopSelf_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
        uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
        ctx.currentInterface.getEigrpConfig(as)->get<config::EigrpInterface::NEXT_HOP_SELF>().set(!ctx.negate);
    }
    return true;
}

bool InterfaceIPv6_NdpRedirects_Handler(INTERFACE_PARAMS)
{
    UNUSED(args);
    ctx.currentInterface.ndp.configs.redirects.store(!ctx.negate, std::memory_order_release);
    return true;
}

bool InterfaceIPv6_SplitHorizon_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
        uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
        ctx.currentInterface.getEigrpConfig(as)->get<config::EigrpInterface::SPLIT_HORIZON>().set(!ctx.negate);
    }
    return true;
}

bool InterfaceIPv6_SummaryAddress_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
        uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
        types::IPv6Prefix _pfx; utils::extractIPv6Prefix(args[2], _pfx);
        types::IPPrefix prefix(_pfx.addr, _pfx.prefixLength);
        auto ifaceIt = ctx.currentInterface.eigrpInterfaceList.find(as);
        if (ifaceIt != ctx.currentInterface.eigrpInterfaceList.end() && ifaceIt->second.IPv6)
        {
            if (ctx.negate)
                ifaceIt->second.IPv6->getAggregator().withdrawSummary(prefix);
            else
                ifaceIt->second.IPv6->getAggregator().installSummary(prefix);
        }
        else
        {
            types::IPAddress netAddr = prefix;
            uint8_t plen = prefix.prefixLength;
            ctx.currentInterface.getEigrpConfig(as)->get<config::EigrpInterface::SUMMARY_ADDRESS>().withWrite(
                [&](std::vector<std::tuple<types::IPAddress, uint8_t>>& v) {
                    auto it = std::find_if(v.begin(), v.end(), [&](const auto& t) {
                        return std::get<0>(t) == netAddr && std::get<1>(t) == plen;
                    });
                    if (!ctx.negate) { if (it == v.end()) v.emplace_back(netAddr, plen); }
                    else { if (it != v.end()) v.erase(it); }
                });
        }
    }
    return true;
}
}
