// RouterEigrpAddressFamilyCommands.cpp

#include "RouterEigrpAddressFamilyCommands.h"

#include "configs/registry/router/EigrpRegistry.h"
#include "cli/runtime/CliSession.h"
#include "cli/parser/CommandUtils.hpp"
#include "interface/configs/InterfaceType.hpp"

#define EIGRP_PARAMS DEFINE_PARAMS(config::EigrpRegistry)

namespace cli
{
static bool handleAfInterface(EIGRP_PARAMS, types::AddressFamily af)
{
    interface::InterfaceKey key;
    if (!utils::setDoubleValue(key, segs[0] >> 0, segs[0] >> 1))
        return false;
    auto& ifaceCtx = ctx.configs.get<config::Eigrp::AF_INTERFACE>().emplaceBack(key);
    if (af == types::AddressFamily::IPv4)
        return ctx.terminal.changeMode<CliMode::RouterEigrpInterfaceV4>(ifaceCtx);
    else
        return ctx.terminal.changeMode<CliMode::RouterEigrpInterfaceV6>(ifaceCtx);
}

static bool handleTopologyBase(Context<config::EigrpRegistry>& ctx, types::AddressFamily af)
{
    if (af == types::AddressFamily::IPv4)
        return ctx.terminal.changeMode<CliMode::RouterEigrpTopologyV4>(ctx.configs);
    else
        return ctx.terminal.changeMode<CliMode::RouterEigrpTopologyV6>(ctx.configs);
}

bool RouterEigrpAddressFamilyV4_Topology_Handler(EIGRP_PARAMS)
{
    UNUSED(segs);
    return handleTopologyBase(ctx, types::AddressFamily::IPv4);
}

bool RouterEigrpAddressFamilyV6_Topology_Handler(EIGRP_PARAMS)
{
    UNUSED(segs);
    return handleTopologyBase(ctx, types::AddressFamily::IPv6);
}

bool RouterEigrpAddressFamilyV4_AfInterface_Handler(EIGRP_PARAMS)
{
    return handleAfInterface(ctx, segs, types::AddressFamily::IPv4);
}

bool RouterEigrpAddressFamilyV6_AfInterface_Handler(EIGRP_PARAMS)
{
    return handleAfInterface(ctx, segs, types::AddressFamily::IPv6);
}

bool RouterEigrpAddressFamily_EigrpDefaultRouteTag_Handler(EIGRP_PARAMS)
{
    auto& tag = ctx.configs.get<config::Eigrp::DEFAULT_ROUTE_TAG>();
    return utils::setFieldValue(tag, ctx, segs >> 0 >> 1);
}

bool RouterEigrpAddressFamily_EigrpEventLogSize_Handler(EIGRP_PARAMS)
{
    auto& eventsiz = ctx.configs.get<config::Eigrp::MAX_EVENT_LOG_SIZE>();
    return utils::setFieldValue(eventsiz, ctx, segs >> 0 >> 1);
}

bool RouterEigrpAddressFamily_Exit_Handler(EIGRP_PARAMS)
{
    UNUSED(segs);
    ctx.terminal.exitMode<CliMode::RouterEigrpNamed, config::EigrpNamedRegistry>(ctx.configs);
    return true;
}

bool RouterEigrpAddressFamily_MaximumPrefix_Handler(EIGRP_PARAMS)
{
    utils::FieldSetter<config::EigrpRegistry,
        config::Eigrp::MAXIMUM_PREFIX,
        config::Eigrp::DAMPENING,
        config::Eigrp::DAMPENING_WARNINGS,
        config::Eigrp::DAMPENING_THRESHOLD,
        config::Eigrp::DAMPENING_RESET_TIME,
        config::Eigrp::DAMPENING_RESTART,
        config::Eigrp::DAMPENING_RESTART_COUNT
    > setter;

    for (const auto& seg : segs)
    {
        switch (seg[0])
        {
            case "maximum-prefix"_tok:
            {
                if (!setter.set(config::Eigrp::MAXIMUM_PREFIX, ctx, seg >> 1))
                    return false;
                setter.set(config::Eigrp::DAMPENING_THRESHOLD, ctx, seg >> 1);
                break;
            }
            case "dampened"_tok:
            {
                if (!setter.set(config::Eigrp::DAMPENING, ctx, seg >> 0))
                    return false;
                break;
            }
            case "reset-time"_tok:
            {
                if (!setter.set(config::Eigrp::DAMPENING_RESET_TIME, ctx, seg >> 1))
                    return false;
                break;
            }
            case "restart"_tok:
            {
                if (!setter.set(config::Eigrp::DAMPENING_RESTART, ctx, seg >> 1))
                    return false;
                break;
            }
            case "restart-count"_tok:
            {
                if (!setter.set(config::Eigrp::DAMPENING_RESTART_COUNT, ctx, seg >> 1))
                    return false;
                break;
            }
        }
    }

    setter.clearLeft(ctx);
    return true;
}

bool RouterEigrpAddressFamily_MetricRibScale_Handler(EIGRP_PARAMS)
{
    auto& ribScale = ctx.configs.get<config::Eigrp::RIB_SCALE>();
    return utils::setFieldValue(ribScale, ctx, segs[0] >> 1);
}

bool RouterEigrpAddressFamily_NeighborMaximumPrefix_Handler(EIGRP_PARAMS)
{
    utils::FieldSetter<config::EigrpRegistry,
        config::Eigrp::NEIGHBOR_MAXIMUM_PREFIX,
        config::Eigrp::NEIGHBOR_DAMPENING,
        config::Eigrp::NEIGHBOR_DAMPENING_WARNINGS,
        config::Eigrp::NEIGHBOR_DAMPENING_THRESHOLD,
        config::Eigrp::NEIGHBOR_DAMPENING_RESET_TIME,
        config::Eigrp::NEIGHBOR_DAMPENING_RESTART,
        config::Eigrp::NEIGHBOR_DAMPENING_RESTART_COUNT
    > setter;

    for (const auto& seg : segs)
    {
        switch (seg[0])
        {
            case "maximum-prefix"_tok:
            {
                if (!setter.set(config::Eigrp::NEIGHBOR_MAXIMUM_PREFIX, ctx, seg >> 1))
                    return false;
                setter.set(config::Eigrp::DAMPENING_THRESHOLD, ctx, seg >> 1);
                break;
            }
            case "dampened"_tok:
            {
                if (!setter.set(config::Eigrp::NEIGHBOR_DAMPENING, ctx, seg >> 0))
                    return false;
                break;
            }
            case "reset-time"_tok:
            {
                if (!setter.set(config::Eigrp::NEIGHBOR_DAMPENING_RESET_TIME, ctx, seg >> 1))
                    return false;
                break;
            }
            case "restart"_tok:
            {
                if (!setter.set(config::Eigrp::NEIGHBOR_DAMPENING_RESTART, ctx, seg >> 1))
                    return false;
                break;
            }
            case "restart-count"_tok:
            {
                if (!setter.set(config::Eigrp::NEIGHBOR_DAMPENING_RESTART_COUNT, ctx, seg >> 1))
                    return false;
                break;
            }
        }
    }

    setter.clearLeft(ctx);
    return true;
}

bool RouterEigrpAddressFamily_SoftSia_Handler(EIGRP_PARAMS)
{
    UNUSED(segs);
    auto& soft = ctx.configs.get<config::Eigrp::NON_STOP_FORWARDING>();
    utils::setToggleValue(soft, ctx);
    return true;
}
}

#undef EIGRP_PARAMS
