#include <Terminal.h>
#include <Global.h>
#include <Dhcp.h>

bool Terminal::executeCommand(std::string &command)
{
	// Reset the command directory
	currentDirectory = workingDirectory;

	isModeChanged = false;
	isExitCommand = false;
	error = false;
	bool no = false;
	std::string preProcessMode = currentMode;
	
	command = normalizeCommand(command);

	isCommandExecutionSuccessful = false;
	
	// Check if it's is a "do" command
	if (command.empty()) return false;
	if (isGlobalCommandExecution || (isHelpModeActive && isRunning)) return true;
	if (!isRunning || isCommandInvalid || !isCommandValid) return false;

	std::vector<std::string> TEMPcommandStream = splitIntoWords(command);
	std::vector<std::string> commandStream;

	bool isList = false;
	bool textLine = false;
	for (size_t index = 0; index < TEMPcommandStream.size(); index++)
	{
		if (index == 0 && TEMPcommandStream[0] == "no")
		{
			no = true;
			continue;
		}
		else if (!textLine)
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

	{

#pragma region UserExec

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
				if (!no)
				{
					Global::getInstance().setHostname(commandStream[1]);
				}
				else
				{
					Global::getInstance().setHostname("Router");
				}
			}
			if (commandStream[0] == "ipv6")
			{
				if (commandStream[1] == "router")
				{
					isList = true;
					if (commandStream[2] == "eigrp")
					{
						std::string type = commandStream[2];
						std::string ID;
						if (commandStream.size() > 3)
						{
							ID = commandStream[3];
							routingProtocolID = Functions::stringToNum(commandStream[3]);
						}
						if (type == "eigrp")
						{
							if (!no)
							{
								Protocol::EigrpAutonomousSystem* as = currentVrf->getEigrpAutonomousSystem(routingProtocolID);
								if (as)
								{
									if (as->ipv6Named)
									{
										std::cout << "\n%" << "ERROR: AS used by named mode";
										return false; // AS used in named mode.
									}
								}
								else
								{
									as = currentVrf->addEigrpAutonomousSystem(routingProtocolID);
								}
								if (!as->ipv6)
								{
									as->ipv6 = new Protocol::Eigrp(routingProtocolID, AddressFamily::IPv6, currentVrf);
								}
								currentEigrp = as->ipv6;
								configureRoutingMode("eigrp_classic", true);
							}
							else
							{
								Protocol::EigrpAutonomousSystem* as = currentVrf->getEigrpAutonomousSystem(routingProtocolID);
								if (as)
								{
									if (!as->ipv6Named && as->ipv6)
									{
										delete as->ipv6;
										as->ipv6 = nullptr;
										if (!as->ipv4)
										{
											currentVrf->removeEigrpAutonomousSystem(routingProtocolID);
										}
									}
								}
							}
						}
						else if (type == "ospf")
						{
							if (!no)
							{
								configureRoutingMode("ospf", true);
								if (!ospfList[static_cast<uint16_t>(routingProtocolID)])
								{
									(ospfList)[static_cast<uint16_t>(routingProtocolID)] = std::make_shared<Protocol::Ospf>();
								}
								currentOspf = ospfList.at(static_cast<uint16_t>(routingProtocolID)).get();
							}
							else
							{

							}
						}
					}
				}
			}
			if (commandStream[0] == "interface")
			{
				isList = true;
				std::string type = commandStream[1];
				std::string interfaceID_temp = commandStream[2];
				interfaceID = static_cast<uint8_t>(Functions::stringToNum(commandStream[2]));
				InterfaceType interfaceType = getInterfaceType(type);
				std::string intType;
				std::string mac;
				size_t id = static_cast<size_t>(std::floor(interfaceID));
				if (!Global::getInstance().getInterface(getInterfaceType(type), interfaceID))
				{
					if (no)
					{
						Global::getInstance().removeInterface(interfaceType, interfaceID);
						currentVrf->removeInterface(interfaceType, interfaceID);
					}
					else
					{
						if ((type == "Ethernet" || type == "GigabitEthernet" || type == "FastEthernet") && physicalInterfaces.size() >= id)
						{
							intType = physicalInterfaces[id];
						}
						else
						{
							intType = "NO_INTERFACE";
						}
						if (commandStream[1] == "Ethernet" && macAddressList.Ethernet.size() >= id)
						{
							mac = OUI + macAddressList.Ethernet[id];
						}
						else if (commandStream[1] == "FastEthernet" && macAddressList.FastEthernet.size() >= id)
						{
							mac = OUI + macAddressList.FastEthernet[id];
						}
						else if (commandStream[1] == "GigabitEthernet" && macAddressList.GigabitEthernet.size() >= id)
						{
							mac = OUI + macAddressList.GigabitEthernet[id];
						}
						else if (commandStream[1] == "Dot11Radio")
						{
							mac = OUI + "0d";
							char buffer[5];
							std::sprintf(buffer, "%04ld", static_cast<long>(interfaceID));
							mac += buffer;
						}
						InterfaceType interfaceTypeEnum = getInterfaceType(type);
						Global::getInstance().addInterface(interfaceType, intType, 1024, 1024, mac, interfaceID, isDebugModeEnabled);
						currentVrf->addInterface(Global::getInstance().getInterface(interfaceTypeEnum, interfaceID), interfaceType, interfaceID);
					}
				}
				configureInterfaceMode(type);
				currentInterface = Global::getInstance().getInterface(interfaceType, interfaceID);
			}
			if (commandStream[0] == "router")
			{
				isList = true;
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
						Protocol::EigrpAutonomousSystem* as = currentVrf->getEigrpAutonomousSystem(routingProtocolID);
						if (!no)
						{
							if (as)
							{
								if (as->ipv4Named)
								{
									std::cout << "ERROR" << std::endl;
									return false; // AS used in named mode.
								}
							}
							else
							{
								as = currentVrf->addEigrpAutonomousSystem(routingProtocolID);
							}
							if (!as->ipv4)
							{
								as->ipv4 = new Protocol::Eigrp(routingProtocolID, AddressFamily::IPv4, Global::getInstance().getRoutingInstance("default"));
							}
							currentEigrp = as->ipv4;
							configureRoutingMode("eigrp_classic");
						}
						else
						{
							if (as)
							{
								if (!as->ipv4Named && as->ipv4)
								{
									delete as->ipv4;
									as->ipv4 = nullptr;
									if (!as->ipv6)
									{
										currentVrf->removeEigrpAutonomousSystem(routingProtocolID);
									}
								}
							}
						}
					}
					else
					{
						if (!no)
						{
							if (!currentVrf->getEigrpNamed(ID))
							{
								currentVrf->addEigrpNamed(ID);
							}
							currentEigrpNamed = currentVrf->getEigrpNamed(ID);
							configureRoutingMode("eigrp_named");
						}
						else
						{
							currentVrf->removeEigrpNamed(ID);
						}
					}
				}
				else if (type == "ospf")
				{
					if (!no)
					{
						configureRoutingMode("ospf");
						if (!ospfList[static_cast<uint16_t>(routingProtocolID)])
						{
							(ospfList)[static_cast<uint16_t>(routingProtocolID)] = std::make_shared<Protocol::Ospf>();
						}
						currentOspf = ospfList.at(static_cast<uint16_t>(routingProtocolID)).get();
					}
				}
				else if (type == "bgp")
				{
					if (!no)
					{
						configureRoutingMode("bgp");
						if (!bgpList[static_cast<uint16_t>(routingProtocolID)])
						{
							(bgpList)[routingProtocolID] = std::make_shared<Protocol::Bgp>();
						}
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
				if (!no)
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
			else if (commandStream[0] == "ipv6")
			{
				if (commandStream[1] == "address")
				{
					if (Functions::isIPv6Address(commandStream[2]))
					{
						ByteString ipv6Address = Functions::addressToByte(commandStream[2]);
						if (Functions::isLocalLink(ipv6Address))
						{
							if (!no)
							{
								currentInterface->setIPv6(ipv6Address, true);
							}
							else
							{
								currentInterface->removeIPv6(true);
							}
						}
						else if (!no)
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
								if (!no)
								{
									currentInterface->setIPv6(Functions::addressToByte(ipv6Address), false, mask);
								}
								else
								{
									currentInterface->removeIPv6(false);
								}
							}
						}
					}
				}
				if (commandStream[1] == "eigrp")
				{
					isList = true;
					uint32_t as = static_cast<uint32_t>(std::stoi(commandStream[2]));
					auto eigrpAs = currentVrf->getEigrpAutonomousSystem(as);
					if (!no)
					{
						if (eigrpAs && eigrpAs->ipv6)
						{
							eigrpAs->ipv6->addEigrpInterface(currentInterface);
						}
						{
							std::unique_lock<std::shared_mutex> lock(currentInterface->configs.ipMutex);
							currentInterface->configs.eigrp.ipv6AutonomousSystems.insert(as);
						}
					}
					else
					{
						if (eigrpAs && eigrpAs->ipv6)
						{
							delete eigrpAs->ipv6->eigrpInterfaceList[currentInterface->configs.interfaceType][currentInterface->configs.id];
						}
					}
				}
			}
		}

