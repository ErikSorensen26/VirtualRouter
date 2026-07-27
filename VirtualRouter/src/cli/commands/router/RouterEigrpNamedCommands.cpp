// RouterEigrpNamedCommands.cpp

#include <sstream>
#include <Global.h>
#include <VirtualRouter.h>
#include "configs/registry/global/GlobalRegistry.h"

#include "RouterEigrpNamedCommands.h"
#include "cli/grammar/CliModeParser.hpp"
#include "cli/session/CliSession.h"
#include "cli/session/CliEngine.h"
#include <cli/grammar/CommandUtils.hpp>

#define EIGRP_NAMED_PARAMS DEFINE_PARAMS(config::EigrpNamedRegistry)

namespace cli
{
bool RouterEigrpNamed_AddressFamilyIPv4_Handler(EIGRP_NAMED_PARAMS)
{
    auto& global = ctx.terminal.engine.global.getConfigs();
    auto namedList = ctx.configs().get<config::EigrpNamed::NAMED_INSTANCES_V4>();

    std::string vrfName = "default";
    uint16_t as = 0;

    for (const auto& seg : segs)
    {
        switch (seg[0])
        {
            case "autonomous-system"_tok:
            {
                if (!utils::setValue(as, seg >> 1))
                    return false;
                break;
            }
            case "vrf"_tok:
            {
                if (!utils::setValue(vrfName, seg >> 1))
                    return false;
                break;
            }
        }
    }

    auto vrfList = global.get<config::Global::VRF_CONFIGS>();
    auto vit = vrfList.find(vrfName);
    if (vit == vrfList.end())
    {
        std::ostringstream oss;
        oss << "\r\n%VRF " << vrfName << " does not exist or is not enabled for IPv4";
        ctx.terminal.controller.print(oss.str());
        return false;
    }

    bool result = true;
    bool exists = false;

    // Verify the selected system doesnt overlap others
    namedList.withRead([&](const config::DefType<typename decltype(namedList)::Field>::type& list) {
        for (const auto& [system, vrf] : list)
        {
            if (vrf == vrfName)
            {
                if (system != as)
                {
                    std::ostringstream oss;
                    oss << "\r\nChanging from AS(" << system
                        << ") to AS(" << as
                        << ") is not allowed";
                    ctx.terminal.controller.print(oss.str());
                    result = false;
                    return;
                }
                else
                {
                    exists = true;
                    return;
                }
            }
        }
    });

    if (!result) return false;

    // Verify selected system is not classic if new
    auto eigrpList = vit->second->get<config::Vrf::ROUTER_EIGRP_V4>();
    if (!exists)
    {
        if (auto it = eigrpList.find(as); it != eigrpList.end())
            if (!it->second->get<config::Eigrp::IS_NAMED>().load())
            {
                std::ostringstream oss;
                oss << "\r\n%ERROR: AS(" << as << ") used by classic router";
                ctx.terminal.controller.print(oss.str());
                return false;
            }
    }
    return ctx.terminal.changeMode<CliMode::RouterEigrpAddressFamilyV4>(
        eigrpList.emplaceBack(as)
    );
}

bool RouterEigrpNamed_AddressFamilyIPv6_Handler(EIGRP_NAMED_PARAMS)
{
    auto& global = ctx.terminal.engine.global.getConfigs();
    auto namedList = ctx.configs().get<config::EigrpNamed::NAMED_INSTANCES_V6>();

    std::string vrfName = "default";
    uint16_t as = 0;

    for (const auto& seg : segs)
    {
        switch (seg[0])
        {
            case "autonomous-system"_tok:
            {
                if (!utils::setValue(as, seg >> 1))
                    return false;
                break;
            }
            case "vrf"_tok:
            {
                if (!utils::setValue(vrfName, seg >> 1))
                    return false;
                break;
            }
        }
    }

    auto vrfList = global.get<config::Global::VRF_CONFIGS>();
    auto vit = vrfList.find(vrfName);
    if (vit == vrfList.end())
    {
        std::ostringstream oss;
        oss << "\r\n%VRF " << vrfName << " does not exist or is not enabled for IPv6";
        ctx.terminal.controller.print(oss.str());
        return false;
    }

    bool result = true;
    bool exists = false;

    // Verify the selected system doesnt overlap others
    namedList.withRead([&](const config::DefType<typename decltype(namedList)::Field>::type& list) {
        for (const auto& [system, vrf] : list)
        {
            if (vrf == vrfName)
            {
                if (system != as)
                {
                    std::ostringstream oss;
                    oss << "\r\nChanging from AS(" << system
                        << ") to AS(" << as
                        << ") is not allowed";
                    ctx.terminal.controller.print(oss.str());
                    result = false;
                    return;
                }
                else
                {
                    exists = true;
                    return;
                }
            }
        }
    });

    if (!result) return false;

    // Verify selected system is not classic if new
    auto eigrpList = vit->second->get<config::Vrf::ROUTER_EIGRP_V6>();
    if (!exists)
    {
        if (auto it = eigrpList.find(as); it != eigrpList.end())
            if (!it->second->get<config::Eigrp::IS_NAMED>().load())
            {
                std::ostringstream oss;
                oss << "\r\n%ERROR: AS(" << as << ") used by classic router";
                ctx.terminal.controller.print(oss.str());
                return false;
            }
    }
    return ctx.terminal.changeMode<CliMode::RouterEigrpAddressFamilyV6>(
        eigrpList.emplaceBack(as)
    );
}

bool RouterEigrpNamed_Exit_Handler(EIGRP_NAMED_PARAMS)
{
    UNUSED(segs);
    return ctx.terminal.popMode();
}

#define ROUTER_EIGRP_NAMED_LIST(X, Y) \
    X(Y, (COMMAND, AddressFamilyIPv4, "address-family"_tok, "ipv4"_tok)) \
    X(Y, (COMMAND, AddressFamilyIPv6, "address-family"_tok, "ipv6"_tok)) \
    X(Y, (COMMAND, Exit, "exit"_tok))

/**
 * @brief Parser for EIGRP named mode (MD5-era) configuration commands.
 * @ingroup CLI_MODE_PARSERS
 *
 * Aggregates address-family entry, logging, metrics, topology access,
 * and named-mode-specific settings.
 */
DEFINE_CMD_MODE(RouterEigrpNamed, config::EigrpNamedRegistry, ROUTER_EIGRP_NAMED_LIST);
}

#undef EIGRP_NAMED_PARAMS
