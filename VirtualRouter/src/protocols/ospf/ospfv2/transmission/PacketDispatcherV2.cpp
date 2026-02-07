// v2PacketDispatcher

#include <PacketDispatcherV2.h>
#include <OspfNeighbor.h>
#include <Ospfv2DBDHeader.hpp>
#include <PacketBuilder.hpp>
#include <OspfArea.h>
#include <IPPacket.h>

namespace OSPF
{
PacketDispatcherV2::PacketDispatcherV2(OspfInterface& iface, Config::Reference<Config::OspfInterfaceBaseRegistry>& cfgs)
    : PacketDispatcher(iface),
      baseConfigs(cfgs),
      configs(baseConfigs->get<Config::OspfInterfaceBase::BASE>().get())
{}

void PacketDispatcherV2::transmit(PacketBuilder& pkt, const uint8_t* dest)
{
    auto* interface = &iface.getIface();

    const uint8_t* destination = dest;
    if (!dest)
    {
        if (iface.isDr.load(std::memory_order_relaxed))
            destination = OSPFV2_ALL_SPF_ROUTERS;
        else
            destination = OSPFV2_ALL_D_ROUTERS;
    }

    Protocol::IPPacket::BuildIP build = {
        .iface = interface,
        .packetInfo = pkt,
        .destIp = destination,
        .hopLimit = 1,
        .protocolType = IP_OSPF
    };

    af == AddressFamily::IPv4
        ? Protocol::IPPacket::buildIpv4(build)
        : Protocol::IPPacket::buildIpv6(build);
}

bool PacketDispatcherV2::setupDbd(Neighbor& neighbor, Ospfv2Header& pkt)
{
    Retransmission& rtr = neighbor.getRtr();
    if (rtr.getDbdActive())
        iface.getTimers().startDbdRetransmissionTimer(neighbor);

    Ospfv2DBDHeader dbd;
    dbd.setBuffer(pkt.getTrailData());
    uint32_t seqNum = dbd.getSequence();
    if (seqNum == 0)
        return false;

    {
        rtr.dbdPacket = UnicastPacket{
            pkt.buffer,
            Ospfv2Header::fixedSize + pkt.getTrail().size(),
            neighbor.ipAddress
        };
        rtr.dbdPacket.sequence = seqNum;
        iface.getTimers().startDbdRetransmissionTimer(neighbor);
    }
    return true;
}

void PacketDispatcherV2::onDbdRetransmissionTimer(Neighbor& nbr)
{
    PacketBuilder retransmissionPacket(&iface.getIface());
    af == AddressFamily::IPv4
        ? Protocol::IPPacket::reserveIpv4(retransmissionPacket)
        : Protocol::IPPacket::reserveIpv6(retransmissionPacket);
    auto* hdr = retransmissionPacket.addHeader(nbr.getRtr().dbdPacket.packet, HeaderType::OSPFV2);
    if (!hdr) return;

    auto* interface = &iface.getIface();
    Protocol::IPPacket::BuildIP build = {
        .iface = interface,
        .packetInfo = retransmissionPacket,
        .destIp = nbr.ipAddress.raw,
        .protocolType = IP_OSPF
    };

    af == AddressFamily::IPv4
        ? Protocol::IPPacket::buildIpv4(build)
        : Protocol::IPPacket::buildIpv6(build);
}
}
