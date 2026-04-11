// InterfaceOspfCommands.cpp

#include <VirtualRouter.h>

#include "InterfaceOspfCommands.h"
#include "configs/registry/router/OspfInterfaceRegistry.h"
#include "cli/parser/CommandUtils.hpp"

#define OSPF_PARAMS DEFINE_PARAMS(config::OspfInterfaceBaseRegistry)

namespace cli
{
bool InterfaceOspf_BFD_Handler(OSPF_PARAMS)
{
    auto& ospf = ctx.configs.get<config::OspfInterfaceBase::BASE>().get();
    auto& bfd = ospf.get<config::OspfInterface::BFD>();
    if (!utils::handleValueReset(bfd, ctx))
	return true;
    bool disable = segs >> 1; // Disable
    bfd.set(!disable);
    return true;
}

bool InterfaceOspf_Cost_Handler(OSPF_PARAMS)
{
    auto& cost = ctx.configs.get<config::OspfInterfaceBase::BASE>().get().get<config::OspfInterface::COST>();
    return utils::setFieldValue(cost, ctx, segs >> 0 >> 1);
}

bool InterfaceOspf_DatabaseFilter_Handler(OSPF_PARAMS)
{
    UNUSED(segs);
    auto& ospf = ctx.configs.get<config::OspfInterfaceBase::BASE>().get();
    auto& dbFilter = ospf.get<config::OspfInterface::DATABASE_FILTER>();
    utils::setToggleValue(dbFilter, ctx);
    return true;
}

bool InterfaceOspf_DeadInterval_Handler(OSPF_PARAMS)
{
    auto& ospf = ctx.configs.get<config::OspfInterfaceBase::BASE>().get();
    auto& deadInterval = ospf.get<config::OspfInterface::DEAD_INTERVAL>();
    auto& helloInterval = ospf.get<config::OspfInterface::HELLO_INTERVAL>();
    auto& helloMultiplier = ospf.get<config::OspfInterface::HELLO_MULTIPLIER>();


    for (const auto& seg : segs)
    {
	switch (seg[0])
	{
	    case "dead-interval"_tok:
	    {
		if (helloMultiplier.hasValue()) helloMultiplier.unset();
		return utils::setFieldValue(deadInterval, ctx, seg >> 1);
	    }
	    case "hello-multiplier"_tok:
	    {

		if (deadInterval.hasValue()) deadInterval.unset();
		if (helloInterval.hasValue()) helloInterval.unset();
		return utils::setFieldValue(helloMultiplier, ctx, seg >> 1);
	    }
	}
    }
    return false;
}

bool InterfaceOspf_DemandCircuit_Handler(OSPF_PARAMS)
{
    UNUSED(segs);
    auto& ospf = ctx.configs.get<config::OspfInterfaceBase::BASE>().get();
    auto& dc = ospf.get<config::OspfInterface::DEMAND_CIRCUIT>();
    auto& dcig = ospf.get<config::OspfInterface::DEMAND_CIRCUIT>();

    if (utils::setFieldValue(dcig, ctx, segs >> 0 >> 1))
	return true;
    utils::setToggleValue(dc, ctx);
    return true;
}

bool InterfaceOspf_FloodReduction_Handler(OSPF_PARAMS)
{
    UNUSED(segs);
    auto& ospf = ctx.configs.get<config::OspfInterfaceBase::BASE>().get();
    auto& floodReduction = ospf.get<config::OspfInterface::FLOOD_REDUCTION>();
    utils::setToggleValue(floodReduction, ctx);
    return true;
}

bool InterfaceOspf_HelloInterval_Handler(OSPF_PARAMS)
{
    auto& ospf = ctx.configs.get<config::OspfInterfaceBase::BASE>().get();
    auto& hello = ospf.get<config::OspfInterface::HELLO_INTERVAL>();
    return utils::setFieldValue(hello, ctx, segs >> 0 >> 1);
}

bool InterfaceOspf_MtuIgnore_Handler(OSPF_PARAMS)
{
    UNUSED(segs);
    auto& ospf = ctx.configs.get<config::OspfInterfaceBase::BASE>().get();
    auto& mtuIgnore = ospf.get<config::OspfInterface::MTU_IGNORE>();
    utils::setToggleValue(mtuIgnore, ctx);
    return true;
}

bool InterfaceOspf_Network_Handler(OSPF_PARAMS)
{
    auto& ospf = ctx.configs.get<config::OspfInterfaceBase::BASE>().get();
    auto& network = ospf.get<config::OspfInterface::NETWORK>();

    if (utils::handleValueReset(network, ctx))
	return true;

    auto* tok = (segs >> 0 >> 0).ptr;
    if (!tok) return false;

    switch (*tok)
    {
	case "broadcast"_tok:
	{
	    network.set(config::ospf::NetworkType::BROADCAST);
	    return true;
	}
	case "non-broadcast"_tok:
	{
	    network.set(config::ospf::NetworkType::NON_BROADCAST);
	    return true;
	}
	case "point-to-multipoint"_tok:
	{
	    if (auto t = segs >> 1; t)
	    {
		if ((*t.ptr)[0] == "non-broadcast"_tok)
		{
		    network.set(config::ospf::NetworkType::POINT_TO_MULTIPOINT);
		    return true;
		}
		return false;
	    }
	    else network.set(config::ospf::NetworkType::POINT_TO_MULTIPOINT_BROADCAST);
	    return true;
	}
	case "point-to-point"_tok:
	{
	    network.set(config::ospf::NetworkType::POINT_TO_POINT);
	    return true;
	}
    }
    return false;
}

bool InterfaceOspf_Priority_Handler(OSPF_PARAMS)
{
    auto& ospf = ctx.configs.get<config::OspfInterfaceBase::BASE>().get();
    auto& priority = ospf.get<config::OspfInterface::PRIORITY>();
    return utils::setFieldValue(priority, ctx, segs >> 0 >> 1);
}

bool InterfaceOspf_RetransmitInterval_Handler(OSPF_PARAMS)
{
    auto& ospf = ctx.configs.get<config::OspfInterfaceBase::BASE>().get();
    auto& retrans = ospf.get<config::OspfInterface::RETRANSMIT_INTERVAL>();
    return utils::setFieldValue(retrans, ctx, segs >> 0 >> 1);
}

bool InterfaceOspf_TransmitDelay_Handler(OSPF_PARAMS)
{
    auto& ospf = ctx.configs.get<config::OspfInterfaceBase::BASE>().get();
    auto& delay = ospf.get<config::OspfInterface::TRANSMIT_DELAY>();
    return utils::setFieldValue(delay, ctx, segs >> 0 >> 1);
}
}

#undef OSPF_PARAMS
