// InterfaceIPCommands.cpp

#include "InterfaceIPCommands.h"
#include "InterfaceHelpers.hpp"
#include "cli/runtime/CliSession.h"
#include "cli/runtime/CliUtils.h"
#include "dhcp/dhcpv4/DhcpClient.h"
#include "eigrp/core/Eigrp.h"
#include "eigrp/interface/EigrpInterface.h"

namespace Cli
{
bool InterfaceIP_AddressSet_Handler(INTERFACE_PARAMS)
{
    if (!ctx.negate)
    {
	if (args[0] != "dhcp")
	{
	    IPv4Address ipAddress; CliUtils::extractIPv4Address(args[0], ipAddress);
	    IPv4Address _mask; CliUtils::extractIPv4Address(args[1], _mask);
	    uint8_t subnet = static_cast<uint8_t>(__builtin_popcount(_mask.addr));
	    ctx.currentInterface.setIPv4(IPv4Prefix(ipAddress.addr, subnet));
	}
	else
	{
	    if (!ctx.currentInterface.dhcp)
	    {
		ctx.currentInterface.dhcp = new Protocol::DhcpClient(&ctx.currentInterface);
	    }
	}
    }
    else
    {
	ctx.currentInterface.removeIPv4();
    }
    return true;
}

bool InterfaceIP_AuthenticationKeyChain_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
	uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
	auto* eigrpConfig = ctx.currentInterface.getEigrpConfig(as, AddressFamily::IPv4, ctx.negate);
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

bool InterfaceIP_AuthenticationMode_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
	uint32_t as = static_cast<uint32_t>(std::stoi(args[1]));
	auto* eigrpConfig = ctx.currentInterface.getEigrpConfig(as, AddressFamily::IPv4, ctx.negate);
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

bool InterfaceIP_BandwidthPercentage_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
	uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
	auto eigrpConfig = ctx.currentInterface.getEigrpConfig(as, AddressFamily::IPv4, ctx.negate);
	if (eigrpConfig)
	{
	    if (ctx.negate)
	    {
		eigrpConfig->bandwidthPercentage.store(50, std::memory_order_release);
		refreshEigrpConfig(ctx.currentInterface, as, AddressFamily::IPv4, eigrpConfig);
	    }
	    else
	    {
		eigrpConfig->bandwidthPercentage.store(static_cast<uint32_t>(std::stoul(args[2]), std::memory_order_relaxed));
	    }
	}
    }
    return true;
}

bool InterfaceIP_DampeningChange_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
	uint32_t as = static_cast<uint32_t>(std::stol(args[1]));
	auto eigrpConfig = ctx.currentInterface.getEigrpConfig(as, AddressFamily::IPv4, ctx.negate);
	if (eigrpConfig)
	{
	    if (ctx.negate)
	    {
		eigrpConfig->dampeningChange.store(1, std::memory_order_release);
		refreshEigrpConfig(ctx.currentInterface, as, AddressFamily::IPv4, eigrpConfig);
	    }
	    else
	    {
		eigrpConfig->dampeningChange.store(static_cast<uint32_t>(std::stoul(args[2]), std::memory_order_relaxed));
	    }
	}
    }
    return true;
}

bool InterfaceIP_DampeningInterval_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
	uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
	auto eigrpConfig = ctx.currentInterface.getEigrpConfig(as, AddressFamily::IPv4, ctx.negate);
	if (eigrpConfig)
	{
	    if (ctx.negate)
	    {
		eigrpConfig->dampeningInterval.store(5, std::memory_order_release);
		refreshEigrpConfig(ctx.currentInterface, as, AddressFamily::IPv4, eigrpConfig);
	    }
	    else
	    {
		eigrpConfig->dampeningInterval.store(static_cast<uint32_t>(std::stoul(args[2]), std::memory_order_relaxed));
	    }
	}
    }
    return true;
}