#pragma endregion

#pragma region RoutingMode

		if (currentMode == mode.routing || currentMode == mode.addressFamily || currentMode == mode.addressFamilyTopology || currentMode == mode.routingV6)
		{
			if (currentSubMode == "eigrp_classic" || currentSubMode == "eigrp_classic_vrf" || (currentSubMode == "eigrp_named" && currentMode == mode.addressFamilyTopology))
			{
				if (commandStream[0] == "address-family")
				{
					isList = true;
					std::string vrf;
					if (commandStream[2] == "vrf")
					{
						vrf = commandStream[3];
					}
					else if (commandStream[3] == "vrf")
					{
						vrf = commandStream[4];
					}
					auto vrfInstance = Global::getInstance().getRoutingInstance(vrf);
					if (!vrfInstance && !no)
					{
						std::cout << "\n%" << "VRF " << vrf << " does not exist or is not enabled for IPv4";
						return false;
					}
					currentVrf = vrfInstance;
					if (!no)
					{
						changeMode(mode.addressFamily);
						if (workingDirectory->size() > 0 && (*workingDirectory)[0].contains("eigrp_classic_vrf"))
						{
							workingDirectory = &(*workingDirectory)[0]["eigrp_classic_vrf"];
						}
					}
				}
				else if (commandStream[0] == "auto-summary")
				{
					currentEigrp->enableAutoSummary(!no);
				}
				else if (commandStream[0] == "default")
				{
					// XXX
				}
				else if (commandStream[0] == "default-information")
				{
					// XXX
				}
				else if (commandStream[0] == "default-metric")
				{
					if (!no)
					{
						std::unique_lock<std::shared_mutex> lock(currentEigrp->getConfigs()->configsMutex);
						auto& defaultMetric = currentEigrp->getConfigs()->defaultMetrics;
						defaultMetric.k1_Bandwidth = static_cast<uint8_t>(std::stoi(commandStream[1]));
						defaultMetric.k3_Delay = static_cast<uint32_t>(std::stoi(commandStream[2]));
						defaultMetric.k4_Reliability = static_cast<uint8_t>(std::stoi(commandStream[3]));
						defaultMetric.k2_Load = static_cast<uint8_t>(std::stoi(commandStream[4]));
						defaultMetric.k5_MTU = static_cast<uint16_t>(std::stoi(commandStream[5]));
					}
					else
					{
						std::unique_lock<std::shared_mutex> lock(currentEigrp->getConfigs()->configsMutex);
						auto& defaultMetric = currentEigrp->getConfigs()->defaultMetrics;
						defaultMetric.k1_Bandwidth = 1;
						defaultMetric.k2_Load = 0;
						defaultMetric.k3_Delay = 1;
						defaultMetric.k4_Reliability = 0;
						defaultMetric.k5_MTU = 0;
						defaultMetric.k6_Power = 0;
					}
				}
				else if (commandStream[0] == "distance")
				{
					if (Functions::isDecimal(commandStream[1]))
					{
						// XXX
					}
					else if (commandStream[1] == "eigrp")
					{
						if (!no)
						{
							std::unique_lock<std::shared_mutex> lock(currentEigrp->getConfigs()->configsMutex);
							currentEigrp->getConfigs()->adminDistance = static_cast<uint8_t>(std::stoi(commandStream[2]));
							currentEigrp->getConfigs()->externalAdminDistance = static_cast<uint8_t>(std::stoi(commandStream[3]));
						}
						else
						{
							std::unique_lock<std::shared_mutex> lock(currentEigrp->getConfigs()->configsMutex);
							currentEigrp->getConfigs()->adminDistance = 90;
							currentEigrp->getConfigs()->externalAdminDistance = 170;
						}
					}
				}
				else if (commandStream[0] == "eigrp")
				{
					std::unique_lock<std::shared_mutex> lock(currentEigrp->getConfigs()->configsMutex);
					if (commandStream[1] == "event-log-size" && Functions::isDecimal(commandStream[2])) { currentEigrp->getConfigs()->eventLogSize = static_cast<uint32_t>(std::stoi(commandStream[2])); }
					else if (commandStream[1] == "logNeighborChanges") { currentEigrp->getConfigs()->logNeighborChanges = !no; }
					else if (commandStream[1] == "logNeighborWarnings") { currentEigrp->getConfigs()->logNeighborWarnings = !no; }
					else if (commandStream[1] == "router-id") { if (!no) currentEigrp->setRouterID(Functions::addressToByte(commandStream[2])); else currentEigrp->clearRouterID(); }
					else if (commandStream[1] == "stub")
					{
						if (!no)
						{
							bool connected = false;
							bool leakMap = false;
							bool redistributed = false;
							bool stat = false;
							bool summary = false;
							for (size_t i = 2; i <= commandStream.size(); i++)
							{
								if (commandStream[i] == "connected") { connected = true; }
								else if (commandStream[i] == "leak-map") { leakMap = true; }
								else if (commandStream[i] == "redistrubuted") { redistributed = true; }
								else if (commandStream[i] == "static") { stat = true; }
								else if (commandStream[i] == "summary") { summary = true; }
							}
							currentEigrp->setStub(true, connected, stat, leakMap, summary, redistributed);
						}
						else
						{
							currentEigrp->setStub(false);
						}
					}
					else if (commandStream[1] == "upgrade-cli")
					{
						// XXX
					}
				}
				else if (command == "exit" && currentMode == mode.addressFamily)
				{
					exitMode(mode.routing);
					currentVrf = Global::getInstance().getRoutingInstance("default");
				}
				else if (commandStream[0] == "exit-af-topology")
				{
					exitMode(mode.addressFamily);
					configureAddressFamily(AddressFamily::IPv4);
					if (workingDirectory->size() > 0 && (*workingDirectory)[0].contains("unicast"))
					{
						workingDirectory = &(*workingDirectory)[0]["unicast"]; // TODO fix unicast/multicast here
					}
				}
				else if (commandStream[0] == "maximum-paths")
				{
					currentEigrp->getConfigs()->maxPaths.store(static_cast<uint8_t>(std::stoi(commandStream[1])));
				}
				else if (commandStream[0] == "metric")
				{
					if (commandStream[1] == "maximum-paths" && Functions::isDecimal(commandStream[2]))
					{
						std::unique_lock<std::shared_mutex> lock(currentEigrp->getConfigs()->configsMutex);
						currentEigrp->getConfigs()->maxPaths = static_cast<uint8_t>(std::stoi(commandStream[2]));
					}
					else if (commandStream[1] == "weights")
					{
						std::unique_lock<std::shared_mutex> lock(currentEigrp->getConfigs()->configsMutex);
						currentEigrp->getConfigs()->TOS = static_cast<uint8_t>(std::stoi(commandStream[2]));
						auto& kvalue = currentEigrp->getConfigs()->kvalue;
						kvalue.k1_Bandwidth = static_cast<uint8_t>(std::stoi(commandStream[3]));
						kvalue.k3_Delay = static_cast<uint32_t>(std::stoi(commandStream[4]));
						kvalue.k4_Reliability = static_cast<uint8_t>(std::stoi(commandStream[5]));
						kvalue.k2_Load = static_cast<uint8_t>(std::stoi(commandStream[6]));
						kvalue.k5_MTU = static_cast<uint16_t>(std::stoi(commandStream[7]));
					}
				}
				else if (commandStream[0] == "neighbor")
				{
					isList = true;
					ByteString neighborIp = Functions::addressToByte(commandStream[1]);
					InterfaceType type = getInterfaceType(commandStream[2]);
					if (type != InterfaceType::UNDEFINED)
					{
						float interfaceId = std::stof(commandStream[3]);

						currentEigrp->enableUnicastNeighbor(neighborIp, type, interfaceId);
					}
				}
				else if (commandStream[0] == "network")
				{
					isList = true;
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
				else if (commandStream[0] == "offset-list")
				{
					// XXX
				}
				else if (commandStream[0] == "passive-interface")
				{
					isList = true;
					currentEigrp->addPassiveInterface(getInterfaceType(commandStream[1]), std::stof(commandStream[2]));
				}
				else if (commandStream[0] == "redistribute")
				{
					// XXX
				}
				else if (commandStream[0] == "snmp")
				{
					// XXX
				}
				else if (commandStream[0] == "timers")
				{
					if (commandStream[1] == "active-time")
					{
						if (Functions::isDecimal(commandStream[2]))
						{
							std::unique_lock<std::shared_mutex> lock(currentEigrp->getConfigs()->configsMutex);
							currentEigrp->getConfigs()->activeTime = static_cast<uint16_t>(std::stoi(commandStream[2]));
							currentEigrp->getConfigs()->stuckInActiveTime = static_cast<uint16_t>(std::stoi(commandStream[2]) / 2);
						}
						else if (commandStream[2] == "disabled")
						{
							currentEigrp->getConfigs()->activeDisabled.store(true, std::memory_order_release);
						}
					}
					else if (commandStream[1] == "graceful-restart")
					{
						currentEigrp->getConfigs()->purgeTime.store(static_cast<uint16_t>(std::stoi(commandStream[3])));
					}
				}
				else if (commandStream[0] == "traffic-share")
				{
					if (commandStream[1] == "balenced")
					{
						std::unique_lock<std::shared_mutex> lock(currentEigrp->getConfigs()->configsMutex);
						currentEigrp->getConfigs()->trafficShareMode = EigrpConfigs::TrafficShareMode::Balenced;
					}
					else if (commandStream[1] == "min")
					{
						std::unique_lock<std::shared_mutex> lock(currentEigrp->getConfigs()->configsMutex);
						currentEigrp->getConfigs()->trafficShareMode = EigrpConfigs::TrafficShareMode::Minimum;
					}
				}
				else if (commandStream[0] == "variance" && Functions::isDecimal(commandStream[1]))
				{
					currentEigrp->setVariance(static_cast<uint8_t>(std::stoi(commandStream[1])));
				}
			}
			else if (currentSubMode == "eigrp_named")
			{
				if (currentMode == mode.routing)
				{
					if (commandStream[0] == "address-family")
					{
						isList = true;
						isModeChanged = true;
						AddressFamily af;
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
							if (af == AddressFamily::IPv4 && currentEigrpNamed->ipv4)
							{
								currentEigrp = currentEigrpNamed->ipv4;
								changeMode(mode.addressFamily);
								configureAddressFamily(AddressFamily::IPv4);
							}
							else if (af == AddressFamily::IPv6 && currentEigrpNamed->ipv6)
							{
								currentEigrp = currentEigrpNamed->ipv6;
								changeMode(mode.addressFamily);
								configureAddressFamily(AddressFamily::IPv4);
							}
							else
							{
								Protocol::EigrpAutonomousSystem* eigrpAs = currentVrf->getEigrpAutonomousSystem(routingProtocolID);
								if (!eigrpAs)
								{
									eigrpAs = currentVrf->addEigrpAutonomousSystem(routingProtocolID);
								}
								if (af == AddressFamily::IPv4)
								{
									if (!eigrpAs->ipv4)
									{
										eigrpAs->ipv4 = new Protocol::Eigrp(routingProtocolID, af, Global::getInstance().getRoutingInstance("default"));
										eigrpAs->ipv4Named = true;
										currentEigrpNamed->ipv4 = eigrpAs->ipv4;
										currentEigrp = eigrpAs->ipv4;
									}
									else
									{
										std::cout << "\nERROR";
										return false;
									}
									changeMode(mode.addressFamily);
									configureAddressFamily(AddressFamily::IPv4);
								}
								else if (af == AddressFamily::IPv6)
								{
									if (!eigrpAs->ipv6)
									{
										eigrpAs->ipv6 = new Protocol::Eigrp(routingProtocolID, af, Global::getInstance().getRoutingInstance("default"));
										eigrpAs->ipv6Named = true;
										currentEigrpNamed->ipv6 = eigrpAs->ipv6;
										currentEigrp = eigrpAs->ipv6;
									}
									else
									{
										std::cout << "\nError";
										return false;
									}
									changeMode(mode.addressFamily);
									configureAddressFamily(AddressFamily::IPv6);
								}
							}
							if (workingDirectory->size() > 0 && (*workingDirectory)[0].contains(comString))
							{
								workingDirectory = &(*workingDirectory)[0][comString];
							}
						}
					}
					else if (commandStream[0] == "default")
					{
						//TODO
					}
					else if (commandStream[0] == "exit")
					{
						changeMode(mode.globalConfiguration);
					}
					else if (commandStream[0] == "no")
					{
						//TODO
					}
					else if (commandStream[0] == "service-family")
					{
						isList = true;
						AddressFamily af;
						uint32_t as;
						std::string vrf = "default";
						if (commandStream[1] == "ipv4")
						{
							af = AddressFamily::IPv4;
						}
						else if (commandStream[1] == "ipv6")
						{
							af = AddressFamily::IPv6;
						}
						if (commandStream[2] == "autonomous-system")
						{
							as = static_cast<uint32_t>(std::stoi(commandStream[3]));
						}
						else if (commandStream[2] == "vrf")
						{
							vrf = commandStream[3];
							as = static_cast<uint32_t>(std::stoi(commandStream[5]));
						}
						currentVrf = Global::getInstance().getRoutingInstance(vrf);
						// TODO add service eigrp
					}
				}
				else if (currentMode == mode.addressFamily)
				{
					if (commandStream[0] == "af-interface")
					{
						isList = true;

						InterfaceType type = getInterfaceType(commandStream[1]);
						float interfaceId = std::stof(commandStream[2]);
						auto& intList = currentEigrp->eigrpInterfaceList[type];

						if (intList.find(interfaceId) != intList.end())
						{
							currentEigrpInterface = intList[interfaceId];
						}

						changeMode(mode.addressFamilyInterface);
						configureAddressFamily(currentEigrp->addressFamily);
					}
					else if (commandStream[0] == "default")
					{
						// TODO
					}
					else if (commandStream[0] == "eigrp")
					{
						std::unique_lock<std::shared_mutex> lock(currentEigrp->getConfigs()->configsMutex);

						if (commandStream[1] == "default-route-tag") 
						{
							uint32_t routeTag;
							if (Functions::isDecimal(commandStream[2]))
							{
								routeTag = static_cast<uint32_t>(std::stoi(commandStream[2]));
							}
							else
							{
								routeTag = Functions::byteToNum(Functions::addressToByte(commandStream[2]));
							}
							// TODO set the route tag
						}
						else if (commandStream[1] == "event-log-size" && Functions::isDecimal(commandStream[2])) { currentEigrp->getConfigs()->eventLogSize = static_cast<uint32_t>(std::stoi(commandStream[2])); }
						else if (commandStream[1] == "logNeighborChanges") { currentEigrp->getConfigs()->logNeighborChanges = true; }
						else if (commandStream[1] == "logNeighborWarnings") { currentEigrp->getConfigs()->logNeighborWarnings = true; }
						else if (commandStream[1] == "router-id") { currentEigrp->setRouterID(Functions::addressToByte(commandStream[2])); }
						else if (commandStream[1] == "stub")
						{
							bool connected = false;
							bool leakMap = false;
							bool redistributed = false;
							bool stat = false;
							bool summary = false;
							for (size_t i = 2; i <= commandStream.size(); i++)
							{
								if (commandStream[i] == "connected") { connected = true; }
								else if (commandStream[i] == "leak-map") { leakMap = true; }
								else if (commandStream[i] == "redistrubuted") { redistributed = true; }
								else if (commandStream[i] == "static") { stat = true; }
								else if (commandStream[i] == "summary") { summary = true; }
							}
							currentEigrp->setStub(true, connected, stat, leakMap, summary, redistributed);
						}
						else if (commandStream[1] == "stub-site")
						{
							// TODO
						}
					}
					else if (command == "exit-address-family" && currentMode == mode.addressFamily)
					{
						exitMode(mode.routing);
						currentVrf = Global::getInstance().getRoutingInstance("default");
					}
					else if (commandStream[1] == "maximum-prefix")
					{
						uint32_t prefNum = static_cast<uint32_t>(std::stoi(commandStream[1]));
						if (Functions::isDecimal(commandStream[2]))
						{
							// TODO
						}
					}
					else if (commandStream[0] == "metric")
					{
						if (commandStream[1] == "rib-scale" && Functions::isDecimal(commandStream[1]))
						{
							uint8_t ribScale = static_cast<uint8_t>(std::stoi(commandStream[2]));
							// TODO
						}
						else if (commandStream[1] == "weights")
						{
							std::unique_lock<std::shared_mutex> lock(currentEigrp->getConfigs()->configsMutex);
							currentEigrp->getConfigs()->TOS = static_cast<uint8_t>(std::stoi(commandStream[2]));
							auto& kvalue = currentEigrp->getConfigs()->kvalue;
							kvalue.k1_Bandwidth = static_cast<uint8_t>(std::stoi(commandStream[3]));
							kvalue.k3_Delay = static_cast<uint32_t>(std::stoi(commandStream[4]));
							kvalue.k4_Reliability = static_cast<uint8_t>(std::stoi(commandStream[5]));
							kvalue.k2_Load = static_cast<uint8_t>(std::stoi(commandStream[6]));
							kvalue.k5_MTU = static_cast<uint16_t>(std::stoi(commandStream[7]));
						}
					}
					else if (commandStream[0] == "neighbor")
					{
						isList = true;
						ByteString neighborIp = Functions::addressToByte(commandStream[1]);
						InterfaceType type = getInterfaceType(commandStream[2]);
						float interfaceId = std::stof(commandStream[3]);

						currentEigrp->enableUnicastNeighbor(neighborIp, type, interfaceId);
					}
					else if (commandStream[0] == "network")
					{
						isList = true;
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
					else if (commandStream[0] == "no")
					{
						// TODO
					}
					else if (commandStream[0] == "soft-sia")
					{
						currentEigrp->getConfigs()->nonStopForwarding.store(true, std::memory_order_relaxed);
					}
					else if (commandStream[0] == "timers")
					{
						if (commandStream[1] == "graceful-restart")
						{
							currentEigrp->getConfigs()->purgeTime.store(static_cast<uint16_t>(std::stoi(commandStream[3])));
						}
					}
					else if (commandStream[0] == "topology")
					{
						changeMode(mode.addressFamilyTopology);
						configureAddressFamily(currentEigrp->addressFamily);
						isModeChanged = true;
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

#pragma region RoutingInterface

		if (currentMode == mode.addressFamilyInterface)
		{
			if (currentSubMode == "eigrp_named")
			{
				if (commandStream[0] == "authentication")
				{
					if (commandStream[1] == "key-chain")
					{
						std::string keychain = commandStream[2];
						//currentEigrpInterface->getConfigs().authKey.key
						// TODO
					}
					else if (commandStream[1] == "mode")
					{
						if (commandStream[2] == "hmac-sha-256")
						{
							currentEigrpInterface->getConfigs().authKey.authType = EigrpConfigs::AuthType::SHA1;
						}
						else if (commandStream[2] == "md5")
						{
							currentEigrpInterface->getConfigs().authKey.authType = EigrpConfigs::AuthType::MD5;
						}
					}
				}
				else if (commandStream[0] == "bandwidth-percentage")
				{
					currentEigrpInterface->getConfigs().bandwidthPercentage.store(static_cast<uint32_t>(std::stoi(commandStream[1])), std::memory_order_release);
				}
				else if (commandStream[0] == "bfd")
				{
					// XXX
				}
				else if (commandStream[0] == "dampening-change")
				{
					currentEigrpInterface->getConfigs().dampeningChange.store(static_cast<uint8_t>(std::stoi(commandStream[1])), std::memory_order_release);
				}
				else if (commandStream[0] == "dampening-interval")
				{
					currentEigrpInterface->getConfigs().dampeningInterval.store(static_cast<uint16_t>(std::stoi(commandStream[1])), std::memory_order_release);
				}
				else if (commandStream[0] == "default")
				{
					// XXX
				}
				else if (commandStream[0] == "exit-af-interface")
				{
					exitMode(mode.addressFamily);
					configureAddressFamily(AddressFamily::IPv4);
					if (workingDirectory->size() > 0 && (*workingDirectory)[0].contains("unicast"))
					{
						workingDirectory = &(*workingDirectory)[0]["unicast"]; // TODO fix unicast/multicast here
					}
				}
				else if (commandStream[0] == "hello-interval")
				{
					currentEigrpInterface->getConfigs().helloTime.store(static_cast<uint16_t>(std::stoi(commandStream[1])), std::memory_order_release);
				}
				else if (commandStream[0] == "hold-time")
				{
					currentEigrpInterface->getConfigs().holdTime.store(static_cast<uint16_t>(std::stoi(commandStream[1])), std::memory_order_release);
				}
				else if (commandStream[0] == "next-hop-self")
				{
					currentEigrpInterface->getConfigs().nextHopSelf.store(true, std::memory_order_release);
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
					currentEigrpInterface->getConfigs().splitHorizon.store(true, std::memory_order_release);
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
		}


#pragma endregion
		
	}

	if (isList)
	{
		executionHistory.push_back(command);
	}
	if (commandStream[0] == "ip" && (commandStream[1] == "route"))
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
