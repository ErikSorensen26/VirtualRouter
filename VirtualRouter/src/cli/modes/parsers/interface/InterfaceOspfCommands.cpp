// InterfaceOspfCommands.cpp

#include <VirtualRouter.h>

#include "InterfaceOspfCommands.h"
#include "interface/Interface.h"
#include "configs/registry/router/OspfInterfaceRegistry.h"
#include "cli/runtime/CliSession.h"

namespace cli
{
bool InterfaceOspf_BFD_Handler(INTERFACE_PARAMS)
{
    auto& bfd = ctx.currentInterface.getOspfConfig()->get<config::OspfInterfaceBase::BASE>().local()->get<config::OspfInterface::BFD>();
    bfd.set(ctx.negate || args.size() == 1);
    return true;
}

bool InterfaceOspf_Cost_Handler(INTERFACE_PARAMS)
{
    if (ctx.negate)
	ctx.currentInterface.getOspfConfig()->get<config::OspfInterfaceBase::BASE>().local()->get<config::OspfInterface::COST>().unset();
    else
	ctx.currentInterface.getOspfConfig()->get<config::OspfInterfaceBase::BASE>().local()->get<config::OspfInterface::COST>().set(static_cast<uint16_t>(std::stoi(args[0])));
    return true;
}

bool InterfaceOspf_DatabaseFilter_Handler(INTERFACE_PARAMS)
{
    UNUSED(args);
    ctx.currentInterface.getOspfConfig()->get<config::OspfInterfaceBase::BASE>().local()->get<config::OspfInterface::DATABASE_FILTER>().set(!ctx.negate);
    return true;
}

bool InterfaceOspf_DeadInterval_Handler(INTERFACE_PARAMS)
{
    auto& configs = ctx.currentInterface.getOspfConfig().get();
    auto& base = configs.get<config::OspfInterfaceBase::BASE>().local().get();

    if (ctx.negate)
    {
	base.get<config::OspfInterface::DEAD_INTERVAL>().unset();
	base.get<config::OspfInterface::HELLO_MULTIPLIER>().unset();
    }

    if (args[0] == "minimal")
    {
	base.get<config::OspfInterface::DEAD_INTERVAL>().set(1);
	base.get<config::OspfInterface::HELLO_MULTIPLIER>().set(static_cast<uint8_t>(std::stoi(args[2])));
    }
    else
    {
	base.get<config::OspfInterface::DEAD_INTERVAL>().set(static_cast<uint16_t>(std::stoi(args[0])));
    }
    return true;
}

bool InterfaceOspf_DemandCircuit_Handler(INTERFACE_PARAMS)
{
    UNUSED(args);
    ctx.currentInterface.getOspfConfig()->get<config::OspfInterfaceBase::BASE>().local()->get<config::OspfInterface::DEMAND_CIRCUIT>().set(!ctx.negate);
    return true;
}

bool InterfaceOspf_FloodReduction_Handler(INTERFACE_PARAMS)
{
    UNUSED(args);
    ctx.currentInterface.getOspfConfig()->get<config::OspfInterfaceBase::BASE>().local()->get<config::OspfInterface::FLOOD_REDUCTION>().set(!ctx.negate);
    return true;
}

bool InterfaceOspf_HelloInterval_Handler(INTERFACE_PARAMS)
{
    ctx.currentInterface.getOspfConfig()->get<config::OspfInterfaceBase::BASE>().local()->get<config::OspfInterface::HELLO_INTERVAL>().set(static_cast<uint16_t>(std::stoi(args[0])));
    return true;
}

bool InterfaceOspf_MtuIgnore_Handler(INTERFACE_PARAMS)
{
    UNUSED(args);
    ctx.currentInterface.getOspfConfig()->get<config::OspfInterfaceBase::BASE>().local()->get<config::OspfInterface::MTU_IGNORE>().set(!ctx.negate);
    return true;
}

bool InterfaceOspf_Network_Handler(INTERFACE_PARAMS)
{
    auto& ntype = ctx.currentInterface.getOspfConfig()->get<config::OspfInterfaceBase::BASE>().local()->get<config::OspfInterface::NETWORK>();
    if (ctx.negate) ntype.unset();

    if (args[0] == "broadcast")
	ntype.set(config::ospf::NetworkType::BROADCAST);
    else if (args[0] == "non-broadcast")
	ntype.set(config::ospf::NetworkType::NON_BROADCAST);
    else if (args[0] == "point-to-point")
	ntype.set(config::ospf::NetworkType::POINT_TO_POINT);
    else
    {
	if (args.size() == 2)
	    ntype.set(config::ospf::NetworkType::POINT_TO_MULTIPOINT);
	else
	    ntype.set(config::ospf::NetworkType::POINT_TO_MULTIPOINT_BROADCAST);
    }
    return true;
}

bool InterfaceOspf_Priority_Handler(INTERFACE_PARAMS)
{
    auto& priority = ctx.currentInterface.getOspfConfig()->get<config::OspfInterfaceBase::BASE>().local()->get<config::OspfInterface::PRIORITY>();
    if (ctx.negate)
    {
	priority.unset();
	return true;
    }

    priority.set(static_cast<uint8_t>(std::stoi(args[0])));
    return true;
}

bool InterfaceOspf_RetransmitInterval_Handler(INTERFACE_PARAMS)
{
    auto& retrans = ctx.currentInterface.getOspfConfig()->get<config::OspfInterfaceBase::BASE>().local()->get<config::OspfInterface::RETRANSMIT_INTERVAL>();
    if (ctx.negate)
    {
	retrans.unset();
	return true;
    }

    retrans.set(static_cast<uint16_t>(std::stoi(args[0])));
    return true;
}

bool InterfaceOspf_TransmitDelay_Handler(INTERFACE_PARAMS)
{
    auto& delay = ctx.currentInterface.getOspfConfig()->get<config::OspfInterfaceBase::BASE>().local()->get<config::OspfInterface::TRANSMIT_DELAY>();
    if (ctx.negate)
    {
	delay.unset();
	return true;
    }

    delay.set(static_cast<uint16_t>(std::stoi(args[0])));
    return true;
}
}
