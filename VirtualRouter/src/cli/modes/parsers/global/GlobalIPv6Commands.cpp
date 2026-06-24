// GlobalIPv6Commands.h

#include <IPAddress.h>
#include <Global.h>
#include <VirtualRouter.h>

#include "GlobalIPv6Commands.h"
#include "cli/parser/CliModeParser.hpp"
#include "GlobalIPv6NDCommands.h"
#include "cli/runtime/CliSession.h"
#include "cli/parser/CommandUtils.hpp"
#include "GlobalHelpers.hpp"
#include "configs/FieldAccessor.hpp"

#define GLOBAL_PARAMS DEFINE_PARAMS(config::GlobalRegistry)
#define GLOBAL_SUB_PARAMS DEFINE_SUB_PARAMS(config::GlobalRegistry)

namespace cli
{
bool GlobalIPv6_ND_SubHandler(GLOBAL_SUB_PARAMS)
{
    auto& ndp = ctx.configs().get<config::Global::IPV6_ND>().get();
    Context<config::NdpBaseRegistry> newCtx(ctx.terminal, ndp);
    newCtx.negate = ctx.negate;
    newCtx.defaulted = ctx.defaulted;
    return GlobalIPv6NDCommands::execute(newCtx, toks, idx);
}

bool GlobalIPv6_Neighbor_Handler(GLOBAL_PARAMS)
{
    //ListField<std::tuple<types::IPv6Address, interface::InterfaceKey, types::Mac> CONFIG_INDEX_ARG(Global::IPV6_NEIGHBOR)>,
    auto neighbors = ctx.configs().get<config::Global::IPV6_NEIGHBOR>();
    typename config::DefType<decltype(neighbors)::Field>::node tup;
    for (const auto& seg : segs)
    {
        switch (seg[0])
        {
            case "neighbor"_tok:
            {
                if (!utils::setTupleElement(std::get<0>(tup), seg >> 1))
                    return false;
                break;
            }
            case ALL_INTERFACE_CASE:
            {
                if (!utils::setDoubleTupleElement(std::get<1>(tup), seg >> 0, seg >> 1))
                    return false;
                if (!utils::setTupleElement(std::get<2>(tup), seg >> 2))
                    return false;
                break;
            }
            default: return false;
        }
    }

    return utils::setListEntry(neighbors, ctx, tup);
}

bool GlobalIPv6_RouterEIGRP_Handler(GLOBAL_PARAMS)
{
    uint16_t asNum;
    if (!utils::stouint(asNum, segs[0][1]))
        return false;
    config::VrfRegistry* vrf;
    if (!getVrfConfigs(vrf, ctx))
        return false;
    auto eigrpList = vrf->get<config::Vrf::ROUTER_EIGRP_V6>();
    if (auto it = eigrpList.find(asNum); it != eigrpList.end())
    {
        if (it->second->get<config::Eigrp::IS_NAMED>().load())
        {
            ctx.terminal.controller.print(std::string("\r\n%") + std::string(" ERROR: AS(" + std::to_string(asNum) + ") used by named mode"));
            return false; 
        }
    }
    else
    {
        utils::setOwnedField(eigrpList, ctx, asNum);
    }
    return ctx.terminal.changeMode<CliMode::RouterEigrpClassicV6>(*eigrpList.get().at(asNum));
}

#define GLOBAL_IPV6_LIST(X, Y) \
    X(Y, (SUBPRSR, ND, "nd"_tok)) \
    X(Y, (COMMAND, Neighbor, "neighbor"_tok)) \
    X(Y, (COMMAND, RouterEIGRP, "router"_tok, "eigrp"_tok))

DEFINE_CMD_MODE(GlobalIPv6, config::GlobalRegistry, GLOBAL_IPV6_LIST);
}

#undef GLOBAL_PARAMS
#undef GLOBAL_SUB_PARAMS
