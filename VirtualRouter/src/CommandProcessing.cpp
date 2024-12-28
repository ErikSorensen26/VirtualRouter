#include <Terminal.h>

void Terminal::executeCommand(std::string &command)
{
	std::cout << modeSchema->dump(4) << std::endl;
	isModeChanged = false;
	isExitCommand = false;
	std::string preProcessMode = currentMode;

	command = normalizeCommand(command);
	isCommandExecutionSuccessful = false;

	std::vector<std::string> TEMPcommandStream = splitIntoWords(command);
	std::vector<std::string> commandStream;

	bool textLine = false;
	for (unsigned long index = 0; index < TEMPcommandStream.size(); index++)
	{
		if (!textLine)
		{
			commandStream.push_back(TEMPcommandStream[index]);
		}
		else
		{
			commandStream[commandStream.size() - 1] += " " + TEMPcommandStream[index];
		}
		if (index > 0)
		{
			if (commandHistory[index] == "LINE")
			{
				textLine = true;
			}
		}
	}
	if (commandStream.empty() || isHelpModeActive || !isRunning)
	{
		return;
	}

	if (!isHelpModeActive && (isCommandValid || isPatternMatching))
	{
		//cout << "\n" << command;
	}

	if (command == "end" && currentMode != mode.userExec)
	{
		while (currentMode != mode.privilegedExec)
		{
			std::string exit = "exit";
			executeCommand(exit);
		}
	}

	isCommandExecutionSuccessful = true;

	if (!no)
	{

#pragma region IDONTKNOW

		if (currentMode == mode.userExec || currentMode == mode.privilegedExec)
		{
			if (command == "show history")
			{
				for (std::string str : history)
				{
					if (str != "")
					{
						std::cout << "\n  " + str;
					}
				}
			}
			if (command == "show clock")
			{
				std::cout << "\n"
					 << timeManager.getTime();
			}
			if (command == "write memory")
			{
				saveConfig();
			}
		}

#pragma endregion

#pragma region UserExec

		if (currentMode == mode.userExec)
		{
			if (command == "enable")
			{
				changeMode(mode.privilegedExec);
			}
			if (command == "exit")
			{
				exit(1);
			}
		}

#pragma endregion

#pragma region PriviledgedExec

		if (currentMode == mode.privilegedExec)
		{
			if (command == "configure terminal")
			{
				changeMode(mode.globalConfiguration);
				std::cout << "\nEnter configuration commands, one per line.  End with CNTL/Z.";
			}
			if (command == "exit")
			{
				exitMode(mode.userExec);
			}
		}

#pragma endregion

#pragma region GlobalConfiguration

		if (currentMode == mode.globalConfiguration)
		{
			if (command == "exit")
			{
				exitMode(mode.privilegedExec);
			}
			if (commandStream[0] == "hostname")
			{
				Global::getInstance().getHostname() = commandStream[1];
			}
			if (commandStream[0] == "interface")
			{
				std::string type = commandStream[1];
				std::string interfaceID_temp = commandStream[2];
				interfaceID = static_cast<unsigned long>(Functions::stringToNum(commandStream[2]));
				std::string intType;
				if ((type == "Ethernet" || type == "GigabitEthernet" || type == "FastEthernet") && physicalInterfaces.size() >= interfaceID)
				{
					intType = physicalInterfaces[interfaceID];
				}
				else
				{
					intType = "NO_INTERFACE";
				}
				std::string mac;
				if (commandStream[1] == "Ethernet" && macAddressList.Ethernet.size() == 9)
				{
					mac = OUI + macAddressList.Ethernet[interfaceID];
				}
				else if (commandStream[1] == "FastEthernet" && macAddressList.FastEthernet.size() == 9)
				{
					mac = OUI + macAddressList.FastEthernet[interfaceID];
				}
				else if (commandStream[1] == "GigabitEthernet" && macAddressList.GigabitEthernet.size() == 9)
				{
					mac = OUI + macAddressList.GigabitEthernet[interfaceID];
				}
				else if (commandStream[1] == "Dot11Radio")
				{
					mac = OUI + "0d";
					char buffer[5];
					std::sprintf(buffer, "%04ld", interfaceID);
					mac += buffer;
				}
				configureInterfaceMode(type);
				if (activeInterfaces->count(interfaceID) == 0)
				{
					std::lock_guard<std::shared_mutex> lock(interfaceListMutex);
					(*activeInterfaces)[interfaceID] = std::make_shared<Interface>(getInterfaceType(type), intType, 1024, 1024, mac, interfaceID, isDebugModeEnabled);
				}
				currentInterface = activeInterfaces->at(interfaceID);
			}
			if (commandStream[0] == "router")
			{
				std::string type = commandStream[1];
				std::string ID = commandStream[2];
				routingProtocolID = Functions::stringToNum(commandStream[2]);
				if (type == "eigrp")
				{
					if (!eigrpList[ID])
					{
						eigrpList[ID] = std::make_shared<Protocol::EigrpInstance>();
					}
					if (Functions::isDecimal(ID))
					{
						std::unique_lock<std::shared_mutex> lock(globalEigrpMutex);
						*currentCommunicationMode = EigrpConfigs::CommunicationMode::MULTICAST;
						auto eigrpAs = eigrpList[ID]->autonomousSystems.find(routingProtocolID);
						if (eigrpAs == eigrpList[ID]->autonomousSystems.end())
						{
							lock.unlock();
							eigrpList[ID]->autonomousSystems[routingProtocolID] = std::make_shared<Protocol::EigrpAutonomousSystems>();
							eigrpAs = eigrpList[ID]->autonomousSystems.find(routingProtocolID);
							lock.lock();
						}
						// Update autonomous system instance list
						for (auto it = eigrpAutonomousSystems.begin(); it != eigrpAutonomousSystems.end();)
						{
							if (auto sharedPtr = it->second.lock())
							{
								++it;
							}
							else
							{
								it = eigrpAutonomousSystems.erase(it);
							}
						}
						if (eigrpAutonomousSystems.find(routingProtocolID) == eigrpAutonomousSystems.end())
						{
							eigrpAutonomousSystems[routingProtocolID] = eigrpAs->second;
						}
						auto eigrpIt = eigrpAs->second->addressFamilies.find(AddressFamily::IPv4);
						if (eigrpIt == eigrpAs->second->addressFamilies.end())
						{
							lock.unlock();
							eigrpAs->second->addressFamilies[AddressFamily::IPv4] = std::make_shared<Protocol::ClassicEigrp>(routingProtocolID, AddressFamily::IPv4);
							eigrpIt = eigrpAs->second->addressFamilies.find(AddressFamily::IPv4);
							lock.lock();
						}
						currentEigrp = eigrpIt->second;
						configureRoutingMode(RoutingMode::EIGRP_CLASSIC);
					}
					else
					{
						*currentCommunicationMode = EigrpConfigs::CommunicationMode::UNICAST;
						configureRoutingMode(RoutingMode::EIGRP_NAMED);
					}
					currentEigrpInstance = eigrpList[ID];
				}
				else if (type == "ospf")
				{
					if (!ospfList[routingProtocolID])
					{
						(ospfList)[routingProtocolID] = std::make_shared<Protocol::Ospf>();
					}
					currentOspf = ospfList.at(routingProtocolID).get();
				}
				else if (type == "bgp")
				{
					if (!bgpList[routingProtocolID])
					{
						(bgpList)[routingProtocolID] = std::make_shared<Protocol::Bgp>();
					}
				}
			}
		}

#pragma endregion

#pragma region InterfaceMode

		if (currentMode == "(config-if)#")
		{
			if (command == "exit")
			{
				exitMode(mode.globalConfiguration);
			}
			if (commandStream[0] == "ip" && commandStream[1] == "address")
			{
				if (commandStream[2] != "dhcp")
				{
					currentInterface.lock()->setIPv4(Functions::addressToByte(commandStream[2]), Functions::byteMaskToNum(Functions::addressToByte(commandStream[3])));
				}
				else
				{
					std::thread dhcpThread([this]()
									  { this->runDhcp(); });
					dhcpThread.detach();
				}
			}
		}

#pragma endregion

#pragma region RoutingMode

		if (currentMode == "(config-router)#")
		{
			if (currentSubMode == "eigrp_classic")
			{
				if (commandStream[0] == "network")
				{
					EigrpConfigs::Network network;
					network.ip = Functions::addressToByte(commandStream[1]);
					if (commandStream.size() == 3)
					{
						network.mask = Functions::addressToByte(commandStream[2]);
					}
					else
					{
						network.mask = Variable::IPv4::broadcast;
					}
					currentEigrp.lock()->addNetwork(network);
					currentEigrp.lock()->updateInterfaceList();
					currentEigrp.lock()->updateRoutingTableForConnected();
				}
			}
			else if (currentSubMode == "eigrp_named")
			{
				if (commandStream[0] == "address-family")
				{
					AddressFamily af;
					if (commandStream[1] == "ipv4")
					{
						af = AddressFamily::IPv4;
					}
					else if (commandStream[1] == "ipv6")
					{
						af = AddressFamily::IPv6;
					}
					if (commandStream[2] == "unicast")
					{
						routingProtocolID = Functions::stringToNum(commandStream[4]);
						if (af == AddressFamily::IPv4)
						{
							*currentCommunicationMode = EigrpConfigs::CommunicationMode::UNICAST;
						}
						else if (af == AddressFamily::IPv6 && /*IPV6 ENABLED*/false)
						{
							*currentCommunicationMode = EigrpConfigs::CommunicationMode::UNICAST;
						}
					}
					else if (commandStream[2] == "multicast")
					{
						routingProtocolID = Functions::stringToNum(commandStream[4]);
						if (/*MULTICAST ENABLED*/false)
						{
							if (af == AddressFamily::IPv4)
							{
								*currentCommunicationMode = EigrpConfigs::CommunicationMode::MULTICAST;
							}
							else if (af == AddressFamily::IPv6)
							{
								if (/*IPV6 ENABLED*/false)
								{
									*currentCommunicationMode = EigrpConfigs::CommunicationMode::MULTICAST;
								}
							}
						}
					}
					else
					{
						routingProtocolID = Functions::stringToNum(commandStream[3]);
					}
					if (commandStream[2] == "autonomous-system" || commandStream[3] == "autonomous-system")
					{
						auto eigrpAs = currentEigrpInstance.lock()->autonomousSystems.find(routingProtocolID);
						if (eigrpAs == currentEigrpInstance.lock()->autonomousSystems.end())
						{
							currentEigrpInstance.lock()->autonomousSystems[routingProtocolID] = std::make_shared<Protocol::EigrpAutonomousSystems>();
							eigrpAs = currentEigrpInstance.lock()->autonomousSystems.find(routingProtocolID);
						}
						// Update autonomous system instance list
						for (auto it = eigrpAutonomousSystems.begin(); it != eigrpAutonomousSystems.end();)
						{
							if (auto sharedPtr = it->second.lock())
							{
								++it;
							}
							else
							{
								it = eigrpAutonomousSystems.erase(it);
							}
						}
						if (eigrpAutonomousSystems.find(routingProtocolID) == eigrpAutonomousSystems.end())
						{
							eigrpAutonomousSystems[routingProtocolID] = eigrpAs->second;
						}
						auto eigrpIt = eigrpAs->second->addressFamilies.find(AddressFamily::IPv4);
						if (eigrpIt == eigrpAs->second->addressFamilies.end())
						{
							// eigrpAs->second->addressFamilies[AddressFamily::IPv4] = std::make_shared<Protocol::NamedEigrp>(routingProtocolID, AddressFamily::IPv4, currentEigrpInstance->); NAMED
							eigrpIt = eigrpAs->second->addressFamilies.find(AddressFamily::IPv4);
						}
						currentEigrp = eigrpIt->second;
						configureRoutingMode(RoutingMode::EIGRP_CLASSIC);
					}
				}
			}
			else if (currentSubMode == "ospf")
			{
			}
			else if (currentSubMode == "bgp")
			{
			}
			else if (currentSubMode == "rip")
			{
			}
		}

#pragma endregion

	}

	bool isList = false;
	if (isList)
	{
		executionHistory.push_back(command);
	}
	if (commandStream[0] == "ip" && (commandStream[1] == "route"))
	{
		isList = true;
	}
	if (commandStream[0] == "interface")
	{
		isList = true;
	}

	if (preProcessMode != mode.userExec && preProcessMode != mode.privilegedExec && command != "error")
	{
		Functions::printVector(commandHistory);
		// cout << "\n" << endl;
		Functions::printVector(commandStream);
		saveCommand(commandHistory, commandStream, isModeChanged, isExitCommand, isList);
		
		// Check if mode changed
		if (isModeChanged)
		{
			modeSchema = tempModeSchema;
		}
	}
}

void Terminal::runDhcp()
{
	ByteString mac;
	{
		auto interfaceInfo = currentInterface.lock()->Get();
		std::shared_lock<std::shared_mutex> lock(interfaceInfo->ipMutex);
		mac = interfaceInfo->macAddress;
	}

	currentInterface.lock()->dhcp->InitializeDhcp(mac);
}
