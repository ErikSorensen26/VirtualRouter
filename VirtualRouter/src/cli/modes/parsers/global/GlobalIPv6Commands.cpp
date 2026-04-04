// GlobalIPv6Commands.h

#include <IPAddress.h>
#include <Global.h>
#include <VirtualRouter.h>

#include "GlobalIPv6Commands.h"
#include "Mac.hpp"
#include "interface/configs/InterfaceType.hpp"
#include "interface/configs/InterfaceConfigs.h"
#include "cli/runtime/CliSession.h"
#include "cli/runtime/CliUtils.h"
#include "cli/modes/contexts/EigrpContext.hpp"
#include "eigrp/core/Eigrp.h"
#include "GlobalHelpers.hpp"

namespace cli
{
bool GlobalIPv6_Neighbor_Handler(GLOBAL_PARAMS)
{
    //ListField<std::tuple<types::IPv6Address, interface::InterfaceKey, types::Mac> CONFIG_INDEX_ARG(Global::IPV6_NEIGHBOR)>,
    config::GlobalRegistry& g = getGlobalConfigs(ctx.global);
    auto& neighbors = g.get<config::Global::IPV6_NEIGHBOR>();
    typename config::DefType<decltype(neighbors)>::type tup;
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

    return utils::addListEntry(neighbors, ctx, tup);
}

bool GlobalIPv6_RouterEIGRP_Handler(GLOBAL_PARAMS)
{
    uint16_t asNum;
    if (!utils::stouint(asNum, segs[0][1]))
        return false;

    if (ctx.negate || ctx.defaulted)
    {
        routing::eigrp::EigrpAutonomousSystem* as = ctx.vrf.getEigrpAutonomousSystem(asNum); if (as)
        {
            if (!as->ipv6Named && as->ipv6)
            {
                delete as->ipv6;
                as->ipv6 = nullptr;
                if (!as->ipv4)
                {
                    ctx.vrf.removeEigrpAutonomousSystem(asNum);
                }
            }
        }
    }
    else
    {
        routing::eigrp::EigrpAutonomousSystem* as = ctx.vrf.getEigrpAutonomousSystem(asNum);
        if (as)
        {
            if (as->ipv6Named)
            {
                ctx.terminal.controller.print(std::string("\r\n%") + std::string(" ERROR: AS(" + std::to_string(asNum) + ") used by named mode"));
                return false; // AS used in named mode.
            }
        }
        else
        {
            as = ctx.vrf.addEigrpAutonomousSystem(asNum);
        }
        if (!as->ipv6)
        {
            as->ipv6 = new routing::eigrp::Eigrp(asNum, types::AddressFamily::IPv6, &ctx.vrf);
        }
        ctx.terminal.changeMode<CliMode::RouterEigrpClassicV6>(as->ipv6, nullptr, nullptr);
    }
    return true;
}
}
