// RouterEigrpAddressFamilyCommands.cpp

#include <Global.h>

#include "RouterEigrpAddressFamilyCommands.h"
#include "cli/parser/CliModeParser.hpp"

#include "RouterEigrpCommands.h"
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
    auto& ifaceCtx = ctx.configs.reg.get<config::Eigrp::AF_INTERFACE>().emplaceBack(key);
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
    auto& tag = ctx.configs.reg.get<config::Eigrp::DEFAULT_ROUTE_TAG>();
    return utils::setFieldValue(tag, ctx, segs >> 0 >> 1);
}

bool RouterEigrpAddressFamily_EigrpEventLogSize_Handler(EIGRP_PARAMS)
{
    auto& eventsiz = ctx.configs.reg.get<config::Eigrp::MAX_EVENT_LOG_SIZE>();
    return utils::setFieldValue(eventsiz, ctx, segs >> 0 >> 1);
}

bool RouterEigrpAddressFamily_Exit_Handler(EIGRP_PARAMS)
{
    UNUSED(segs);
    return ctx.terminal.popMode();
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
    auto& ribScale = ctx.configs.reg.get<config::Eigrp::RIB_SCALE>();
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
    auto& soft = ctx.configs.reg.get<config::Eigrp::NON_STOP_FORWARDING>();
    utils::setToggleValue(soft, ctx);
    return true;
}

#define ROUTER_EIGRP_LIST(X, Y) \
    X(Y, (COMMAND, EigrpDefaultRouteTag, "eigrp"_tok, "default-route-tag"_tok)) \
    X(Y, (COMMAND, EigrpEventLogSize, "eigrp"_tok, "event-log-size"_tok)) \
    X(Y, (COMMAND, Exit, "exit-address-family"_tok)) \
    X(Y, (COMMAND, MaximumPrefix, "maximum-prefix"_tok)) \
    X(Y, (COMMAND, MetricRibScale, "metric"_tok, "rib-scale"_tok)) \
    X(Y, (COMMAND, NeighborMaximumPrefix, "neighbor"_tok, "maximum-prefix"_tok)) \
    X(Y, (COMMAND, SoftSia, "soft-sia"_tok)) \

/**
 * @brief Parser for EIGRPv4 address-family commands (shared IPv4/IPv6).
 * @ingroup CLI_MODE_PARSERS
 *
 * Aggregates AF-level configuration including networks, neighbors, metrics,
 * and stub mode settings.
 */
DEFINE_CMD_MODE(RouterEigrpAddressFamily, config::EigrpRegistry, ROUTER_EIGRP_LIST);

#define ROUTER_EIGRP_LIST_V4(X, Y) \
    X(Y, (COMMAND, AfInterface, "af-interface"_tok)) \
    X(Y, (COMMAND, Topology, "topology"_tok, "base"_tok)) \
    X(Y, (INHERIT, RouterEigrpCommands)) \
    X(Y, (INHERIT, RouterEigrpAddressFamilyCommands))

/**
 * @brief IPv4 address-family mode parser.
 * @ingroup CLI_MODE_PARSERS
 */
DEFINE_CMD_MODE(RouterEigrpAddressFamilyV4, config::EigrpRegistry, ROUTER_EIGRP_LIST_V4)

#define ROUTER_EIGRP_LIST_V6(X, Y) \
    X(Y, (COMMAND, AfInterface, "af-interface"_tok)) \
    X(Y, (COMMAND, Topology, "topology"_tok, "base"_tok)) \
    X(Y, (INHERIT, RouterEigrpCommands)) \
    X(Y, (INHERIT, RouterEigrpAddressFamilyCommands))

/**
 * @brief IPv6 address-family mode parser.
 * @ingroup CLI_MODE_PARSERS
 */
DEFINE_CMD_MODE(RouterEigrpAddressFamilyV6, config::EigrpRegistry, ROUTER_EIGRP_LIST_V6)
}

#undef EIGRP_PARAMS
