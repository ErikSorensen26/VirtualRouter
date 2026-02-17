// InterfaceOspfv3Commands.cpp

#include <VirtualRouter.h>

#include "InterfaceOspfv3Commands.h"
#include "interface/Interface.h"
#include "configs/registry/router/OspfInterfaceRegistry.h"
#include "cli/runtime/CliSession.h"
#include "ospf/OspfProcess.h"
#include "ospf/interface/OspfInterface.h"

namespace Cli
{
bool InterfaceOspfv3_Area_Handler(INTERFACE_PARAMS)
{
    auto& ifaceConfigs = ctx.currentInterface.getOspfConfig();

    // TODO: add to process queue
    auto* vrf = ctx.currentInterface.getVRF();
    if (ctx.negate)
    {
	auto* ospf = vrf->getOspf(static_cast<uint32_t>(std::stoi(args[0])));
	if (!ospf) return true;
	ospf->getIfaceMgr().removeInterface({ctx.currentInterface.configs.ipv4.getPrimaryAddress(), static_cast<uint32_t>(std::stoi(args[2]))});

	ifaceConfigs.get<Config::OspfInterfaceBase::PROCESS_ID>().unset();
	ifaceConfigs.get<Config::OspfInterfaceBase::AREA_ID>().unset();
	ifaceConfigs.get<Config::OspfInterfaceBase::INSTANCE_ID>().unset();
    }
    else if (ifaceConfigs.context().hasCtx())
    {
	auto* ospf = vrf->getOspf(static_cast<uint32_t>(std::stoi(args[0])));
	if (!ospf || !ctx.currentInterface.configs.ipv4.hasPrimaryAddress()) return false;

	auto& context = *static_cast<OSPF::OspfInterface*>(ifaceConfigs.context().get());
	if (context.getArea().process().getProcId() != static_cast<uint32_t>(std::stoi(args[0])) ||
	    context.getArea().areaId != static_cast<uint32_t>(std::stoi(args[1])))
	{
	    // Remove interface from other area
	    context.getArea().process().getIfaceMgr().removeInterface(OSPF::OspfInterfaceId(ctx.currentInterface.configs.ipv4.getPrimaryAddress(), context.getArea().areaId));
	}

	ifaceConfigs.context().clear();
	uint32_t areaId = static_cast<uint32_t>(std::stoi(args[1]));
	ospf->getIfaceMgr().createInterface(ctx.currentInterface, {ctx.currentInterface.configs.ipv4.getPrimaryAddress(), areaId});

	ifaceConfigs.get<Config::OspfInterfaceBase::PROCESS_ID>().set(ospf->getProcId());
	ifaceConfigs.get<Config::OspfInterfaceBase::AREA_ID>().set(areaId);

	if (args.size() == 5)
	    ifaceConfigs.get<Config::OspfInterfaceBase::INSTANCE_ID>().set(static_cast<uint8_t>(std::stoi(args[4])));
    }
    
    return true;
}

bool InterfaceOspfv3_Authentication_Handler(INTERFACE_PARAMS)
{
    auto& configs = ctx.currentInterface.getOspfConfig().get<Config::OspfInterfaceBase::IPSEC>().local();
    auto& authSpi = configs->get<Config::OspfInterfaceIPSec::SPI>();
    auto& authType = configs->get<Config::OspfInterfaceIPSec::AUTHENTICATION_TYPE>();
    auto& authKey = configs->get<Config::OspfInterfaceIPSec::AUTHENTICATION_KEY>();

    if (ctx.negate && configs->get<Config::OspfInterfaceIPSec::ENCRYPTION_TYPE>().hasValue())
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
		authType.set(OSPF::IPsecAuthType::MD5);
		std::copy_n(args[4].data(), std::min(args[4].size(), size_t(32)), keyString.data());
	    }
	    else
	    {
		authType.set(OSPF::IPsecAuthType::SHA1);
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
	    authType.set(OSPF::IPsecAuthType::NULL_AUTH);
    }
    return true;
}

bool InterfaceOspfv3_Encryption_Handler(INTERFACE_PARAMS)
{
    auto& configs = ctx.currentInterface.getOspfConfig().get<Config::OspfInterfaceBase::IPSEC>().local();
    auto& espSpi = configs->get<Config::OspfInterfaceIPSec::SPI>();
    auto& authType = configs->get<Config::OspfInterfaceIPSec::AUTHENTICATION_TYPE>();
    auto& authKey = configs->get<Config::OspfInterfaceIPSec::AUTHENTICATION_KEY>();
    auto& encryptType = configs->get<Config::OspfInterfaceIPSec::ENCRYPTION_TYPE>();
    auto& encryptkey = configs->get<Config::OspfInterfaceIPSec::ENCRYPTION_KEY>();

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
		encryptType.set(OSPF::IPsecEncryptType::_3DES);
		std::copy_n(args[5].data(), std::min(args[5].size(), size_t(48)), keyString.data());
		index = 6;
	    }
	    else if (args[4] == "aes-cbc")
	    {
		if (args[5] == "128")
		{
		    encryptType.set(OSPF::IPsecEncryptType::AES_CBC_128);
		    std::copy_n(args[6].data(), std::min(args[5].size(), size_t(32)), keyString.data());
		}
		else if (args[5] == "192")
		{
		    encryptType.set(OSPF::IPsecEncryptType::AES_CBC_192);
		    std::copy_n(args[6].data(), std::min(args[5].size(), size_t(48)), keyString.data());
		}
		else
		{
		    encryptType.set(OSPF::IPsecEncryptType::AES_CBC_256);
		    std::copy_n(args[6].data(), std::min(args[5].size(), size_t(64)), keyString.data());
		}
		index = 7;
	    }
	    else if (args[4] == "des")
	    {
		encryptType.set(OSPF::IPsecEncryptType::DES);
		std::copy_n(args[5].data(), std::min(args[5].size(), size_t(16)), keyString.data());
		index = 6;
	    }
	    else
	    {
		encryptType.set(OSPF::IPsecEncryptType::NULL_TYPE);
		index = 5;
	    }

	    if (args[index] == "md5")
	    {
		authType.set(OSPF::IPsecAuthType::MD5);
	    }
	    else
	    {
		authType.set(OSPF::IPsecAuthType::SHA1);
	    }

	    std::array<uint8_t, 40> authString;
	    if (args[index] == "md5")
	    {
		authType.set(OSPF::IPsecAuthType::MD5);
		std::copy_n(args[index + 1].data(), std::min(args[index + 1].size(), size_t(32)), keyString.data());
	    }
	    else
	    {
		authType.set(OSPF::IPsecAuthType::SHA1);
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
	    authType.set(OSPF::IPsecAuthType::NULL_AUTH);
    }
    return true;
}

bool InterfaceOspfv3_Neighbor_Handler(INTERFACE_PARAMS)
{
    IPAddress nbrIp = Functions::getAddress(args[0]);
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

    ctx.currentInterface.getOspfConfig().get<Config::OspfInterfaceBase::BASE>().local()->get<Config::OspfInterface::NEIGHBOR>().withWrite([&](auto& nbrs)
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
