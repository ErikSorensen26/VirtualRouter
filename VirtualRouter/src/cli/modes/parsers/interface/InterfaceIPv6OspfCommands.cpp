// InterfaceIPv6OspfCommands.cpp

#include <VirtualRouter.h>

#include "InterfaceIPv6OspfCommands.h"
#include "interface/Interface.h"
#include "configs/registry/router/OspfInterfaceRegistry.h"
#include "cli/runtime/CliSession.h"
#include "cli/runtime/CliUtils.h"
#include "ospf/OspfProcess.h"
#include "ospf/interface/OspfInterface.h"

namespace cli
{
bool InterfaceIPv6Ospf_Area_Handler(INTERFACE_PARAMS)
{
    auto& ifaceConfigs = ctx.currentInterface.getOspfConfig().get();

    // TODO: add to process queue
    auto* vrf = ctx.currentInterface.getVRF();
    if (ctx.negate)
    {
		auto* ospf = vrf->getOspf(static_cast<uint32_t>(std::stoi(args[0])));
		if (!ospf) return true;
		ospf->getIfaceMgr().removeInterface({ctx.currentInterface.configs.key.getId(), static_cast<uint32_t>(std::stoi(args[2]))});

		ifaceConfigs.get<config::OspfInterfaceBase::PROCESS_ID>().unset();
		ifaceConfigs.get<config::OspfInterfaceBase::AREA_ID>().unset();
		ifaceConfigs.get<config::OspfInterfaceBase::INSTANCE_ID>().unset();
    }
    else if (ifaceConfigs.context().hasCtx())
    {
		auto* ospf = vrf->getOspf(static_cast<uint32_t>(std::stoi(args[0])));
		if (!ospf || !ctx.currentInterface.configs.ipv4.hasPrimaryAddress()) return false;

		auto& context = *static_cast<routing::ospf::OspfInterface*>(ifaceConfigs.context().get());
		if (context.getArea().process().getProcId() != static_cast<uint32_t>(std::stoi(args[0])) ||
			context.getArea().areaId != static_cast<uint32_t>(std::stoi(args[1])))
		{
			// Remove interface from other area
			context.getArea().process().getIfaceMgr().removeInterface(routing::ospf::OspfInterfaceId(ctx.currentInterface.configs.key.getId(), context.getArea().areaId));
		}

		ifaceConfigs.context().clear();
		uint32_t areaId = static_cast<uint32_t>(std::stoi(args[1]));
		ospf->getIfaceMgr().createInterface(ctx.currentInterface, {ctx.currentInterface.configs.key.getId(), areaId});

		ifaceConfigs.get<config::OspfInterfaceBase::PROCESS_ID>().set(ospf->getProcId());
		ifaceConfigs.get<config::OspfInterfaceBase::AREA_ID>().set(areaId);

		if (args.size() == 5)
			ifaceConfigs.get<config::OspfInterfaceBase::INSTANCE_ID>().set(static_cast<uint8_t>(std::stoi(args[4])));
    }
    
    return true;
}

bool InterfaceIPv6Ospf_Authentication_Handler(INTERFACE_PARAMS)
{
    auto& configs = ctx.currentInterface.getOspfConfig()->get<config::OspfInterfaceBase::IPSEC>().local();
    auto& authSpi = configs->get<config::OspfInterfaceIPSec::SPI>();
    auto& authType = configs->get<config::OspfInterfaceIPSec::AUTHENTICATION_TYPE>();
    auto& authKey = configs->get<config::OspfInterfaceIPSec::AUTHENTICATION_KEY>();

    if (ctx.negate && configs->get<config::OspfInterfaceIPSec::ENCRYPTION_TYPE>().hasValue())
	return false;

    if (args[0] == "ipsec")
    {
	uint32_t spi = static_cast<uint32_t>(std::stoi(args[2]));
	if (ctx.negate)
	{
	    if (authSpi.hasValue() && authSpi.load() == spi)
	    {
		authType.unset();
		authSpi.unset();
		authKey.unset();
	    }
	}
	else
	{
	    authSpi.set(spi);
	    std::array<uint8_t, 40> keyString;
	    if (args[3] == "md5")
	    {
			authType.set(config::ospf::IPsecAuthType::MD5);
			std::copy_n(args[4].data(), std::min(args[4].size(), size_t(32)), keyString.data());
	    }
	    else
	    {
			authType.set(config::ospf::IPsecAuthType::SHA1);
			std::copy_n(args[4].data(), std::min(args[4].size(), size_t(40)), keyString.data());
	    }

	    // TODO: handle encryption

	    authKey.withWrite([&](std::array<uint8_t, 40>& key) {
			key = keyString;
	    });
	}
    }
    else
    {
	if (ctx.negate)
	    authType.unset();
	else
	    authType.set(config::ospf::IPsecAuthType::NULL_AUTH);
    }
    return true;
}

bool InterfaceIPv6Ospf_Encryption_Handler(INTERFACE_PARAMS)
{
    auto& configs = ctx.currentInterface.getOspfConfig()->get<config::OspfInterfaceBase::IPSEC>().local();
    auto& espSpi = configs->get<config::OspfInterfaceIPSec::SPI>();
    auto& authType = configs->get<config::OspfInterfaceIPSec::AUTHENTICATION_TYPE>();
    auto& authKey = configs->get<config::OspfInterfaceIPSec::AUTHENTICATION_KEY>();
    auto& encryptType = configs->get<config::OspfInterfaceIPSec::ENCRYPTION_TYPE>();
    auto& encryptkey = configs->get<config::OspfInterfaceIPSec::ENCRYPTION_KEY>();

    if (args[0] == "ipsec")
    {
		uint32_t spi = static_cast<uint32_t>(std::stoi(args[2]));
		size_t index;
		if (ctx.negate)
		{
			if (espSpi.hasValue() && espSpi.load() == spi)
			{
				espSpi.unset();
				encryptType.unset();
				encryptkey.unset();
				authType.unset();
				authKey.unset();
			}
		}
		else
		{
			espSpi.set(spi);
			std::array<uint8_t, 64> keyString;
			if (args[4] == "3des")
			{
				encryptType.set(config::ospf::IPsecEncryptType::_3DES);
				std::copy_n(args[5].data(), std::min(args[5].size(), size_t(48)), keyString.data());
				index = 6;
			}
			else if (args[4] == "aes-cbc")
			{
				if (args[5] == "128")
				{
					encryptType.set(config::ospf::IPsecEncryptType::AES_CBC_128);
					std::copy_n(args[6].data(), std::min(args[5].size(), size_t(32)), keyString.data());
				}
				else if (args[5] == "192")
				{
					encryptType.set(config::ospf::IPsecEncryptType::AES_CBC_192);
					std::copy_n(args[6].data(), std::min(args[5].size(), size_t(48)), keyString.data());
				}
				else
				{
					encryptType.set(config::ospf::IPsecEncryptType::AES_CBC_256);
					std::copy_n(args[6].data(), std::min(args[5].size(), size_t(64)), keyString.data());
				}
				index = 7;
			}
			else if (args[4] == "des")
			{
				encryptType.set(config::ospf::IPsecEncryptType::DES);
				std::copy_n(args[5].data(), std::min(args[5].size(), size_t(16)), keyString.data());
				index = 6;
			}
			else
			{
				encryptType.set(config::ospf::IPsecEncryptType::NULL_TYPE);
				index = 5;
			}

			if (args[index] == "md5")
			{
				authType.set(config::ospf::IPsecAuthType::MD5);
			}
			else
			{
				authType.set(config::ospf::IPsecAuthType::SHA1);
			}

			std::array<uint8_t, 40> authString;
			if (args[index] == "md5")
			{
				authType.set(config::ospf::IPsecAuthType::MD5);
				std::copy_n(args[index + 1].data(), std::min(args[index + 1].size(), size_t(32)), keyString.data());
			}
			else
			{
				authType.set(config::ospf::IPsecAuthType::SHA1);
				std::copy_n(args[index + 1].data(), std::min(args[index + 1].size(), size_t(40)), keyString.data());
			}

			// TODO: handle encryption

			authKey.withWrite([&](std::array<uint8_t, 40>& key) {
				key = authString;
			});
		}
    }
    else
    {
		if (ctx.negate)
			authType.unset();
		else
			authType.set(config::ospf::IPsecAuthType::NULL_AUTH);
    }
    return true;
}

bool InterfaceIPv6Ospf_Neighbor_Handler(INTERFACE_PARAMS)
{
    types::IPAddress nbrIp; utils::extractIPAddress(args[0], nbrIp);
    std::optional<uint16_t> cost{std::nullopt};
    std::optional<bool> df{std::nullopt};
    std::optional<uint16_t> poll{std::nullopt};
    std::optional<uint8_t> priority{std::nullopt};

    // Start after neighbor address
    for (size_t i = 1; i < args.size(); i++)
    {
		if (args[i] == "cost")
		{
			cost = static_cast<uint16_t>(std::stoi(args[++i]));
		}
		else if (args[i] == "database-filter")
		{
			df = true;
			i++;
		}
		else if (args[i] == "poll-interval")
		{
			poll = static_cast<uint16_t>(std::stoi(args[++i]));
		}
		else if (args[i] == "priority")
		{
			priority = static_cast<uint8_t>(std::stoi(args[++i]));
		}
    }

    ctx.currentInterface.getOspfConfig()->get<config::OspfInterfaceBase::BASE>().local()->get<config::OspfInterface::NEIGHBOR>().withWrite([&](auto& nbrs)
    {
		if (ctx.negate)
		{
			nbrs.erase(std::remove_if(nbrs.begin(), nbrs.end(), [&](const auto& nbr) {
			auto& [ip, c, d, pi, p] = nbr;
			return ip == nbrIp && c == cost && d == df && pi == poll && p == priority;
			}));
		}
		else
		{
			auto it = std::find_if(nbrs.begin(), nbrs.end(), [&](const auto& nbr) { return std::get<0>(nbr) == nbrIp; });
			if (it != nbrs.end())
			{
			auto& [_, c, d, pi, p] = *it;
			c = cost;
			d = df;
			pi = poll;
			p = priority;
			}
			else
			{
			nbrs.push_back({nbrIp, cost, df, poll, priority});
			}
		}
    });
    return true;
}
}
