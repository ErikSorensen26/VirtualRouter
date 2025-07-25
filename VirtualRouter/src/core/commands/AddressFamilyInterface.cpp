#include "CommandProcessor.h"
#include <Eigrp.h>
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
			}
			else if (commandStream[1] == "mode")
			{
				if (commandStream[2] == "hmac-sha-256")
				{
					currentEigrpInterface->authKey.authType = EigrpConfigs::AuthType::SHA1;
				}
				else if (commandStream[2] == "md5")
				{
					currentEigrpInterface->authKey.authType = EigrpConfigs::AuthType::MD5;
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
			if (!negate)
			{
				currentEigrp->addPassiveInterface(currentEigrpInterface->key);
			}
			else
			{
				currentEigrp->addPassiveInterface(currentEigrpInterface->key);
			}
		}
		else if (commandStream[0] == "shutdown")
		{
			currentEigrpInterface->shutdown = negate;
			currentEigrp->updateInterfaceList();
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
			Protocol::EigrpInterface* iface = nullptr;
			{
				std::shared_lock<std::shared_mutex> lock(currentEigrp->interfaceMutex);
				auto intIt = currentEigrp->eigrpInterfaceList.find(currentEigrpInterface->key);
				if (intIt != currentEigrp->eigrpInterfaceList.end())
				{
					iface = intIt->second;
				}
			}

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

			if (iface)
			{
				negate
				  ? iface->removeSummaryRoute(network, mask)
				  : iface->addSummaryRoute(network.raw, mask);
			}
			else
			{
				if (!negate)
				{
					std::shared_lock<std::shared_mutex> lock(currentEigrpInterface->configsMutex);
					if (!std::any_of(currentEigrpInterface->summaryRoutes.begin(), currentEigrpInterface->summaryRoutes.end(),
						[&](EigrpConfigs::SummaryRoute& summary) {
							return summary.summary->network == network && summary.summary->mask == mask;
						})
					)
					{
						currentEigrpInterface->pendingSummaryRoutes.push_back({network, mask});
					}
				}
				else
				{
					std::shared_lock<std::shared_mutex> lock(currentEigrpInterface->configsMutex);
					std::erase_if(currentEigrpInterface->summaryRoutes, [&](EigrpConfigs::SummaryRoute& summary) {
						return summary.summary->network == network && summary.summary->mask == mask;
					});
					std::erase_if(currentEigrpInterface->pendingSummaryRoutes, [&](std::pair<IPAddress, uint8_t>& summary) {
						return summary.first == network && summary.second == mask;
					});
				}
			}
		}
	}
	else return false;
	return true;
}
