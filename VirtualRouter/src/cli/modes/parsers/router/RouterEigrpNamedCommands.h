// RouterEigrpNamedCommands.h

#ifndef ROUTER_EIGRP_COMMANDS_H
#define ROUTER_EIGRP_COMMANDS_H

#include <EigrpContext.hpp>
#include <CliModeParser.hpp>

namespace Cli
{
bool RouterEigrpNamed_AddressFamilyIPv4_Handler(EIGRP_PARAMS);
using RouterEigrpNamed_AddressFamilyIPv4 = commandAdder<EigrpContext,
    RouterEigrpNamed_AddressFamilyIPv4_Handler,
    "address-family"_tok, "ipv4"_tok, "autonomous-system"_tok, ARG
>;

//bool RouterEigrpNamed_AddressFamilyIPv4Multicast_Handler(EIGRP_PARAMS); //TODO
//bool RouterEigrpNamed_AddressFamilyIPv4Unicast_Handler(EIGRP_PARAMS); //TODO

bool RouterEigrpNamed_AddressFamilyIPv4Vrf_Handler(EIGRP_PARAMS);
using RouterEigrpNamed_AddressFamilyIPv4Vrf = commandAdder<EigrpContext,
    RouterEigrpNamed_AddressFamilyIPv4Vrf_Handler,
    "address-family"_tok, "ipv4"_tok, "vrf"_tok, ARG, "autonomous-system"_tok, ARG
>;

bool RouterEigrpNamed_AddressFamilyIPv6_Handler(EIGRP_PARAMS);
using RouterEigrpNamed_AddressFamilyIPv6 = commandAdder<EigrpContext,
    RouterEigrpNamed_AddressFamilyIPv6_Handler,
    "address-family"_tok, "ipv6"_tok, "autonomous-system"_tok, ARG
>;

//bool RouterEigrpNamed_AddressFamilyIPv6Unicast_Handler(EIGRP_PARAMS); // TODO

bool RouterEigrpNamed_AddressFamilyIPv6Vrf_Handler(EIGRP_PARAMS);
using RouterEigrpNamed_AddressFamilyIPv6Vrf = commandAdder<EigrpContext,
    RouterEigrpNamed_AddressFamilyIPv6Vrf_Handler,
    "address-family"_tok, "ipv6"_tok, "vrf"_tok, ARG, "autonomous-system"_tok, ARG
>;

bool RouterEigrpNamed_Exit_Handler(EIGRP_PARAMS);
using RouterEigrpNamed_Exit = commandAdder<EigrpContext,
    RouterEigrpNamed_Exit_Handler,
    "exit"_tok
>;

//bool RouterEigrpNamed_ServiceFamily_Handler(EIGRP_PARAMS); //TODO
// bool RouterEigrpNamed_Shutdown_Handler(EIGRP_PARAMS); //TODO

using RouterEigrpNamedCommands = CliModeParser<CliMode::RouterEigrpNamed, EigrpContext,
    RouterEigrpNamed_AddressFamilyIPv4,
    RouterEigrpNamed_AddressFamilyIPv4Vrf,
    RouterEigrpNamed_AddressFamilyIPv6,
    RouterEigrpNamed_AddressFamilyIPv6Vrf,
    RouterEigrpNamed_Exit
>;
}

#endif
