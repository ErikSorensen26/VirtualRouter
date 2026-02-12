// GlobalIPDHCPCommands.h

#ifndef GLOBAL_IP_DHCP_COMMANDS_H
#define GLOBAL_IP_DHCP_COMMANDS_H

#include "cli/parser/CliModeParser.hpp"
#include "cli/parser/Command.hpp"
#include "cli/modes/contexts/GlobalContext.hpp"

namespace Cli
{
bool GlobalIPDHCP_Binding_Handler(GLOBAL_PARAMS);
using GlobalIPDHCP_Binding = commandAdder<GlobalContext,
    GlobalIPDHCP_Binding_Handler,
    "binding"_tok, ARG_REST
>;

bool GlobalIPDHCP_Bootp_Handler(GLOBAL_PARAMS);
using GlobalIPDHCP_Bootp = commandAdder<GlobalContext,
    GlobalIPDHCP_Bootp_Handler,
    "bootp"_tok
>;

bool GlobalIPDHCP_ConflictLogging_Handler(GLOBAL_PARAMS);
using GlobalIPDHCP_ConflictLogging = commandAdder<GlobalContext,
    GlobalIPDHCP_ConflictLogging_Handler,
    "conflict"_tok, "logging"_tok
>;

bool GlobalIPDHCP_ConflictResolution_Handler(GLOBAL_PARAMS);
using GlobalIPDHCP_ConflictResolution = commandAdder<GlobalContext,
    GlobalIPDHCP_ConflictResolution_Handler,
    "conflict"_tok, "resolution"_tok
>;

bool GlobalIPDHCP_DatabaseTimeout_Handler(GLOBAL_PARAMS);
using GlobalIPDHCP_DatabaseTimeout = commandAdder<GlobalContext,
    GlobalIPDHCP_DatabaseTimeout_Handler,
    "database"_tok, ARG, "timeout"_tok, ARG_REST
>;

bool GlobalIPDHCP_DatabaseWrite_Handler(GLOBAL_PARAMS);
using GlobalIPDHCP_DatabaseWrite = commandAdder<GlobalContext,
    GlobalIPDHCP_DatabaseWrite_Handler,
    "database"_tok, ARG, "write-delay"_tok, ARG_REST
>;

bool GlobalIPDHCP_Debug_Handler(GLOBAL_PARAMS);
using GlobalIPDHCP_Debug = commandAdder<GlobalContext,
    GlobalIPDHCP_Debug_Handler,
    "debug"_tok, ARG_REST
>;

bool GlobalIPDHCP_ExcludedAddress_Handler(GLOBAL_PARAMS);
using GlobalIPDHCP_ExcludedAddress = commandAdder<GlobalContext,
    GlobalIPDHCP_ExcludedAddress_Handler,
    "excluded-address"_tok, ARG_REST
>;

using GlobalIPDHCPCommands = CliModeParser<CliMode::GlobalConfiguration, GlobalContext,
    GlobalIPDHCP_Binding
>;

/*if (commandStream[1] == "dhcp")
{
    else if (commandStream[2] == "limit")
    {
            if (commandStream[4] == "log")
            {
                    //TODO limit dhcp lease
                    global.dhcpServer->configs.limitLeases.store(true, std::memory_order_release);
            }
            else if (commandStream[4] == "per")
            {
                    global.dhcpServer->configs.leasesPerInterface.store(std::stoi(commandStream[6]), std::memory_order_release);
            }
    }
    else if (commandStream[2] == "limited-broadcast-address")
    {
            global.dhcpServer->configs.limitBroadcastAddress.store(true, std::memory_order_release);
    }
    else if (commandStream[2] == "ping")
    {
            if (commandStream[3] == "packets")
            {
                    //TODO
            }
            else if (commandStream[3] == "timeout")
            {
                    global.dhcpServer->configs.pingTimeout.store(std::stoi(commandStream[4]));
            }
    }
    else if (commandStream[2] == "pool")
    {
            auto poolIt = global.dhcpServer->networks.find(commandStream[3]);
            if (poolIt != global.dhcpServer->networks.end())
            {
                    currentDhcpPool = poolIt->second;
            }
            else {
                    currentDhcpPool = global.dhcpServer->addPool(commandStream[3]);
            }
            terminal.changeMode(Mode::dhcpConfig);
    }
    else if (commandStream[2] == "relay")
    {
            //TODO DO ALL OF THIS
            if (commandStream[3] == "bootp")
            {
                    global.dhcpServer->configs.bootp.relayIgnore.store(true, std::memory_order_release);
            }
            else if (commandStream[3] == "information")
            {
                    if (commandStream[4] == "check")
                    {
                            global.dhcpServer->configs.bootp.validateRelay.store(true, std::memory_order_release);
                    }
                    else if (commandStream[4] == "option")
                    {
                            if (commandStream.size() == 6)
                            {
                                    global.dhcpServer->configs.bootp.includeVPNrelayInfo.store(true);
                            }
                            else
                            {
                                    global.dhcpServer->configs.bootp.includeRelayInfo.store(true, std::memory_order_release);
                            }
                    }
                    else if (commandStream[4] == "policy")
                    {
                            std::string com = commandStream[5];
                            if (com == "drop")
                            {
                                    global.dhcpServer->configs.bootp.drop.store(true, std::memory_order_release);
                            }
                            else if (com == "encapsulate")
                            {
                                    global.dhcpServer->configs.bootp.encapsulate.store(true, std::memory_order_release);
                            }
                            else if (com == "keep")
                            {
                                    global.dhcpServer->configs.bootp.keep.store(true, std::memory_order_release);
                            }
                            else if (com == "replace")
                            {
                                    global.dhcpServer->configs.bootp.replace.store(true, std::memory_order_release);
                            }
                    }
                    else if (commandStream[4] == "trust-all")
                    {
                            global.dhcpServer->configs.bootp.trustAll.store(true, std::memory_order_release);
                    }
            }
            else if (commandStream[3] == "override")
            {
                    global.dhcpServer->configs.bootp.linkSelectOverride.store(true, std::memory_order_release);
            }
    }
    else if (commandStream[2] == "remember")
    {
            global.dhcpServer->configs.remember.store(true, std::memory_order_release);
    }
    else if (commandStream[2] == "route")
    {
            if (commandStream[3] == "connected")
            {
                    global.dhcpServer->configs.addConnected.store(true, std::memory_order_release);
            }
            else if (commandStream[3] == "static")
            {
                    global.dhcpServer->configs.addStatic.store(true, std::memory_order_release);
            }
    }
    else if (commandStream[2] == "smart-relay")
    {
            global.dhcpServer->configs.smartRelay.store(true, std::memory_order_release);
    }
    else if (commandStream[2] == "snooping")
    {
            if (commandStream[3] == "database")
            {
                    global.dhcpServer->configs.snooping.databases.insert(commandStream[4]);
            }
            else if (commandStream[3] == "information")
            {
                    if (commandStream.size() == 5)
                    {
                            global.dhcpServer->configs.snooping.informationOption.store(true, std::memory_order_release);
                    }
                    else
                    {
                            global.dhcpServer->configs.snooping.allowUntrusted.store(true, std::memory_order_release);
                    }
            }
            else if (commandStream[3] == "verify")
            {
                    if (commandStream[4] == "mac-address")
                    {
                            global.dhcpServer->configs.snooping.verifyMac.store(true, std::memory_order_release);
                    }
                    else if (commandStream[4] == "no-relay-agent-address")
                    {
                            global.dhcpServer->configs.snooping.verifyGiaddr.store(true, std::memory_order_release);
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
                            global.dhcpServer->configs.snooping.vlans[std::stoi(pair->first)].insert(std::stoi(pair->second) - std::stoi(pair->first) + 1);
                    }
                    else
                    {
                            vlanStart = Functions::addressToIntv4(commandStream[4]);
                            if (commandStream.size() > 5)
                            {
                                    uint32_t vlanEnd = Functions::addressToIntv4(commandStream[5]);
                                    if (vlanEnd >= vlanStart)
                                    {
                                            size = (vlanEnd - vlanStart) + 1;
                                    }
                                    else
                                    {
                                            return false;
                                    }
                            }
                            global.dhcpServer->configs.snooping.vlans[vlanStart].insert(size);
                    }
            }
    }
    else if (commandStream[2] == "support")
    {
            if (commandStream[3] == "option55-override")
            {
                    global.dhcpServer->configs.option55Override.store(true, std::memory_order_release);
            }
            else if (commandStream[3] == "sip")
            {
                    global.dhcpServer->configs.sipParameterNak.store(true, std::memory_order_release);
            }
            else if (commandStream[3] == "tunnel")
            {
                    global.dhcpServer->configs.tunnelUnicastParameter.store(true, std::memory_order_release);
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
                                    global.dhcpServer->configs.updateDNS.both.store(true, std::memory_order_release);
                            }
                            else if (dns == "before")
                            {
                                    global.dhcpServer->configs.updateDNS.before.store(true, std::memory_order_release);
                            }
                            else if (dns == "override")
                            {
                                    global.dhcpServer->configs.updateDNS.override.store(true, std::memory_order_release);
                            }
                    }
            }
    }
    else if (commandStream[2] == "use")
    {
            //TODO
    }
}*/
}

#endif // GLOBAL_IP_DHCP_COMMANDS_H
