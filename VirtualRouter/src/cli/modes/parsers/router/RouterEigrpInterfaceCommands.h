// RouterEigrpInterfaceCommands.h

#ifndef ROUTER_EIGRP_INTERFACE_COMMANDS_H
#define ROUTER_EIGRP_INTERFACE_COMMANDS_H

#include "cli/parser/CliModeParser.hpp"
#include "cli/parser/Command.hpp"
#include "cli/modes/contexts/EigrpContext.hpp"

namespace cli
{
bool RouterEigrpInterface_AuthenticationKeyChain_Handler(EIGRP_PARAMS);
using RouterEigrpInterface_AuthenticationKeyChain = commandAdder<EigrpContext,
    RouterEigrpInterface_AuthenticationKeyChain_Handler,
    "authentication"_tok, "key-chain"_tok, ARG_REST
>;

bool RouterEigrpInterface_AuthenticationMode_Handler(EIGRP_PARAMS);
using RouterEigrpInterface_AuthenticationMode = commandAdder<EigrpContext,
    RouterEigrpInterface_AuthenticationMode_Handler,
    "authentication"_tok, "mode"_tok, ARG_REST
>;

bool RouterEigrpInterface_BandwidthPercentage_Handler(EIGRP_PARAMS);
using RouterEigrpInterface_BandwidthPercentage = commandAdder<EigrpContext,
    RouterEigrpInterface_BandwidthPercentage_Handler,
    "bandwidth-percentage"_tok, ARG_REST
>;

bool RouterEigrpInterface_DampeningChange_Handler(EIGRP_PARAMS);
using RouterEigrpInterface_DampeningChange = commandAdder<EigrpContext,
    RouterEigrpInterface_DampeningChange_Handler,
    "dampening-change"_tok, ARG_REST
>;

bool RouterEigrpInterface_DampeningInterval_Handler(EIGRP_PARAMS);
using RouterEigrpInterface_DampeningInterval = commandAdder<EigrpContext,
    RouterEigrpInterface_DampeningInterval_Handler,
    "dampening-interval"_tok, ARG_REST
>;

bool RouterEigrpInterface_Exit_Handler(EIGRP_PARAMS);
using RouterEigrpInterface_Exit = commandAdder<EigrpContext,
    RouterEigrpInterface_Exit_Handler,
    "exit-af-interface"_tok
>;

bool RouterEigrpInterface_HelloInterval_Handler(EIGRP_PARAMS);
using RouterEigrpInterface_HelloInterval = commandAdder<EigrpContext,
    RouterEigrpInterface_HelloInterval_Handler,
    "hello-interval"_tok, ARG_REST
>;

bool RouterEigrpInterface_HoldTime_Handler(EIGRP_PARAMS);
using RouterEigrpInterface_HoldTime = commandAdder<EigrpContext,
    RouterEigrpInterface_HoldTime_Handler,
    "hold-time"_tok, ARG_REST
>;

bool RouterEigrpInterface_NextHopSelf_Handler(EIGRP_PARAMS);
using RouterEigrpInterface_NextHopSelf = commandAdder<EigrpContext,
    RouterEigrpInterface_NextHopSelf_Handler,
    "next-hop-self"_tok
>;

bool RouterEigrpInterface_PassiveInterface_Handler(EIGRP_PARAMS);
using RouterEigrpInterface_PassiveInterface = commandAdder<EigrpContext,
    RouterEigrpInterface_PassiveInterface_Handler,
    "passive-interface"_tok
>;

// bool RouterEigrpInterface_Shutdown_Handler(EIGRP_PARAMS); //TODO

bool RouterEigrpInterface_SplitHorizon_Handler(EIGRP_PARAMS);
using RouterEigrpInterface_SplitHorizon = commandAdder<EigrpContext,
    RouterEigrpInterface_SplitHorizon_Handler,
    "split-horizon"_tok
>;

bool RouterEigrpInterface_SummaryAddress_Handler(EIGRP_PARAMS);
using RouterEigrpInterface_SummaryAddress = commandAdder<EigrpContext,
    RouterEigrpInterface_SummaryAddress_Handler,
    "summary-address"_tok, ARG, ARG_REST
>;

using RouterEigrpInterfaceCommands = CliModeParser<CliMode::None, EigrpContext,
    RouterEigrpInterface_AuthenticationKeyChain,
    RouterEigrpInterface_AuthenticationMode,
    RouterEigrpInterface_BandwidthPercentage,
    RouterEigrpInterface_DampeningChange,
    RouterEigrpInterface_DampeningInterval,
    RouterEigrpInterface_Exit,
    RouterEigrpInterface_HelloInterval,
    RouterEigrpInterface_HoldTime,
    RouterEigrpInterface_NextHopSelf,
    RouterEigrpInterface_PassiveInterface,
    RouterEigrpInterface_SplitHorizon,
    RouterEigrpInterface_SummaryAddress
>;

using RouterEigrpInterfaceV4Commands = CliModeParser<CliMode::RouterEigrpInterfaceV4, EigrpContext,
    RouterEigrpInterfaceCommands
>;

using RouterEigrpInterfaceV6Commands = CliModeParser<CliMode::RouterEigrpInterfaceV6, EigrpContext,
    RouterEigrpInterfaceCommands
>;
}

#endif // ROUTER_EIGRP_INTERFACE_COMMANDS_H
