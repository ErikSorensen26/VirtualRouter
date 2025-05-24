#include "CommandProcessor.h"
#include <VirtualRouter.h>
#include <CliEngine.h>
#include <DhcpServer.h>
#include <Eigrp.h>
#include <Arp.h>
#include <Ospf.h>
#include <Bgp.h>
#include <Ndp.h>

bool CommandProcessor::handleGlobalConfiguration(const std::vector<std::string> commandStream)
{
	if (commandStream[0] == "arp")
	{
		int offset = 0;
		std::string vrfName = "default";
		if (commandStream[1] == "vrf")
		{
			vrfName = commandStream[2];
			offset = 2;
		}

		std::shared_lock<std::shared_mutex> lock(global.configs.arp.neighborMutex);
		if (negate && global.configs.arp.neighbors.contains(vrfName) && global.configs.arp.neighbors.contains(commandStream[offset + 1]))
		{
			GlobalConfigs::Arp::Neighbor entry = global.configs.arp.neighbors[vrfName][commandStream[offset + 1]];
			auto* vrf = global.getRoutingInstance(vrfName, AddressFamily::IPv4);
			if (auto iface = vrf ? vrf->getInterface(entry.interface.first, entry.interface.second) : nullptr)
			{
				if (iface->arp)
				{
					iface->arp->removeArpEntry(commandStream[offset + 1], true);
				}
			}
			global.configs.arp.neighbors[vrfName].erase(commandStream[offset + 1]);
		}
		else
		{
			GlobalConfigs::Arp::Neighbor entry{
				commandStream[offset + 2],
				{terminal.engine.getInterfaceType(commandStream[offset + 3]), std::stoi(commandStream[offset + 4])},
				commandStream.size() == 6
			};
			global.configs.arp.neighbors[vrfName].emplace(
				commandStream[offset + 1], entry
			);
			auto* vrf = global.getRoutingInstance(vrfName, AddressFamily::IPv4);
			if (auto iface = vrf ? vrf->getInterface(entry.interface.first, entry.interface.second) : nullptr)
			{
				if (iface->arp)
				{
					iface->arp->addArpEntry(commandStream[1], entry.mac, entry.proxy, true);
				}
			}
		}
	}
	if (commandStream[0] == "exit")
	{
		terminal.exitMode(Mode::privilegedExec);
	}
	else if (commandStream[0] == "hostname")
	{
		if (!negate)
		{
			global.setHostname(commandStream[1]);
		}
		else
		{
			global.setHostname("Router");
		}
	}
	else if (commandStream[0] == "end")
	{
		terminal.changeMode(Mode::privilegedExec);
	}
	else if (commandStream[0] == "ip")
	{
		if (commandStream[1] == "dhcp")
		{
			if (commandStream[2] == "aaa")
			{
				std::string username = commandStream[5];
				//TODO
			}
			else if (commandStream[2] == "binding")
			{
				uint16_t cleanupTime = std::stoi(commandStream[5]);
				global.dhcpServer->globalConfig.bindingCleanup.store(cleanupTime, std::memory_order_release);
			}
			else if (commandStream[2] == "bootp")
			{
				if (currentVrf)
				{
					global.dhcpServer->globalConfig.bootp.ignore.store(true, std::memory_order_release);
				}
			}
			else if (commandStream[2] == "class")
			{
				std::string dhcpClass = commandStream[3];
				//TODO
			}
			else if (commandStream[2] == "compatibility")
			{
				if (commandStream[3] == "lease-query")
				{
					if (commandStream[5] == "cisco")
					{
						//TODO
					}
					else if (commandStream[5] == "standard")
					{
						//TODO
					}
				}
				else if (commandStream[3] == "suboption")
				{
					if (commandStream[5] == "cisco")
					{
						//TODO
					}
					else if (commandStream[5] == "standard")
					{
						//TODO
					}
				}
			}
			else if (commandStream[2] == "conflict")
			{
				if (commandStream[3] == "logging")
				{
					global.dhcpServer->globalConfig.logConflicts.store(true, std::memory_order_release);
				}
				if (commandStream[3] == "resolution")
				{
					if (commandStream.size() == 4)
					{
						//TODO
					}
					else
					{
						global.dhcpServer->globalConfig.conflictInterval.store(std::stoi(commandStream[5]));
					}
				}
			}
			else if (commandStream[2] == "database")
			{
				if (commandStream[4] == "timeout")
				{
					std::unique_lock<std::shared_mutex> lock(global.dhcpServer->globalConfig.configMutex);
					global.dhcpServer->globalConfig.databaseSaveInterval[commandStream[3]] = std::stoi(commandStream[5]);
				}
				if (commandStream[4] == "write-delay")
				{
					std::unique_lock<std::shared_mutex> lock(global.dhcpServer->globalConfig.configMutex);
					global.dhcpServer->globalConfig.writeDelay[commandStream[3]] = {std::stoi(commandStream[5]), commandStream.size() >= 8 ? std::stoi(commandStream[7]) : 0};
				}
			}
			else if (commandStream[2] == "debug")
			{
				global.dhcpServer->globalConfig.logAsciiClientID.store(false, std::memory_order_relaxed);
			}
			else if (commandStream[2] == "excluded-address")
			{
				bool hasVrf = false;
				std::string vrf = currentVrf->instanceName;
				if (commandStream[3] == "vrf")
				{
					hasVrf = true;
				}
				uint32_t ipStart = Functions::byteToNum(Functions::addressToByte(commandStream[hasVrf ? 5 : 3]));
				uint32_t size = 1;
				if (commandStream.size() > (hasVrf ? 6 : 4))
				{
					uint32_t ipEnd = Functions::byteToNum(Functions::addressToByte(commandStream[hasVrf ? 6 : 4]));
					if (ipEnd >= ipStart)
					{
						size = (ipEnd - ipStart) + 1;
					}
					else
					{
						return false;
					}
				}
				std::unique_lock<std::shared_mutex> lock(global.dhcpServer->vrfConfigs.configMutex);
				global.dhcpServer->vrfConfigs.excludedAddresses[currentVrf->instanceName][ipStart].insert(size);
			}
			else if (commandStream[2] == "global-options")
			{
				//TODO
			}
			else if (commandStream[2] == "limit")
			{
				if (commandStream[4] == "log")
				{
					//TODO limit dhcp lease
					global.dhcpServer->globalConfig.limitLeases.store(true, std::memory_order_release);
				}
				else if (commandStream[4] == "per")
				{
					global.dhcpServer->globalConfig.leasesPerInterface.store(std::stoi(commandStream[6]), std::memory_order_release);
				}
			}
			else if (commandStream[2] == "limited-broadcast-address")
			{
				global.dhcpServer->globalConfig.limitBroadcastAddress.store(true, std::memory_order_release);
			}
			else if (commandStream[2] == "ping")
			{
				if (commandStream[3] == "packets")
				{
					//TODO
				}
				else if (commandStream[3] == "timeout")
				{
					global.dhcpServer->globalConfig.pingTimeout.store(std::stoi(commandStream[4]));
				}
			}
			else if (commandStream[2] == "pool")
			{
				auto poolIt = global.dhcpServer->poolConfigs.find(commandStream[3]);
				if (poolIt != global.dhcpServer->poolConfigs.end())
				{
					currentDhcpPool = poolIt->second;
				}
				else
				{
					currentDhcpPool = new Protocol::Dhcp::DhcpNetworkConfig();
					global.dhcpServer->poolConfigs[commandStream[3]] = currentDhcpPool;
				}
				terminal.changeMode(Mode::dhcpConfig);
			}
			else if (commandStream[2] == "relay")
			{
				//TODO DO ALL OF THIS
				if (commandStream[3] == "bootp")
				{
					global.dhcpServer->globalConfig.bootp.relayIgnore.store(true, std::memory_order_release);
				}
				else if (commandStream[3] == "information")
				{
					if (commandStream[4] == "check")
					{
						global.dhcpServer->globalConfig.bootp.validateRelay.store(true, std::memory_order_release);
					}
					else if (commandStream[4] == "option")
					{
						if (commandStream.size() == 6)
						{
							global.dhcpServer->globalConfig.bootp.includeVPNrelayInfo.store(true);
						}
						else
						{
							global.dhcpServer->globalConfig.bootp.includeRelayInfo.store(true, std::memory_order_release);
						}
					}
					else if (commandStream[4] == "policy")
					{
						std::string com = commandStream[5];
						if (com == "drop")
						{
							global.dhcpServer->globalConfig.bootp.drop.store(true, std::memory_order_release);
						}
						else if (com == "encapsulate")
						{
							global.dhcpServer->globalConfig.bootp.encapsulate.store(true, std::memory_order_release);
						}
						else if (com == "keep")
						{
							global.dhcpServer->globalConfig.bootp.keep.store(true, std::memory_order_release);
						}
						else if (com == "replace")
						{
							global.dhcpServer->globalConfig.bootp.replace.store(true, std::memory_order_release);
						}
					}
					else if (commandStream[4] == "trust-all")
					{
						global.dhcpServer->globalConfig.bootp.trustAll.store(true, std::memory_order_release);
					}
				}
				else if (commandStream[3] == "override")
				{
					global.dhcpServer->globalConfig.bootp.linkSelectOverride.store(true, std::memory_order_release);
				}
			}
			else if (commandStream[2] == "remember")
			{
				global.dhcpServer->globalConfig.remember.store(true, std::memory_order_release);
			}
			else if (commandStream[2] == "route")
			{
				if (commandStream[3] == "connected")
				{
					global.dhcpServer->globalConfig.addConnected.store(true, std::memory_order_release);
				}
				else if (commandStream[3] == "static")
				{
					global.dhcpServer->globalConfig.addStatic.store(true, std::memory_order_release);
				}
			}
			else if (commandStream[2] == "smart-relay")
			{
				global.dhcpServer->globalConfig.smartRelay.store(true, std::memory_order_release);
			}
			else if (commandStream[2] == "snooping")
			{
				if (commandStream[3] == "database")
				{
					global.dhcpServer->globalConfig.snooping.databases.insert(commandStream[4]);
				}
				else if (commandStream[3] == "information")
				{
					if (commandStream.size() == 5)
					{
						global.dhcpServer->globalConfig.snooping.informationOption.store(true, std::memory_order_release);
					}
					else
					{
						global.dhcpServer->globalConfig.snooping.allowUntrusted.store(true, std::memory_order_release);
					}
				}
				else if (commandStream[3] == "verify")
				{
					if (commandStream[4] == "mac-address")
					{
						global.dhcpServer->globalConfig.snooping.verifyMac.store(true, std::memory_order_release);
					}
					else if (commandStream[4] == "no-relay-agent-address")
					{
						global.dhcpServer->globalConfig.snooping.verifyGiaddr.store(true, std::memory_order_release);
					}
				}
				else if (commandStream[3] == "vlan")
				{
					uint16_t vlanStart;
					uint16_t size = 1;
					if (commandStream[4].find('-') != std::string::npos)
					{
						auto pair = Functions::splitMiddle(commandStream[4], '-');
						if (pair->first > pair->second) return false;
						global.dhcpServer->globalConfig.snooping.vlans[std::stoi(pair->first)].insert(std::stoi(pair->second) - std::stoi(pair->first) + 1);
					}
					else
					{
						vlanStart = Functions::byteToNum(Functions::addressToByte(commandStream[4]));
						if (commandStream.size() > 5)
						{
							uint32_t vlanEnd = Functions::byteToNum(Functions::addressToByte(commandStream[5]));
							if (vlanEnd >= vlanStart)
							{
								size = (vlanEnd - vlanStart) + 1;
							}
							else
							{
								return false;
							}
						}
						global.dhcpServer->globalConfig.snooping.vlans[vlanStart].insert(size);
					}
				}
			}
			else if (commandStream[2] == "support")
			{
				if (commandStream[3] == "option55-override")
				{
					global.dhcpServer->globalConfig.option55Override.store(true, std::memory_order_release);
				}
				else if (commandStream[3] == "sip")
				{
					global.dhcpServer->globalConfig.sipParameterNak.store(true, std::memory_order_release);
				}
				else if (commandStream[3] == "tunnel")
				{
					global.dhcpServer->globalConfig.tunnelUnicastParameter.store(true, std::memory_order_release);
				}
			}
			else if (commandStream[2] == "update")
			{
				if (commandStream[3] == "dns")
				{
					for (const auto& dns : commandStream)
					{
						if (dns == "both")
						{
							global.dhcpServer->globalConfig.updateDNS.both.store(true, std::memory_order_release);
						}
						else if (dns == "before")
						{
							global.dhcpServer->globalConfig.updateDNS.before.store(true, std::memory_order_release);
						}
						else if (dns == "override")
						{
							global.dhcpServer->globalConfig.updateDNS.override.store(true, std::memory_order_release);
						}
					}
				}
			}
			else if (commandStream[2] == "use")
			{
				//TODO
			}
		}
	}
	else if (commandStream[0] == "ipv6")
	{
		if (commandStream[1] == "nd")
		{
			if (commandStream[2] == "cache")
			{
				if (commandStream[3] == "expire")
				{
					uint16_t value = negate ? 600 : std::stoi(commandStream[4]);
					global.configs.ndp.cacheExpire.store(value, std::memory_order_release);

					bool setRefresh = (commandStream.size() > 5 && !negate) || negate;

					for (const auto& [_, iface] : global.getInterfaceList())
					{
						if (!iface->ndp->configs.cacheExpireLocal)
						{
							iface->ndp->configs.cacheExpire.store(value, std::memory_order_release); 
						}
						if (setRefresh && !iface->ndp->configs.refreshLocal)
						{
							iface->ndp->configs.refresh.store(!negate, std::memory_order_relaxed);
						}
					}
				}
				else if (commandStream[3] == "interface-limit")
				{
					uint16_t value = negate ? 600 : std::stoi(commandStream[4]);
					global.configs.ndp.interfaceLimit.store(value, std::memory_order_release);

					bool setLog = false;
					uint16_t log;
					if ((commandStream.size() > 5 && !negate) || negate)
					{
						setLog = true;
						log = negate ? 0 : std::stoi(commandStream[6]);
						global.configs.ndp.loggingRate.store(log, std::memory_order_release);
					}

					for (const auto& [_, iface] : global.getInterfaceList())
					{
						if (!iface->ndp->configs.interfaceLimitLocal)
						{
							iface->ndp->configs.interfaceLimit.store(value, std::memory_order_release); 
						}
						if (setLog && !iface->ndp->configs.loggingRateLocal)
						{
							iface->ndp->configs.loggingRate.store(log, std::memory_order_release);
						}
					}
				}
			}
			else if (commandStream[2] == "dad")
			{
				uint16_t time = negate ? 1000 : std::stoi(commandStream[4]);
				for (const auto& [_, iface] : global.getInterfaceList())
				{
					if (!iface->ndp->configs.dadTimeLocal)
					{
						iface->ndp->configs.dadTime.store(time, std::memory_order_release);
					}
				}
			}
			else if (commandStream[2] == "host")
			{
				if (commandStream[3] == "mode")
				{
					global.configs.ndp.strictMode.store(!negate, std::memory_order_relaxed);
				}
			}
			else if (commandStream[2] == "nsf")
			{
				if (commandStream[3] == "convergence")
				{
					global.configs.ndp.nsfConvergenceTime.store(negate ? 180 : std::stoi(commandStream[4]), std::memory_order_release);
				}
				else if (commandStream[3] == "dad")
				{
					if (commandStream[4] == "supress")
					{
						global.configs.ndp.nsfDadSupressionTime.store(negate ? 180 : std::stoi(commandStream[5]), std::memory_order_release);
					}
				}
				else if (commandStream[3] == "throttle")
				{
					global.configs.ndp.nsfThrottleResolutions.store(negate ? 1000 : std::stoi(commandStream[4]), std::memory_order_release);
				}
			}
			else if (commandStream[2] == "nud")
			{
				if (commandStream[3] == "limit")
				{
					global.configs.ndp.nudLimit.store(negate ? 5 : std::stoi(commandStream[4]), std::memory_order_release);
					if (commandStream.size() > 5)
					{
						global.configs.ndp.nudRefreshPeriod.store(negate ? 5 : std::stoi(commandStream[6]), std::memory_order_release);
					}
				}
			}
			else if (commandStream[2] == "reachable-time")
			{
				uint16_t value = negate ? 30000 : std::stoi(commandStream[3]);
				global.configs.ndp.reachableTime.store(value, std::memory_order_release);
				for (const auto& [_, iface] : global.getInterfaceList())
				{
					if (!iface->ndp->configs.reachableTimeLocal)
					{
						iface->ndp->configs.reachableTime.store(value, std::memory_order_release);
					}
				}
			}
			else if (commandStream[2] == "resolution")
			{
				global.configs.ndp.resolutionLimit.store(negate ? 512 : std::stoi(commandStream[5]), std::memory_order_release);
			}
			else if (commandStream[2] == "route-owner")
			{
				global.configs.ndp.ndAsRouteOwner.store(!negate, std::memory_order_release);
			}
		}
		else if (commandStream[1] == "neighbor")
		{
			ByteString address = commandStream[2];
			if (!negate)
			{
				InterfaceType type = terminal.engine.getInterfaceType(commandStream[3]);
				float id = std::stof(commandStream[4]);
				
				GlobalConfigs::Ndp::Neighbor entry{
					{type, id},
					commandStream[5]
				};
				global.configs.ndp.neighbors.emplace(
					address,
					entry
				);
				for (const auto& [pair, iface] : global.getInterfaceList())
				{
					if (pair.first == type && pair.second == id)
					{
						iface->ndp->addNdpEntry(address, entry.macAddress, false, true);
					}
				}
			}
		}
		else if (commandStream[1] == "router")
		{
			terminal.isList = true;
			if (commandStream[2] == "eigrp")
			{
				std::string type = commandStream[2];
				std::string ID;
				if (commandStream.size() > 3)
				{
					ID = commandStream[3];
					terminal.routingProtocolID = Functions::stringToNum(commandStream[3]);
				}
				if (type == "eigrp")
				{
					if (!negate)
					{
						Protocol::EigrpAutonomousSystem* as = currentVrf->getEigrpAutonomousSystem(terminal.routingProtocolID);
						if (as)
						{
							if (as->ipv6Named)
							{
								std::cout << "\n%" << " ERROR: AS(" + ID + ") used by named mode";
								return false; // AS used in named mode.
							}
						}
						else
						{
							as = currentVrf->addEigrpAutonomousSystem(terminal.routingProtocolID);
						}
						if (!as->ipv6)
						{
							as->ipv6 = new Protocol::Eigrp(terminal.routingProtocolID, AddressFamily::IPv6, currentVrf);
						}
						currentEigrp = as->ipv6;
						terminal.configureRoutingMode("eigrp_classic", true);
					}
					else
					{
						Protocol::EigrpAutonomousSystem* as = currentVrf->getEigrpAutonomousSystem(terminal.routingProtocolID);
						if (as)
						{
							if (!as->ipv6Named && as->ipv6)
							{
								delete as->ipv6;
								as->ipv6 = nullptr;
								if (!as->ipv4)
								{
									currentVrf->removeEigrpAutonomousSystem(terminal.routingProtocolID);
								}
							}
						}
					}
				}
				else if (type == "ospf")
				{
					if (!negate)
					{
						terminal.configureRoutingMode("ospf", true);
						if (!ospfList[static_cast<uint16_t>(terminal.routingProtocolID)])
						{
							(ospfList)[static_cast<uint16_t>(terminal.routingProtocolID)] = std::make_shared<Protocol::Ospf>();
						}
						currentOspf = ospfList.at(static_cast<uint16_t>(terminal.routingProtocolID)).get();
					}
					else
					{

					}
				}
			}
		}
	}
	else if (commandStream[0] == "interface")
	{
		terminal.isList = true;
		std::string type = commandStream[1];
		std::string interfaceID_temp = commandStream[2];
		terminal.interfaceID = static_cast<uint8_t>(Functions::stringToNum(commandStream[2]));
		InterfaceType interfaceType = terminal.engine.getInterfaceType(type);
		std::string intType;
		size_t id = static_cast<size_t>(std::floor(terminal.interfaceID));
		if (!global.getInterface(terminal.engine.getInterfaceType(type), terminal.interfaceID))
		{
			if (negate)
			{
				global.removeInterface(interfaceType, terminal.interfaceID);
				currentVrf->removeInterface(interfaceType, terminal.interfaceID);
			}
			else
			{
				if ((type == "Ethernet" || type == "GigabitEthernet" || type == "FastEthernet") && terminal.engine.physicalInterfaces.size() >= id)
				{
						intType = terminal.engine.physicalInterfaces[id];
				}
				else
				{
						intType = "NO_INTERFACE";
				}
				InterfaceType interfaceTypeEnum = terminal.engine.getInterfaceType(type);
				std::string mac = terminal.engine.getMac(interfaceTypeEnum, id);
				if (mac.empty() || intType == "NO_INTERFACE") return false;
				global.addInterface(interfaceType, intType, 1024, 1024, mac, terminal.interfaceID, terminal.isDebugModeEnabled);
				currentVrf->addInterface(global.getInterface(interfaceTypeEnum, terminal.interfaceID), interfaceType, terminal.interfaceID);
			}
		}
		terminal.configureInterfaceMode(type);
		currentInterface = global.getInterface(interfaceType, terminal.interfaceID);
	}
	else if (commandStream[0] == "router")
	{
		terminal.isList = true;
		std::string type = commandStream[1];
		std::string ID;
		if (commandStream.size() > 2)
		{
			ID = commandStream[2];
			terminal.routingProtocolID = Functions::stringToNum(commandStream[2]);
		}
		if (type == "eigrp")
		{
			if (Functions::isDecimal(ID))
			{
				Protocol::EigrpAutonomousSystem* as = currentVrf->getEigrpAutonomousSystem(terminal.routingProtocolID);
				if (!negate)
				{
					if (as)
					{
						if (as->ipv4Named)
						{
							std::cout << "\n%" << " ERROR: AS(" + ID + ") used by named mode";
							return false; // AS used in named mode.
						}
					}
					else
					{
						as = currentVrf->addEigrpAutonomousSystem(terminal.routingProtocolID);
					}
					if (!as->ipv4)
					{
						as->ipv4 = new Protocol::Eigrp(terminal.routingProtocolID, AddressFamily::IPv4, global.getRoutingInstance("default"));
					}
					currentEigrp = as->ipv4;
					terminal.configureRoutingMode("eigrp_classic");
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
			else
			{
				if (!negate)
				{
					if (!currentVrf->getEigrpNamed(ID))
					{
						currentVrf->addEigrpNamed(ID);
					}
					currentEigrpNamed = currentVrf->getEigrpNamed(ID);
					terminal.configureRoutingMode("eigrp_named");
				}
				else
				{
					currentVrf->removeEigrpNamed(ID);
				}
			}
		}
		else if (type == "ospf")
		{
			if (!negate)
			{
				terminal.configureRoutingMode("ospf");
				if (!ospfList[static_cast<uint16_t>(terminal.routingProtocolID)])
				{
					(ospfList)[static_cast<uint16_t>(terminal.routingProtocolID)] = std::make_shared<Protocol::Ospf>();
				}
				currentOspf = ospfList.at(static_cast<uint16_t>(terminal.routingProtocolID)).get();
			}
		}
		else if (type == "bgp")
		{
			if (!negate)
			{
				terminal.configureRoutingMode("bgp");
				if (!bgpList[static_cast<uint16_t>(terminal.routingProtocolID)])
				{
					(bgpList)[terminal.routingProtocolID] = std::make_shared<Protocol::Bgp>();
				}
			}
		}
	}
	else return false;
	return true;
}
