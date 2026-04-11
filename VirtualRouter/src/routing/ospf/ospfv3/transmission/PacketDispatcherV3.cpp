// v3PacketDispatcher

#include <VirtualRouter.h>

#include "PacketDispatcherV3.h"
#include "ospf/neighbor/Neighbor.h"
#include "ospf/area/Area.h"
#include "packet/headers/embedded/ospf/Ospfv3DBDHeader.hpp"
#include "processing/PacketBuilder.hpp"
#include "infrastructure/IPPacket.h"
#include "configs/registry/interface/InterfaceRegistry.h"

namespace routing::ospf
{
PacketDispatcherV3::PacketDispatcherV3(OspfInterface& iface)
    : PacketDispatcher(iface)
{}

config::OspfInterfaceBaseRegistry& PacketDispatcherV3::getConfigs()
{
    auto& configs = iface.getIface().configs.getConfigs();
    if (iface.getProcess().isV3)
    {
        return configs.get<config::Interface::OSPFV3>().emplaceBack(iface.getProcess().getProcId());
    }
    else 
    {
        return configs.get<config::Interface::IPV6_OSPF>().get();
    }
}

void PacketDispatcherV3::transmit(processing::PacketBuilder& pkt, const types::IPAddress* dest)
{
    auto* interface = &iface.getIface();

    types::IPAddress destination;
    if (!dest)
    {
        if (iface.isDr.load(std::memory_order_relaxed))
            destination = types::IPAddress(OSPFV3_ALL_SPF_ROUTERS, types::AddressFamily::IPv6);
        else
            destination = types::IPAddress(OSPFV3_ALL_D_ROUTERS, types::AddressFamily::IPv6);
    }
    else
    {
        destination = *dest;
    }

    infrastructure::ippacket::BuildIP build = {
        .iface = interface,
        .packetInfo = pkt,
        .destIp = destination,
        .hopLimit = 1,
        .protocolType = IP_OSPF
    };

    af == types::AddressFamily::IPv4
        ? infrastructure::ippacket::buildIpv4(build)
        : infrastructure::ippacket::buildIpv6(build);
}

bool PacketDispatcherV3::setupDbd(Neighbor& neighbor, packet::Ospfv3Header& pkt)
{
    Retransmission& rtr = neighbor.getRtr();
    if (rtr.getDbdActive())
        iface.getTimers().startDbdRetransmissionTimer(neighbor);

    packet::Ospfv3DBDHeader dbd;
    dbd.setBuffer(pkt.getTrailData());
    uint32_t seqNum = dbd.getSeqNum();
    if (seqNum == 0)
        return false;

    {
        rtr.dbdPacket = UnicastPacket{
            pkt.buffer,
            packet::Ospfv3Header::fixedSize + pkt.getTrail().size(),
            neighbor.ipAddress
        };
        rtr.dbdPacket.sequence = seqNum;
        iface.getTimers().startDbdRetransmissionTimer(neighbor);
    }
    return true;
}

void PacketDispatcherV3::onDbdRetransmissionTimer(Neighbor& nbr)
{
    processing::PacketBuilder retransmissionPacket(&iface.getIface());
    af == types::AddressFamily::IPv4
        ? infrastructure::ippacket::reserveIpv4(retransmissionPacket)
        : infrastructure::ippacket::reserveIpv6(retransmissionPacket);
    auto* hdr = retransmissionPacket.addHeader(nbr.getRtr().dbdPacket.packet, packet::HeaderType::OSPFV3);
    if (!hdr) return;

    auto* interface = &iface.getIface();
    infrastructure::ippacket::BuildIP build = {
        .iface = interface,
        .packetInfo = retransmissionPacket,
        .destIp = nbr.ipAddress,
        .protocolType = IP_OSPF
    };

    af == types::AddressFamily::IPv4
        ? infrastructure::ippacket::buildIpv4(build)
        : infrastructure::ippacket::buildIpv6(build);
}
} // namespace routing
