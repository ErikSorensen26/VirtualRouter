// InterfaceIPOspfCommands.cpp

#include "InterfaceIPOspfCommands.h"
#include <Interface.h>
#include <VirtualRouter.h>
#include <OspfInterfaceRegistry.h>

#include <CliSession.h>
#include <Mode.hpp>

namespace Cli
{
bool InterfaceIPOspf_Area_Handler(INTERFACE_PARAMS)
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
	ifaceConfigs.get<Config::OspfInterfaceBase::INCLUDE_SECONDARIES>().unset();
    }
    else if (ifaceConfigs.context().hasCtx())
    {
	auto* ospf = vrf->getOspf(static_cast<uint32_t>(std::stoi(args[0])));
	if (!ospf || !ctx.currentInterface.configs.ipv4.hasPrimaryAddress()) return false;

	auto& context = ifaceConfigs.context().get();
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
    }
    
    bool includeSecondaries = args.size() != 4;
    ifaceConfigs.get<Config::OspfInterfaceBase::INCLUDE_SECONDARIES>().set(ctx.negate ? !includeSecondaries : includeSecondaries);
    return true;
}

bool InterfaceIPOspf_Authentication_Handler(INTERFACE_PARAMS)
{
    auto& authType = ctx.currentInterface.getOspfConfig().get<Config::OspfInterfaceBase::AUTHENTICATION_TYPE>();

    if (ctx.negate)
    {
	authType.unset();
	return true;
    }

    if (args[0] == "message-digest")
	authType.set(OSPF::AuthType::CRYPTO);
    else if (args[0] == "null")
	authType.set(OSPF::AuthType::NULL_AUTH);
    else
	authType.set(OSPF::AuthType::SIMPLE);

    return true;
}

bool InterfaceIPOspf_AuthenticationKey_Handler(INTERFACE_PARAMS)
{
    // TODO: handle encryption type

    auto& key = ctx.currentInterface.getOspfConfig().get<Config::OspfInterfaceBase::AUTHENTICATION_KEY>();
    if (ctx.negate)
    {
	key.unset();
	return true;
    }

    key.set(readU64(reinterpret_cast<const uint8_t*>(args[1].data())));
    return true;
}

bool InterfaceIPOspf_LLS_Handler(INTERFACE_PARAMS)
{
    ctx.currentInterface.getOspfConfig().get<Config::OspfInterfaceBase::LLS>().set(!(ctx.negate || args.size() == 1));
    return true;
}

bool InterfaceIPOspf_MessageDigestKey_Handler(INTERFACE_PARAMS)
{
    ctx.currentInterface.getOspfConfig().get<Config::OspfInterfaceBase::MESSAGE_DIGEST_KEYS>().withRead(
    [&](std::vector<std::tuple<uint8_t, std::array<uint8_t, 16>, uint64_t>>& keys) {
	    if (ctx.negate)
	    {
		keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const auto& key) {
		    return std::get<0>(key) == std::stoi(args[0]);
		}));
		return;
	    }

	    // TODO: handle encryption

	    std::array<uint8_t, 16> keyString;
	    std::copy_n(args[2].data(), std::min(args[2].size(), size_t(16)), keyString.data());
	    uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()
	    ).count());

	    for (auto& key : keys)
	    {
		if (std::get<0>(key) == std::stoi(args[0]))
		{
		    std::get<1>(key) = keyString;
		    std::get<2>(key) = now;
		}
	    }

	    keys.push_back(std::make_tuple(
		static_cast<uint8_t>(std::stoi(args[0])),
		keyString, now
	    ));
	});
    return true;
}

bool InterfaceIPOspf_PrefixSuppression_Handler(INTERFACE_PARAMS)
{
    ctx.currentInterface.getOspfConfig().get<Config::OspfInterfaceBase::PREFIX_SUPPRESSION>().set(!(ctx.negate || args.size() == 1));
    return true;
}

bool InterfaceIPOspf_ResyncTimeout_Handler(INTERFACE_PARAMS)
{
    auto& resync = ctx.currentInterface.getOspfConfig().get<Config::OspfInterfaceBase::RESYNC_TIMEOUT>();
    if (ctx.negate)
    {
	resync.unset();
	return true;
    }

    resync.set(static_cast<uint16_t>(std::stoi(args[0])));
    return true;
}

bool InterfaceIPOspf_Shutdown_Handler(INTERFACE_PARAMS)
{
    UNUSED(args);
    ctx.currentInterface.getOspfConfig().get<Config::OspfInterfaceBase::SHUTDOWN>().set(!ctx.negate);
    return true;
}

bool InterfaceIPOspf_TtlSecurity_Handler(INTERFACE_PARAMS)
{
    auto& config = ctx.currentInterface.getOspfConfig().get<Config::OspfInterfaceBase::BASE>().local();
    auto& ttl = config->get<Config::OspfInterface::TTL_SEC>();
    auto& hops = config->get<Config::OspfInterface::TTL_SEC_HOPS>();

    if (!ctx.negate || (args.size() == 1))
    {
	ttl.unset();
	ttl.unset();
	return true;
    }

    ttl.set(true);
    if (args.size() == 2)
	hops.set(static_cast<uint8_t>(std::stoi(args[1])));
    return true;
}
}
