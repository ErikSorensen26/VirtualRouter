// v2PacketDispatcher

#include "PacketDispatcherV2.h"
#include "ospf/neighbor/Neighbor.h"
#include "ospf/OspfProcess.h"
#include "packet/headers/embedded/ospf/Ospfv2DBDHeader.hpp"
#include "processing/PacketBuilder.hpp"
#include "infrastructure/IPPacket.h"

namespace routing::ospf
{
PacketDispatcherV2::PacketDispatcherV2(OspfInterfaceBase& iface)
    : PacketDispatcher(iface)
{}

void PacketDispatcherV2::transmit(processing::PacketBuilder& pkt, const types::IPAddress* dest)
{
    auto* interface = iface.getTransmitInterface();
    if (!interface) return;

    types::IPAddress destination;
    if (!dest)
    {
        // Virtual links never send multicast (getIsMulticast() == false)
        if (static_cast<OspfInterface&>(iface).getIsDr())
            destination = types::IPAddress(OSPFV2_ALL_SPF_ROUTERS, types::AddressFamily::IPv4);
        else
            destination = types::IPAddress(OSPFV2_ALL_D_ROUTERS, types::AddressFamily::IPv4);
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

    infrastructure::ippacket::buildIpv4(build);
}

bool PacketDispatcherV2::setupDbd(Neighbor& neighbor, packet::Ospfv2Header& pkt)
{
    Retransmission& rtr = neighbor.getRtr();
    if (rtr.getDbdActive())
        getTmgr().startDbdRetransmissionTimer(neighbor);

    packet::Ospfv2DBDHeader dbd;
    dbd.setBuffer(pkt.getTrailData());
    uint32_t seqNum = dbd.getSequence();
    if (seqNum == 0)
        return false;

    {
        rtr.dbdPacket = UnicastPacket{
            pkt.buffer,
            packet::Ospfv2Header::fixedSize + pkt.getTrail().size(),
            neighbor.ipAddress
        };
        rtr.dbdPacket.sequence = seqNum;
        getTmgr().startDbdRetransmissionTimer(neighbor);
    }
    return true;
}

void PacketDispatcherV2::onDbdRetransmissionTimer(Neighbor& nbr)
{
    auto* interface = iface.getTransmitInterface();
    if (!interface) return;

    processing::PacketBuilder retransmissionPacket(interface);
    infrastructure::ippacket::reserveIpv4(retransmissionPacket);
    auto* hdr = retransmissionPacket.addHeader(nbr.getRtr().dbdPacket.packet, packet::HeaderType::OSPFV2);
    if (!hdr) return;

    infrastructure::ippacket::BuildIP build = {
        .iface = interface,
        .packetInfo = retransmissionPacket,
        .destIp = nbr.ipAddress,
        .protocolType = IP_OSPF
    };

    infrastructure::ippacket::buildIpv4(build);
}
} // namespace routing
