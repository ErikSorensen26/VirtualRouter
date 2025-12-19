// InterfaceIPv6NDCommands.h

#ifndef INTERFACE_IPV6_ND_COMMANDS_H
#define INTERFACE_IPV6_ND_COMMANDS_H

#include <CliModeParser.hpp>
#include <InterfaceContext.hpp>

namespace Cli
{

}
/*
        if (commandStream[2] == "advertisement-interval")
        {
                currentInterface->ndp->configs.advertisementInterval.store(!negate, std::memory_order_release);
        }
        else if (commandStream[2] == "autoconfig")
        {
                if (commandStream[3] == "default-route")
                {
                        currentInterface->ndp->configs.autoConfigDefaultRoute.store(!negate, std::memory_order_release);
                }
                else if (commandStream[3] == "prefix")
                {
                        currentInterface->ndp->configs.autoConfigPrefix.store(!negate, std::memory_order_release);
                }
        }
        else if (commandStream[2] == "cache")
        {
                if (commandStream[3] == "expire")
                {
                        currentInterface->ndp->configs.cacheExpire.store(negate
                                ? global.configs.ndp.cacheExpire.load(std::memory_order_relaxed)
                                : static_cast<uint16_t>(std::stoul(commandStream[4]), std::memory_order_release));
                        currentInterface->ndp->configs.cacheExpireLocal = !negate;
                        if ((commandStream.size() > 5 && commandStream[5] == "refresh" && !negate) || negate)
                        {
                                currentInterface->ndp->configs.refreshLocal = !negate;
                                currentInterface->ndp->configs.refresh.store(negate ? global.configs.ndp.refresh.load(std::memory_order_relaxed) : true, std::memory_order_release);
                        }
                }
                else if (commandStream[3] == "interface-limit")
                {
                        currentInterface->ndp->configs.interfaceLimit.store(negate
                                ? global.configs.ndp.interfaceLimit.load(std::memory_order_relaxed)
                                : static_cast<uint32_t>(std::stoul(commandStream[4]), std::memory_order_release));
                        currentInterface->ndp->configs.interfaceLimitLocal = !negate;
                        if ((commandStream.size() > 5 && commandStream[5] == "log") || negate)
                        {
                                currentInterface->ndp->configs.loggingRate.store(negate
                                        ? global.configs.ndp.loggingRate.load(std::memory_order_relaxed)
                                        : static_cast<uint16_t>(std::stoul(commandStream[6]), std::memory_order_release));
                                currentInterface->ndp->configs.loggingRateLocal = !negate;
                        }
                }
        }
        else if (commandStream[2] == "dad")
        {
                if (commandStream[3] == "attempts")
                {
                        currentInterface->ndp->configs.dadAttempts.store(negate ? 1 : static_cast<uint16_t>(std::stoul(commandStream[4])), std::memory_order_release);
                }
                else if (commandStream[3] == "time")
                {
                        currentInterface->ndp->configs.dadTime.store(negate
                                ? global.configs.ndp.dadTime.load(std::memory_order_relaxed)
                                : static_cast<uint16_t>(std::stoul(commandStream[4]), std::memory_order_release));
                        currentInterface->ndp->configs.dadTimeLocal = !negate;
                }
        }
        else if (commandStream[2] == "destination-guard")
        {
                currentInterface->ndp->configs.destinationGuard.store(!negate, std::memory_order_release);
        }
        else if (commandStream[2] == "managed-config-flag")
        {
                currentInterface->ndp->configs.managedConfigFlag.store(!negate, std::memory_order_release);
        }
        else if (commandStream[2] == "na" && commandStream[3] == "glean")
        {
                currentInterface->ndp->configs.naGlean.store(!negate, std::memory_order_release);
        }
        else if (commandStream[2] == "ns-interval")
        {
                currentInterface->ndp->configs.nsInterval.store(negate ? 1000
                        : static_cast<uint32_t>(std::stoul(commandStream[3])), std::memory_order_release);
        }
        else if (commandStream[2] == "nud")
        {
                if (commandStream[3] == "igp")
                {
                        currentInterface->ndp->configs.nudIgp.store(!negate, std::memory_order_release);
                }
                else if (commandStream[3] == "retry")
                {
                        if (negate)
                        {
                                std::unique_lock<std::shared_mutex> lock(currentInterface->ndp->configs.configMutex);
                                currentInterface->ndp->configs.nudBase = 3;
                                currentInterface->ndp->configs.nudBaseInterval = 1000;
                                currentInterface->ndp->configs.nudBaseAttempts = 3;
                                currentInterface->ndp->configs.nudFinalWait = 60000;
                        }
                        else
                        {
                                std::unique_lock<std::shared_mutex> lock(currentInterface->ndp->configs.configMutex);
                                currentInterface->ndp->configs.nudBase = static_cast<uint8_t>(std::stoul(commandStream[4]));
                                currentInterface->ndp->configs.nudBaseInterval = static_cast<uint16_t>(std::stoul(commandStream[5]));
                                currentInterface->ndp->configs.nudBaseAttempts = static_cast<uint16_t>(std::stoul(commandStream[6]));
                                if (commandStream.size() > 7)
                                {
                                        currentInterface->ndp->configs.nudFinalWait = static_cast<uint16_t>(std::stoul(commandStream[7]));
                                }
                        }
                }
        }
        else if (commandStream[2] == "other-config-flag")
        {
                currentInterface->ndp->configs.otherConfigFlag.store(!negate, std::memory_order_release);
        }
        else if (commandStream[2] == "prefix")
        {

        }
        else if (commandStream[2] == "ra")
        {
                if (commandStream[3] == "hop-limit")
                {
                        currentInterface->ndp->configs.raHopLimitUnspecified.store(!negate, std::memory_order_release);
                }
                else if (commandStream[3] == "interval")
                {
                        if (negate)
                        {
                                std::unique_lock<std::shared_mutex> lock(currentInterface->ndp->configs.configMutex);
                                currentInterface->ndp->configs.raIntervalMS = true;
                                currentInterface->ndp->configs.raInterval = 600000;
                                currentInterface->ndp->configs.raIntervalMin = 3000;
                        }
                        else if (Functions::isNumber(commandStream[4]))
                        {
                                std::unique_lock<std::shared_mutex> lock(currentInterface->ndp->configs.configMutex);
                                currentInterface->ndp->configs.raIntervalMS = false;
                                currentInterface->ndp->configs.raInterval = static_cast<uint32_t>(std::stoul(commandStream[4]));
                                currentInterface->ndp->configs.raIntervalMin = static_cast<uint32_t>(std::stoul(commandStream[5]));
                        }
                        else if (commandStream[4] == "msec")
                        {
                                std::unique_lock<std::shared_mutex> lock(currentInterface->ndp->configs.configMutex);
                                currentInterface->ndp->configs.raIntervalMS = true;
                                currentInterface->ndp->configs.raInterval = static_cast<uint32_t>(std::stoul(commandStream[5]));
                                currentInterface->ndp->configs.raIntervalMin = static_cast<uint32_t>(std::stoul(commandStream[6]));
                        }
                }
                else if (commandStream[3] == "lifetime")
                {
                        currentInterface->ndp->configs.routerLifetime.store(negate ? 1800 : static_cast<uint16_t>(std::stoul(commandStream[4])));
                }
                else if (commandStream[3] == "mtu")
                {
                        currentInterface->ndp->configs.mtuSuppress.store(!negate, std::memory_order_release);
                }
                else if (commandStream[3] == "suppress")
                {
                        if (commandStream.size() == 4)
                        {
                                currentInterface->ndp->configs.suppressRA.store(!negate, std::memory_order_release);
                        }
                        else if (commandStream.size() == 5)
                        {
                                currentInterface->ndp->configs.raSuppressAll.store(!negate, std::memory_order_release);
                        }
                }
        }
        else if (commandStream[2] == "reachable-time")
        {
                currentInterface->ndp->configs.reachableTime.store(negate
                        ? global.configs.ndp.reachableTime.load(std::memory_order_relaxed)
                        : static_cast<uint32_t>(std::stoul(commandStream[3])));
                currentInterface->ndp->configs.reachableTimeLocal = !negate;
        }
        else if (commandStream[2] == "router-preference")
        {
                if (negate)
                {
                        currentInterface->ndp->configs.preference = Protocol::Ndp::Configs::Preference::MEDIUM;
                }
                else
                {
                        if (commandStream[3] == "high")
                        {
                                currentInterface->ndp->configs.preference = Protocol::Ndp::Configs::Preference::HIGH;
                        }
                        else if (commandStream[3] == "medium")
                        {
                                currentInterface->ndp->configs.preference = Protocol::Ndp::Configs::Preference::MEDIUM;
                        }
                        else if (commandStream[3] == "low")
                        {
                                currentInterface->ndp->configs.preference = Protocol::Ndp::Configs::Preference::LOW;
                        }
                }
        }
*/

#endif // INTERFACE_IPV6_ND_COMMANDS_H
