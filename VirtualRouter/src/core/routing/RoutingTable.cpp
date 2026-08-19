// RoutingTable.cpp

#include "RoutingTable.h"
#include "interface/Interface.h"
#include "interface/InterfaceManager.h"

namespace core
{
RoutingTable::RoutingTable(ControlScheduler& cs, interface::InterfaceManager& ifaceMgr)
    : rib4(cs.create()), rib6(cs.create())
{
    //uint32_t ipv4PrimaryId, ipv4SecondaryId, ipv6Id, ipv6LlId;

    auto applyIpv4 = [](void* ctx, interface::Interface& iface, const types::IPPrefix& prefix)
        { static_cast<RoutingTable*>(ctx)->applyConnectedRoute<types::IPv4Prefix>(iface, static_cast<types::IPv4Prefix>(prefix)); };
    auto delIpv4 = [](void* ctx, interface::Interface&, const types::IPPrefix& prefix)
        { static_cast<RoutingTable*>(ctx)->removeConnectedRoute<types::IPv4Prefix>(static_cast<types::IPv4Prefix>(prefix)); };
    auto applyIpv6 = [](void* ctx, interface::Interface& iface, const types::IPPrefix& prefix)
        { static_cast<RoutingTable*>(ctx)->applyConnectedRoute<types::IPv6Prefix>(iface, static_cast<types::IPv6Prefix>(prefix)); };
    auto delIpv6 = [](void* ctx, interface::Interface&, const types::IPPrefix& prefix)
        { static_cast<RoutingTable*>(ctx)->removeConnectedRoute<types::IPv6Prefix>(static_cast<types::IPv6Prefix>(prefix)); };

    ipv4PrimaryReadyId = ifaceMgr.subscribe(interface::IPEvent::IPV4_READY, this, applyIpv4);
    ipv4SecondaryReadyId = ifaceMgr.subscribe(interface::IPEvent::IPV4_SECONDARY_READY, this, applyIpv4);
    ipv4PrimaryDelId = ifaceMgr.subscribe(interface::IPEvent::IPV4_DEL, this, delIpv4);
    ipv4SecondaryDelId = ifaceMgr.subscribe(interface::IPEvent::IPV4_DEL, this, delIpv4);

    ipv6ReadyId = ifaceMgr.subscribe(interface::IPEvent::IPV6_READY, this, applyIpv6);
    ipv6LlReadyId = ifaceMgr.subscribe(interface::IPEvent::IPV6_READY, this, applyIpv6);
    ipv6DelId = ifaceMgr.subscribe(interface::IPEvent::IPV6_DEL, this, delIpv6);
    ipv6LlDelId = ifaceMgr.subscribe(interface::IPEvent::IPV6_DEL, this, delIpv6);
}

template <typename Prefix>
void RoutingTable::applyConnectedRoute(interface::Interface& iface, Prefix network)
{
    network.addPrefixLen(network.prefixLength);

    auto* entry = new core::RibEntry<decltype(network.addr)>();
    entry->prefix = network.addr;
    entry->length = network.prefixLength;
    entry->source = core::RouteSource::CONNECTED;
    entry->processId = 0;
    entry->adminDistance = 0;
    entry->metric = 0;
    entry->addNextHopInterface(iface.configs.key.getId());
    addRoute(entry);
}

template <typename Prefix>
void RoutingTable::removeConnectedRoute(Prefix network)
{
    network.addPrefixLen(network.prefixLength);

    removeRoute(network.addr, network.prefixLength, core::RouteSource::CONNECTED, 0);
}

template void RoutingTable::applyConnectedRoute<types::IPv4Prefix>(interface::Interface&, types::IPv4Prefix);
template void RoutingTable::applyConnectedRoute<types::IPv6Prefix>(interface::Interface&, types::IPv6Prefix);

template void RoutingTable::removeConnectedRoute<types::IPv4Prefix>(types::IPv4Prefix);
template void RoutingTable::removeConnectedRoute<types::IPv6Prefix>(types::IPv6Prefix);
}
