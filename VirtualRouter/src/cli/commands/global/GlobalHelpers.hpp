/**
 * @file GlobalHelpers.hpp
 * @brief Shared helper utilities for global-mode CLI command handlers.
 * @ingroup CLI_MODES
 */

#ifndef GLOBAL_HELPERS_HPP
#define GLOBAL_HELPERS_HPP

#include <Global.h>
#include "configs/registry/global/GlobalRegistry.h"
#include "configs/FieldAccessor.hpp"
#include "cli/modes/contexts/Context.hpp"
#include "cli/session/CliSession.h"

namespace cli
{
/**
 * @brief Looks up a VRF configuration registry by name, printing an error on miss.
 *
 * Searches the global registry's `VRF_CONFIGS` map for the given VRF name.
 * On success, sets `vrf` to a non-const pointer into the registry and returns
 * true. On failure, prints an IOS-style error to the session terminal and
 * returns false, leaving `vrf` unchanged.
 *
 * @param[out] vrf   Set to the matching `VrfRegistry` pointer on success.
 * @param ctx        Global-mode execution context carrying the registry and terminal.
 * @param name       VRF name to look up; defaults to `"default"`.
 * @return True if the VRF was found, false otherwise.
 */
inline static bool getVrfConfigs(config::VrfRegistry*& vrf, cli::Context<config::GlobalRegistry>& ctx, std::string_view name = "default")
 {
    auto vrfs = ctx.configs().get<config::Global::VRF_CONFIGS>();
    if (auto it = vrfs.find(std::string(name)); it != vrfs.end())
    {
        vrf = const_cast<config::VrfRegistry*>(it->second);
        return true;
    }
    ctx.terminal.controller.print("% IP routing table " + std::string(name) + " does not exist. Create it first.");
    return false;
}
}

#endif // GLOBAL_HELPERS_HPP
