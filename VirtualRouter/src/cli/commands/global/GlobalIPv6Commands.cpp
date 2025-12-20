// GlobalIPv6Commands.h

#include "GlobalIPv6Commands.h"
#include "GlobalIPv6NDCommands.h"
#include <IPAddress.hpp>
#include <InterfaceType.hpp>
#include <InterfaceConfigs.h>
#include <Global.h>
#include <Ndp.h>
#include <Functions.h>
#include <VirtualRouter.h>
#include <CliSession.h>
#include <EigrpContext.hpp>

namespace Cli
{
bool GlobalIPv6_ND_Handler(GLOBAL_PARAMS)
{
    return GlobalIPv6NDCommands::execute(ctx, args);
}

bool GlobalIPv6_Neighbor_Handler(GLOBAL_PARAMS)
{
    IPAddress address = Functions::getAddress(args[0]);
    if (!ctx.negate)
    {
        InterfaceType type = getInterfaceType(args[1]);
        float id = std::stof(args[2]);
        uint32_t intID = calculateInterfaceKey(type, id);
        GlobalConfigs::Ndp::Neighbor entry{
                intID,
                Functions::macToInt(args[3])
        };
        ctx.global.configs.ndp.neighbors.emplace(
                address,
                entry
        );
        for (const auto& [key, iface] : ctx.global.getInterfaceList())
        {
                if (key == entry.interface)
                {
                        iface->ndp->addNdpEntry(address, entry.macAddress, false, true);
                }
        }
    }
    return true;
}

bool GlobalIPv6_RouterEIGRP_Handler(GLOBAL_PARAMS)
{
    std::string ID = args[0];
    ctx.terminal.routingProtocolID = std::stoi(args[0]);
    if (!ctx.negate)
    {
        Eigrp::EigrpAutonomousSystem* as = ctx.vrf.getEigrpAutonomousSystem(ctx.terminal.routingProtocolID);
        if (as)
        {
            if (as->ipv6Named)
            {
                ctx.terminal.iConsole->print(std::string("\r\n%") + std::string(" ERROR: AS(" + ID + ") used by named mode"));
                return false; // AS used in named mode.
            }
        }
        else
        {
            as = ctx.vrf.addEigrpAutonomousSystem(ctx.terminal.routingProtocolID);
        }
        if (!as->ipv6)
        {
            as->ipv6 = new Eigrp::Eigrp(ctx.terminal.routingProtocolID, AddressFamily::IPv6, &ctx.vrf);
        }
        ctx.terminal.configureRoutingMode("eigrp_classic", true);
        ctx.terminal.changeModeConfig(new Cli::EigrpContext(ctx, as->ipv6, nullptr));
    }
    else
    {
        Eigrp::EigrpAutonomousSystem* as = ctx.vrf.getEigrpAutonomousSystem(ctx.terminal.routingProtocolID); if (as)
        {
            if (!as->ipv6Named && as->ipv6)
            {
                delete as->ipv6;
                as->ipv6 = nullptr;
                if (!as->ipv4)
                {
                    ctx.vrf.removeEigrpAutonomousSystem(ctx.terminal.routingProtocolID);
                }
            }
        }
    }
    return true;
}
}
