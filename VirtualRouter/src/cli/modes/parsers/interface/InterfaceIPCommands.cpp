// InterfaceIPCommands.cpp

#include "InterfaceIPCommands.h"
#include "cli/runtime/CliSession.h"
#include "cli/runtime/CliUtils.h"
#include "dhcp/dhcpv4/DhcpClient.h"
#include "eigrp/core/Eigrp.h"
#include "eigrp/interface/EigrpInterface.h"
#include "interface/Interface.h"
#include "configs/registry/router/EigrpInterfaceRegistry.h"

namespace
{
config::InterfaceRegistry& getIfaceConfigs(interface::Interface& iface)
{
    return iface.configs.getConfigs();
}
}

namespace cli
{
bool InterfaceIP_AddressSet_Handler(INTERFACE_PARAMS)
{
    auto& ifcfg = getIfaceConfigs(ctx.currentInterface);

    if (ctx.negate || ctx.defaulted)
    {
        if (args.empty())
        {
            ifcfg.get<config::Interface::IP_ADDRESS>().unset();
            return true;
        }
        else if (args[0] == "dhcp")
        {
            if (args.size() == 1)
            {
                ifcfg.get<config::Interface::IP_ADDRESS_DHCP>().unset();
                return true;
            }
            else
            {
                return false;
                // TODO handle client id and hostname
            }
        }
        else if (args[0] == "pool")
        {
            return false;
            // TODO pool
        }
        else if (args.size() >= 2)
        {
            types::IPv4Prefix prefix;
            if (cli::utils::extractIPv4Prefix(args[0], args[1], prefix))
            {
                auto& primary = ifcfg.get<config::Interface::IP_ADDRESS>();
                auto& secondary = ifcfg.get<config::Interface::IP_ADDRESS_SECONDARY>();

                if (primary.hasValue() && primary.load() == prefix)
                {
                    primary.unset();
                    return true;
                }

                secondary.withWrite([&](std::vector<std::tuple<types::IPv4Prefix, std::string>>& ips) {
                    auto it = std::find_if(ips.begin(), ips.end(), [&](const auto& ip) {
                        return std::get<0>(ip) == prefix;
                    });

                    for (size_t i = 2; i < args.size(); ++i)
                    {
                        if (args[i] == "vrf")
                            if (args[++i] != std::get<1>(*it))
                                return;
                    }
                    ips.erase(it);
                });
                return true;
            }
        }
        return false;
    }

    if (args[0] == "dhcp")
    {
        if (auto& primary = ifcfg.get<config::Interface::IP_ADDRESS>(); primary.hasValue())
            primary.unset();
        ifcfg.get<config::Interface::IP_ADDRESS_DHCP>().set(true);

        for (int i = 1; i < args.size(); i++)
        {
            if (args[i] == "client-id")
            {
                // TODO
                i += 2;
                return false;
            }
            if (args[i] == "hostname")
            {
                // TODO
                ++i;
                return false;
            }
        }
    }
    else if (args[0] == "pool")
    {
        // TODO
        return false;
    }
    else if (args.size() >= 2)
    {
        types::IPPrefix prefix; 
        cli::utils::extractIPv4Prefix(args[0], args[1], prefix);
        if (args.size() == 2)
        {
            if (auto& dhcp = ifcfg.get<config::Interface::IP_ADDRESS_DHCP>(); dhcp.load())
                dhcp.set(false);
            ifcfg.get<config::Interface::IP_ADDRESS>().set(prefix);
            return true;
        }
        else
        {
            std::string vrf = "default";
            for (size_t i = 2; i < args.size(), i++)
            {
            }
            if (auto& dhcp = ifcfg.get<config::Interface::IP>) 
        }
        ctx.currentInterface.setIPv4()
    }


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
