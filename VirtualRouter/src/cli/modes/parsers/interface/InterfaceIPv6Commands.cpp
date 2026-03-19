// InterfaceIPv6Commands.cpp

#include <VirtualRouter.h>

#include "InterfaceIPv6Commands.h"
#include "InterfaceHelpers.hpp"
#include "cli/runtime/CliSession.h"
#include "cli/runtime/CliUtils.h"
#include "eigrp/core/Eigrp.h"
#include "eigrp/interface/EigrpInterface.h"
#include <infrastructure/Ndp.h>

namespace Cli
{
bool InterfaceIPv6_AddressSet_Handler(INTERFACE_PARAMS)
{
    if (ctx.negate && args.empty())
    {
	ctx.currentInterface.removeAllIPv6();
	return true;
    }

    if (args.empty()) return false;

    if (CliUtils::isIPv6Address(args[0]))
    {
	IPv6Address ipv6Address; CliUtils::extractIPv6Address(args[0], ipv6Address);
	if (!ctx.negate)
	{
	    if (!ipv6Address.isLocalLink())
	    {
			ctx.terminal.iConsole->print(std::string("\r\n%") + std::string(" Invalid local-link address"));
			return false;
	    }

	    ctx.currentInterface.setIPv6(IPv6Prefix(ipv6Address.addr, 64), true);
	}
	else
	{
	    ctx.currentInterface.removeIPv6();
	}
    }
    else if (CliUtils::isIPv6AddressWithMask(args[0]))
    {
	IPv6Prefix _pfx;
	if (CliUtils::extractIPv6Prefix(args[0], _pfx))
	{
	    IPv6Address ipv6Address(_pfx.addr);
	    uint8_t mask = _pfx.prefixLength;
	    //TODO anycast
	    if (!ctx.negate)
	    {
			ctx.currentInterface.setIPv6(_pfx, false);
	    }
	    else
	    {
			ctx.currentInterface.removeIPv6(&_pfx);
	    }
	}
    }
    else if (args[0] == "dhcp")
    {
	if (!ctx.currentInterface.dhcp)
	{
	    //ctx.currentInterface->dhcp = new Protocol::Dhcpv6Client();
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
	auto* eigrpConfig = ctx.currentInterface.getEigrpConfig(as, AddressFamily::IPv6, ctx.negate);
	if (eigrpConfig)
	{
	    if (ctx.negate)
	    {
		eigrpConfig->auth.fullyEnabled.store(false, std::memory_order_release);
		eigrpConfig->auth.key = uint32_t{};
		refreshEigrpConfig(ctx.currentInterface, as, AddressFamily::IPv4, eigrpConfig);
	    }
	    else
	    {
		eigrpConfig->auth.key = args[2];
		if (eigrpConfig->auth.authType != EigrpConfigs::AuthType::NONE)
		    eigrpConfig->auth.fullyEnabled.store(true, std::memory_order_release);
	    }
	}
    }
    return true;
}

bool InterfaceIPv6_AuthenticationMode_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
	uint32_t as = static_cast<uint32_t>(std::stoi(args[1]));
	auto* eigrpConfig = ctx.currentInterface.getEigrpConfig(as, AddressFamily::IPv6, ctx.negate);
	if (eigrpConfig)
	{
	    if (ctx.negate)
	    {
		eigrpConfig->auth.fullyEnabled.store(false, std::memory_order_relaxed);
		eigrpConfig->auth.authType = EigrpConfigs::AuthType::NONE;
		refreshEigrpConfig(ctx.currentInterface, as, AddressFamily::IPv6, eigrpConfig);
	    }
	    else
	    {
		eigrpConfig->auth.fullyEnabled.store(false, std::memory_order_relaxed);
		eigrpConfig->auth.authType = EigrpConfigs::AuthType::MD5;
		if (std::holds_alternative<std::string>(eigrpConfig->auth.key) && !std::get<std::string>(eigrpConfig->auth.key).empty())
		{
		    eigrpConfig->auth.fullyEnabled.store(true, std::memory_order_release);
		}
	    }
	}
    }
    return true;
}

bool InterfaceIPv6_BandwidthPercent_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
	uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
	auto* eigrpConfig = ctx.currentInterface.getEigrpConfig(as, AddressFamily::IPv6, ctx.negate);
	if (ctx.negate)
	{
	    eigrpConfig->bandwidthPercentage.store(50, std::memory_order_release);
	    refreshEigrpConfig(ctx.currentInterface, as, AddressFamily::IPv6, eigrpConfig);
	}
	else
	{
	    eigrpConfig->bandwidthPercentage.store(static_cast<uint32_t>(std::stoul(args[2]), std::memory_order_release));
	}
    }
    return true;
}

bool InterfaceIPv6_DampeningChange_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
	uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
	auto eigrpConfig = ctx.currentInterface.getEigrpConfig(as, AddressFamily::IPv6, ctx.negate);
	if (eigrpConfig)
	{
	    if (ctx.negate)
	    {
		eigrpConfig->dampeningChange.store(1, std::memory_order_release);
		refreshEigrpConfig(ctx.currentInterface, as, AddressFamily::IPv6, eigrpConfig);
	    }
	    else
	    {
		eigrpConfig->dampeningChange.store(static_cast<uint32_t>(std::stoul(args[2]), std::memory_order_relaxed));
	    }
	}
    }
    return true;
}

