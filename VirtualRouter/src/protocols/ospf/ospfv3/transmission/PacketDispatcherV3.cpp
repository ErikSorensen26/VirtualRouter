// v3PacketDispatcher

#include <PacketDispatcherV3.h>
#include <OspfNeighbor.h>
#include <Ospfv3DBDHeader.hpp>
#include <PacketBuilder.hpp>
#include <OspfArea.h>
#include <IPPacket.h>
#include <OspfProcess.h>
#include <VirtualRouter.h>

#include <OspfInterfaceRegistry.hpp>

namespace OSPF
{
PacketDispatcherV3::PacketDispatcherV3(OspfInterface& iface, Config::Reference<Config::OspfInterfaceBaseRegistry>& cfgs)
    : PacketDispatcher(iface),
    baseConfigs(cfgs),
    configs([&cfgs, &iface]() {
        auto& registry = iface.getProcess().routingInstance->getRegistry();
        auto& processConfigs = cfgs->get<Config::OspfInterfaceBase::PROCESS_CONFIGS>();
        uint32_t procId = iface.getProcess().getProcId();
        auto key = Config::generateOspfAfInterfaceKey(
            iface.interfaceId, procId, AddressFamily::NONE, iface.getProcess().isV3);
        auto afBase = registry.emplaceBack(processConfigs, procId, key);
        auto base = registry.emplace(afBase->get<Config::OspfInterfaceAddressFamily::BASE>(), cfgs->get<Config::OspfInterfaceBase::BASE>().local(), key);
        auto af = iface.getProcess().getAF();

        auto afKey = Config::generateOspfAfInterfaceKey(iface.interfaceId, procId, af, iface.getProcess().isV3);
        auto buh = registry.emplace(afBase->get<Config::OspfInterfaceAddressFamily::IPV4>(), afKey);
        return af == AddressFamily::IPv4
            ? registry.emplace(afBase->get<Config::OspfInterfaceAddressFamily::IPV4>(), base, afKey)
            : registry.emplace(afBase->get<Config::OspfInterfaceAddressFamily::IPV6>(), base, afKey);
    }())
{}

void PacketDispatcherV3::transmit(PacketBuilder& pkt, const uint8_t* dest)
{
    auto* interface = &iface.getIface();

    const uint8_t* destination = dest;
    if (!dest)
    {
        if (iface.isDr.load(std::memory_order_relaxed))
            destination = OSPFV3_ALL_SPF_ROUTERS;
        else
            destination = OSPFV3_ALL_D_ROUTERS;
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

bool PacketDispatcherV3::setupDbd(Neighbor& neighbor, Ospfv3Header& pkt)
{
    Retransmission& rtr = neighbor.getRtr();
    if (rtr.getDbdActive())
        iface.getTimers().startDbdRetransmissionTimer(neighbor);

    Ospfv3DBDHeader dbd;
    dbd.setBuffer(pkt.getTrailData());
    uint32_t seqNum = dbd.getSeqNum();
    if (seqNum == 0)
        return false;

    {
        rtr.dbdPacket = UnicastPacket{
            pkt.buffer,
            Ospfv3Header::fixedSize + pkt.getTrail().size(),
            neighbor.ipAddress
        };
        rtr.dbdPacket.sequence = seqNum;
        iface.getTimers().startDbdRetransmissionTimer(neighbor);
    }
    return true;
}

void PacketDispatcherV3::onDbdRetransmissionTimer(Neighbor& nbr)
{
    PacketBuilder retransmissionPacket(&iface.getIface());
    af == AddressFamily::IPv4
        ? Protocol::IPPacket::reserveIpv4(retransmissionPacket)
        : Protocol::IPPacket::reserveIpv6(retransmissionPacket);
    auto* hdr = retransmissionPacket.addHeader(nbr.getRtr().dbdPacket.packet, HeaderType::OSPFV3);
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
