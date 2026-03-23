// InterfaceIPCommands.cpp

#include "InterfaceIPCommands.h"
#include "cli/runtime/CliSession.h"
#include "cli/runtime/CliUtils.h"
#include "dhcp/dhcpv4/DhcpClient.h"
#include "eigrp/core/Eigrp.h"
#include "eigrp/interface/EigrpInterface.h"
#include "interface/Interface.h"
#include "configs/registry/router/EigrpInterfaceRegistry.h"

namespace cli
{
bool InterfaceIP_AddressSet_Handler(INTERFACE_PARAMS)
{
    if (!ctx.negate)
    {
	if (args[0] != "dhcp")
	{
	    types::IPv4Address ipAddress; cli::utils::extractIPv4Address(args[0], ipAddress);
	    types::IPv4Address _mask; cli::utils::extractIPv4Address(args[1], _mask);
	    uint8_t subnet = static_cast<uint8_t>(__builtin_popcount(_mask.addr));
	    ctx.currentInterface.setIPv4(types::IPv4Prefix(ipAddress.addr, subnet));
	}
	else
	{
	    if (!ctx.currentInterface.dhcp)
	    {
		ctx.currentInterface.dhcp = new services::dhcp::DhcpClient(&ctx.currentInterface);
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

bool InterfaceIP_AuthenticationMode_Handler(INTERFACE_PARAMS)
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

bool InterfaceIP_BandwidthPercentage_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
        uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
        ctx.currentInterface.getEigrpConfig(as)->get<config::EigrpInterface::BANDWIDTH_PERCENTAGE>().set(
            ctx.negate ? 50u : static_cast<uint32_t>(std::stoul(args[2])));
    }
    return true;
}

bool InterfaceIP_DampeningChange_Handler(INTERFACE_PARAMS)
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

bool InterfaceIP_DampeningInterval_Handler(INTERFACE_PARAMS)
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

bool InterfaceIP_HelloInterval_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
        uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
        ctx.currentInterface.getEigrpConfig(as)->get<config::EigrpInterface::HELLO_INTERVAL>().set(
            ctx.negate ? static_cast<uint16_t>(5) : static_cast<uint16_t>(std::stoul(args[2])));
    }
    return true;
}

bool InterfaceIP_HoldTime_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
        uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
        ctx.currentInterface.getEigrpConfig(as)->get<config::EigrpInterface::HOLD_TIME>().set(
            ctx.negate ? static_cast<uint16_t>(15) : static_cast<uint16_t>(std::stoul(args[2])));
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
        ctx.currentInterface.getEigrpConfig(as)->get<config::EigrpInterface::NEXT_HOP_SELF>().set(!ctx.negate);
    }
    return true;
}

bool InterfaceIP_SplitHorizon_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
        uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
        ctx.currentInterface.getEigrpConfig(as)->get<config::EigrpInterface::SPLIT_HORIZON>().set(!ctx.negate);
    }
    return true;
}

bool InterfaceIP_SummaryAddress_Handler(INTERFACE_PARAMS)
{
    if (args[0] == "eigrp")
    {
        uint32_t as = static_cast<uint32_t>(std::stoul(args[1]));
        types::IPv4Address network; cli::utils::extractIPv4Address(args[2], network);
        uint8_t mask; cli::utils::extractSubnetMask(network.addr, mask);
        types::IPPrefix prefix(network.addr, mask);
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
