// PacketDispatcher.cpp

#include "PacketDispatcher.h"
#include <OspfInterface.h>
#include <OspfPacket.hpp>
#include <OspfInterface.h>
#include <OspfProcess.h>
#include <OspfNeighbor.h>
#include <OspfFletcher.hpp>

#include <IPPacket.h>
#include <PacketBuilder.hpp>

#define SUPPORT_RESYNC false

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
        .hopLimit = 1,
        .protocolType = IP_OSPF
    };

    af == AddressFamily::IPv4
        ? Protocol::IPPacket::buildIpv4(build)
        : Protocol::IPPacket::buildIpv6(build);
}

uint16_t PacketDispatcher::calculateAge(bool floodReduction, const LsaRecord& record)
{
    // Extract DoNotAge from LS age
    bool dna = (record.header.age & 0x8000) != 0;
    uint32_t age = record.header.age & 0x7FFF;

    if (!dna)
    {
        uint16_t delta = static_cast<uint16_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - record.lastRefreshTime
            ).count()
        );
        age = (record.header.age & 0x7FFF) + delta;
    }

    age += iface.getConfigs().get<Config::OspfInterface::TRANSMIT_DELAY>().load();

    if (age > OSPF_MAX_AGE) age = OSPF_MAX_AGE;

    bool dnaOut = age != 3600 && floodReduction;

    return static_cast<uint16_t>(age) | (dnaOut ? 0x8000 : 0);
}

bool PacketDispatcher::retransmitLsu(Neighbor& nbr)
{
    return sendLSUpdate(&nbr, nbr.getRtr().getLsu());
}

bool PacketDispatcher::retransmitLsr(Neighbor& nbr)
{
    return sendLSRequest(nbr, nbr.getRtr().getLsr());
}

size_t PacketDispatcher::addLinkLocalExtension(uint8_t* buf, bool restart)
{
    writeU32(buf, 0x00000000); // Checksum and size not calculated yet
    writeU16(buf + 4, 0x0001); // Ext TLV type
    writeU16(buf + 6, 0x0004); // Ext TLV size

    uint32_t options = 0x0000;
    if (SUPPORT_RESYNC)
        options |= 0x0001; // Resync flag
    if (restart)
        options |= 0x0002; // Restart signal
    
    writeU32(buf + 8, options);

    return 12;
}

void PacketDispatcher::addLinkLocalChecksum(uint8_t* buf)
{
    writeU16(buf + 2, 0x0003); // 12 (default)
    ChecksumFletcher check;
    check.addBytes(buf, 12);
    writeU16(buf, check.finalize());
}
}
