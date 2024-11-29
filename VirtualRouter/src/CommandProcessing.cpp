#include <Terminal.h>

void Terminal::executeCommand(string &command)
{
	isModeChanged = false;
	string preProcessMode = currentMode;

	command = normalizeCommand(command);
	isCommandExecutionSuccessful = false;

	vector<string> TEMPcommandStream = splitIntoWords(command);
	vector<string> commandStream;

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
			string exit = "exit";
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
						cout << "\n  " + str;
					}
				}
			}
			if (command == "show clock")
			{
				cout << "\n"
					 << timeManager.GetTime();
			}
			if (command == "write memory")
			{
				if (!doc.save_file("../VirtualRouter/Dir/startup-config.xml"))
				{
					std::cerr << "Error saving XML file" << std::endl;
				}
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
				cout << "\nEnter configuration commands, one per line.  End with CNTL/Z.";
			}
			if (command == "exit")
			{
				changeMode(mode.userExec);
			}
		}

#pragma endregion

#pragma region GlobalConfiguration

		if (currentMode == mode.globalConfiguration)
		{
			if (command == "exit")
			{
				changeMode(mode.privilegedExec);
			}
			if (commandStream[0] == "hostname")
			{
				Global::getInstance().Hostname() = commandStream[1];
			}
			if (commandStream[0] == "interface")
			{
				string type = commandStream[1];
				string interfaceID_temp = commandStream[2];
				interfaceID = static_cast<unsigned long>(Functions::stringToNum(commandStream[2]));
				string intType;
				if ((type == "Ethernet" || type == "GigabitEthernet" || type == "FastEthernet") && physicalInterfaces.size() >= interfaceID)
				{
					intType = physicalInterfaces[interfaceID];
				}
				else
				{
					intType = "NO_INTERFACE";
				}
				string mac;
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
					(*activeInterfaces)[interfaceID] = std::make_shared<Interface>(intType, 1024, 1024, mac, interfaceID, isDebugModeEnabled);
				}
				CurrentInterface = activeInterfaces->at(interfaceID).get();
			}
			if (commandStream[0] == "router")
			{
				string type = commandStream[1];
				string ID = commandStream[2];
				routingProtocolID = Functions::stringToNum(commandStream[2]);
				if (type == "eigrp")
				{
					if (!eigrpList[ID])
					{
						eigrpList[ID] = std::make_shared<Protocol::EigrpInstance>();
					}
					if (Functions::isDecimal(ID))
					{
						*currentCommunicationMode = EigrpConfigs::CommunicationMode::MULTICAST;
						auto eigrpAs = eigrpList[ID]->autonomousSystems.find(routingProtocolID);
						if (eigrpAs == eigrpList[ID]->autonomousSystems.end())
						{
							eigrpList[ID]->autonomousSystems[routingProtocolID] = std::make_shared<Protocol::EigrpAutonomousSystems>();
							eigrpAs = eigrpList[ID]->autonomousSystems.find(routingProtocolID);
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
							eigrpAs->second->addressFamilies[AddressFamily::IPv4] = std::make_shared<Protocol::ClassicEigrp>(routingProtocolID, AddressFamily::IPv4);
							eigrpIt = eigrpAs->second->addressFamilies.find(AddressFamily::IPv4);
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
					if (!OspfList[routingProtocolID])
					{
						(OspfList)[routingProtocolID] = std::make_shared<Protocol::Ospf>();
					}
					CurrentOspf = OspfList.at(routingProtocolID).get();
				}
				else if (type == "bgp")
				{
					if (!BgpList[routingProtocolID])
					{
						(BgpList)[routingProtocolID] = std::make_shared<Protocol::Bgp>();
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
				changeMode(mode.globalConfiguration);
			}
			if (commandStream[0] == "ip" && commandStream[1] == "address")
			{
				if (commandStream[2] != "dhcp")
				{
					CurrentInterface->setIPv4(Functions::addressToByte(commandStream[2]), Functions::addressToByte(commandStream[3]));
				}
				else
				{
					thread dhcpThread([this]()
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
					EigrpConfigs::network network;
					network.ip = Functions::addressToByte(commandStream[1]);
					if (commandStream.size() == 3)
					{
						network.mask = Functions::addressToByte(commandStream[2]);
					}
					else
					{
						network.mask = variable.ip.broadcast;
					}
					currentEigrp->AddNetwork(network);
					currentEigrp->UpdateInterfaceList();
					currentEigrp->UpdateRoutingTableForConnected();
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
						auto eigrpAs = currentEigrpInstance->autonomousSystems.find(routingProtocolID);
						if (eigrpAs == currentEigrpInstance->autonomousSystems.end())
						{
							currentEigrpInstance->autonomousSystems[routingProtocolID] = std::make_shared<Protocol::EigrpAutonomousSystems>();
							eigrpAs = currentEigrpInstance->autonomousSystems.find(routingProtocolID);
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
							eigrpAs->second->addressFamilies[AddressFamily::IPv4] = std::make_shared<Protocol::ClassicEigrp>(routingProtocolID, AddressFamily::IPv4);
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

	bool notlist = false;
	if (notlist)
	{
		executionHistory.push_back(command);
	}
	if (commandStream[0] == "ip" && (commandStream[1] == "route"))
	{
		notlist = true;
	}

	if (preProcessMode != mode.userExec && preProcessMode != mode.privilegedExec && command != "error")
	{
		Functions::printVector(commandHistory);
		// cout << "\n" << endl;
		Functions::printVector(commandStream);
		saveCommand(commandHistory, commandStream, isModeChanged, notlist);
	}
}

void Terminal::runDhcp()
{
	std::string mac = CurrentInterface->Get().mac;

	CurrentInterface->dhcp->InitializeDhcp(mac);
}
