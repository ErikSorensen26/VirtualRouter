// GlobalIPv6Commands.h

#include <IPAddress.h>
#include <Global.h>
#include <VirtualRouter.h>

#include "GlobalIPv6Commands.h"
#include "interface/configs/InterfaceType.hpp"
#include "interface/configs/InterfaceConfigs.h"
#include "infrastructure/Ndp.h"
#include "cli/runtime/CliSession.h"
#include "cli/runtime/CliUtils.h"
#include "cli/modes/contexts/EigrpContext.hpp"
#include "eigrp/core/Eigrp.h"

namespace cli
{
bool GlobalIPv6_Neighbor_Handler(GLOBAL_PARAMS)
{
    types::IPv6Address address; cli::utils::extractIPv6Address(args[0], address);
    if (!ctx.negate)
    {
        interface::InterfaceType type = interface::getInterfaceType(args[1]);
        float id = std::stof(args[2]);
        uint32_t intID = interface::calculateInterfaceKey(type, id);
        uint64_t _mac = 0; cli::utils::extractMacAddress(args[3], _mac);
        core::GlobalConfigs::Ndp::Neighbor entry{
                intID,
                _mac
        };
        ctx.global.configs.ndp.neighbors.emplace(
            address,
            entry
        );
        for (const auto& [key, iface] : ctx.global.getInterfaceList())
        {
                if (key == entry.interface)
                {
                    iface->ndp.addNdpEntry(address, entry.macAddress, false, true);
                }
        }
    }
    return true;
}

bool GlobalIPv6_RouterEIGRP_Handler(GLOBAL_PARAMS)
{
    uint16_t asNum = static_cast<uint16_t>(std::stoi(args[0]));
    if (!ctx.negate)
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
    else
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
    return true;
}
}
