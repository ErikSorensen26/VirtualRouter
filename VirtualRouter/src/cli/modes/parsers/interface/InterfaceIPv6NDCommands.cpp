// InterfaceIpv6NDCommands.cpp

#include <VirtualRouter.h>
#include <Global.h>

#include "InterfaceIPv6NDCommands.h"
#include "interface/Interface.h"
#include "infrastructure/Ndp.h"
#include "cli/runtime/CliUtils.h"

namespace cli
{
bool InterfaceIPv6ND_AdvertisementInterval_Handler(INTERFACE_PARAMS)
{
    UNUSED(args);
    ctx.currentInterface.ndp.configs.advertisementInterval.store(!ctx.negate, std::memory_order_release);
    return true;
}

bool InterfaceIPv6ND_AutoconfigDefRoute_Handler(INTERFACE_PARAMS)
{
    UNUSED(args);
    ctx.currentInterface.ndp.configs.autoConfigDefaultRoute.store(!ctx.negate, std::memory_order_release);
    return true;
}

bool InterfaceIPv6ND_AutoconfigPrefix_Handler(INTERFACE_PARAMS)
{
    UNUSED(args);
    ctx.currentInterface.ndp.configs.autoConfigPrefix.store(!ctx.negate, std::memory_order_release);
    return true;
}

bool InterfaceIPv6ND_CacheExpire_Handler(INTERFACE_PARAMS)
{
    /*
    ctx.currentInterface.ndp.configs.cacheExpire.store(ctx.negate
	? ctx.currentInterface.getVRF()->getGlobal().configs.ndp.cacheExpire.load(std::memory_order_relaxed)
	: static_cast<uint16_t>(std::stoul(args[0]), std::memory_order_release));
    ctx.currentInterface.ndp.configs.cacheExpireLocal = !ctx.negate;
    if ((args.size() == 2 && args[1] == "refresh" && !ctx.negate) || ctx.negate)
    {
	ctx.currentInterface.ndp.configs.refreshLocal = !ctx.negate;
	ctx.currentInterface.ndp.configs.refresh.store(ctx.negate ? ctx.currentInterface.getVRF()->getGlobal().configs.ndp.refresh.load(std::memory_order_relaxed) : true, std::memory_order_release);
    }
    return true;
    */
}

bool InterfaceIPv6ND_CacheInterfaceLimit_Handler(INTERFACE_PARAMS)
{
    /*
    ctx.currentInterface.ndp.configs.interfaceLimit.store(ctx.negate
	? ctx.currentInterface.getVRF()->getGlobal().configs.ndp.interfaceLimit.load(std::memory_order_relaxed)
	: static_cast<uint32_t>(std::stoul(args[0]), std::memory_order_release));
    ctx.currentInterface.ndp.configs.interfaceLimitLocal = !ctx.negate;
    if ((args.size() == 3 && args[1] == "log") || ctx.negate)
    {
	ctx.currentInterface.ndp.configs.loggingRate.store(ctx.negate
	    ? ctx.currentInterface.getVRF()->getGlobal().configs.ndp.loggingRate.load(std::memory_order_relaxed)
	    : static_cast<uint16_t>(std::stoul(args[2]), std::memory_order_release));
	ctx.currentInterface.ndp.configs.loggingRateLocal = !ctx.negate;
    }
    */
    return true;
}

bool InterfaceIPv6ND_DADAttempts_Handler(INTERFACE_PARAMS)
{
    ctx.currentInterface.ndp.configs.dadAttempts.store(ctx.negate ? 1 : static_cast<uint16_t>(std::stoul(args[0])), std::memory_order_release);
    return true;
}

bool InterfaceIPv6ND_DADTime_Handler(INTERFACE_PARAMS)
{
    /*
    ctx.currentInterface.ndp.configs.dadTime.store(ctx.negate
	? ctx.currentInterface.getVRF()->getGlobal().configs.ndp.dadTime.load(std::memory_order_relaxed)
	: static_cast<uint16_t>(std::stoul(args[0]), std::memory_order_release));
    ctx.currentInterface.ndp.configs.dadTimeLocal = !ctx.negate;
    */
    return true;
}

bool InterfaceIPv6ND_DestinationGuard_Handler(INTERFACE_PARAMS)
{
    UNUSED(args);
    ctx.currentInterface.ndp.configs.destinationGuard.store(!ctx.negate, std::memory_order_release);
    return true;
}

bool InterfaceIPv6ND_ManagedConfigFlag_Handler(INTERFACE_PARAMS)
{
    UNUSED(args);
    ctx.currentInterface.ndp.configs.managedConfigFlag.store(!ctx.negate, std::memory_order_release);
    return true;
}

bool InterfaceIPv6ND_NaGlean_Handler(INTERFACE_PARAMS)
{
    UNUSED(args);
    ctx.currentInterface.ndp.configs.naGlean.store(!ctx.negate, std::memory_order_release);
    return true;
}

bool InterfaceIPv6ND_NsInterval_Handler(INTERFACE_PARAMS)
{
    ctx.currentInterface.ndp.configs.nsInterval.store(ctx.negate ? 1000
	: static_cast<uint32_t>(std::stoul(args[0])), std::memory_order_release);
    return true;
}

bool InterfaceIPv6ND_NudIGP_Handler(INTERFACE_PARAMS)
{
    UNUSED(args);
    ctx.currentInterface.ndp.configs.nudIgp.store(!ctx.negate, std::memory_order_release);
    return true;
}

bool InterfaceIPv6ND_NudRetry_Handler(INTERFACE_PARAMS)
{
    if (ctx.negate)
    {
	    std::unique_lock<std::shared_mutex> lock(ctx.currentInterface.ndp.configs.configMutex);
	    ctx.currentInterface.ndp.configs.nudBase = 3;
	    ctx.currentInterface.ndp.configs.nudBaseInterval = 1000;
	    ctx.currentInterface.ndp.configs.nudBaseAttempts = 3;
	    ctx.currentInterface.ndp.configs.nudFinalWait = 60000;
    }
    else
    {
	    std::unique_lock<std::shared_mutex> lock(ctx.currentInterface.ndp.configs.configMutex);
	    ctx.currentInterface.ndp.configs.nudBase = static_cast<uint8_t>(std::stoul(args[0]));
	    ctx.currentInterface.ndp.configs.nudBaseInterval = static_cast<uint16_t>(std::stoul(args[1]));
	    ctx.currentInterface.ndp.configs.nudBaseAttempts = static_cast<uint16_t>(std::stoul(args[2]));
	    if (args.size() > 3)
	    {
		    ctx.currentInterface.ndp.configs.nudFinalWait = static_cast<uint16_t>(std::stoul(args[3]));
	    }
    }
    return true;
}

bool InterfaceIPv6ND_OtherConfigFlag_Handler(INTERFACE_PARAMS)
{
    UNUSED(args);
    ctx.currentInterface.ndp.configs.otherConfigFlag.store(!ctx.negate, std::memory_order_release);
    return true;
}

bool InterfaceIPv6ND_RaHopLimitUnspecified_Handler(INTERFACE_PARAMS)
{
    UNUSED(args);
    ctx.currentInterface.ndp.configs.raHopLimitUnspecified.store(!ctx.negate, std::memory_order_release);
    return true;
}

bool InterfaceIPv6ND_RaInterval_Handler(INTERFACE_PARAMS)
{
    if (ctx.negate)
    {
	std::unique_lock<std::shared_mutex> lock(ctx.currentInterface.ndp.configs.configMutex);
	ctx.currentInterface.ndp.configs.raIntervalMS = true;
	ctx.currentInterface.ndp.configs.raInterval = 600000;
	ctx.currentInterface.ndp.configs.raIntervalMin = 3000;
    }
    else if (utils::isNumber(args[0]))
    {
	std::unique_lock<std::shared_mutex> lock(ctx.currentInterface.ndp.configs.configMutex);
	ctx.currentInterface.ndp.configs.raIntervalMS = false;
	ctx.currentInterface.ndp.configs.raInterval = static_cast<uint32_t>(std::stoul(args[0]));
	ctx.currentInterface.ndp.configs.raIntervalMin = static_cast<uint32_t>(std::stoul(args[1]));
    }
    else if (args[0] == "msec")
    {
	std::unique_lock<std::shared_mutex> lock(ctx.currentInterface.ndp.configs.configMutex);
	ctx.currentInterface.ndp.configs.raIntervalMS = true;
	ctx.currentInterface.ndp.configs.raInterval = static_cast<uint32_t>(std::stoul(args[1]));
	ctx.currentInterface.ndp.configs.raIntervalMin = static_cast<uint32_t>(std::stoul(args[2]));
    }
    return true;
}

bool InterfaceIPv6ND_RaLifetime_Handler(INTERFACE_PARAMS)
{
    ctx.currentInterface.ndp.configs.routerLifetime.store(ctx.negate ? 1800 : static_cast<uint16_t>(std::stoul(args[0])));
    return true;
}

bool InterfaceIPv6ND_RaMtuSuppression_Handler(INTERFACE_PARAMS)
{
    UNUSED(args);
    ctx.currentInterface.ndp.configs.mtuSuppress.store(!ctx.negate, std::memory_order_release);
    return true;
}

bool InterfaceIPv6ND_RaSuppression_Handler(INTERFACE_PARAMS)
{
    UNUSED(args);
    ctx.currentInterface.ndp.configs.suppressRA.store(!ctx.negate, std::memory_order_release);
    return true;
}

bool InterfaceIPv6ND_RaSuppressionAll_Handler(INTERFACE_PARAMS)
{
    UNUSED(args);
    ctx.currentInterface.ndp.configs.raSuppressAll.store(!ctx.negate, std::memory_order_release);
    return true;
}

bool InterfaceIPv6ND_ReachableTime_Handler(INTERFACE_PARAMS)
{
    /*
    ctx.currentInterface.ndp.configs.reachableTime.store(ctx.negate
	    ? ctx.currentInterface.getVRF()->getGlobal().configs.ndp.reachableTime.load(std::memory_order_relaxed)
	    : static_cast<uint32_t>(std::stoul(args[0])));
    ctx.currentInterface.ndp.configs.reachableTimeLocal = !ctx.negate;
    */
    return true;
}

bool InterfaceIPv6ND_RouterPreference_Handler(INTERFACE_PARAMS)
{
    if (ctx.negate)
    {
	ctx.currentInterface.ndp.configs.preference = infrastructure::Ndp::Configs::Preference::MEDIUM;
    }
    else
    {
	if (args[0] == "high")
	{
	    ctx.currentInterface.ndp.configs.preference = infrastructure::Ndp::Configs::Preference::HIGH;
	}
	else if (args[0] == "medium")
	{
	    ctx.currentInterface.ndp.configs.preference = infrastructure::Ndp::Configs::Preference::MEDIUM;
	}
	else if (args[0] == "low")
	{
	    ctx.currentInterface.ndp.configs.preference = infrastructure::Ndp::Configs::Preference::LOW;
	}
    }
    return true;
}
}
