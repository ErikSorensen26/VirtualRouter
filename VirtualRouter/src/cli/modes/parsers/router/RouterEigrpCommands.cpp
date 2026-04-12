// RouterEigrpCommands.cpp

#include "RouterEigrpCommands.h"
#include "cli/parser/CliModeParser.hpp"
#include "cli/parser/CommandUtils.hpp"

#define EIGRP_PARAMS DEFINE_PARAMS(config::EigrpRegistry)

namespace cli
{
bool RouterEigrp_EigrpLogNeighborChanges_Handler(EIGRP_PARAMS)
{
    UNUSED(segs);
    auto& lc = ctx.configs.reg.get<config::Eigrp::LOG_NEIGHBOR_CHANGES>();
    utils::setToggleValue(lc, ctx);
    return true;
}

bool RouterEigrp_EigrpLogNeighborWarnings_Handler(EIGRP_PARAMS)
{
    auto& lw = ctx.configs.reg.get<config::Eigrp::LOG_NEIGHBOR_WARNINGS>();
    auto& lwinterval = ctx.configs.reg.get<config::Eigrp::LOG_NEIGHBOR_WARNINGS_INTERVAL>();

    if (!utils::setFieldValue(lw, ctx, segs >> 0 >> 1))
        return false;
    utils::setFieldValue(lwinterval, ctx, segs >> 1 >> 1);
    return true;
}

bool RouterEigrp_EigrpRouterId_Handler(EIGRP_PARAMS)
{
    auto& rid = ctx.configs.reg.get<config::Eigrp::ROUTER_ID>();
    return utils::setFieldValue(rid, ctx, segs >> 0 >> 1);
}

bool RouterEigrp_EigrpStub_Handler(EIGRP_PARAMS)
{
    auto& stubField = ctx.configs.reg.get<config::Eigrp::STUB>();
    if (utils::handleValueReset(stubField, ctx))
        return true;
    types::EnumBitMap<config::eigrp::Stub> stub;

    for (const auto& seg : segs)
    {
        switch (seg[0])
        {
            case "connected"_tok:
            {
                stub.set(config::eigrp::Stub::CONNECTED);
                break;
            }
            case "leak-map"_tok:
            {
                auto& leak = ctx.configs.reg.get<config::Eigrp::STUB_LEAK_MAP>();
                return utils::setFieldValue(leak, ctx, seg >> 1);
            }
            case "receive-only"_tok:
            {
                stub.set(config::eigrp::Stub::RECEIVE_ONLY);
                break;
            }
            case "redistributed"_tok:
            {
                stub.set(config::eigrp::Stub::REDISTRIBUTED);
                break;
            }
            case "static"_tok:
            {
                stub.set(config::eigrp::Stub::STATIC);
                break;
            }
            case "summary"_tok:
            {
                stub.set(config::eigrp::Stub::SUMMARY);
                break;
            }
        }
    }
    stubField.set(stub.raw());
    return true;
}

bool RouterEigrp_MetricWeights_Handler(EIGRP_PARAMS)
{
    auto& eigrp = ctx.configs;
    utils::setFieldValue(eigrp.reg.get<config::Eigrp::WEIGHT_K1>(), ctx, segs[0] >> 1);
    utils::setFieldValue(eigrp.reg.get<config::Eigrp::WEIGHT_K2>(), ctx, segs[0] >> 2);
    utils::setFieldValue(eigrp.reg.get<config::Eigrp::WEIGHT_K3>(), ctx, segs[0] >> 3);
    utils::setFieldValue(eigrp.reg.get<config::Eigrp::WEIGHT_K4>(), ctx, segs[0] >> 4);
    utils::setFieldValue(eigrp.reg.get<config::Eigrp::WEIGHT_K5>(), ctx, segs[0] >> 5);
    utils::setFieldValue(eigrp.reg.get<config::Eigrp::WEIGHT_K6>(), ctx, segs[0] >> 6);
    return true;
}

bool RouterEigrp_Neighbor_Handler(EIGRP_PARAMS)
{
    auto& neighbor = ctx.configs.reg.get<config::Eigrp::NEIGHBOR>();
    config::DefType<decltype(neighbor)>::node tup;

    if (!utils::setTupleElement(std::get<0>(tup), segs >> 0 >> 1))
        return false;
    if (!utils::setDoubleTupleElement(std::get<1>(tup), segs >> 1 >> 0, segs >> 1 >> 1))
        return false;
    return utils::setListEntry(neighbor, ctx, tup);
}

bool RouterEigrp_Network_Handler(EIGRP_PARAMS)
{
    auto& networks = ctx.configs.reg.get<config::Eigrp::NETWORK>();
    config::DefType<decltype(networks)>::node tup;
    if (!utils::setTupleElement(std::get<0>(tup), segs >> 0 >> 1))
        return false;
    types::IPv4Prefix mask;
    if (utils::setValue(mask, segs >> 0 >> 2))
    {
        if (!utils::extractSubnetMask(mask.addr, std::get<1>(tup)))
            return false;
    }
    else
    {
        std::get<1>(tup) = std::get<0>(tup).getDefaultMask();
    }
    return utils::setListEntry(networks, ctx, tup);
}

bool RouterEigrp_TimersGracefulRestart_Handler(EIGRP_PARAMS)
{
    auto& purgeTime = ctx.configs.reg.get<config::Eigrp::GRACEFUL_PURGE_TIME>();
    return utils::setFieldValue(purgeTime, ctx, segs[0] >> 1);
}

#define ROUTER_EIGRP_LIST(X, Y) \
    X(Y, (COMMAND, EigrpLogNeighborChanges, "eigrp"_tok, "log-neighbor-changes"_tok)) \
    X(Y, (COMMAND, EigrpLogNeighborWarnings, "eigrp"_tok, "log-neighbor-warnings"_tok)) \
    X(Y, (COMMAND, EigrpRouterId, "eigrp"_tok, "router-id"_tok)) \
    X(Y, (COMMAND, EigrpStub, "eigrp"_tok, "stub"_tok)) \
    X(Y, (COMMAND, MetricWeights, "metric"_tok, "weights"_tok)) \
    X(Y, (COMMAND, Neighbor, "neighbor"_tok)) \
    X(Y, (COMMAND, Network, "network"_tok)) \
    X(Y, (COMMAND, TimersGracefulRestart, "timers"_tok, "graceful-restart"_tok))

/**
 * @brief Parser for EIGRP classic mode commands.
 * @ingroup CLI_MODE_PARSERS
 *
 * Aggregates network/neighbor configuration, logging, metrics, stub mode,
 * and topology base access for classic (flat) EIGRP model.
 */
DEFINE_CMD_MODE(RouterEigrp, config::EigrpRegistry, ROUTER_EIGRP_LIST)
}

#undef EIGRP_PARAMS