bool InterfaceIPv6_DampeningInterval_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
	uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
	auto eigrpConfig = ctx.currentInterface.getEigrpConfig(as, AddressFamily::IPv6, ctx.negate);
	if (eigrpConfig)
	{
	    if (ctx.negate)
	    {
		eigrpConfig->dampeningInterval.store(5, std::memory_order_release);
		refreshEigrpConfig(ctx.currentInterface, as, AddressFamily::IPv6, eigrpConfig);
	    }
	    else
	    {
		eigrpConfig->dampeningInterval.store(static_cast<uint32_t>(std::stoul(args[2]), std::memory_order_relaxed));
	    }
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
	auto eigrpConfig = ctx.currentInterface.getEigrpConfig(as, AddressFamily::IPv6, ctx.negate);
	if (eigrpConfig)
	{
	    if (ctx.negate)
	    {
		eigrpConfig->helloTime.store(5, std::memory_order_release);
		refreshEigrpConfig(ctx.currentInterface, as, AddressFamily::IPv6, eigrpConfig);
	    }
	    else
	    {
		eigrpConfig->helloTime.store(static_cast<uint32_t>(std::stoul(args[2]), std::memory_order_relaxed));
	    }
	}
    }
    return true;
}

bool InterfaceIPv6_HoldTime_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
	uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
	auto eigrpConfig = ctx.currentInterface.getEigrpConfig(as, AddressFamily::IPv6, ctx.negate);
	if (eigrpConfig)
	{
	    if (ctx.negate)
	    {
		eigrpConfig->holdTime.store(15, std::memory_order_release);
		refreshEigrpConfig(ctx.currentInterface, as, AddressFamily::IPv6, eigrpConfig);
	    }
	    else
	    {
		eigrpConfig->holdTime.store(static_cast<uint32_t>(std::stoul(args[2]), std::memory_order_relaxed));
	    }
	}
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
	uint32_t as = static_cast<uint32_t>(std::stoi(args[1]));
	bool disableEcmp = args.size() == 3 && args[2] == "no-ecmp-mode";
	{
	    auto* eigrpConfig = ctx.currentInterface.getEigrpConfig(as, AddressFamily::IPv6, ctx.negate);
	    if (eigrpConfig)
	    {
		eigrpConfig->nextHopSelf.store(!ctx.negate, std::memory_order_release);
		if (args.size() == 2 || ctx.negate)
		{
		    eigrpConfig->noEcmpMode.store(ctx.negate ? disableEcmp : false, std::memory_order_release);
		}
		if (ctx.negate)
		{
		    refreshEigrpConfig(ctx.currentInterface, as, AddressFamily::IPv6, eigrpConfig);
		}
	    }
	}
    }
    return true;
}

bool InterfaceIPv6_NdpRedirects_Handler(INTERFACE_PARAMS)
{
    UNUSED(args);
    ctx.currentInterface.ndp->configs.redirects.store(!ctx.negate, std::memory_order_release);
    return true;
}

bool InterfaceIPv6_SplitHorizon_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
	uint32_t as = static_cast<uint32_t>(std::stoi(args[1]));
	auto* eigrpConfig = ctx.currentInterface.getEigrpConfig(as, AddressFamily::IPv6, ctx.negate);
	if (eigrpConfig)
	{
	    eigrpConfig->splitHorizon.store(!ctx.negate, std::memory_order_relaxed);
	    if (ctx.negate)
	    {
		refreshEigrpConfig(ctx.currentInterface, as, AddressFamily::IPv6, eigrpConfig);
	    }
	}
    }
    return true;
}

bool InterfaceIPv6_SummaryAddress_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
	uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
	IPv6Prefix _pfx; CliUtils::extractIPv6Prefix(args[2], _pfx);
	IPv6Address network(_pfx.addr);
	uint8_t mask = _pfx.prefixLength;
	IPPrefix prefix(_pfx.addr, _pfx.prefixLength);
	auto ifaceIt = ctx.currentInterface.eigrpInterfaceList.find(as);
	if (ifaceIt != ctx.currentInterface.eigrpInterfaceList.end() && ifaceIt->second.IPv4)
	{
	    if (ctx.negate)
		ifaceIt->second.IPv4->getAggregator().withdrawSummary(prefix);
	    else
		ifaceIt->second.IPv4->getAggregator().installSummary(prefix);
	}
	else
	{
	    auto eigrpConfig = ctx.currentInterface.getEigrpConfig(as, AddressFamily::IPv6, ctx.negate);
	    if (eigrpConfig)
	    {
		if (ctx.negate)
		{
		    std::erase_if(
			eigrpConfig->pendingSummaryRoutes,
			[&](const IPPrefix& net) -> bool { return net == prefix; });
		    refreshEigrpConfig(ctx.currentInterface, as, AddressFamily::IPv6, eigrpConfig);
		}
		else
		{
		    eigrpConfig->pendingSummaryRoutes.push_back(IPPrefix(_pfx.addr, _pfx.prefixLength));
		}
	    }
	}
    }
    return true;
}
}
