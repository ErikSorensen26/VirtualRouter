#include "CommandProcessor.h"
#include <VirtualRouter.h>
#include <CliEngine.h>
#include <EigrpInterface.h>
#include <algorithm>
#include <InterfaceConfigs.h>
#include <InterfaceType.hpp>

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
			auto vrfInstance = global.getRoutingInstance(vrf, AddressFamily::IPv4);
			if (!vrfInstance)
			{
				terminal.iConsole->print(std::string("\r\n%") + vrf + " does not exist or is not enabled for IPv4");
				return false;
			}
			Eigrp::EigrpAutonomousSystem* as = currentVrf->getEigrpAutonomousSystem(terminal.routingProtocolID);
			if (!negate)
			{
				currentVrf = vrfInstance;
				if (as)
				{
					if (as->ipv4Named)
					{
						terminal.iConsole->print(std::string("\r\n%") + " ERROR: AS(" + std::to_string(terminal.routingProtocolID) + ") used by name mode");
						return false; // AS used in named mode.
					}
				}
				else
				{
					as = currentVrf->addEigrpAutonomousSystem(terminal.routingProtocolID);
				}
				if (!as->ipv4)
				{
					as->ipv4 = new Eigrp::Eigrp(terminal.routingProtocolID, AddressFamily::IPv4, global.getRoutingInstance("default"));
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
			currentEigrp->getAggregator().enableAutoSummary(!negate);
		}
		else if (commandStream[0] == "default-information")
		{
			// XXX
		}
		else if (commandStream[0] == "default-metric")
		{
			auto& configs = currentEigrp->getConfigs();
			if (!negate)
			{
				std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
				auto& defaultMetric = configs.defaultMetrics;
				defaultMetric.k1_Bandwidth = static_cast<uint8_t>(std::stoi(commandStream[1]));
				defaultMetric.k3_Delay = static_cast<uint8_t>(std::stoi(commandStream[2]));
				defaultMetric.k4_Reliability = static_cast<uint8_t>(std::stoi(commandStream[3]));
				defaultMetric.k2_Load = static_cast<uint8_t>(std::stoi(commandStream[4]));
				defaultMetric.k5_MTU = static_cast<uint8_t>(std::stoi(commandStream[5]));
			}
			else
			{
				std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
				auto& defaultMetric = configs.defaultMetrics;
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
			if (Functions::isNumber(commandStream[1]))
			{
				// XXX
			}
			else if (commandStream[1] == "eigrp")
			{
				auto& configs = currentEigrp->getConfigs();
				if (!negate)
				{
					configs.adminDistance.store(static_cast<uint8_t>(std::stoi(commandStream[2])), std::memory_order_release);
					configs.externalAdminDistance.store(static_cast<uint8_t>(std::stoi(commandStream[3])), std::memory_order_release);
				}
				else
				{
					configs.adminDistance.store(90, std::memory_order_release);
					configs.externalAdminDistance.store(170, std::memory_order_release);
				}
			}
		}
		else if (commandStream[0] == "eigrp")
		{
			auto& configs = currentEigrp->getConfigs();
			if (commandStream[1] == "event-log-size" && Functions::isNumber(commandStream[2])) { configs.eventLogSize.store(static_cast<uint32_t>(std::stoi(commandStream[2])), std::memory_order_release); }
			else if (commandStream[1] == "logNeighborChanges") { configs.logNeighborChanges.store(!negate, std::memory_order_release); }
			else if (commandStream[1] == "logNeighborWarnings") { configs.logNeighborWarnings.store(!negate, std::memory_order_release); }
			else if (commandStream[1] == "router-id") { if (!negate) currentEigrp->setRouterId(readU32(Functions::getAddress(commandStream[2]).raw)); else currentEigrp->clearRouterID(); }
			else if (commandStream[1] == "stub")
			{
				std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
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
					currentEigrp->getGlobalConfigMgr().enableStub(true, connected, stat, leakMap, summary, redistributed);
				}
				else
				{
					currentEigrp->getGlobalConfigMgr().enableStub(false);
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
				currentVrf = global.getRoutingInstance("default");
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
		}
		else if (commandStream[0] == "maximum-paths")
		{
			auto& configs = currentEigrp->getConfigs();
			if (!negate)
			{
				configs.maxPaths.store(static_cast<uint8_t>(std::stoi(commandStream[1])));
			}
			else
			{
				configs.maxPaths.store(static_cast<uint8_t>(4));
			}
		}
		else if (commandStream[0] == "metric")
		{
			auto& configs = currentEigrp->getConfigs();
			if (commandStream[1] == "maximum-paths" && Functions::isNumber(commandStream[2]))
			{
				configs.maxHops.store(static_cast<uint8_t>(
					negate ? 100 : std::stoi(commandStream[2])), 
					std::memory_order_release
				);
			}
			else if (commandStream[1] == "weights")
			{
				EigrpConfigs::KValue kvalue;
				if (!negate)
				{
					configs.TOS.store(static_cast<uint8_t>(std::stoi(commandStream[2])), std::memory_order_release);
					kvalue.k1_Bandwidth = static_cast<uint8_t>(std::stoi(commandStream[3]));
					kvalue.k3_Delay = static_cast<uint8_t>(std::stoi(commandStream[4]));
					kvalue.k4_Reliability = static_cast<uint8_t>(std::stoi(commandStream[5]));
					kvalue.k2_Load = static_cast<uint8_t>(std::stoi(commandStream[6]));
					kvalue.k5_MTU = static_cast<uint8_t>(std::stoi(commandStream[7]));
				}
				else
				{
					configs.TOS.store(0, std::memory_order_release);
					kvalue.k1_Bandwidth = 1;
					kvalue.k3_Delay = 0;
					kvalue.k4_Reliability = 1;
					kvalue.k2_Load = 0;
					kvalue.k5_MTU = 0;
				}
				std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
				configs.kvalue = kvalue;
			}
		}
		else if (commandStream[0] == "neighbor")
		{
			terminal.isList = true;
			IPAddress neighborIp = Functions::getAddress(commandStream[1]);
			InterfaceType type = getInterfaceType(commandStream[2]);
			if (type != InterfaceType::UNDEFINED)
			{
				uint32_t key = calculateInterfaceKey(type, std::stof(commandStream[3]));
				Eigrp::EigrpInterface* iface = currentEigrp->getIfaceMgr().getInterface(key);
				if (!iface) return false;

				if (!negate)
					iface->getNTable().createNeighbor(neighborIp, Eigrp::Neighbor::Version::UNKNOWN, true);
				else
					iface->getNTable().deleteNeighbor(neighborIp, true);
			}
		}
		else if (commandStream[0] == "network")
		{
			terminal.isList = true;
			EigrpConfigs::Network network(AddressFamily::IPv4);
			network.ip = Functions::getAddress(commandStream[1]);
			if (commandStream.size() == 3)
				network.mask = 32 - Functions::prefixToPrefixLength(Functions::addressToIntv4(commandStream[2]));
			else
				network.mask = 32 - Functions::getDefaultMask(readU32(network.ip.raw));

			if (!negate)
			{
				currentEigrp->getGlobalConfigMgr().addNetworkRange(network);
			}
			else
			{
				{
					auto& configs = currentEigrp->getConfigs();
					std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
					auto& networks = configs.networks;
					networks.erase(std::remove(networks.begin(), networks.end(), network), networks.end());
				}

				currentEigrp->refreshInterfaceList();
			}
		}
		else if (commandStream[0] == "offset-list")
		{
			// XXX
		}
		else if (commandStream[0] == "passive-interface")
		{
			terminal.isList = true;
			uint32_t key = calculateInterfaceKey(getInterfaceType(commandStream[1]), std::stof(commandStream[2]));
			auto* iface = currentEigrp->getIfaceMgr().getInterface(key);

			if (iface)
			{
				iface->setPassiveMode(!negate);
			}
			else
			{
				currentEigrp->getConfigs().passiveInterfaces.insert(key);
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
			auto& configs = currentEigrp->getConfigs();
			if (commandStream[1] == "active-time")
			{
				if (!negate)
				{
					if (Functions::isNumber(commandStream[2]))
					{
						configs.stuckInActiveTime.store(static_cast<uint16_t>(std::stoi(commandStream[2]) / 2), std::memory_order_release);
						configs.activeDisabled.store(false, std::memory_order_release);
					}
					else if (commandStream[2] == "disabled")
					{
						configs.activeDisabled.store(true, std::memory_order_release);
					}
				}
				else
				{
					configs.stuckInActiveTime.store(90, std::memory_order_relaxed);
					configs.activeDisabled.store(false, std::memory_order_release);
				}
			}
			else if (commandStream[1] == "graceful-restart")
			{
				negate
				  ? configs.purgeTime.store(240, std::memory_order_release)
				  : configs.purgeTime.store(static_cast<uint16_t>(std::stoi(commandStream[3])));
			}
		}
		else if (commandStream[0] == "traffic-share")
		{
			auto& configs = currentEigrp->getConfigs();
			if (!negate)
			{
				if (commandStream[1] == "balanced")
				{
					configs.trafficShareMode.store(EigrpConfigs::TrafficShareMode::Balanced, std::memory_order_release);
				}
				else if (commandStream[1] == "min")
				{
					configs.trafficShareMode.store(EigrpConfigs::TrafficShareMode::Minimum, std::memory_order_release);
				}
			}
			else
			{
				configs.trafficShareMode.store(EigrpConfigs::TrafficShareMode::Balanced, std::memory_order_release);
			}
		}
		else if (commandStream[0] == "variance" && Functions::isNumber(commandStream[1]))
		{
			negate
			  ? currentEigrp->getGlobalConfigMgr().setVariance(1)
			  : currentEigrp->getGlobalConfigMgr().setVariance(static_cast<uint8_t>(std::stoul(commandStream[1])));
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
						terminal.routingProtocolID = static_cast<uint32_t>(std::stoul(commandStream[5]));
					}
					else if (commandStream[3] == "vrf")
					{
						vrfName = commandStream[4];
						terminal.routingProtocolID = static_cast<uint32_t>(std::stoul(commandStream[6]));
						if (commandStream[2] == "multicast")
						{
							multicast = true;
						}
					}

					vrf = global.getRoutingInstance(vrfName, af);
					if (!vrf) 
					{
						terminal.iConsole->print(std::string("\r\n%") + commandStream[3] + " does not exist or is not enabled for IPv4");
					}

					if (/*multicast enabled*/false)
					{
						comString = "multicast";
					}
					else
					{
						terminal.iConsole->print(std::string("\r\n%") + "ERROR multicast not enabled");
						return false;
					}
				}
				else if (commandStream[2] == "unicast")
				{
					terminal.routingProtocolID = static_cast<uint32_t>(std::stoul(commandStream[4]));
				}
				else if (commandStream[2] == "multicast")
				{
					if (/*multicast enabled*/false)
					{
						terminal.routingProtocolID = static_cast<uint32_t>(std::stoul(commandStream[4]));
						comString = "multicast";
					}
					else
					{
						terminal.iConsole->print(std::string("\r\n%") + "ERROR multicast not enabled");
						return false;
					}
				}
				else
				{
					terminal.routingProtocolID = std::stoi(commandStream[3]);
				}

				if (commandStream[2] == "autonomous-system" || commandStream[3] == "autonomous-system" || commandStream[4] == "autonomous-system" || commandStream[5] == "autonomous-system")
				{
					if (af == AddressFamily::IPv4 && currentEigrpNamed->ipv4)
					{
						if (currentEigrpNamed->ipv4->getAS() != terminal.routingProtocolID && !negate)
						{
							terminal.iConsole->print("\r\nChanging from AS(" + std::to_string(currentEigrpNamed->ipv4->getAS()) + ") to AS(" + std::to_string(terminal.routingProtocolID) + ") is not allowed");
							return false;
						}
						currentEigrp = currentEigrpNamed->ipv4;
					}
					else if (af == AddressFamily::IPv6 && currentEigrpNamed->ipv6)
					{
						if (currentEigrpNamed->ipv6->getAS() != terminal.routingProtocolID && !negate)
						{
							terminal.iConsole->print("\r\nChanging from AS(" + std::to_string(currentEigrpNamed->ipv6->getAS()) + ") to AS(" + std::to_string(terminal.routingProtocolID) + ") is not allowed");
							return false;
						}
						currentEigrp = currentEigrpNamed->ipv6;
					}
					{
						Eigrp::EigrpAutonomousSystem* eigrpAs = currentVrf->getEigrpAutonomousSystem(terminal.routingProtocolID);
						if (!eigrpAs && !negate)
						{
							eigrpAs = currentVrf->addEigrpAutonomousSystem(terminal.routingProtocolID);
						}
						else if (!eigrpAs && negate) return true; // Already removed
						
						if (af == AddressFamily::IPv4)
						{
							if (negate)
							{
								if (eigrpAs->ipv4)
								{
									delete eigrpAs->ipv4;
									eigrpAs->ipv4 = nullptr;
									eigrpAs->ipv4Named = false;
									currentEigrpNamed->ipv4 = nullptr;
									if (!eigrpAs->ipv6)
									{
										currentVrf->removeEigrpAutonomousSystem(terminal.routingProtocolID);
									}
								}
							}
							else if (!eigrpAs->ipv4)
							{
								eigrpAs->ipv4 = new Eigrp::Eigrp(terminal.routingProtocolID, af, global.getRoutingInstance("default"), true);
								eigrpAs->ipv4Named = true;
								currentEigrpNamed->ipv4 = eigrpAs->ipv4;
								currentEigrp = eigrpAs->ipv4;
								terminal.changeMode(Mode::routerAddressFamily);
								terminal.configureAddressFamily(AddressFamily::IPv4);
							}
							else if (currentEigrpNamed && currentEigrpNamed->ipv4)
							{
								currentEigrp = currentEigrpNamed->ipv4;
								terminal.changeMode(Mode::routerAddressFamily);
								terminal.configureAddressFamily(AddressFamily::IPv4);
							}
							else
							{
								terminal.iConsole->print(std::string("\r\n%") + " ERROR: AS(" + std::to_string(terminal.routingProtocolID) + ") in use by classic router");
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
									eigrpAs->ipv6 = nullptr;
									eigrpAs->ipv6Named = false;
									currentEigrpNamed->ipv6 = nullptr;
									if (!eigrpAs->ipv4)
									{
										currentVrf->removeEigrpAutonomousSystem(terminal.routingProtocolID);
									}
								}
							}
							else if (!eigrpAs->ipv6)
							{
								eigrpAs->ipv6 = new Eigrp::Eigrp(terminal.routingProtocolID, af, global.getRoutingInstance("default"), true);
								eigrpAs->ipv6Named = true;
								currentEigrpNamed->ipv6 = eigrpAs->ipv6;
								currentEigrp = eigrpAs->ipv6;
								terminal.changeMode(Mode::routerAddressFamily);
								terminal.configureAddressFamily(AddressFamily::IPv6);
							}
							else if (currentEigrpNamed && currentEigrpNamed->ipv6)
							{
								currentEigrp = currentEigrpNamed->ipv6;
								terminal.changeMode(Mode::routerAddressFamily);
								terminal.configureAddressFamily(AddressFamily::IPv6);
							}
							else
							{
								terminal.iConsole->print(std::string("\r\n%") + " ERROR: AS(" + std::to_string(terminal.routingProtocolID) + ") in use by classic router");
								return false;
							}
						}
					}
				}
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
				currentVrf = global.getRoutingInstance(vrf);
				// TODO add service eigrp
			}
		}
		else if (terminal.modeConfig.currentMode == Mode::routerAddressFamily)
		{
			if (commandStream[0] == "af-interface")
			{
				terminal.isList = true;

				InterfaceType type = getInterfaceType(commandStream[1]);
				float interfaceId = std::stof(commandStream[2]);
				uint32_t key = calculateInterfaceKey(type, interfaceId);
				
				{
					auto& ifaceMgr = currentEigrp->getIfaceMgr();
					std::unique_lock<std::shared_mutex> lock(ifaceMgr.interfaceMutex);
					auto intIt = ifaceMgr.eigrpInterfaceConfigList.find(key);

					if (!negate)
					{
						if (intIt != ifaceMgr.eigrpInterfaceConfigList.end())
						{
							currentEigrpInterface = intIt->second;
							currentEigrpInterface->userMade = true;
						}
						else
						{
							EigrpConfigs::InterfaceConfigs* newConfigs = new EigrpConfigs::InterfaceConfigs(key);
							newConfigs->userMade = true;
							ifaceMgr.eigrpInterfaceConfigList[key] = newConfigs;
						}
						terminal.changeMode(Mode::routerAddressFamilyInterface);
						terminal.configureAddressFamily(currentEigrp->getAF());
					}
					else
					{
						if (intIt != ifaceMgr.eigrpInterfaceConfigList.end())
						{
							currentEigrpInterface = intIt->second;
							if (currentEigrpInterface->userMade)
							{
								delete ifaceMgr.eigrpInterfaceConfigList[key];
								ifaceMgr.eigrpInterfaceConfigList.erase(key);
							}
							else return true;
						}
						else
						{
							return true;
						}
					}
				}
				currentEigrp->refreshInterfaceList();
			}
			else if (commandStream[0] == "eigrp")
			{
				auto& configs = currentEigrp->getConfigs();
				std::unique_lock<std::shared_mutex> lock(configs.configsMutex);

				if (commandStream[1] == "default-route-tag") 
				{
					uint32_t routeTag;
					if (!negate)
					{
						if (Functions::isNumber(commandStream[2]))
						{
							routeTag = static_cast<uint32_t>(std::stoi(commandStream[2]));
						}
						else
						{
							routeTag = Functions::addressToIntv4(commandStream[2]);
						}
					}
					else
					{
						
					}
					// TODO set the route tag
				}
				else if (commandStream[1] == "event-log-size" && Functions::isNumber(commandStream[2])) 
				{
					if (!negate)
					{
						if (Functions::isNumber(commandStream[2]))
							configs.eventLogSize.store(static_cast<uint32_t>(std::stoi(commandStream[2])), std::memory_order_release);
					}
					else
					{
						configs.eventLogSize.store(500, std::memory_order_relaxed);
					}
				}
				else if (commandStream[1] == "logNeighborChanges") 
				{
					configs.logNeighborChanges = !negate;
				}
				else if (commandStream[1] == "logNeighborWarnings")
				{
					configs.logNeighborWarnings = !negate;
				}
				else if (commandStream[1] == "router-id")
				{
					if (!negate)
					{
						currentEigrp->setRouterID(Functions::getAddress(commandStream[2]).raw);
					}
					else
					{
						currentEigrp->clearRouterID();
					}
				}
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
						currentEigrp->getGlobalConfigMgr().enableStub(true, connected, stat, leakMap, summary, redistributed);
					}
					else
					{
						currentEigrp->getGlobalConfigMgr().enableStub(false);
					}

				}
				else if (commandStream[1] == "stub-site")
				{
					// TODO
				}
			}
			else if (commandStream[0] == "exit-address-family" && terminal.modeConfig.currentMode == Mode::routerAddressFamily)
			{
				if (currentEigrp->getAF() == AddressFamily::IPv4)
				{
					terminal.configureRoutingMode("eigrp_named", false);
				}
				else if (currentEigrp->getAF() == AddressFamily::IPv6)
				{
					terminal.configureRoutingMode("eigrp_named", false);
				}
				terminal.isExitCommand = true;

				currentVrf = global.getRoutingInstance("default");
			}
			else if (commandStream[1] == "maximum-prefix")
			{
				auto& configs = currentEigrp->getConfigs();
				if (!negate)
				{
					configs.maximumPrefix.store(static_cast<uint32_t>(std::stoi(commandStream[1])), std::memory_order_release);
					if (commandStream.size() > 2)
					{
						for (size_t i = 2; i < commandStream.size(); i++)
						{
							if (Functions::isNumber(commandStream[i]))
							{
								configs.dampeningInterval.store(static_cast<uint8_t>(std::stoul(commandStream[i])));
							}
							else if (commandStream[i] == "dampened")
							{
								configs.dampening.store(true, std::memory_order_release);
							}
							else if (commandStream[i] == "reset-time")
							{
								configs.dampeningResetTime.store(static_cast<uint8_t>(std::stoul(commandStream[i + 1]), std::memory_order_release));
								i++;
							}
							else if (commandStream[i] == "restart")
							{
								configs.dampeningRestart.store(static_cast<uint8_t>(std::stoul(commandStream[i + 1]), std::memory_order_release));
								i++;
							}
							else if (commandStream[i] == "restart-count")
							{
								configs.dampeningRestartCount.store(static_cast<uint8_t>(std::stoul(commandStream[i + 1]), std::memory_order_release));
								i++;
							}
							else if (commandStream[i] == "warning-only")
							{
								configs.dampeningWarnings.store(true, std::memory_order_release);
							}
						}
					}
				}
				else
				{
					configs.maximumPrefix.store(0, std::memory_order_relaxed);
					configs.dampeningInterval.store(75, std::memory_order_release);
					configs.dampening.store(false, std::memory_order_release);
					configs.dampeningResetTime.store(0, std::memory_order_release);
					configs.dampeningRestart.store(0, std::memory_order_release);
					configs.dampeningRestartCount.store(1, std::memory_order_release);
					configs.dampeningWarnings.store(false, std::memory_order_release);
				}
			}
			else if (commandStream[0] == "metric")
			{
				if (commandStream[1] == "rib-scale")
				{
					if (!negate)
					{
						if (Functions::isNumber(commandStream[2]))
							currentEigrp->getConfigs().ribScale.store(std::stoi(commandStream[2]), std::memory_order_release);
					}
					else
					{
						currentEigrp->getConfigs().ribScale.store(128, std::memory_order_release);
					}
				}
				else if (commandStream[1] == "weights")
				{
					auto& configs = currentEigrp->getConfigs();
					EigrpConfigs::KValue kvalue;
					if (!negate)
					{
						configs.TOS.store(static_cast<uint8_t>(std::stoi(commandStream[2])), std::memory_order_release);
						kvalue.k1_Bandwidth = static_cast<uint8_t>(std::stoi(commandStream[3]));
						kvalue.k3_Delay = static_cast<uint8_t>(std::stoi(commandStream[4]));
						kvalue.k4_Reliability = static_cast<uint8_t>(std::stoi(commandStream[5]));
						kvalue.k2_Load = static_cast<uint8_t>(std::stoi(commandStream[6]));
						kvalue.k5_MTU = static_cast<uint8_t>(std::stoi(commandStream[7]));
					}
					else
					{
						configs.TOS.store(0, std::memory_order_release);
						kvalue.k1_Bandwidth = 1;
						kvalue.k3_Delay = 0;
						kvalue.k4_Reliability = 1;
						kvalue.k2_Load = 0;
						kvalue.k5_MTU = 0;
					}
					std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
					configs.kvalue = kvalue;
				}
			}
			else if (commandStream[0] == "neighbor")
			{
				terminal.isList = true;
				IPAddress neighborIp = Functions::getAddress(commandStream[1]);
				InterfaceType type = getInterfaceType(commandStream[2]);
				float interfaceId = std::stof(commandStream[3]);
				uint32_t key = calculateInterfaceKey(type, interfaceId);
				auto* iface = currentEigrp->getIfaceMgr().getInterface(key);

				if (iface)
				{
					if (!negate)
						iface->getNTable().createNeighbor(neighborIp, Eigrp::Neighbor::Version::UNKNOWN, true);
					else
						iface->getNTable().deleteNeighbor(neighborIp, true);
				}
				else
				{
					auto& configs = currentEigrp->getConfigs();
					std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
					if (negate)
						configs.unicastNeighbors[key].erase(neighborIp);
					else
						configs.unicastNeighbors[key].insert(neighborIp);
				}
			}
			else if (commandStream[0] == "network")
			{
				terminal.isList = true;
				EigrpConfigs::Network network(AddressFamily::IPv4);
				network.ip = Functions::getAddress(commandStream[1]);
				if (commandStream.size() == 3)
				{
					network.mask = Functions::prefixToPrefixLength(Functions::addressToIntv4(commandStream[2]));
				}
				else
				{
					network.mask = Functions::getDefaultMask(readU32(network.ip.raw));
				}

				if (!negate)
				{
					currentEigrp->getGlobalConfigMgr().addNetworkRange(network);
				}
				else
				{
					auto& configs = currentEigrp->getConfigs();
					if (commandStream.size() == 2)
					{
						std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
						auto& networks = configs.networks;
						std::erase_if(networks, [&](const EigrpConfigs::Network& net) -> bool { return net.ip == network.ip; });
					}
					else
					{
						std::unique_lock<std::shared_mutex> lock(configs.configsMutex);
						auto& networks = configs.networks;
						std::erase_if(networks, [&](EigrpConfigs::Network net) {
							return net.ip == network.ip && (commandStream.size() > 2 ? net.mask == network.mask : true);
						});
					}

					currentEigrp->refreshInterfaceList();
				}
			}
			else if (commandStream[0] == "soft-sia")
			{
				currentEigrp->getConfigs().nonStopForwarding.store(!negate, std::memory_order_release);
			}
			else if (commandStream[0] == "timers")
			{
				if (commandStream[1] == "graceful-restart")
				{
					negate
					  ? currentEigrp->getConfigs().purgeTime.store(240, std::memory_order_release)
					  : currentEigrp->getConfigs().purgeTime.store(static_cast<uint16_t>(std::stoi(commandStream[3])), std::memory_order_release);
				}
			}
			else if (commandStream[0] == "topology")
			{
				if (!negate)
				{
					if (commandStream[1] == "base")
					{
						terminal.changeMode(Mode::routerAddressFamilyTopology);
						terminal.configureAddressFamily(currentEigrp->getAF());
						terminal.isModeChanged = true;
					}
					else
					{
						//TODO ADD named topologies based on route distinguishers - maybe
					}
				}
				else
				{
					if (commandStream[1] != "base")
					{
						//TODO ADD named topologies based on route distinguishers - maybe
					}
				}
			}
		}
	}
	else if (terminal.currentSubMode == "ospf")
	{
		if (commandStream[0] == "exit")
		{
			terminal.exitMode(Mode::globalConfiguration);
		}
	}
	else if (terminal.currentSubMode == "bgp")
	{
		if (commandStream[0] == "exit")
		{
			terminal.exitMode(Mode::globalConfiguration);
		}
	}
	else if (terminal.currentSubMode == "rip")
	{
		if (commandStream[0] == "exit")
		{
			terminal.exitMode(Mode::globalConfiguration);
		}
	}
	else return false;
	return true;
}



