// InterfaceCommands.cpp

#include "cli/grammar/CommandUtils.hpp"

#include "cli/session/CliSession.h"
#include "InterfaceIPCommands.h"
#include "InterfaceIPv6Commands.h"
#include "InterfaceOspfv3Commands.h"
#include "configs/FieldAccessor.hpp"

#define INTERFACE_PARAMS DEFINE_PARAMS(config::InterfaceRegistry)
#define INTERFACE_SUB_PARAMS DEFINE_SUB_PARAMS(config::InterfaceRegistry)

namespace cli
{
bool Interface_Exit_Handler(INTERFACE_PARAMS)
{
    UNUSED(segs);
    return ctx.terminal.popMode();
}

bool Interface_Shutdown_Handler(INTERFACE_PARAMS)
{
    UNUSED(segs);
    //auto shut = config::AtomicFieldAccessor<>I
    auto shut = ctx.configs().get<config::Interface::SHUTDOWN>();
    utils::setToggleValue(shut, ctx);
    return true;
}

bool Interface_IP_SubHandler(INTERFACE_SUB_PARAMS)
{
    return InterfaceIPCommands::execute(ctx, toks, idx);
}

bool Interface_IPv6_SubHandler(INTERFACE_SUB_PARAMS)
{
    return InterfaceIPv6Commands::execute(ctx, toks, idx);
}

bool Interface_Ospfv3_SubHandler(INTERFACE_SUB_PARAMS)
{
    return InterfaceOspfv3BaseCommands::execute(ctx, toks, idx);
}

#define INTERFACE_LIST(X, Y) \
    X(Y, (COMMAND, Exit, "exit"_tok)) \
    X(Y, (SUBPRSR, IP, "ip"_tok)) \
    X(Y, (SUBPRSR, IPv6, "ipv6"_tok)) \
    X(Y, (SUBPRSR, Ospfv3, "ospfv3"_tok)) \
    X(Y, (COMMAND, Shutdown, "shutdown"_tok))


/**
 * @brief Complete parser for the Interface Configuration CLI mode.
 * @ingroup CLI_MODE_PARSERS
 *
 * Covers `CliMode::Interface` with `InterfaceContext` and composes
 * exit, shutdown, and `ip`/`ipv6` sub-trees.
 */
DEFINE_CMD_MODE(Interface, config::InterfaceRegistry, INTERFACE_LIST);
}