bool InterfaceIP_HelloInterval_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
	uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
	auto eigrpConfig = ctx.currentInterface.getEigrpConfig(as, AddressFamily::IPv4, ctx.negate);
	if (eigrpConfig)
	{
	    if (ctx.negate)
	    {
		eigrpConfig->helloTime.store(5, std::memory_order_release);
		refreshEigrpConfig(ctx.currentInterface, as, AddressFamily::IPv4, eigrpConfig);
	    }
	    else
	    {
		eigrpConfig->helloTime.store(static_cast<uint32_t>(std::stoul(args[2]), std::memory_order_relaxed));
	    }
	}
    }
    return true;
}

bool InterfaceIP_HoldTime_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
	uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
	auto eigrpConfig = ctx.currentInterface.getEigrpConfig(as, AddressFamily::IPv4, ctx.negate);
	if (eigrpConfig)
	{
	    if (ctx.negate)
	    {
		eigrpConfig->helloTime.store(5, std::memory_order_release);
		refreshEigrpConfig(ctx.currentInterface, as, AddressFamily::IPv4, eigrpConfig);
	    }
	    else
	    {
		eigrpConfig->helloTime.store(static_cast<uint32_t>(std::stoul(args[2]), std::memory_order_relaxed));
	    }
	}
    }
    return true;
}

bool InterfaceIP_Mtu_Handler(INTERFACE_PARAMS)
{
    ctx.currentInterface.configs.ipv4.mtu.store(ctx.negate ? 1500 : static_cast<uint16_t>(std::stoul(args[0])), std::memory_order_release);
    return true;
}

bool InterfaceIP_NextHopSelf_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
	uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
	auto eigrpConfig = ctx.currentInterface.getEigrpConfig(as, AddressFamily::IPv4, ctx.negate);
	if (eigrpConfig)
	{
	    eigrpConfig->nextHopSelf.store(!ctx.negate, std::memory_order_relaxed);
	    if (ctx.negate)
	    {
		refreshEigrpConfig(ctx.currentInterface, as, AddressFamily::IPv4, eigrpConfig);
	    }
	}
    }
    return true;
}

bool InterfaceIP_SplitHorizon_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
	uint32_t as = static_cast<uint32_t>(std::stoi(args[1]));
	auto* eigrpConfig = ctx.currentInterface.getEigrpConfig(as, AddressFamily::IPv4, ctx.negate);
	if (eigrpConfig)
	{
	    eigrpConfig->splitHorizon.store(!ctx.negate, std::memory_order_relaxed);
	    if (ctx.negate)
	    {
		refreshEigrpConfig(ctx.currentInterface, as, AddressFamily::IPv4, eigrpConfig);
	    }
	}
    }
    return true;
}

bool InterfaceIP_SummaryAddress_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
	uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
	auto ifaceIt = ctx.currentInterface.eigrpInterfaceList.find(as);
	IPv4Address network; CliUtils::extractIPv4Address(args[2], network);
	uint8_t mask; CliUtils::extractSubnetMask(network.addr, mask);
	IPPrefix prefix(network.addr, mask);
	if (ifaceIt != ctx.currentInterface.eigrpInterfaceList.end() && ifaceIt->second.IPv4)
	{
	    if (ctx.negate)
		ifaceIt->second.IPv4->getAggregator().withdrawSummary(prefix);
	    else
		ifaceIt->second.IPv4->getAggregator().installSummary(prefix);
	}
	else
	{
	    auto eigrpConfig = ctx.currentInterface.getEigrpConfig(as, AddressFamily::IPv4, ctx.negate);
	    if (eigrpConfig)
	    {
		if (ctx.negate)
		{
		    std::erase_if(
			eigrpConfig->pendingSummaryRoutes,
			[&](const IPPrefix& net) -> bool { return net == prefix; });
		    refreshEigrpConfig(ctx.currentInterface, as, AddressFamily::IPv4, eigrpConfig);
		}
		else
		{
		    eigrpConfig->pendingSummaryRoutes.push_back(prefix);
		}
	    }
	}
    }
    return true;
}
}
