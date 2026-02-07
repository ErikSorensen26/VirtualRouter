// GlobalCommands.cpp

#include "GlobalCommands.h"
#include <shared_mutex>
#include <Global.h>
#include <VirtualRouter.h>
#include <Arp.h>
#include <InterfaceType.hpp>
#include <CliSession.h>
#include <PrivilegedExecContext.hpp>
#include <InterfaceContext.hpp>
#include <EigrpContext.hpp>
#include <HardwareManager.h>

namespace Cli
{
bool Global_Arp_Handler(GLOBAL_PARAMS)
{
    size_t offset = 0;
    std::string vrfName = "default";
    if (args[0] == "vrf")
    {
        vrfName = args[3];
        offset = 2;
    }

    std::shared_lock<std::shared_mutex> lock(ctx.global.configs.arp.neighborMutex);
    if (ctx.negate && ctx.global.configs.arp.neighbors.count(vrfName) && ctx.global.configs.arp.neighbors[vrfName].count(Functions::addressToIntv4(args[offset + 1])))
    {
        GlobalConfigs::Arp::Neighbor entry = ctx.global.configs.arp.neighbors[vrfName][Functions::addressToIntv4(args[offset + 1])];
        auto* vrf = ctx.global.getRoutingInstance(vrfName, AddressFamily::IPv4);
        uint32_t addr = Functions::addressToIntv4(args[offset + 1]);
        if (auto iface = vrf ? ctx.vrf.getInterface(entry.interface) : nullptr)
        {
            if (iface->arp)
            {
                iface->arp->removeArpEntry(addr);
            }
        }
        ctx.global.configs.arp.neighbors[vrfName].erase(addr);
    }
    else
    {
        uint32_t ifaceKey = calculateInterfaceKey(getInterfaceType(args[offset + 3]), std::stof(args[offset + 4]));
        GlobalConfigs::Arp::Neighbor entry;
        Functions::macToInt(args[offset + 2]);
        entry.interface = ifaceKey;
        entry.proxy = args.size() == 5;

        ctx.global.configs.arp.neighbors[vrfName].emplace(
            Functions::addressToIntv4(args[offset + 1]), entry
        );
        auto* vrf = ctx.global.getRoutingInstance(vrfName, AddressFamily::IPv4);
        if (auto iface = vrf ? vrf->getInterface(ifaceKey) : nullptr)
        {
            if (iface->arp)
            {
                iface->arp->addArpEntry(Functions::addressToIntv4(args[0]), entry.mac, entry.proxy, true);
            }
        }
    }
    return true;
}

bool Global_Exit_Handler(GLOBAL_PARAMS)
{
    UNUSED(args);
    ctx.terminal.exitMode<CliMode::PrivilegedExec>();
    return true;
}

bool Global_SetHostname_Handler(GLOBAL_PARAMS)
{
    if (!ctx.negate)
    {
        ctx.global.setHostname(args[0]);
    }
    else
    {
        ctx.global.setHostname("Router");
    }
    return true;
}

bool Global_End_Handler(GLOBAL_PARAMS)
{
    UNUSED(args);
    ctx.terminal.exitMode<CliMode::PrivilegedExec>();
    return true;
}

bool Global_IP_Handler(GLOBAL_PARAMS)
{
    return GlobalIPCommands::execute(ctx, args);
}

bool Global_IPv6_Handler(GLOBAL_PARAMS)
{
    return GlobalIPv6Commands::execute(ctx, args);
}

bool Global_Interface_Handler(GLOBAL_PARAMS)
{
    ctx.terminal.isList = true;
    std::string type = args[0];
    ctx.terminal.interfaceID = std::stof(args[1]);
    InterfaceType interfaceType = getInterfaceType(type);
    uint32_t hwIface;
    int id = static_cast<int>(std::floor(ctx.terminal.interfaceID));
    uint32_t key = calculateInterfaceKey(interfaceType, ctx.terminal.interfaceID);
    if (!ctx.global.getInterface(key))
    {
        if (ctx.negate)
        {
            ctx.global.removeInterface(key);
            ctx.vrf.removeInterface(key);
        }
        else
        {
            hwIface = ctx.terminal.engine.hwManager->getInterface(interfaceType, id);
            const HwIfaceInfo* info = ctx.terminal.engine.hwManager->getHwInfo(hwIface);
            if (!info) return false;
            const HwIfaceInfo& hwInfo = *info;
            ctx.global.addInterface(interfaceType, hwInfo, ctx.terminal.interfaceID, ctx.terminal.isDebugModeEnabled);
            ctx.vrf.addInterface(ctx.global.getInterface(key), key);
        }
    }
    ctx.terminal.changeMode<CliMode::Interface>(*ctx.global.getInterface(key));
    return true;
}


bool Global_RouterEIGRP_Handler(GLOBAL_PARAMS)
{
    ctx.terminal.isList = true;
    std::string id = args[0];

    if (Functions::isNumber(id))
    {
        uint16_t asNum = static_cast<uint16_t>(std::stoi(id));
        Eigrp::EigrpAutonomousSystem* as = ctx.vrf.getEigrpAutonomousSystem(asNum);
        if (!ctx.negate)
        {
            if (as)
            {
                if (as->ipv4Named)
                {
                    ctx.terminal.iConsole->print(std::string("\r\n%" + std::string(" ERROR: AS(" + id + ") used by named mode")));
                    return false; // AS used in named mode. }
                }
            }
            else
            {
                as = ctx.vrf.addEigrpAutonomousSystem(asNum);
            }
            if (!as->ipv4)
            {
                as->ipv4 = new Eigrp::Eigrp(asNum, AddressFamily::IPv4, ctx.global.getRoutingInstance("default"));
            }
            ctx.terminal.changeMode<CliMode::RouterEigrpClassicV4>(as->ipv4, nullptr, nullptr);
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
                        ctx.vrf.removeEigrpAutonomousSystem(asNum);
                    }
                }
            }
        }
    }
    else
    {
        if (!ctx.negate)
        {
            if (!ctx.vrf.getEigrpNamed(id))
            {
                ctx.vrf.addEigrpNamed(id);
            }
            ctx.terminal.changeMode<CliMode::RouterEigrpNamed>(nullptr, ctx.vrf.getEigrpNamed(id), nullptr);
        }
        else
        {
            ctx.vrf.removeEigrpNamed(id);
        }
    }
    return true;
}

bool Global_RouterOSPF_Handler(GLOBAL_PARAMS)
{
    return true;
}

bool Global_RouterBGP_Handler(GLOBAL_PARAMS)
{
    return true;
}

#undef ROUTER_CONFIG



}
