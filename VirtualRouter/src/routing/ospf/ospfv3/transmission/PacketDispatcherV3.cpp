// v3PacketDispatcher

#include <VirtualRouter.h>

#include "PacketDispatcherV3.h"
#include "ospf/neighbor/Neighbor.h"
#include "ospf/OspfProcess.h"
#include "packet/headers/embedded/ospf/Ospfv3DBDHeader.hpp"
#include "processing/PacketBuilder.hpp"
#include "infrastructure/IPPacket.h"

namespace routing::ospf
{
PacketDispatcherV3::PacketDispatcherV3(OspfInterfaceBase& iface)
    : PacketDispatcher(iface)
{}

void PacketDispatcherV3::transmit(processing::PacketBuilder& pkt, const types::IPAddress* dest)
{
    auto* interface = iface.getTransmitInterface();
    if (!interface) return;

    types::IPAddress destination;
    if (!dest)
    {
        // Virtual links never send multicast (getIsMulticast() == false), so
        // this branch is only reached for OspfInterface.
        if (static_cast<OspfInterface&>(iface).getIsDr())
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
        getTmgr().startDbdRetransmissionTimer(neighbor);

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
        getTmgr().startDbdRetransmissionTimer(neighbor);
    }
    return true;
}

void PacketDispatcherV3::onDbdRetransmissionTimer(Neighbor& nbr)
{
    auto* interface = iface.getTransmitInterface();
    if (!interface) return;

    processing::PacketBuilder retransmissionPacket(interface);
    af == types::AddressFamily::IPv4
        ? infrastructure::ippacket::reserveIpv4(retransmissionPacket)
        : infrastructure::ippacket::reserveIpv6(retransmissionPacket);
    auto* hdr = retransmissionPacket.addHeader(nbr.getRtr().dbdPacket.packet, packet::HeaderType::OSPFV3);
    if (!hdr) return;

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
