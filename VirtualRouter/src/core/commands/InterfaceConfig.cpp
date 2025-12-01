#include "CommandProcessor.h"
#include <DhcpClient.h>
#include <EigrpInterface.h>
#include <Eigrp.h>
#include <Ndp.h>
#include <VirtualRouter.h>
#include <CliEngine.h>

bool CommandProcessor::handleInterfaceConfiguration(const std::vector<std::string>& commandStream)
{
	// HELPER FUNCTIONS
	auto refreshEigrpConfig = [&](uint32_t as, AddressFamily af, EigrpConfigs::InterfaceConfigs* config)
	{
		if (!currentInterface->eigrpInterfaceList.contains(as) && config->isDefault())
		{
			delete config;
			currentInterface->configs.eigrp.eigrpInterfaceConfigList.erase({as, af});
		}
	};

	if (commandStream[0] == "exit")
	{
		terminal.exitMode(Mode::globalConfiguration);
	}
	else if (commandStream[0] == "ip")
	{
		if (commandStream[1] == "address")
		{
			if (!negate)
			{
				if (commandStream[2] != "dhcp")
				{
					uint32_t ipAddress = Functions::addressToIntv4(commandStream[2]);
					uint8_t subnet = __builtin_popcount(Functions::addressToIntv4(commandStream[3]));
					currentInterface->setIPv4(ipAddress, subnet);
				}
				else
				{
					if (!currentInterface->dhcp)
					{
						currentInterface->dhcp = new Protocol::DhcpClient(currentInterface);
					}
				}
			}
			else
			{
				currentInterface->removeIPv4();
			}
		}
		else if (commandStream[1] == "authentication")
		{
			if (commandStream[2] == "key-chain")
			{
				if (commandStream[3] == "eigrp")
				{
					uint32_t as = static_cast<uint32_t>(std::stoul(commandStream[4]));
					auto* eigrpConfig = currentInterface->getEigrpConfig(as, AddressFamily::IPv4, negate);
					if (eigrpConfig)
					{
						if (negate)
						{
							//TODO
						}
						else
						{
							//TODO
						}
					}
				}
			}
			else if (commandStream[2] == "mode")
			{
				if (commandStream[3] == "eigrp")
				{
					uint32_t as = static_cast<uint32_t>(std::stoul(commandStream[4]));
					auto* eigrpConfig = currentInterface->getEigrpConfig(as, AddressFamily::IPv4, negate);
					if (eigrpConfig)
					{
						if (negate)
						{
							eigrpConfig->authKey.fullyEnabled.store(false, std::memory_order_relaxed);
							eigrpConfig->authKey.authType = EigrpConfigs::AuthType::NONE;
							refreshEigrpConfig(as, AddressFamily::IPv4, eigrpConfig);
						}
						else
						{
							eigrpConfig->authKey.fullyEnabled.store(false, std::memory_order_relaxed);
							eigrpConfig->authKey.authType = EigrpConfigs::AuthType::MD5;
							if (eigrpConfig->authKey.keyId != 0)
							{
								// Enable authentication.
								eigrpConfig->authKey.fullyEnabled.store(true, std::memory_order_relaxed);
							}
						}
					}
				}
			}
		}
		else if (commandStream[1] == "bandwidth-percentage")
		{
			if (commandStream[2] == "eigrp")
			{
				uint32_t as = static_cast<uint32_t>(std::stoul(commandStream[3]));
				auto eigrpConfig = currentInterface->getEigrpConfig(as, AddressFamily::IPv4, negate);
				if (eigrpConfig)
				{
					if (negate)
					{
						eigrpConfig->bandwidthPercentage.store(50, std::memory_order_release);
						refreshEigrpConfig(as, AddressFamily::IPv4, eigrpConfig);
					}
					else
					{
						eigrpConfig->bandwidthPercentage.store(static_cast<uint32_t>(std::stoul(commandStream[4]), std::memory_order_relaxed));
					}
				}
			}
		}
		else if (commandStream[1] == "dampening-change")
		{
			if (commandStream[2] == "eigrp")
			{
				uint32_t as = static_cast<uint32_t>(std::stol(commandStream[3]));
				auto eigrpConfig = currentInterface->getEigrpConfig(as, AddressFamily::IPv4, negate);
				if (eigrpConfig)
				{
					if (negate)
					{
						eigrpConfig->dampeningChange.store(1, std::memory_order_release);
						refreshEigrpConfig(as, AddressFamily::IPv4, eigrpConfig);
					}
					else
					{
						eigrpConfig->dampeningChange.store(static_cast<uint32_t>(std::stoul(commandStream[4]), std::memory_order_relaxed));
					}
				}
			}
		}
		else if (commandStream[1] == "dampening-interval")
		{
			if (commandStream[2] == "eigrp")
			{
				uint32_t as = static_cast<uint32_t>(std::stoul(commandStream[3]));
				auto eigrpConfig = currentInterface->getEigrpConfig(as, AddressFamily::IPv4, negate);
				if (eigrpConfig)
				{
					if (negate)
					{
						eigrpConfig->dampeningInterval.store(5, std::memory_order_release);
						refreshEigrpConfig(as, AddressFamily::IPv4, eigrpConfig);
					}
					else
					{
						eigrpConfig->dampeningInterval.store(static_cast<uint32_t>(std::stoul(commandStream[4]), std::memory_order_relaxed));
					}
				}
			}
		}
		else if (commandStream[1] == "dhcp")
		{

		}
		else if (commandStream[1] == "hello-interval")
		{
			if (commandStream[2] == "eigrp")
			{
				uint32_t as = static_cast<uint32_t>(std::stoul(commandStream[3]));
				auto eigrpConfig = currentInterface->getEigrpConfig(as, AddressFamily::IPv4, negate);
				if (eigrpConfig)
				{
					if (negate)
					{
						eigrpConfig->helloTime.store(5, std::memory_order_release);
						refreshEigrpConfig(as, AddressFamily::IPv4, eigrpConfig);
					}
					else
					{
						eigrpConfig->helloTime.store(static_cast<uint32_t>(std::stoul(commandStream[4]), std::memory_order_relaxed));
					}
				}
			}
		}
		else if (commandStream[1] == "helper-address")
		{

		}
		else if (commandStream[1] == "hold-time")
		{
			if (commandStream[2] == "eigrp")
			{
				uint32_t as = static_cast<uint32_t>(std::stoul(commandStream[3]));
				auto eigrpConfig = currentInterface->getEigrpConfig(as, AddressFamily::IPv4, negate);
				if (eigrpConfig)
				{
					if (negate)
					{
						eigrpConfig->helloTime.store(5, std::memory_order_release);
						refreshEigrpConfig(as, AddressFamily::IPv4, eigrpConfig);
					}
					else
					{
						eigrpConfig->helloTime.store(static_cast<uint32_t>(std::stoul(commandStream[4]), std::memory_order_relaxed));
					}
				}
			}
		}
		else if (commandStream[1] == "mtu")
		{
			currentInterface->configs.ipv4.mtu.store(negate ? 1500 : static_cast<uint16_t>(std::stoul(commandStream[2])), std::memory_order_release);
		}
		else if (commandStream[1] == "next-hop-self")
		{
			if (commandStream[2] == "eigrp")
			{
				uint32_t as = static_cast<uint32_t>(std::stoul(commandStream[3]));
				auto eigrpConfig = currentInterface->getEigrpConfig(as, AddressFamily::IPv4, negate);
				if (eigrpConfig)
				{
					eigrpConfig->nextHopSelf.store(!negate, std::memory_order_relaxed);
					if (negate)
					{
						refreshEigrpConfig(as, AddressFamily::IPv4, eigrpConfig);
					}
				}
			}
		}
		else if (commandStream[1] == "split-horizon")
		{
			if (commandStream[2] == "eigrp")
			{
				uint32_t as = static_cast<uint32_t>(std::stoi(commandStream[3]));
				auto* eigrpConfig = currentInterface->getEigrpConfig(as, AddressFamily::IPv4, negate);
				if (eigrpConfig)
				{
					eigrpConfig->splitHorizon.store(!negate, std::memory_order_relaxed);
					if (negate)
					{
						refreshEigrpConfig(as, AddressFamily::IPv4, eigrpConfig);
					}
				}
			}
		}
		else if (commandStream[1] == "summary-address")
		{
			if (commandStream[2] == "eigrp")
			{
				uint32_t as = static_cast<uint32_t>(std::stoul(commandStream[3]));
				auto ifaceIt = currentInterface->eigrpInterfaceList.find(as);
				IPAddress network = Functions::getAddress(commandStream[4]);
				uint8_t mask = Functions::prefixToPrefixLength(Functions::addressToIntv4(commandStream[5]));
				IPPrefix prefix = { network.raw, mask, AddressFamily::IPv4 };
				if (ifaceIt != currentInterface->eigrpInterfaceList.end() && ifaceIt->second.IPv4)
				{
					if (negate)
						ifaceIt->second.IPv4->getAggregator().withdrawSummary(prefix);
					else
						ifaceIt->second.IPv4->getAggregator().installSummary(prefix);
				}
				else
				{
					auto eigrpConfig = currentInterface->getEigrpConfig(as, AddressFamily::IPv4, negate);
					if (eigrpConfig)
					{
						if (negate)
						{
							std::erase_if(
								eigrpConfig->pendingSummaryRoutes,
								[&](const IPPrefix& net) -> bool { return net == prefix; });
							refreshEigrpConfig(as, AddressFamily::IPv4, eigrpConfig);
						}
						else
						{
							eigrpConfig->pendingSummaryRoutes.push_back(prefix);
						}
					}
				}
			}
		}
	}
	else if (commandStream[0] == "ipv6")
	{
		if (commandStream[1] == "address")
		{
			if (!negate)
			{
				if (Functions::isIPv6Address(commandStream[2]))
				{
					IPAddress ipv6Address = Functions::getAddress(commandStream[2]);
					if (Functions::isLocalLink(ipv6Address.raw))
					{
						if (!negate)
						{
							currentInterface->setIPv6(ipv6Address.raw, true);
						}
						else
						{
							currentInterface->removeIPv6();
						}
					}
					else if (!negate)
					{
						terminal.iConsole->print(std::string("\r\n%") + std::string("Invalid local-link address"));
					}
				}
				else if (Functions::isIPv6AddressWithMask(commandStream[2]))
				{
					{
						IPAddress ipv6Address;
						uint8_t mask;
						if (Functions::splitSlashMiddle(commandStream[2], ipv6Address, mask))
						{
							if (!negate)
							{
								currentInterface->setIPv6(ipv6Address.raw, false, mask);
							}
							else
							{
								currentInterface->removeIPv6(ipv6Address.raw);
							}
						}
					}
				}
				else if (commandStream[2] == "dhcp")
				{
					if (!currentInterface->dhcp)
					{
						//currentInterface->dhcp = new Protocol::Dhcpv6Client();
					}
				}
			}
		}
		else if (commandStream[1] == "authentication")
		{
			if (commandStream[2] == "key-chain")
			{
				if (commandStream[3] == "eigrp")
				{
					uint32_t as = static_cast<uint32_t>(std::stoul(commandStream[4]));
					auto* eigrpConfig = currentInterface->getEigrpConfig(as, AddressFamily::IPv6, negate);
					if (eigrpConfig)
					{
						if (negate)
						{
							//TODO
						}
						else
						{
							//TODO
						}
					}
				}
			}
			else if (commandStream[2] == "mode")
			{
				if (commandStream[3] == "eigrp")
				{
					uint32_t as = static_cast<uint32_t>(std::stoi(commandStream[4]));
					auto* eigrpConfig = currentInterface->getEigrpConfig(as, AddressFamily::IPv6, negate);
					if (eigrpConfig)
					{
						if (negate)
						{
							eigrpConfig->authKey.fullyEnabled.store(false, std::memory_order_relaxed);
							eigrpConfig->authKey.authType = EigrpConfigs::AuthType::NONE;
							refreshEigrpConfig(as, AddressFamily::IPv6, eigrpConfig);
						}
						else
						{
							eigrpConfig->authKey.fullyEnabled.store(false, std::memory_order_relaxed);
							eigrpConfig->authKey.authType = EigrpConfigs::AuthType::MD5;
							if (eigrpConfig->authKey.keyId != 0)
							{
								// Enable authentication.
								eigrpConfig->authKey.fullyEnabled.store(true, std::memory_order_relaxed);
							}
						}
					}
				}
			}
		}
		else if (commandStream[1] == "bandwidth-percent")
		{
			if (commandStream[2] == "eigrp")
			{
				uint32_t as = static_cast<uint32_t>(std::stoul(commandStream[3]));
				auto* eigrpConfig = currentInterface->getEigrpConfig(as, AddressFamily::IPv6, negate);
				if (negate)
				{
					eigrpConfig->bandwidthPercentage.store(50, std::memory_order_release);
					refreshEigrpConfig(as, AddressFamily::IPv6, eigrpConfig);
				}
				else
				{
					eigrpConfig->bandwidthPercentage.store(static_cast<uint32_t>(std::stoul(commandStream[4]), std::memory_order_release));
				}
			}
		}
		else if (commandStream[1] == "dampening-change")
		{
			if (commandStream[2] == "eigrp")
			{
				uint32_t as = static_cast<uint32_t>(std::stoul(commandStream[3]));
				auto eigrpConfig = currentInterface->getEigrpConfig(as, AddressFamily::IPv6, negate);
				if (eigrpConfig)
				{
					if (negate)
					{
						eigrpConfig->dampeningChange.store(1, std::memory_order_release);
						refreshEigrpConfig(as, AddressFamily::IPv6, eigrpConfig);
					}
					else
					{
						eigrpConfig->dampeningChange.store(static_cast<uint32_t>(std::stoul(commandStream[4]), std::memory_order_relaxed));
					}
				}
			}
		}
		else if (commandStream[1] == "dampening-interval")
		{
			if (commandStream[2] == "eigrp")
			{
				uint32_t as = static_cast<uint32_t>(std::stoul(commandStream[3]));
				auto eigrpConfig = currentInterface->getEigrpConfig(as, AddressFamily::IPv6, negate);
				if (eigrpConfig)
				{
					if (negate)
					{
						eigrpConfig->dampeningInterval.store(5, std::memory_order_release);
						refreshEigrpConfig(as, AddressFamily::IPv6, eigrpConfig);
					}
					else
					{
						eigrpConfig->dampeningInterval.store(static_cast<uint32_t>(std::stoul(commandStream[4]), std::memory_order_relaxed));
					}
				}
			}
		}
		else if (commandStream[1] == "eigrp")
		{
			terminal.isList = true;
			uint32_t as = static_cast<uint32_t>(std::stoi(commandStream[2]));
			auto eigrpAs = currentVrf->getEigrpAutonomousSystem(as);
			if (!negate)
			{
				if (eigrpAs && eigrpAs->ipv6)
				{
					eigrpAs->ipv6->getIfaceMgr().createInterface(currentInterface);
				}
				{
					std::unique_lock<std::shared_mutex> lock(currentInterface->configs.ipMutex);
					currentInterface->configs.eigrp.ipv6AutonomousSystems.insert(as);
				}
			}
			else
			{
				currentInterface->configs.eigrp.ipv6AutonomousSystems.erase(static_cast<uint32_t>(std::stoul(commandStream[2])));
			}

			if (eigrpAs && eigrpAs->ipv6)
			{
				eigrpAs->ipv6->refreshInterfaceList();
			}
		}
		else if (commandStream[1] == "hello-interval")
		{
			if (commandStream[2] == "eigrp")
			{
				uint32_t as = static_cast<uint32_t>(std::stoul(commandStream[3]));
				auto eigrpConfig = currentInterface->getEigrpConfig(as, AddressFamily::IPv6, negate);
				if (eigrpConfig)
				{
					if (negate)
					{
						eigrpConfig->helloTime.store(5, std::memory_order_release);
						refreshEigrpConfig(as, AddressFamily::IPv6, eigrpConfig);
					}
					else
					{
						eigrpConfig->helloTime.store(static_cast<uint32_t>(std::stoul(commandStream[4]), std::memory_order_relaxed));
					}
				}
			}
		}
		else if (commandStream[1] == "hold-time")
		{
			if (commandStream[2] == "eigrp")
			{
				uint32_t as = static_cast<uint32_t>(std::stoul(commandStream[3]));
				auto eigrpConfig = currentInterface->getEigrpConfig(as, AddressFamily::IPv6, negate);
				if (eigrpConfig)
				{
					if (negate)
					{
						eigrpConfig->holdTime.store(15, std::memory_order_release);
						refreshEigrpConfig(as, AddressFamily::IPv6, eigrpConfig);
					}
					else
					{
						eigrpConfig->holdTime.store(static_cast<uint32_t>(std::stoul(commandStream[4]), std::memory_order_relaxed));
					}
				}
			}
		}
		else if (commandStream[1] == "mtu")
		{
			currentInterface->configs.ipv6.mtu.store(negate ? 1500 : static_cast<uint16_t>(std::stoul(commandStream[2]), std::memory_order_release));
		}
		else if (commandStream[1] == "nd")
		{
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
		}
		else if (commandStream[1] == "next-hop-self")
		{
			uint32_t as = static_cast<uint32_t>(std::stoi(commandStream[3]));
			bool disableEcmp = commandStream.size() == 5 && commandStream[4] == "no-ecmp-mode";
			{
				auto* eigrpConfig = currentInterface->getEigrpConfig(as, AddressFamily::IPv6, negate);
				if (eigrpConfig)
				{
					eigrpConfig->nextHopSelf.store(!negate, std::memory_order_release);
					if (commandStream.size() == 5 || negate)
					{
						eigrpConfig->noEcmpMode.store(negate ? disableEcmp : false, std::memory_order_release);
					}
					if (negate)
					{
						refreshEigrpConfig(as, AddressFamily::IPv6, eigrpConfig);
					}
				}
			}
		}
		else if (commandStream[1] == "redirects")
		{
			currentInterface->ndp->configs.redirects.store(!negate, std::memory_order_release);
		}
		else if (commandStream[1] == "split-horizon")
		{
			if (commandStream[2] == "eigrp")
			{
				uint32_t as = static_cast<uint32_t>(std::stoi(commandStream[3]));
				auto* eigrpConfig = currentInterface->getEigrpConfig(as, AddressFamily::IPv6, negate);
				if (eigrpConfig)
				{
					eigrpConfig->splitHorizon.store(!negate, std::memory_order_relaxed);
					if (negate)
					{
						refreshEigrpConfig(as, AddressFamily::IPv6, eigrpConfig);
					}
				}
			}
		}
		else if (commandStream[1] == "summary-address")
		{
			if (commandStream[2] == "eigrp")
			{
				uint32_t as = static_cast<uint32_t>(std::stoul(commandStream[3]));
				IPAddress network;
				uint8_t mask;
				Functions::splitSlashMiddle(commandStream[4], network, mask);
				IPPrefix prefix = { network.raw, mask, AddressFamily::IPv6 };
				auto ifaceIt = currentInterface->eigrpInterfaceList.find(as);
				if (ifaceIt != currentInterface->eigrpInterfaceList.end() && ifaceIt->second.IPv4)
				{
					if (negate)
						ifaceIt->second.IPv4->getAggregator().withdrawSummary(prefix);
					else
						ifaceIt->second.IPv4->getAggregator().installSummary(prefix);
				}
				else
				{
					auto eigrpConfig = currentInterface->getEigrpConfig(as, AddressFamily::IPv6, negate);
					if (eigrpConfig)
					{
						if (negate)
						{
							std::erase_if(
								eigrpConfig->pendingSummaryRoutes,
								[&](const IPPrefix& net) -> bool { return net == prefix; });
							refreshEigrpConfig(as, AddressFamily::IPv6, eigrpConfig);
						}
						else
						{
							eigrpConfig->pendingSummaryRoutes.push_back({network, mask});
						}
					}
				}
			}
		}
	}
	else if (commandStream.size() >= 2 && commandStream[1] == "mtu")
	{
		
	}
	else return false;
	return true;
}
