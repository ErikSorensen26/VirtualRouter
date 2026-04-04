// GlobalHelper.hpp

// TODO finish doxy

#ifndef GLOBAL_HELPERS_HPP
#define GLOBAL_HELPERS_HPP

#include <Global.h>
#include "configs/registry/global/GlobalRegistry.h"
#include "cli/modes/contexts/GlobalContext.hpp"
#include "cli/runtime/CliSession.h"

namespace cli
{
inline static config::GlobalRegistry& getGlobalConfigs(core::Global& g)
{
    return g.configs.get();
}

inline static bool getVrfConfigs(config::VrfRegistry*& vrf, GlobalContext& ctx, std::string_view name = "default")
{
    auto& vrfs = ctx.global.configs->get<config::Global::VRF_CONFIGS>();
    if (auto it = vrfs.getMutable().find(std::string(name)); it != vrfs.getMutable().end())
    {
        vrf = &it->second.get();
        return true;
    }
    ctx.terminal.controller.print("% IP routing table " + std::string(name) + " does not exist. Create it first.");
    return false;
}
}

#endif // GLOBAL_HELPERS_HPP
