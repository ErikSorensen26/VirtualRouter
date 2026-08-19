// InterfaceRegistry.cpp

#include <VirtualRouter.h>
#include "InterfaceRegistry.h"
#include "interface/Interface.h"
#include "ospf/OspfProcess.h"
#include "ospf/interface/InterfaceId.hpp"

namespace config
{
DEFINE_CONFIG_APPLIER(Interface, OSPFV3, ctx, reg, procId)
{
    interface::Interface* iface = Context::cast<interface::Interface*>(ctx);
    if (!iface)
        return;

    routing::ospf::OspfInterfaceId id{iface->configs.key.getId(), 0};
    core::VirtualRouter* vrf = iface->getVRF();

    for (auto af : {types::AddressFamily::IPv4, types::AddressFamily::IPv6})
    {
        routing::ospf::OspfProcess* proc = vrf->getOspfv3(procId, af);
        if (!proc)
            continue;

        if (reg)
        {
            const config::OspfGlobalInterfaceRegistry& afCfg =
                af == types::AddressFamily::IPv4 ? reg->get<config::OspfInterfaceAf::IPV4>().get()
                                                  : reg->get<config::OspfInterfaceAf::IPV6>().get();
            proc->getIfaceMgr().createInterface(*iface, id, afCfg);
        }
        else
        {
            proc->getIfaceMgr().removeInterface(id);
        }
    }
}

DEFINE_CONFIG_APPLIER(Interface, IP_ADDRESS, i, ip)
{
    if (interface::Interface* iface = Context::cast<interface::Interface*>(i); iface)
    {
        if (ip)
            iface->setIPv4(*ip);
        else
            iface->removeIPv4(nullptr);
    }
}

DEFINE_CONFIG_APPLIER(Interface, IP_ADDRESS_SECONDARY, i, ip, add)
{
    if (interface::Interface* iface = Context::cast<interface::Interface*>(i); iface)
    {
        if (add)
            iface->setIPv4(ip.prefix(), true);
        else
            iface->removeIPv4(&ip.prefix());
    }
}

DEFINE_CONFIG_APPLIER(Interface, IP_ADDRESS_DHCP, i, dhcp)
{
    if (interface::Interface* iface = Context::cast<interface::Interface*>(i); iface)
        iface->configs.syncPrimaryIP(); // TODO
}

DEFINE_CONFIG_APPLIER(Interface, SHUTDOWN, i, shut)
{
    if (interface::Interface* iface = Context::cast<interface::Interface*>(i); iface)
        iface->shutdown(shut);
}
}

