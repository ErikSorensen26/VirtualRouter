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

bool PacketDispatcherV3::setupDbd(Neighbor& neighbor, Ospfv3Header& pkt)
{
    Retransmission& rtr = neighbor.getRtr();
    Ospfv3DBDHeader dbd;
    dbd.setBuffer(pkt.getTrailData());
    uint32_t seqNum = dbd.getSeqNum();
    if (seqNum == 0)
        return false;

    {
        std::lock_guard<std::mutex> lock(rtr.reliableMtx);
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

void PacketDispatcherV3::setupLsu(Neighbor& neighbor, std::vector<LsaRecordRef>& records)
{
    Retransmission& rtr = neighbor.getRtr();
    std::lock_guard<std::mutex> lock(rtr.reliableMtx);
    bool active = rtr.getLsuActive();
    for (auto& lsa : records)
    {
        rtr.addLsu(lsa);
    }
    if (active)
        iface.getTimers().startLsuRetransmissionTimer(neighbor);
}

void PacketDispatcherV3::setupLsr(Neighbor& neighbor, const std::vector<LsaKey>& keys)
{
    Retransmission& rtr = neighbor.getRtr();
    std::lock_guard<std::mutex> lock(rtr.reliableMtx);
    bool active = rtr.getLsrActive();
    for (auto& lsa : keys)
    {
        rtr.addLsr(lsa);
    }
    if (active)
        iface.getTimers().startLsrRetransmissionTimer(neighbor);
}

void PacketDispatcherV3::setupMulticastLsu(std::vector<LsaRecordRef>& keys)
{
    std::shared_lock<std::shared_mutex> lock(ntable.mu);
    for (auto& [_, nbr] : ntable.neighbors)
    {
        setupLsu(nbr, keys);
    }
}

void PacketDispatcherV3::retransmitDbd(Neighbor& nbr)
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
