// RouterEigrpNamedCommands.cpp

#include "RouterEigrpNamedCommands.h"

#include <sstream>
#include <Global.h>
#include <VirtualRouter.h>
#include "cli/runtime/CliSession.h"
#include <cli/parser/CommandUtils.hpp>

#define GLOBAL_PARAMS DEFINE_PARAMS(config::EigrpNamedRegistry)

namespace cli
{
bool RouterEigrpNamed_AddressFamilyIPv4_Handler(GLOBAL_PARAMS)
{
    auto& global = ctx.configs.resolveParent<config::GlobalRegistry>();
    auto& namedList = ctx.configs.get<config::EigrpNamed::NAMED_INSTANCES_V4>();

    std::string vrfName = "default";
    uint16_t as;

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

    auto& vrfList = global.get<config::Global::VRF_CONFIGS>();
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
    namedList.withRead([&](const config::DefType<decltype(namedList)>::type& list) {
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
    auto& eigrpList = vit->second.get<config::Vrf::ROUTER_EIGRP_V4>();
    if (!exists)
    {
        if (auto it = eigrpList.find(as); it != eigrpList.end())
            if (!it->second.get<config::Eigrp::IS_NAMED>().load())
            {
                std::ostringstream oss;
                oss << "\r\n%ERROR: AS(" << as << ") used by classic router";
                ctx.terminal.controller.print(oss.str());
                return false;
            }
    }
    ctx.terminal.changeMode<CliMode::RouterEigrpAddressFamilyV4>(
        eigrpList.emplaceBack(as)
    );
    return true;
}

bool RouterEigrpNamed_AddressFamilyIPv6_Handler(GLOBAL_PARAMS)
{
    auto& global = ctx.configs.resolveParent<config::GlobalRegistry>();
    auto& namedList = ctx.configs.get<config::EigrpNamed::NAMED_INSTANCES_V6>();

    std::string vrfName = "default";
    uint16_t as;

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

    auto& vrfList = global.get<config::Global::VRF_CONFIGS>();
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
    namedList.withRead([&](const config::DefType<decltype(namedList)>::type& list) {
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
    auto& eigrpList = vit->second.get<config::Vrf::ROUTER_EIGRP_V6>();
    if (!exists)
    {
        if (auto it = eigrpList.find(as); it != eigrpList.end())
            if (!it->second.get<config::Eigrp::IS_NAMED>().load())
            {
                std::ostringstream oss;
                oss << "\r\n%ERROR: AS(" << as << ") used by classic router";
                ctx.terminal.controller.print(oss.str());
                return false;
            }
    }
    ctx.terminal.changeMode<CliMode::RouterEigrpAddressFamilyV6>(
        eigrpList.emplaceBack(as)
    );
    return true;
}

bool RouterEigrpNamed_Exit_Handler(GLOBAL_PARAMS)
{
    UNUSED(segs);
    return ctx.terminal.exitMode<CliMode::GlobalConfiguration, config::GlobalRegistry>(ctx.configs);
}
}

#undef EIGRP_PARAMS
