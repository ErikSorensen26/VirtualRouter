// GlobalIPv6NDCommands.cpp

#include <Global.h>

#include "GlobalIPv6NDCommands.h"
#include "infrastructure/Ndp.h"

namespace cli
{
bool GlobalIPv6ND_CacheExpire_Handler(GLOBAL_PARAMS)
{
    uint16_t value = ctx.negate ? 600 : static_cast<uint16_t>(std::stoi(args[0]));
    ctx.global.configs.ndp.cacheExpire.store(value, std::memory_order_release);

    bool setRefresh = (args.size() > 1 && !ctx.negate) || ctx.negate;

    for (const auto& [_, iface] : ctx.global.getInterfaceList())
    {
        if (!iface->ndp->configs.cacheExpireLocal)
        {
            iface->ndp->configs.cacheExpire.store(value, std::memory_order_release); 
        }
        if (setRefresh && !iface->ndp->configs.refreshLocal)
        {
            iface->ndp->configs.refresh.store(!ctx.negate, std::memory_order_relaxed);
        }
    }
    return true;
}

bool GlobalIPv6ND_CacheIntLimit_Handler(GLOBAL_PARAMS)
{
    uint16_t value = ctx.negate ? 600 : static_cast<uint16_t>(std::stoi(args[0]));
    ctx.global.configs.ndp.interfaceLimit.store(value, std::memory_order_release);

    bool setLog = false;
    uint16_t log;
    if ((args.size() > 1 && !ctx.negate) || ctx.negate)
    {
        setLog = true;
        log = ctx.negate ? 0 : static_cast<uint16_t>(std::stoi(args[1]));
        ctx.global.configs.ndp.loggingRate.store(log, std::memory_order_release);
    }

    for (const auto& [_, iface] : ctx.global.getInterfaceList())
    {
        if (!iface->ndp->configs.interfaceLimitLocal)
        {
            iface->ndp->configs.interfaceLimit.store(value, std::memory_order_release); 
        }
        if (setLog && !iface->ndp->configs.loggingRateLocal)
        {
            iface->ndp->configs.loggingRate.store(log, std::memory_order_release);
        }
    }
    return true;
}

bool GlobalIPv6ND_DADTime_Handler(GLOBAL_PARAMS)
{
    uint16_t time = ctx.negate ? 1000 : static_cast<uint16_t>(std::stoi(args[0]));
    for (const auto& [_, iface] : ctx.global.getInterfaceList())
    {
        if (!iface->ndp->configs.dadTimeLocal)
        {
            iface->ndp->configs.dadTime.store(time, std::memory_order_release);
        }
    }
    return true;
}

bool GlobalIPv6ND_HostMode_Handler(GLOBAL_PARAMS)
{
    UNUSED(args);
    ctx.global.configs.ndp.strictMode.store(!ctx.negate, std::memory_order_relaxed);
    return true;
}

bool GlobalIPv6ND_NSFConvergence_Handler(GLOBAL_PARAMS)
{
    ctx.global.configs.ndp.nsfConvergenceTime.store(ctx.negate ? 180 : static_cast<uint16_t>(std::stoi(args[0])), std::memory_order_release);
    return true;
}

bool GlobalIPv6ND_NSFDADSuppress_Handler(GLOBAL_PARAMS)
{
    ctx.global.configs.ndp.nsfDadSupressionTime.store(ctx.negate ? 180 : static_cast<uint16_t>(std::stoi(args[0])), std::memory_order_release);
    return true;
}

bool GlobalIPv6ND_NSFThrottle_Handler(GLOBAL_PARAMS)
{
    ctx.global.configs.ndp.nsfThrottleResolutions.store(ctx.negate ? 1000 : static_cast<uint16_t>(std::stoi(args[0])), std::memory_order_release);
    return true;
}

bool GlobalIPv6ND_NudLimit_Handler(GLOBAL_PARAMS)
{
    ctx.global.configs.ndp.nudLimit.store(ctx.negate ? 5 : static_cast<uint16_t>(std::stoi(args[0])), std::memory_order_release);
    if (args.size() > 1)
    {
        ctx.global.configs.ndp.nudRefreshPeriod.store(ctx.negate ? 5 : static_cast<uint16_t>(std::stoi(args[2])), std::memory_order_release);
    }
    return true;
}

bool GlobalIPv6ND_ReachableTime_Handler(GLOBAL_PARAMS)
{
    uint16_t value = ctx.negate ? 30000 : static_cast<uint16_t>(std::stoi(args[0]));
    ctx.global.configs.ndp.reachableTime.store(value, std::memory_order_release);
    for (const auto& [_, iface] : ctx.global.getInterfaceList())
    {
        if (!iface->ndp->configs.reachableTimeLocal)
        {
            iface->ndp->configs.reachableTime.store(value, std::memory_order_release);
        }
    }
    return true;
}

bool GlobalIPv6ND_ResolutionLimit_Handler(GLOBAL_PARAMS)
{
    ctx.global.configs.ndp.resolutionLimit.store(ctx.negate ? 512 : static_cast<uint16_t>(std::stoi(args[0])), std::memory_order_release);
    return true;
}

bool GlobalIPv6ND_RouteOwner_Handler(GLOBAL_PARAMS)
{
    UNUSED(args);
    ctx.global.configs.ndp.ndAsRouteOwner.store(!ctx.negate, std::memory_order_release);
    return true;
}

}
