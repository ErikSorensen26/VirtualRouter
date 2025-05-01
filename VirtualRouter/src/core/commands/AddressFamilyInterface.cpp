#include "CommandProcessor.h"
#include <Eigrp.h>

bool CommandProcessor::handleAddressFamilyInterface(const std::vector<std::string>& commandStream)
{
	if (terminal.currentSubMode == "eigrp_named")
	{
		if (commandStream[0] == "authentication")
		{
			if (commandStream[1] == "key-chain")
			{
				std::string keychain = commandStream[2];
				//currentEigrpInterface->configs.authKey.key
				// TODO
			}
			else if (commandStream[1] == "mode")
			{
				if (commandStream[2] == "hmac-sha-256")
				{
					currentEigrpInterface->configs.authKey.authType = EigrpConfigs::AuthType::SHA1;
				}
				else if (commandStream[2] == "md5")
				{
					currentEigrpInterface->configs.authKey.authType = EigrpConfigs::AuthType::MD5;
				}
			}
		}
		else if (commandStream[0] == "bandwidth-percentage")
		{
			currentEigrpInterface->configs.bandwidthPercentage.store(static_cast<uint32_t>(std::stoi(commandStream[1])), std::memory_order_release);
		}
		else if (commandStream[0] == "bfd")
		{
			// XXX
		}
		else if (commandStream[0] == "dampening-change")
		{
			currentEigrpInterface->configs.dampeningChange.store(static_cast<uint8_t>(std::stoi(commandStream[1])), std::memory_order_release);
		}
		else if (commandStream[0] == "dampening-interval")
		{
			currentEigrpInterface->configs.dampeningInterval.store(static_cast<uint16_t>(std::stoi(commandStream[1])), std::memory_order_release);
		}
		else if (commandStream[0] == "default")
		{
			// XXX
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
			currentEigrpInterface->configs.helloTime.store(static_cast<uint16_t>(std::stoi(commandStream[1])), std::memory_order_release);
		}
		else if (commandStream[0] == "hold-time")
		{
			currentEigrpInterface->configs.holdTime.store(static_cast<uint16_t>(std::stoi(commandStream[1])), std::memory_order_release);
		}
		else if (commandStream[0] == "next-hop-self")
		{
			currentEigrpInterface->configs.nextHopSelf.store(true, std::memory_order_release);
		}
		else if (commandStream[0] == "no")
		{
			// XXX
		}
		else if (commandStream[0] == "passive-interface")
		{
			currentEigrpInterface->setPassive(true);
		}
		else if (commandStream[0] == "shutdown")
		{
			// XXX
		}
		else if (commandStream[0] == "split-horizon")
		{
			currentEigrpInterface->configs.splitHorizon.store(true, std::memory_order_release);
		}
		else if (commandStream[0] == "summary-address")
		{
			uint8_t size = 0;
			ByteString network;
			uint8_t mask;
			if (!Functions::splitSlashMiddle(commandStream[1], network, mask))
			{
				network = Functions::addressToByte(commandStream[1]);
				mask = static_cast<uint8_t>(std::stoi(commandStream[2]));
				size = 3;
			}
			else
			{
				network = Functions::addressToByte(network);
				size = 2;
			}
			if (commandStream.size() != size && commandStream[size] == "leak-map")
			{
				// XXX
			}
		}
	}
	else return false;
	return true;
}
