// PacketDispatcher.cpp

#include "PacketDispatcher.h"
#include <OspfInterface.h>
#include <OspfPacket.hpp>
#include <OspfInterface.h>
#include <OspfProcess.h>
#include <OspfNeighbor.h>

#include <IPPacket.h>
#include <PacketBuilder.hpp>

namespace OSPF
{
PacketDispatcher::PacketDispatcher(OspfInterface& iface)
    : iface(iface), ntable(iface.getNTable()), af(iface.process.getAF()) {}

void PacketDispatcher::transmit(PacketBuilder& pkt, const uint8_t* dest)
{
    auto* interface = &iface.getIface();
    Protocol::IPPacket::BuildIP build = {
        .iface = interface,
        .packetInfo = pkt,
        .destIp = dest,
        .protocolType = IP_OSPF
    };

    af == AddressFamily::IPv4
        ? Protocol::IPPacket::buildIpv4(build)
        : Protocol::IPPacket::buildIpv6(build);
}

bool PacketDispatcher::retransmitLsu(Neighbor& nbr)
{
    return sendLSUpdate(&nbr, nbr.getRtr().getLsu());
}

bool PacketDispatcher::retransmitLsr(Neighbor& nbr)
{
    return sendLSRequest(nbr, nbr.getRtr().getLsr());
}
}
