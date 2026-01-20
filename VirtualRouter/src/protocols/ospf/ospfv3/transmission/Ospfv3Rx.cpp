// Ospfv2Rx.cpp

#include "v3PacketDispatcher.h"
#include <OspfProcess.h>
#include <OspfInterface.h>

#include <Ospfv3HelloHeader.hpp>
#include <Ospfv3DBDHeader.hpp>
#include <Ospfv3LSAHeader.hpp>
#include <Ospfv3LSRHeader.hpp>
#include <OspfNeighbor.h>

#include <OspfArea.h>

namespace OSPF
{
void PacketDispatcher::handleIncoming(const Ospfv3Header& ospfHeader, const uint8_t* neighborIp, bool multicast)
{
    IPAddress neigIp(neighborIp, iface.process.getAF());
    uint32_t rid = ospfHeader.getRouterID();

    // Check passive
    if (iface.configs->isPassive.load(std::memory_order_relaxed))
        return;

    // Validate version
    if (ospfHeader.getVersion() != OSPFV3_VERSION)
        return;

    if (ospfHeader.getAreaID() != iface.areaId)
        return;

    // Validate size
    if (ospfHeader.getPacketLen() != Ospfv3Header::fixedSize /* + ospfHeader.trail.size() */)
        return;

    HeaderInfo info(ospfHeader, neigIp, rid);
    info.neighbor = iface.getNTable().lookup(rid);
    // TODO header stuff

    if (ospfHeader.getType() == OSPFV3_TYPE_HELLO)
    {
        if (iface.getNTable().isUnicast(rid) == multicast)
            return;
        processHello(info);
    }
    else if (info.neighbor)
    {
        switch (ospfHeader.getType())
        {
            case OSPFV3_TYPE_HELLO:
                processHello(info);
                break;
            case OSPFV3_TYPE_DATABASE_DESCRIPTION:
                processDBD(info);
                break;
            case OSPFV3_TYPE_LINK_STATE_ACK:
                processLSAck(info);
                break;
            case OSPFV3_TYPE_LINK_STATE_REQUEST:
                processLSRequest(info);
                break;
            case OSPFV3_TYPE_LINK_STATE_UPDATE:
                processLSUpdate(info);
                break;
        }
    }
}

bool PacketDispatcherV3::processOptions(uint32_t options)
{
    auto& flags = iface.getFlags();
    auto& areaFlags = iface.getArea().getFlags();

    if (flags.getDemandCircuits() && !InterfaceFlagManager::getDemandCircuits(options) &&
        !iface.configs.demandCircuitIgnore.load(std::memory_order_relaxed))
    {
        if (!isStatic)
            flags.setDemandCircuits(false);
        else return false;
    }
    if (areaFlags.getExternalRouting() != AreaFlagManager::getExternalRouting(options))
        return false;
    if (areaFlags.getNssa() != AreaFlagManager::getNssa(options))
        return false;

    if (areaFlags.getV6() != AreaFlagManager::getV6(options))
        return false;
    if (areaFlags.getRouterBit() != AreaFlagManager::getRouterBit(options))
        return false;
    if (areaFlags.getAddressFamilySupport() != AreaFlagManager::getAddressFamilySupport(options))
        return false;
    if (areaFlags.getLBit() != AreaFlagManager::getLBit(options))
        return false;
    return true;
}

void PacketDispatcher::processHello(PacketDispatcher::HeaderInfo& info)
{
    Ospfv3HelloHeader hdr;
    hdr.setBuffer(info.payload);

    info.offset += Ospfv3HelloHeader::fixedSize;
    if (info.offset > info.payloadSize)
        return;

    //TODO do hello stuff
}

void PacketDispatcher::processDBD(PacketDispatcher::HeaderInfo& info)
{
    Ospfv3DBDHeader hdr;
    hdr.setBuffer(info.payload);

    info.offset += Ospfv3DBDHeader::fixedSize;
    if (info.offset > info.payloadSize) 
        return;

    // Varify only 20 byte lsa blocks exist
    if ((info.payloadSize - info.offset) % 20 != 0)
        return;
    
    //TODO do dbd stuff

    // Process DB description
    while (info.offset < info.payloadSize)
    {
        Ospfv3LSAHeader lsaHdr;
        lsaHdr.setBuffer(info.payload + info.offset);

        info.offset += Ospfv3LSAHeader::fixedSize;
        if (info.offset > info.payloadSize) 
            return;

        LsaKey key(lsaHdr.getType(), lsaHdr.getLsId(), lsaHdr.getAdvRouter());

        LsaHeader lsa = {
            .sequence = lsaHdr.getSeqNumber(),
            .checksum = lsaHdr.getChecksum(),
            .length = lsaHdr.getLen(),
            .age = lsaHdr.getAge()
        };

        if (iface.area.compareLSASummary(lsa, key))
            info.neighbor->lsaDbd.push_back(key);
    }

    if (!hdr.getFlagM())
    {
        // TODO: Send request with lsas
    }
}

void PacketDispatcher::processLSRequest(PacketDispatcher::HeaderInfo& info)
{
    Ospfv3LSRHeader hdr;
    hdr.setBuffer(info.payload);

    info.offset += Ospfv3LSRHeader::fixedSize;
    if (info.offset > info.payloadSize) 
        return;

    // Varify only 12 byte lsa blocks exist
    if ((info.payloadSize - info.offset) % 12 != 0)
        return;
    
    //TODO do req stuff

    // Process request
    std::vector<LsaKey> keys;
    while (info.offset < info.payloadSize)
    {
        Ospfv3LSRHeader lsrHdr;
        lsrHdr.setBuffer(info.payload + info.offset);

        info.offset += Ospfv3LSAHeader::fixedSize;
        if (info.offset > info.payloadSize) 
            return;

        LsaKey key(static_cast<uint16_t>(lsrHdr.getType()), lsrHdr.getLsID(), lsrHdr.getAdvRouter());
        if (iface.area.lsdb().contains(key))
        {
            keys.push_back({
                static_cast<uint16_t>(lsrHdr.getType()),
                lsrHdr.getLsID(),
                lsrHdr.getAdvRouter()
            });
        }
    }

    // TODO: Send updates for requests
}

void PacketDispatcher::processLSUpdate(PacketDispatcher::HeaderInfo& info)
{
    if (info.payloadSize < 4)
        return;

    uint32_t lsuSize = readU32(info.payload);
    uint32_t routerId = iface.process.getRouterId();

    std::vector<OspfArea::Result> results;

    std::vector<LsaRecord*> acks;

    for (int i = 0; i < static_cast<int>(lsuSize); i++)
    {
        Ospfv3LSAHeader lsaHdr;
        lsaHdr.setBuffer(info.payload + info.offset);

        size_t off = info.offset;

        off += Ospfv3LSAHeader::fixedSize;
        if (off > info.payloadSize) 
            return;

        // TODO: Validate checksum
        bool checksumValid = false;

        bool selfOrigin = lsaHdr.getAdvRouter() == routerId;

        LsaKey key = {
            lsaHdr.getType(),
            lsaHdr.getLsId(),
            lsaHdr.getAdvRouter()
        };
        LsaHeader hdr = {
            lsaHdr.getSeqNumber(),
            lsaHdr.getChecksum(),
            lsaHdr.getLen(),
            lsaHdr.getAge()
        };

        OspfArea::IncomingLsaContext context{
            key,
            hdr,
            checksumValid,
            selfOrigin,
            iface.configs->key,
            true
        };

        uint16_t bodyLen = hdr.length - Ospfv3LSAHeader::fixedSize;
        auto body = buildLsaBody(static_cast<uint8_t>(key.lsaType), info.payload + off, bodyLen);
        info.offset += hdr.length;

        if (!body.has_value()) continue;

        auto result = iface.area.processLsa(context, body.value());

        if (result.decision.shouldAck)
            acks.push_back(result.record);

        results.push_back(result);
    }

    if (!acks.empty())
        sendLSAck(acks);

    iface.area.flood();
}

std::optional<LsaBody> PacketDispatcher::buildLsaBody(uint8_t type, const uint8_t* buf, uint16_t len)
{
    switch (type)
    {
        case OSPFV3_LSA_ROUTER:
            return RouterLsaV3::build(buf, len);
        case OSPFV3_LSA_NETWORK:
            return NetworkLsaV3::build(buf, len);
        case OSPFV3_LSA_INTER_AREA_PREFIX:
            return InterAreaPrefixLsa::build(buf, len);
        case OSPFV3_LSA_INTER_AREA_ROUTER:
            return InterAreaRouterLsa::build(buf, len);
        case OSPFV3_LSA_AS_EXTERNAL:
        case OSPFV3_LSA_NSSA_EXTERNAL:
            return ExternalLsaV3::build(buf, len);
        case OSPFV3_LSA_LINK:
            return LinkLsa::build(buf, len);
        case OSPFV3_LSA_INTRA_AREA_PREFIX:
            return IntraAreaPrefixLsa::build(buf, len);
        default:
            return std::nullopt;
    }
    return std::nullopt;
}
}
