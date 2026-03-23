// GlobalIPDHCPCommands.cpp

#include <cstdint>

#include <Global.h>
#include <VirtualRouter.h>

#include "GlobalIPDHCPCommands.h"
#include "cli/runtime/CliUtils.h"
#include "dhcp/dhcpv4/DhcpServer.h"

namespace cli
{
bool GlobalIPDHCP_Binding_Handler(GLOBAL_PARAMS)
{
    uint16_t cleanupTime = static_cast<uint16_t>(std::stoi(args[2]));
    ctx.global.dhcpServer->configs.bindingCleanup.store(cleanupTime, std::memory_order_release);
    return true;
}

bool GlobalIPDHCP_Bootp_Handler(GLOBAL_PARAMS)
{
    UNUSED(args);
    ctx.global.dhcpServer->configs.bootp.ignore.store(true, std::memory_order_release);
    return true;
}

bool GlobalIPDHCP_ConflictLogging_Handler(GLOBAL_PARAMS)
{
    UNUSED(args);
    ctx.global.dhcpServer->configs.logConflicts.store(true, std::memory_order_release);
    return true;
}

bool GlobalIPDHCP_ConflictResolution_Handler(GLOBAL_PARAMS)
{
    if (args.empty())
    {
        //TODO
    }
    else
    {
        ctx.global.dhcpServer->configs.conflictInterval.store(static_cast<uint8_t>(std::stoi(args[1])));
    }
    return true;
}

bool GlobalIPDHCP_DatabaseTimeout_Handler(GLOBAL_PARAMS)
{
    std::unique_lock<std::shared_mutex> lock(ctx.global.dhcpServer->configs.configMutex);
    ctx.global.dhcpServer->configs.databaseSaveInterval[args[0]] = static_cast<uint16_t>(std::stoi(args[1]));
    return true;
}

bool GlobalIPDHCP_DatabaseWrite_Handler(GLOBAL_PARAMS)
{
    std::unique_lock<std::shared_mutex> lock(ctx.global.dhcpServer->configs.configMutex);
    ctx.global.dhcpServer->configs.writeDelay[args[0]] = {std::stoi(args[1]), args.size() >= 4 ? std::stoi(args[3]) : 0};
    return true;
}

bool GlobalIPDHCP_Debug_Handler(GLOBAL_PARAMS)
{
    UNUSED(args);
    ctx.global.dhcpServer->configs.logAsciiClientID.store(false, std::memory_order_relaxed);
    return true;
}

bool GlobalIPDHCP_ExcludedAddress_Handler(GLOBAL_PARAMS)
{
    size_t start = 0;
    core::VirtualRouter* vrf = &ctx.vrf;
    if (args[0] == "vrf")
    {
        vrf = ctx.global.getRoutingInstance(args[1]);
        start = 2;
    }
    if (!vrf) return false;

    types::IPv4Address ipStart; cli::utils::extractIPv4Address(args[start], ipStart);
    types::IPv4Address ipEnd; cli::utils::extractIPv4Address(args[start + 1], ipEnd);

    {
        //TODO
        //std::unique_lock<std::shared_mutex> lock(ctx.global.dhcpServer->configs)
    }
    return true;
}


}
