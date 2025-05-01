#include "CommandProcessor.h"
#include <CliEngine.h>
#include <Eigrp.h>

bool CommandProcessor::handleRoutingConfiguration(const std::vector<std::string>& commandStream)
{
	if (terminal.currentSubMode == "eigrp_classic" || terminal.currentSubMode == "eigrp_classic_vrf" || (terminal.currentSubMode == "eigrp_named" && terminal.modeConfig.currentMode == Mode::routerAddressFamilyTopology))
	{
		if (commandStream[0] == "address-family")
		{
			terminal.isList = true;
			std::string vrf;
			if (commandStream[2] == "vrf")
			{
				vrf = commandStream[3];
			}
			else if (commandStream[3] == "vrf")
			{
				vrf = commandStream[4];
			}
			auto vrfInstance = Global::getInstance().getRoutingInstance(vrf, AddressFamily::IPv4);
			if (!vrfInstance)
			{
				std::cout << "\n%" << "VRF " << vrf << " does not exist or is not enabled for IPv4";
				return false;
			}
			Protocol::EigrpAutonomousSystem* as = currentVrf->getEigrpAutonomousSystem(terminal.routingProtocolID);
			if (!negate)
			{
				currentVrf = vrfInstance;
				if (as)
				{
					if (as->ipv4Named)
					{
						std::cout << "\n%" << " ERROR: AS(" + std::to_string(terminal.routingProtocolID) + ") used by named mode";
						return false; // AS used in named mode.
					}
				}
				else
				{
					as = currentVrf->addEigrpAutonomousSystem(terminal.routingProtocolID);
				}
				if (!as->ipv4)
				{
					as->ipv4 = new Protocol::Eigrp(terminal.routingProtocolID, AddressFamily::IPv4, Global::getInstance().getRoutingInstance("default"));
				}
				currentEigrp = as->ipv4;
				if (terminal.workingDirectory->size() > 0 && (*terminal.workingDirectory)[0].contains("eigrp_classic_vrf"))
				{
					terminal.workingDirectory = &(*terminal.workingDirectory)[0]["eigrp_classic_vrf"];
				}
				terminal.changeMode(Mode::routerAddressFamilyInterface);
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
							currentVrf->removeEigrpAutonomousSystem(terminal.routingProtocolID);
						}
					}
				}
			}
		}
		else if (commandStream[0] == "auto-summary")
		{
			currentEigrp->enableAutoSummary(!negate);
		}
		else if (commandStream[0] == "default-information")
		{
			// XXX
		}
		else if (commandStream[0] == "default-metric")
		{
			if (!negate)
			{
				std::unique_lock<std::shared_mutex> lock(currentEigrp->configs.configsMutex);
				auto& defaultMetric = currentEigrp->configs.defaultMetrics;
				defaultMetric.k1_Bandwidth = static_cast<uint8_t>(std::stoi(commandStream[1]));
				defaultMetric.k3_Delay = static_cast<uint32_t>(std::stoi(commandStream[2]));
				defaultMetric.k4_Reliability = static_cast<uint8_t>(std::stoi(commandStream[3]));
				defaultMetric.k2_Load = static_cast<uint8_t>(std::stoi(commandStream[4]));
				defaultMetric.k5_MTU = static_cast<uint16_t>(std::stoi(commandStream[5]));
			}
			else
			{
				std::unique_lock<std::shared_mutex> lock(currentEigrp->configs.configsMutex);
				auto& defaultMetric = currentEigrp->configs.defaultMetrics;
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
				if (!negate)
				{
					std::unique_lock<std::shared_mutex> lock(currentEigrp->configs.configsMutex);
					currentEigrp->configs.adminDistance = static_cast<uint8_t>(std::stoi(commandStream[2]));
					currentEigrp->configs.externalAdminDistance = static_cast<uint8_t>(std::stoi(commandStream[3]));
				}
				else
				{
					std::unique_lock<std::shared_mutex> lock(currentEigrp->configs.configsMutex);
					currentEigrp->configs.adminDistance = 90;
					currentEigrp->configs.externalAdminDistance = 170;
				}
			}
		}
		else if (commandStream[0] == "eigrp")
		{
			std::unique_lock<std::shared_mutex> lock(currentEigrp->configs.configsMutex);
			if (commandStream[1] == "event-log-size" && Functions::isDecimal(commandStream[2])) { currentEigrp->configs.eventLogSize = static_cast<uint32_t>(std::stoi(commandStream[2])); }
			else if (commandStream[1] == "logNeighborChanges") { currentEigrp->configs.logNeighborChanges = !negate; }
			else if (commandStream[1] == "logNeighborWarnings") { currentEigrp->configs.logNeighborWarnings = !negate; }
			else if (commandStream[1] == "router-id") { if (!negate) currentEigrp->setRouterID(Functions::addressToByte(commandStream[2])); else currentEigrp->clearRouterID(); }
			else if (commandStream[1] == "stub")
			{
				if (!negate)
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
		else if (commandStream[0] == "exit")
		{
			if (terminal.modeConfig.currentMode == Mode::routerAddressFamily)
			{
				terminal.exitMode(Mode::routing);
				currentVrf = Global::getInstance().getRoutingInstance("default");
			}
			else
			{
				terminal.exitMode(Mode::globalConfiguration);
			}
		}
		else if (commandStream[0] == "exit-af-topology")
		{
			terminal.exitMode(Mode::routerAddressFamily);
			terminal.configureAddressFamily(AddressFamily::IPv4);
			if (terminal.workingDirectory->size() > 0 && (*terminal.workingDirectory)[0].contains("unicast"))
			{
				terminal.workingDirectory = &(*terminal.workingDirectory)[0]["unicast"]; // TODO fix unicast/multicast here
			}
		}
		else if (commandStream[0] == "maximum-paths")
		{
			if (!negate)
			{
				currentEigrp->configs.maxPaths.store(static_cast<uint8_t>(std::stoi(commandStream[1])));
			}
			else
			{
				currentEigrp->configs.maxPaths.store(static_cast<uint8_t>(4));
			}
		}
		else if (commandStream[0] == "metric")
		{
			if (commandStream[1] == "maximum-paths" && Functions::isDecimal(commandStream[2]))
			{
				currentEigrp->configs.maxHops.store(static_cast<uint8_t>(
					negate ? 100 : std::stoi(commandStream[2])), 
					std::memory_order_release
				);
			}
			else if (commandStream[1] == "weights")
			{
				EigrpConfigs::KValue kvalue;
				if (!negate)
				{
					currentEigrp->configs.TOS.store(static_cast<uint8_t>(std::stoi(commandStream[2])), std::memory_order_release);
					kvalue.k1_Bandwidth = static_cast<uint8_t>(std::stoi(commandStream[3]));
					kvalue.k3_Delay = static_cast<uint32_t>(std::stoi(commandStream[4]));
					kvalue.k4_Reliability = static_cast<uint8_t>(std::stoi(commandStream[5]));
					kvalue.k2_Load = static_cast<uint8_t>(std::stoi(commandStream[6]));
					kvalue.k5_MTU = static_cast<uint16_t>(std::stoi(commandStream[7]));
				}
				else
				{
					currentEigrp->configs.TOS.store(0, std::memory_order_release);
					kvalue.k1_Bandwidth = 1;
					kvalue.k3_Delay = 0;
					kvalue.k4_Reliability = 1;
					kvalue.k2_Load = 0;
					kvalue.k5_MTU = 0;
				}
				std::unique_lock<std::shared_mutex> lock(currentEigrp->configs.configsMutex);
				currentEigrp->configs.kvalue = kvalue;
			}
		}
		else if (commandStream[0] == "neighbor")
		{
			terminal.isList = true;
			ByteString neighborIp = Functions::addressToByte(commandStream[1]);
			InterfaceType type = terminal.engine.getInterfaceType(commandStream[2]);
			if (type != InterfaceType::UNDEFINED)
			{
				float interfaceId = std::stof(commandStream[3]);

				if (!negate)
				{
					currentEigrp->enableUnicastNeighbor(neighborIp, type, interfaceId);
				}
				else
				{
					currentEigrp->disableUnicastNeighbor(neighborIp, type, interfaceId);
				}
			}
		}
		else if (commandStream[0] == "network")
		{
			terminal.isList = true;
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

			if (!negate)
			{
				currentEigrp->addNetwork(network);
			}
			else
			{
				std::unique_lock<std::shared_mutex> lock(currentEigrp->configs.configsMutex);
				auto& networks = currentEigrp->configs.networks;
				networks.erase(std::remove(networks.begin(), networks.end(), network), networks.end());

				currentEigrp->updateInterfaceList();
				currentEigrp->updateRoutingTableForConnected();
			}
		}
		else if (commandStream[0] == "offset-list")
		{
			// XXX
		}
		else if (commandStream[0] == "passive-interface")
		{
			terminal.isList = true;
			if (!negate)
			{
				currentEigrp->addPassiveInterface(terminal.engine.getInterfaceType(commandStream[1]), std::stof(commandStream[2]));
			}
			else
			{
				currentEigrp->addPassiveInterface(terminal.engine.getInterfaceType(commandStream[1]), std::stof(commandStream[2]), false);
			}
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
				if (!negate)
				{
					if (Functions::isDecimal(commandStream[2]))
					{
						currentEigrp->configs.activeTime.store(static_cast<uint16_t>(std::stoi(commandStream[2])), std::memory_order_release);
						currentEigrp->configs.stuckInActiveTime.store(static_cast<uint16_t>(std::stoi(commandStream[2]) / 2), std::memory_order_release);
						currentEigrp->configs.activeDisabled.store(false, std::memory_order_release);
					}
					else if (commandStream[2] == "disabled")
					{
						currentEigrp->configs.activeDisabled.store(true, std::memory_order_release);
					}
				}
				else
				{
					currentEigrp->configs.activeTime.store(180, std::memory_order_release);
					currentEigrp->configs.stuckInActiveTime.store(90, std::memory_order_relaxed);
					currentEigrp->configs.activeDisabled.store(false, std::memory_order_release);
				}
			}
			else if (commandStream[1] == "graceful-restart")
			{
				negate
				  ? currentEigrp->configs.purgeTime.store(240, std::memory_order_release)
				  : currentEigrp->configs.purgeTime.store(static_cast<uint16_t>(std::stoi(commandStream[3])));
			}
		}
		else if (commandStream[0] == "traffic-share")
		{
			if (!negate)
			{
				if (commandStream[1] == "balenced")
				{
					currentEigrp->configs.trafficShareMode.store(EigrpConfigs::TrafficShareMode::Balenced, std::memory_order_release);
				}
				else if (commandStream[1] == "min")
				{
					currentEigrp->configs.trafficShareMode.store(EigrpConfigs::TrafficShareMode::Minimum, std::memory_order_release);
				}
			}
			else
			{
				currentEigrp->configs.trafficShareMode.store(EigrpConfigs::TrafficShareMode::Balenced, std::memory_order_release);
			}
		}
		else if (commandStream[0] == "variance" && Functions::isDecimal(commandStream[1]))
		{
			negate
			  ? currentEigrp->setVariance(1)
			  : currentEigrp->setVariance(static_cast<uint8_t>(std::stoi(commandStream[1])));
		}
	}
	if (terminal.currentSubMode == "eigrp_named")
	{
		if (terminal.modeConfig.currentMode == Mode::routing)
		{
			if (commandStream[0] == "address-family")
			{
				terminal.isList = true;
				terminal.isModeChanged = true;
				AddressFamily af;
				std::string comString = "unicast";
				commandStream[1] == "ipv4"
				  ? af = AddressFamily::IPv4
				  : af = AddressFamily::IPv6;
				VirtualRouter* vrf = currentVrf;

				if (commandStream[2] == "vrf" || commandStream[3] == "vrf")
				{
					std::string vrfName;
					bool multicast = false;
					if (commandStream[2] == "vrf")
					{
						vrfName = commandStream[3];
						terminal.routingProtocolID = std::stoi(commandStream[5]);
					}
					else if (commandStream[3] == "vrf")
					{
						vrfName = commandStream[4];
						terminal.routingProtocolID = std::stoi(commandStream[6]);
						if (commandStream[2] == "multicast")
						{
							multicast = true;
						}
					}

					vrf = global.getRoutingInstance(vrfName, af);
					if (!vrf) 
					{
						std::cout << "\n%" << "VRF " << commandStream[3] << " does not exist or is not enabled for IPv4";
					}

					if (/*multicast enabled*/false)
					{
						comString = "multicast";
					}
					else
					{
						std::cout << "\n%" << "ERROR multicast not enabled";
						return false;
					}
				}
				else if (commandStream[2] == "unicast")
				{
					terminal.routingProtocolID = Functions::stringToNum(commandStream[4]);
				}
				else if (commandStream[2] == "multicast")
				{
					if (/*multicast enabled*/false)
					{
						terminal.routingProtocolID = Functions::stringToNum(commandStream[4]);
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
					terminal.routingProtocolID = Functions::stringToNum(commandStream[3]);
				}

				if (commandStream[2] == "autonomous-system" || commandStream[3] == "autonomous-system" || commandStream[4] == "autonomous-system" || commandStream[5] == "autonomous-system")
				{
					if (af == AddressFamily::IPv4 && currentEigrpNamed->ipv4)
					{
						if (currentEigrpNamed->ipv4->asNumber != terminal.routingProtocolID)
						{
							std::cout << "\nChanging from AS(" << currentEigrpNamed->ipv4->asNumber << ") to AS(" << terminal.routingProtocolID << ") is not allowed";
							return false;
						}
						currentEigrp = currentEigrpNamed->ipv4;
						terminal.changeMode(Mode::routerAddressFamily);
						terminal.configureAddressFamily(AddressFamily::IPv4);
					}
					else if (af == AddressFamily::IPv6 && currentEigrpNamed->ipv6)
					{
						if (currentEigrpNamed->ipv6->asNumber != terminal.routingProtocolID)
						{
							std::cout << "\nChanging from AS(" << currentEigrpNamed->ipv4->asNumber << ") to AS(" << terminal.routingProtocolID << ") is not allowed";
							return false;
						}
						currentEigrp = currentEigrpNamed->ipv6;
						terminal.changeMode(Mode::routerAddressFamily);
						terminal.configureAddressFamily(AddressFamily::IPv6);
					}
					{
						Protocol::EigrpAutonomousSystem* eigrpAs = currentVrf->getEigrpAutonomousSystem(terminal.routingProtocolID);
						if (!eigrpAs && !negate)
						{
							eigrpAs = currentVrf->addEigrpAutonomousSystem(terminal.routingProtocolID);
						}
						else if (negate) return true; // Already removed
						
						if (af == AddressFamily::IPv4)
						{
							if (negate)
							{
								if (eigrpAs->ipv4)
								{
									delete eigrpAs->ipv4;
									if (!eigrpAs->ipv6)
									{
										currentVrf->removeEigrpAutonomousSystem(terminal.routingProtocolID);
									}
								}
							}
							else if (!eigrpAs->ipv4)
							{
								eigrpAs->ipv4 = new Protocol::Eigrp(terminal.routingProtocolID, af, Global::getInstance().getRoutingInstance("default"));
								eigrpAs->ipv4Named = true;
								currentEigrpNamed->ipv4 = eigrpAs->ipv4;
								currentEigrp = eigrpAs->ipv4;
								terminal.changeMode(Mode::routerAddressFamily);
								terminal.configureAddressFamily(AddressFamily::IPv4);
							}
							else
							{
								std::cout << "\n%" << " ERROR: AS(" << terminal.routingProtocolID << ") in use by classic router";
								return false;
							}
						}
						else if (af == AddressFamily::IPv6)
						{
							if (negate)
							{
								if (eigrpAs->ipv6)
								{
									delete eigrpAs->ipv6;
									if (!eigrpAs->ipv4)
									{
										currentVrf->removeEigrpAutonomousSystem(terminal.routingProtocolID);
									}
								}
							}
							if (!eigrpAs->ipv6)
							{
								eigrpAs->ipv6 = new Protocol::Eigrp(terminal.routingProtocolID, af, Global::getInstance().getRoutingInstance("default"));
								eigrpAs->ipv6Named = true;
								currentEigrpNamed->ipv6 = eigrpAs->ipv6;
								currentEigrp = eigrpAs->ipv6;
								terminal.changeMode(Mode::routerAddressFamily);
								terminal.configureAddressFamily(AddressFamily::IPv6);
							}
							else if (currentEigrpNamed && currentEigrpNamed->ipv6)
							{
								std::cout << "\nChanging from AS(" << eigrpAs->ipv4->asNumber << ") to AS(" << terminal.routingProtocolID << ") is not allowed";
							}
							else
							{
								std::cout << "\n%" << " ERROR: AS(" << terminal.routingProtocolID << ") in use by classic router";
								return false;
							}
						}
					}
					if (negate && terminal.workingDirectory->size() > 0 && (*terminal.workingDirectory)[0].contains(comString))
					{
						terminal.workingDirectory = &(*terminal.workingDirectory)[0][comString];
					}
				}
			}
			else if (commandStream[0] == "default")
			{
				//TODO
			}
			else if (commandStream[0] == "exit")
			{
				terminal.changeMode(Mode::globalConfiguration);
			}
			else if (commandStream[0] == "service-family")
			{
				terminal.isList = true;
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
		else if (terminal.modeConfig.currentMode == Mode::routerAddressFamily)
		{
			if (commandStream[0] == "af-interface")
			{
				terminal.isList = true;

				InterfaceType type = terminal.engine.getInterfaceType(commandStream[1]);
				float interfaceId = std::stof(commandStream[2]);
				auto intIt = currentEigrp->eigrpInterfaceList.find({type, interfaceId});

				if (intIt != currentEigrp->eigrpInterfaceList.end())
				{
					currentEigrpInterface = intIt->second;
				}
				else
				{
					currentEigrpInterface = currentEigrp->addEigrpInterface(currentVrf->getInterface(type, interfaceId));
				}

				if (!negate)
				{
					terminal.changeMode(Mode::routerAddressFamilyInterface);
					terminal.configureAddressFamily(currentEigrp->addressFamily);
				}
				else
				{
					auto& eigrpInt = *currentEigrpInterface;
					// Reset all of the settings on the interface
					eigrpInt.configureAuthentication();
					eigrpInt.configs.bandwidthPercentage.store(50, std::memory_order_release);
					//TODO disable bfd if enabled
					eigrpInt.configs.dampeningChange.store(0, std::memory_order_release);
					eigrpInt.configs.dampeningInterval.store(5, std::memory_order_release);
					eigrpInt.configs.helloTime.store(5, std::memory_order_release);
					eigrpInt.configs.holdTime.store(15, std::memory_order_relaxed);
					eigrpInt.configs.nextHopSelf.store(false, std::memory_order_release);
					eigrpInt.setPassive(false);
					eigrpInt.configs.splitHorizon.store(true, std::memory_order_relaxed);
					auto summaries = eigrpInt.configs.summaryRoutes;
					for (const auto& route : summaries)
					{
						eigrpInt.removeSummaryRoute(route.summary->network, route.summary->mask);
					}

					{
						std::unique_lock<std::shared_mutex> lock(eigrpInt.neighborMutex);
						for (const auto& [ip, neighbor] : eigrpInt.neighbors)
						{
							eigrpInt.handleNeighborRestart(neighbor, ip);
						}
					}
				}
			}
			else if (commandStream[0] == "default")
			{
				// TODO
			}
			else if (commandStream[0] == "eigrp")
			{
				std::unique_lock<std::shared_mutex> lock(currentEigrp->configs.configsMutex);

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
				else if (commandStream[1] == "event-log-size" && Functions::isDecimal(commandStream[2])) { currentEigrp->configs.eventLogSize = static_cast<uint32_t>(std::stoi(commandStream[2])); }
				else if (commandStream[1] == "logNeighborChanges") { currentEigrp->configs.logNeighborChanges = true; }
				else if (commandStream[1] == "logNeighborWarnings") { currentEigrp->configs.logNeighborWarnings = true; }
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
			else if (commandStream[0] == "exit-address-family" && terminal.modeConfig.currentMode == Mode::routerAddressFamily)
			{
				if (currentEigrp->addressFamily == AddressFamily::IPv4)
				{
					terminal.configureRoutingMode("eigrp_named", false);
				}
				else if (currentEigrp->addressFamily == AddressFamily::IPv6)
				{
					terminal.configureRoutingMode("eigrp_named", false);
				}
				terminal.isExitCommand = true;

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
					std::unique_lock<std::shared_mutex> lock(currentEigrp->configs.configsMutex);
					currentEigrp->configs.TOS = static_cast<uint8_t>(std::stoi(commandStream[2]));
					auto& kvalue = currentEigrp->configs.kvalue;
					kvalue.k1_Bandwidth = static_cast<uint8_t>(std::stoi(commandStream[3]));
					kvalue.k3_Delay = static_cast<uint32_t>(std::stoi(commandStream[4]));
					kvalue.k4_Reliability = static_cast<uint8_t>(std::stoi(commandStream[5]));
					kvalue.k2_Load = static_cast<uint8_t>(std::stoi(commandStream[6]));
					kvalue.k5_MTU = static_cast<uint16_t>(std::stoi(commandStream[7]));
				}
			}
			else if (commandStream[0] == "neighbor")
			{
				terminal.isList = true;
				ByteString neighborIp = Functions::addressToByte(commandStream[1]);
				InterfaceType type = terminal.engine.getInterfaceType(commandStream[2]);
				float interfaceId = std::stof(commandStream[3]);

				currentEigrp->enableUnicastNeighbor(neighborIp, type, interfaceId);
			}
			else if (commandStream[0] == "network")
			{
				terminal.isList = true;
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
				currentEigrp->configs.nonStopForwarding.store(true, std::memory_order_relaxed);
			}
			else if (commandStream[0] == "timers")
			{
				if (commandStream[1] == "graceful-restart")
				{
					currentEigrp->configs.purgeTime.store(static_cast<uint16_t>(std::stoi(commandStream[3])));
				}
			}
			else if (commandStream[0] == "topology")
			{
				terminal.changeMode(Mode::routerAddressFamilyTopology);
				terminal.configureAddressFamily(currentEigrp->addressFamily);
				terminal.isModeChanged = true;
			}
		}
	}
	else if (terminal.currentSubMode == "ospf")
	{
	}
	else if (terminal.currentSubMode == "bgp")
	{
	}
	else if (terminal.currentSubMode == "rip")
	{
	}
	else return false;
	return true;
}



