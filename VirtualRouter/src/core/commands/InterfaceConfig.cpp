#include "CommandProcessor.h"
#include <DhcpClient.h>
#include <Eigrp.h>

bool CommandProcessor::handleInterfaceConfiguration(const std::vector<std::string>& commandStream)
{
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
					currentInterface->setIPv4(Functions::addressToByte(ByteString(commandStream[2])), Functions::byteMaskToNum(Functions::addressToByte(ByteString(commandStream[3]))));
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
	}
	else if (commandStream[0] == "ipv6")
	{
		if (commandStream[1] == "address")
		{
			if (!negate)
			{
				if (Functions::isIPv6Address(commandStream[2]))
				{
					ByteString ipv6Address = Functions::addressToByte(commandStream[2]);
					if (Functions::isLocalLink(ipv6Address))
					{
						if (!negate)
						{
							currentInterface->setIPv6(ipv6Address, true);
						}
						else
						{
							currentInterface->removeIPv6(ipv6Address, true);
						}
					}
					else if (!negate)
					{
						std::cout << "\n%" << "Invalid local-link address";
					}
				}
				else if (Functions::isIPv6AddressWithMask(commandStream[2]))
				{
					{
						ByteString ipv6Address;
						uint8_t mask;
						if (Functions::splitSlashMiddle(commandStream[2], ipv6Address, mask))
						{
							if (!negate)
							{
								currentInterface->setIPv6(Functions::addressToByte(ipv6Address), false, mask);
							}
							else
							{
								currentInterface->removeIPv6(ipv6Address, false);
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
		else if (commandStream[1] == "eigrp")
		{
			terminal.isList = true;
			uint32_t as = static_cast<uint32_t>(std::stoi(commandStream[2]));
			auto eigrpAs = currentVrf->getEigrpAutonomousSystem(as);
			if (!negate)
			{
				if (eigrpAs && eigrpAs->ipv6)
				{
					eigrpAs->ipv6->addEigrpInterface(currentInterface);
				}
				{
					std::unique_lock<std::shared_mutex> lock(currentInterface->configs.ipMutex);
					currentInterface->configs.eigrp.ipv6AutonomousSystems[currentVrf->instanceName].insert(as);
				}
			}
			else
			{
				currentInterface->configs.eigrp.ipv6AutonomousSystems[currentVrf->instanceName].erase(std::stoi(commandStream[2]));
			}

			if (eigrpAs && eigrpAs->ipv6)
			{
				eigrpAs->ipv6->updateInterfaceList();
			}
		}
	}
	else return false;
	return true;
}
