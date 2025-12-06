#include "CommandProcessor.h"
#include <Eigrp.h>
#include <EigrpInterface.h>
#include <AddressFamily.hpp>
#include <Mode.hpp>

bool CommandProcessor::handleAddressFamilyInterface(const std::vector<std::string>& commandStream)
{
	if (terminal.currentSubMode == "eigrp_named")
	{
		if (commandStream[0] == "authentication")
		{
			if (commandStream[1] == "key-chain")
			{
				std::string keychain = commandStream[2];
				// TODO
				if (negate)
				{
					currentEigrpInterface->auth.fullyEnabled.store(false, std::memory_order_relaxed);
					currentEigrpInterface->auth.authType = EigrpConfigs::AuthType::NONE;
				}
				else
				{
					currentEigrpInterface->auth.fullyEnabled.store(false, std::memory_order_relaxed);
					currentEigrpInterface->auth.authType = EigrpConfigs::AuthType::MD5;
					if (std::holds_alternative<std::string>(currentEigrpInterface->auth.key) && !std::get<std::string>(currentEigrpInterface->auth.key).empty())
					{
						currentEigrpInterface->auth.fullyEnabled.store(true, std::memory_order_release);
					}
				}
			}
			else if (commandStream[1] == "mode")
			{
				if (commandStream[2] == "hmac-sha-256")
				{
					currentEigrpInterface->auth.authType = EigrpConfigs::AuthType::SHA256;
					if (commandStream[3].size() > 32)
					{
						terminal.iConsole->print(std::string("\r\n%EIGRP: HMAC-SHA-256 password accepted but truncated, max length is 32 characters"));
						currentEigrpInterface->auth.key = std::string(commandStream[3].substr(0, 32));
					}
					currentEigrpInterface->auth.key = std::string(commandStream[3]);
				}
				else if (commandStream[2] == "md5")
				{
					currentEigrpInterface->auth.authType = EigrpConfigs::AuthType::MD5;
				}
			}
		}
		else if (commandStream[0] == "bandwidth-percentage")
		{
			negate
			  ? currentEigrpInterface->bandwidthPercentage.store(50, std::memory_order_release)
			  : currentEigrpInterface->bandwidthPercentage.store(static_cast<uint32_t>(std::stoi(commandStream[1])), std::memory_order_release);
		}
		else if (commandStream[0] == "bfd")
		{
			// XXX
		}
		else if (commandStream[0] == "dampening-change")
		{
			negate
			  ? currentEigrpInterface->dampeningChange.store(0, std::memory_order_release)
			  : currentEigrpInterface->dampeningChange.store(static_cast<uint8_t>(std::stoi(commandStream[1])), std::memory_order_release);
		}
		else if (commandStream[0] == "dampening-interval")
		{
			negate
			  ? currentEigrpInterface->dampeningInterval.store(5, std::memory_order_release)
			  : currentEigrpInterface->dampeningInterval.store(static_cast<uint16_t>(std::stoi(commandStream[1])), std::memory_order_release);
		}
		else if (commandStream[0] == "exit-af-interface")
		{
			terminal.exitMode(Mode::routerAddressFamily);
			terminal.configureAddressFamily(AddressFamily::IPv4);
			if (terminal.workingDirectory->size() > 0 && (*terminal.workingDirectory)[0].contains("unicast"))
			{
				terminal.workingDirectory = &(*terminal.workingDirectory)[0]["unicast"]; // TODO fix unicast/multicast here
			}
		}
		else if (commandStream[0] == "hello-interval")
		{
			negate
			  ? currentEigrpInterface->helloTime.store(5, std::memory_order_release)
			  : currentEigrpInterface->helloTime.store(static_cast<uint16_t>(std::stoi(commandStream[1])), std::memory_order_release);
		}
		else if (commandStream[0] == "hold-time")
		{
			negate
			  ? currentEigrpInterface->holdTime.store(15, std::memory_order_release)
			  : currentEigrpInterface->holdTime.store(static_cast<uint16_t>(std::stoi(commandStream[1])), std::memory_order_release);
		}
		else if (commandStream[0] == "next-hop-self")
		{
			currentEigrpInterface->nextHopSelf.store(!negate, std::memory_order_release);
		}
		else if (commandStream[0] == "passive-interface")
		{
			terminal.isList = true;
			currentEigrp->getGlobalConfigMgr().setPassiveInterface(currentInterface->configs.key, !negate);
		}
		else if (commandStream[0] == "shutdown")
		{
			std::cout << negate << std::endl;
			currentEigrpInterface->shutdown = negate;
			currentEigrp->refreshInterfaceList();
		}
		else if (commandStream[0] == "split-horizon")
		{
			currentEigrpInterface->splitHorizon.store(!negate, std::memory_order_release);
		}
		else if (commandStream[0] == "summary-address")
		{
			uint8_t size = 0;
			IPAddress network;
			uint8_t mask;
			Eigrp::EigrpInterface* iface = nullptr;
			iface = currentEigrp->getIfaceMgr().getInterface(currentInterface->configs.key);

			if (!Functions::splitSlashMiddle(commandStream[1], network, mask))
			{
				network = Functions::getAddress(commandStream[1]);
				mask = static_cast<uint8_t>(std::stoi(commandStream[2]));
				size = 3;
			}
			else
			{
				size = 2;
			}

			if (commandStream.size() != size && commandStream[size] == "leak-map")
			{
				// XXX
			}

			IPPrefix prefix = { network.raw, mask, currentEigrp->getAF() };

			if (iface)
			{
				negate
				  ? iface->getAggregator().installSummary(prefix, false)
				  : iface->getAggregator().withdrawSummary(prefix);
			}
			else
			{
				if (!negate)
				{
					std::shared_lock<std::shared_mutex> lock(currentEigrpInterface->configsMutex);
					if (!std::any_of(currentEigrpInterface->pendingSummaryRoutes.begin(), currentEigrpInterface->pendingSummaryRoutes.end(),
						[&](IPPrefix& pfx) { return pfx == prefix; }))
					{
						currentEigrpInterface->pendingSummaryRoutes.push_back(prefix);
					}
				}
				else
				{
					std::shared_lock<std::shared_mutex> lock(currentEigrpInterface->configsMutex);
					currentEigrpInterface->pendingSummaryRoutes.erase(std::remove(currentEigrpInterface->pendingSummaryRoutes.begin(),
						currentEigrpInterface->pendingSummaryRoutes.end(), prefix), currentEigrpInterface->pendingSummaryRoutes.end());
				}
			}
		}
	}
	else return false;
	return true;
}
