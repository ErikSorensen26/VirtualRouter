// GlobalIPDHCPCommands.cpp

#include "GlobalIPDHCPCommands.h"
#include <cstdint>
#include <Global.h>
#include <DhcpServer.h>

namespace Cli
{
bool GlobalIPDHCP_Binding_Handler(GLOBAL_PARAMS)
{
    uint16_t cleanupTime = std::stoi(args[2]);
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
        ctx.global.dhcpServer->configs.conflictInterval.store(std::stoi(args[1]));
    }
    return true;
}

bool GlobalIPDHCP_DatabaseTimeout_Handler(GLOBAL_PARAMS)
{
    std::unique_lock<std::shared_mutex> lock(ctx.global.dhcpServer->configs.configMutex);
    ctx.global.dhcpServer->configs.databaseSaveInterval[args[0]] = std::stoi(args[1]);
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
}
