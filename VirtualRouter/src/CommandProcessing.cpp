#include <Terminal.h>

bool Terminal::executeCommand(std::string &command)
{
	// Reset the command directory
	currentDirectory = workingDirectory;

	isModeChanged = false;
	isExitCommand = false;
	std::string preProcessMode = currentMode;
	
	command = normalizeCommand(command);

	isCommandExecutionSuccessful = false;
	
	// Check if it's is a "do" command
	if (command.empty()) return false;
	if (isGlobalCommandExecution || (isHelpModeActive && isRunning)) return true;
	if (!isRunning || isCommandInvalid || !isCommandValid) return false;

	std::vector<std::string> TEMPcommandStream = splitIntoWords(command);
	std::vector<std::string> commandStream;

	bool textLine = false;
	for (size_t index = 0; index < TEMPcommandStream.size(); index++)
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
	if (commandStream.empty())
	{
		return false;
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
				Global::getInstance().setHostname(commandStream[1]);
			}
			if (commandStream[0] == "ipv6")
			{
				if (commandStream[1] == "router")
				{
					if (commandStream[2] == "eigrp")
					{
						std::string type = commandStream[2];
						std::string ID;
						if (commandStream.size() > 3)
						{
							ID = commandStream[3];
							routingProtocolID = Functions::stringToNum(commandStream[2]);
						}
						if (type == "eigrp")
						{
							std::unique_lock<std::shared_mutex> lock(globalEigrpMutex);
							// Update autonomous system instance list
							Protocol::EigrpAutonomousSystem* as;
							if (eigrpList.find((routingProtocolID)) != eigrpList.end())
							{
								if (!eigrpList[routingProtocolID]->isNamed)
								{
									as = eigrpList[routingProtocolID];
								}
								else
								{
									std::cout << "\n%" << "ERROR: AS used by named mode";
									return false; // AS used in named mode.
								}
							}
							else
							{
								eigrpList[routingProtocolID] = new Protocol::EigrpAutonomousSystem();
								as = eigrpList[routingProtocolID];
							}
							if (!as->ipv6)
							{
								lock.unlock();
								as->ipv6 = new Protocol::Eigrp(routingProtocolID, AddressFamily::IPv6);
								lock.lock();
							}
							currentEigrp = as->ipv4;
							configureRoutingMode("eigrp_classic", true);
						}
						else if (type == "ospf")
						{
							configureRoutingMode("ospf", true);
							if (!ospfList[static_cast<uint16_t>(routingProtocolID)])
							{
								(ospfList)[static_cast<uint16_t>(routingProtocolID)] = std::make_shared<Protocol::Ospf>();
							}
							currentOspf = ospfList.at(static_cast<uint16_t>(routingProtocolID)).get();
						}
					}
				}
			}
			if (commandStream[0] == "interface")
			{
				std::string type = commandStream[1];
				std::string interfaceID_temp = commandStream[2];
				interfaceID = static_cast<uint8_t>(Functions::stringToNum(commandStream[2]));
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
				if (commandStream[1] == "Ethernet" && macAddressList.Ethernet.size() >= interfaceID)
				{
					mac = OUI + macAddressList.Ethernet[interfaceID];
				}
				else if (commandStream[1] == "FastEthernet" && macAddressList.FastEthernet.size() >= interfaceID)
				{
					mac = OUI + macAddressList.FastEthernet[interfaceID];
				}
				else if (commandStream[1] == "GigabitEthernet" && macAddressList.GigabitEthernet.size() >= interfaceID)
				{
					mac = OUI + macAddressList.GigabitEthernet[interfaceID];
				}
				else if (commandStream[1] == "Dot11Radio")
				{
					mac = OUI + "0d";
					char buffer[5];
					std::sprintf(buffer, "%04ld", static_cast<long>(interfaceID));
					mac += buffer;
				}
				configureInterfaceMode(type);
				if (activeInterfaces->count(interfaceID) == 0)
				{
					std::lock_guard<std::shared_mutex> lock(interfaceListMutex);
					(*activeInterfaces)[interfaceID] = new Interface(getInterfaceType(type), intType, 1024, 1024, mac, interfaceID, isDebugModeEnabled);
				}
				currentInterface = activeInterfaces->at(interfaceID);
			}
			if (commandStream[0] == "router")
			{
				std::string type = commandStream[1];
				std::string ID;
				if (commandStream.size() > 2)
				{
					ID = commandStream[2];
					routingProtocolID = Functions::stringToNum(commandStream[2]);
				}
				if (type == "eigrp")
				{
					if (Functions::isDecimal(ID))
					{
						std::unique_lock<std::shared_mutex> lock(globalEigrpMutex);
						// Update autonomous system instance list
						Protocol::EigrpAutonomousSystem* as;
						if (eigrpList.find((routingProtocolID)) != eigrpList.end())
						{
							if (!eigrpList[routingProtocolID]->isNamed)
							{
								as = eigrpList[routingProtocolID];
							}
							else
							{
								std::cout << "ERROR" << std::endl;
								return false; // AS used in named mode.
							}
						}
						else
						{
							eigrpList[routingProtocolID] = new Protocol::EigrpAutonomousSystem();
							as = eigrpList[routingProtocolID];
						}
						if (!as->ipv4)
						{
							lock.unlock();
							as->ipv4 = new Protocol::Eigrp(routingProtocolID, AddressFamily::IPv4);
							lock.lock();
						}
						currentEigrp = as->ipv4;
						configureRoutingMode("eigrp_classic");
					}
					else
					{
						if (namedEigrpList.find(ID) == namedEigrpList.end())
						{
							namedEigrpList[ID] = new Protocol::EigrpNamed();
						}
						currentEigrpNamed = namedEigrpList[ID];
						configureRoutingMode("eigrp_named");
					}
				}
				else if (type == "ospf")
				{
					configureRoutingMode("ospf");
					if (!ospfList[static_cast<uint16_t>(routingProtocolID)])
					{
						(ospfList)[static_cast<uint16_t>(routingProtocolID)] = std::make_shared<Protocol::Ospf>();
					}
					currentOspf = ospfList.at(static_cast<uint16_t>(routingProtocolID)).get();
				}
				else if (type == "bgp")
				{
					configureRoutingMode("bgp");
					if (!bgpList[static_cast<uint16_t>(routingProtocolID)])
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
		}

#pragma endregion

#pragma region RoutingMode

		if (currentMode == "(config-router)#")
		{
			if (command == "exit")
			{
				exitMode(mode.globalConfiguration);
			}
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
					currentEigrp->addNetwork(network);
					currentEigrp->updateInterfaceList();
					currentEigrp->updateRoutingTableForConnected();
				}
			}
			else if (currentSubMode == "eigrp_named")
			{
				if (commandStream[0] == "address-family")
				{
					AddressFamily af;
					EigrpConfigs::CommunicationMode comMode = EigrpConfigs::CommunicationMode::UNICAST;
					std::string comString = "unicast";
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
					}
					else if (commandStream[2] == "multicast")
					{
						if (/*multicast enabled*/false)
						{
							routingProtocolID = Functions::stringToNum(commandStream[4]);
							comMode = EigrpConfigs::CommunicationMode::MULTICAST;
							comString = "multicast";
						}
						else
						{
							std::cout << "\n%" << "ERROR multicast not enabled";
							return false;
						}
					}
					else
					{
						routingProtocolID = Functions::stringToNum(commandStream[3]);
					}
					if (commandStream[2] == "autonomous-system" || commandStream[3] == "autonomous-system")
					{
						Protocol::EigrpAutonomousSystem* eigrpAs;
						if (currentEigrpNamed->autonomousSystems.find(routingProtocolID) == currentEigrpNamed->autonomousSystems.end())
						{
							const auto& eigrp = eigrpList.find(routingProtocolID);
							if (eigrp == eigrpList.end())
							{
								auto* newAS = new Protocol::EigrpAutonomousSystem();
								newAS->isNamed = true;
								currentEigrpNamed->autonomousSystems[routingProtocolID] = newAS;
								eigrpList[routingProtocolID] = newAS;
								eigrpAs = newAS;
							}
							else if (!eigrp->second->isNamed)
							{
								std::cout << "ERROR" << std::endl;
							}
						}
						else
						{
							eigrpAs = currentEigrpNamed->autonomousSystems[routingProtocolID];
						}

						if (af == AddressFamily::IPv4)
						{
							if (eigrpAs->ipv4)
							{
								currentEigrp = eigrpAs->ipv4;
							}
							else
							{
								eigrpAs->ipv4 = new Protocol::Eigrp(routingProtocolID, af);
								currentEigrp = eigrpAs->ipv4;
								changeMode(mode.eigrpAddressFamily);
								configureAddressFamily(AddressFamily::IPv4);
							}
						}
						else if (af == AddressFamily::IPv6)
						{
							if (eigrpAs->ipv6)
							{
								currentEigrp = eigrpAs->ipv6;
							}
							else
							{
								eigrpAs->ipv6 = new Protocol::Eigrp(routingProtocolID, af);
								currentEigrp = eigrpAs->ipv6;
								changeMode(mode.eigrpAddressFamily);
								configureAddressFamily(AddressFamily::IPv6);
							}
						}
						if (workingDirectory.size() > 0 && workingDirectory[0].contains(comString))
						{
							workingDirectory = workingDirectory[0][comString];
						}
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

	bool executeSuccess = false;

	if (preProcessMode != mode.userExec && preProcessMode != mode.privilegedExec && command != "error")
	{
		Functions::printVector(commandHistory);
		// cout << "\n" << endl;
		Functions::printVector(commandStream);

		// TEMPORARY
		// Need to put it in command json
		if (isExitCommand)
		{
			commandHistory = commandStream;
		}
		// TEMPORARY
		
		executeSuccess = saveCommand(commandHistory, commandStream, isModeChanged, isExitCommand, isList);
		
		// Check if mode changed
		if (isModeChanged)
		{
			modeSchema = tempModeSchema;
		}
	}
	if (executeSuccess || isCommandValid)
	{
		return true;
	}
	return false;
}
