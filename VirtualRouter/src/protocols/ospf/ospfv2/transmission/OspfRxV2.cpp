// Ospfv2Rx.cpp

#include "PacketDispatcherV2.h"
#include <OspfProcess.h>
#include <OspfInterface.h>
#include <OspfInterfaceTimers.h>

#include <Ospfv2HelloHeader.hpp>
#include <Ospfv2DBDHeader.hpp>
#include <Ospfv2LSAHeader.hpp>
#include <Ospfv2LSRHeader.hpp>
#include <OspfNeighbor.h>
#include <OspfTopology.h>
#include <OspfArea.h>
#include <OspfFlagManager.h>

#include <Interface.h>
#include <InterfaceConfigs.h>

namespace OSPF
{
void PacketDispatcherV2::handleIncoming(const Ospfv2Header& ospfHeader, const uint8_t* neighborIp, bool multicast)
{
    IPAddress neigIp(neighborIp, iface.process.getAF());
    uint32_t rid = ospfHeader.getRouterID();

    // Check passive
    if (iface.configs.isPassive.load(std::memory_order_relaxed))
        return;

    // Validate version
    if (ospfHeader.getVersion() != OSPFV2_VERSION)
        return;

    if (ospfHeader.getAreaID() != iface.getAreaId())
        return;

    // Validate size
    if (ospfHeader.getPacketLen() != Ospfv2Header::fixedSize /* + ospfHeader.trail.size() */)
        return;

    //TODO check auth stuff

    HeaderInfo info(ospfHeader.getTrail().data(), ospfHeader.getPacketLen(), neigIp, rid);
    info.neighbor = iface.getNTable().lookup(rid);
    // TODO header stuff

    if (ospfHeader.getType() == OSPFV2_TYPE_HELLO)
    {
        if (iface.getNTable().isUnicast(rid) == multicast)
            return;
        processHello(info, !multicast);
    }
    else if (info.neighbor)
    {
        switch (ospfHeader.getType())
        {
            case OSPFV2_TYPE_DATABASE_DESCRIPTION:
                processDBD(info);
                break;
            case OSPFV2_TYPE_LINK_STATE_ACK:
                processLSAck(info);
                break;
            case OSPFV2_TYPE_LINK_STATE_REQUEST:
                processLSRequest(info);
                break;
            case OSPFV2_TYPE_LINK_STATE_UPDATE:
                processLSUpdate(info);
                break;
        }
    }
}

bool PacketDispatcherV2::processOptions(uint32_t options, bool isStatic)
{
    auto& flags = iface.getFlags();
    auto& areaFlags = iface.getArea().getFlags();

    if (iface.demandCircuit.load(std::memory_order_relaxed) == OspfInterface::DcDecision::UNDECIDED)
    {
        if (InterfaceFlagManager::getDemandCircuits(options) && flags.getDemandCircuits() &&
            iface.configs.networkType.load(std::memory_order_relaxed) == InterfaceConfigs::NetworkType::POINT_TO_POINT)
            iface.demandCircuit.store(OspfInterface::DcDecision::ENABLED, std::memory_order_release);
        else
            iface.demandCircuit.store(OspfInterface::DcDecision::DISABLED, std::memory_order_release);
    }
    if (iface.opaqueEnabled.load(std::memory_order_relaxed) && AreaFlagManager::getOpaque(options))
        iface.opaqueEnabled.store(false, std::memory_order_relaxed);
    if (areaFlags.getExternalRouting() != AreaFlagManager::getExternalRouting(options))
        return false;
    if (areaFlags.getNssa() != AreaFlagManager::getNssa(options))
        return false;
    return true;
}

void PacketDispatcherV2::processHello(PacketDispatcher::HeaderInfo& info, bool unicast)
{
    Ospfv2HelloHeader hdr;
    hdr.setBuffer(info.payload);

    info.offset += Ospfv2HelloHeader::fixedSize;
    if (info.offset > info.payloadSize)
        return;

    // Validate timers
    if (hdr.getHelloInterval() != iface.configs.helloInterval.load(std::memory_order_relaxed) ||
        hdr.getDeadInterval() != iface.configs.deadInterval.load(std::memory_order_relaxed))
    {
        info.neighbor->setState(Neighbor::State::DOWN);
        return;
    }

    // Store options
    if (info.neighbor->getState() != Neighbor::State::FULL && !processOptions(static_cast<uint32_t>(hdr.getOptions())))
    {
        info.neighbor->setState(Neighbor::State::DOWN);
        return;
    }

    auto ntype = iface.configs.networkType.load(std::memory_order_relaxed);
    bool multiAccess =
        ntype == InterfaceConfigs::NetworkType::BROADCAST ||
        ntype == InterfaceConfigs::NetworkType::NON_BROADCAST;

    if (multiAccess)
    {
        if (hdr.getMask() != iface.interfaceAddress.getMask())
        {
            info.neighbor->setState(Neighbor::State::DOWN);
            return;
        }
    }

    if (!info.neighbor)
    {
        info.neighbor = ntable.createNeighbor(info.rid, info.neighborIp);
    }
    else if (unicast && info.neighbor)
    {
        auto state = info.neighbor->getState();
        if (state == Neighbor::State::DOWN || state == Neighbor::State::ATTEMPT)
            info.neighbor->setState(Neighbor::State::INIT);
    }

    if (ntype == InterfaceConfigs::NetworkType::BROADCAST ||
        ntype == InterfaceConfigs::NetworkType::NON_BROADCAST)
    {
        uint8_t* neighborList = info.payload + Ospfv2HelloHeader::fixedSize;
        size_t listSize = info.payloadSize - Ospfv2HelloHeader::fixedSize;
        if (listSize % 4 != 0) return;

        // Find RID
        bool ridFound = false;
        for (size_t i = 0; i < listSize; i += 4)
        {
            if (readU32(neighborList + i) == iface.process.getRouterId())
            {
                ridFound = true;
                break;
            }
        }

        // Handle two way
        if (!ridFound) return;
        if (ridFound && info.neighbor->getState() == Neighbor::State::INIT)
            info.neighbor->setState(Neighbor::State::TWOWAY);
    }
    else if (info.neighbor->getState() == Neighbor::State::INIT)
    {
        info.neighbor->setState(Neighbor::State::TWOWAY);
    }

    info.neighbor->markHeard();
    iface.getTimers().startInactiveTimer(*info.neighbor);

    // Update neighbor variables
    if (info.neighbor->priority.load(std::memory_order_relaxed) != hdr.getPriority())
        info.neighbor->priority.store(hdr.getPriority(), std::memory_order_release);

    if (multiAccess)
    {
        // Read new values from the Hello
        const uint8_t  newPriority = hdr.getPriority();
        const uint32_t newDr       = hdr.getDR();
        const uint32_t newBdr      = hdr.getBDR();

        // Update neighbor-advertised DR/BDR and priority from the Hello
        info.neighbor->priority.store(newPriority, std::memory_order_relaxed);
        info.neighbor->dr.store(newDr, std::memory_order_relaxed);
        info.neighbor->bdr.store(newBdr, std::memory_order_relaxed);

        uint32_t currentDr = iface.dr.rid.load(std::memory_order_relaxed);
        uint32_t currentBdr = iface.bdr.rid.load(std::memory_order_relaxed);

        const bool election = (newPriority == 0 &&
            (info.neighbor->routerID == currentBdr ||
             info.neighbor->routerID == currentDr)) ||
            (info.neighbor->getState() == Neighbor::State::TWOWAY &&
            ((iface.dr.rid.load(std::memory_order_relaxed) == 0) ||
            (iface.bdr.rid.load(std::memory_order_relaxed) == 0)));

        if (election)
            iface.election();
    }
}

void PacketDispatcherV2::processDBD(PacketDispatcher::HeaderInfo& info)
{
    OspfArea& area = iface.getArea();

    auto state = info.neighbor->getState();
    if (state < Neighbor::State::EXSTART) return;

    Ospfv2DBDHeader hdr;
    hdr.setBuffer(info.payload);

    info.offset += Ospfv2DBDHeader::fixedSize;
    if (info.offset > info.payloadSize) 
        return;

    // Varify only 20 byte lsa blocks exist
    if ((info.payloadSize - info.offset) % 20 != 0)
        return;

    if (!processOptions(hdr.getOptions(), true))
    {
        info.neighbor->setState(Neighbor::State::DOWN);
        return;
    }

    // Verify MTU
    if (iface.configs.mtuIgnore.load(std::memory_order_relaxed) && info.neighbor->mtu != hdr.getMtu())
    {
        info.neighbor->setState(Neighbor::State::DOWN);
        return;
    }

    bool ack = hdr.getSequence() == info.neighbor->currentSeq.load(std::memory_order_relaxed);
    bool init = hdr.getFlagI();

    if (init)
    {
        // Run INIT
        if (!hdr.getFlagI())
            return;
        if (info.offset != info.payloadSize)
            return;

        if (info.offset != info.payloadSize)
            return;

        Neighbor::Role role = info.neighbor->routerID > iface.process.getRouterId()
            ? Neighbor::Role::MASTER
            : Neighbor::Role::SLAVE;
        info.neighbor->setRole(role);

        if (role == Neighbor::Role::SLAVE)
            info.neighbor->currentSeq.store(hdr.getSequence());

        else if (state != Neighbor::State::EXSTART)
            // Move to EXSTART, will send init either acking or setting initial sequence.
            info.neighbor->setState(Neighbor::State::EXSTART);

        if (role == Neighbor::Role::SLAVE) // SLAVE
        {
            // Ack MASTERs init, move to EXCHANGE, and await MASTER.
            sendInitDBD(*info.neighbor);
            info.neighbor->setState(Neighbor::State::EXCHANGE);
        }
        else if (ack) // MASTER and Ack received, move to EXCHANGE.
        {
            info.neighbor->setState(Neighbor::State::EXCHANGE);
        }
    }
    else if (state == Neighbor::State::EXCHANGE)
    {
        // Process DB description.
        auto& rtr = info.neighbor->getRtr();
        while (info.offset < info.payloadSize)
        {
            Ospfv2LSAHeader lsaHdr;
            lsaHdr.setBuffer(info.payload + info.offset);

            info.offset += Ospfv2LSAHeader::fixedSize;
            if (info.offset > info.payloadSize) 
                return;

            LsaKey key(lsaHdr.getType(), lsaHdr.getLsID(), lsaHdr.getAdvRouter());

            LsaHeader lsa = {
                .sequence = lsaHdr.getSeqNumber(),
                .checksum = lsaHdr.getChecksum(),
                .length = lsaHdr.getLen(),
                .age = lsaHdr.getAge()
            };

            if (area.compareLSASummary(lsa, key))
                rtr.addLsr(key);
        }

        if ((info.neighbor->currentDbd.has_value() || hdr.getFlagM()) || info.neighbor->getRole() == Neighbor::Role::SLAVE)
        {
            sendDBD(*info.neighbor);
        }
        else
        {
            if (info.neighbor->getRole() == Neighbor::Role::SLAVE)
                sendDBD(*info.neighbor); // Respond to MASTER even if no lsas to process

            // Move to LOADING
            info.neighbor->setState(Neighbor::State::LOADING); 
        }
    }

    if (hdr.getOptEA())
        processLLSDataBlock(info);
}

void PacketDispatcherV2::processLSAck(HeaderInfo& info)
{
    // Process ACKs.
    auto& rtr = info.neighbor->getRtr();
    while (info.offset < info.payloadSize)
    {
        Ospfv2LSAHeader lsaHdr;
        lsaHdr.setBuffer(info.payload + info.offset);

        info.offset += Ospfv2LSAHeader::fixedSize;
        if (info.offset > info.payloadSize) 
            return;

        LsaKey key(lsaHdr.getType(), lsaHdr.getLsID(), lsaHdr.getAdvRouter());

        LsaHeader lsa = {
            .sequence = lsaHdr.getSeqNumber(),
            .checksum = lsaHdr.getChecksum(),
            .length = lsaHdr.getLen(),
            .age = lsaHdr.getAge()
        };

        auto record = rtr.getLsu(key);
        if (!record.has_value()) continue;

        if (record->record->header == lsa)
            rtr.eraseLsu(key);
    }
}

void PacketDispatcherV2::processLSRequest(PacketDispatcher::HeaderInfo& info)
{
    Ospfv2LSRHeader hdr;
    hdr.setBuffer(info.payload);

    // Varify only 12 byte lsa blocks exist
    if ((info.payloadSize - info.offset) % 12 != 0)
        return;

    // Process request
    std::vector<std::pair<FloodInfo, LsaRecordRef>> records;
    auto& lsdb = iface.getArea().lsdb();
    while (info.offset < info.payloadSize)
    {
        Ospfv2LSRHeader lsrHdr;
        lsrHdr.setBuffer(info.payload + info.offset);

        info.offset += Ospfv2LSRHeader::fixedSize;
        if (info.offset > info.payloadSize) 
            return;

        LsaKey key(static_cast<uint16_t>(lsrHdr.getType()), lsrHdr.getLsID(), lsrHdr.getAdvRouter());
        if (LsaRecord* record = lsdb.find(key); record)
        {
            if (key.lsaType < OSPFV2_LSA_OPAQUE_LINK || iface.opaqueEnabled.load(std::memory_order_relaxed))
                records.push_back({{FloodReason::UPDATE}, {key, *record}});
        }
    }

    sendReliableLSUpdate(info.neighbor, records);
}

void PacketDispatcherV2::processLSUpdate(PacketDispatcher::HeaderInfo& info)
{
    auto& topology = *iface.topology.load(std::memory_order_relaxed);
    auto& area = iface.getArea();
    if (info.payloadSize < 4)
        return;

    if (info.neighbor->getState() < Neighbor::State::EXCHANGE)
        return;

    uint32_t lsuSize = readU32(info.payload);
    uint32_t routerId = iface.process.getRouterId();

    info.offset += 4;

    std::vector<LsaRecordRef> acks;

    for (int i = 0; i < static_cast<int>(lsuSize); i++)
    {
        Ospfv2LSAHeader lsaHdr;
        lsaHdr.setBuffer(info.payload + info.offset);

        size_t off = info.offset;

        off += Ospfv2LSAHeader::fixedSize;
        if (off > info.payloadSize) 
            return;

        bool selfOrigin = lsaHdr.getAdvRouter() == routerId;

        LsaKey key = {
            lsaHdr.getType(),
            lsaHdr.getLsID(),
            lsaHdr.getAdvRouter()
        };
        LsaHeader hdr = {
            lsaHdr.getSeqNumber(),
            lsaHdr.getChecksum(),
            lsaHdr.getLen(),
            lsaHdr.getAge(),
                lsaHdr.getOptions()
        };

        uint16_t bodyLen = hdr.length - Ospfv2LSAHeader::fixedSize;
        auto body = buildLsaBody(static_cast<uint8_t>(key.lsaType), info.payload + off, bodyLen);

        if (hdr.length < Ospfv2LSAHeader::fixedSize)
            return;
        if (info.offset + hdr.length > info.payloadSize)
            return;

        info.offset += hdr.length;

        if (!body.has_value()) continue;

        auto calc = runLsaCalculations<PolicyV2>(hdr, key, body.value());
        bool checksumValid = calc.checksum == hdr.checksum;

        IncomingLsaContext context{
            key,
            hdr,
            checksumValid,
            selfOrigin,
            {FloodReason::UPDATE},
            iface.configs.key,
            info.neighbor->routerID
        };

        OspfArea::Result result = area.processLsa<PolicyV2>(context, body.value());
        if (result.decision.shouldAck)
            acks.push_back({context.key, *result.record});
    }

    if (!acks.empty())
        sendLSAck(*info.neighbor, acks);

    topology.flood<PolicyV2>();
}

void PacketDispatcherV2::processLLSDataBlock(PacketDispatcher::HeaderInfo& info)
{
    if (info.offset != info.payloadSize)
        return;

    uint16_t checksum = readU16(info.payload + info.offset);
    uint16_t llsLen = readU16(info.payload + info.offset + 2);

    if (info.offset + llsLen > info.payloadSize)
        return;

    //TODO check checksum of datablock

    //TODO handle options

    info.offset += llsLen;
}

std::optional<LsaBody> PacketDispatcherV2::buildLsaBody(uint8_t type, const uint8_t* buf, uint16_t len)
{
    switch (type)
    {
        case OSPFV2_LSA_ROUTER:
            return RouterLsaV2::build(buf, len);
        case OSPFV2_LSA_NETWORK:
            return NetworkLsaV2::build(buf, len);
        case OSPFV2_LSA_SUM_NET:
            return SummaryNetworkLsa::build(buf, len);
        case OSPFV2_LSA_SUM_ASBR:
            return SummaryRouterLsa::build(buf, len);
        case OSPFV2_LSA_EXTERNAL:
        case OSPFV2_LSA_NSSA:
            return ExternalLsaV2::build(buf, len);
        case OSPFV2_LSA_OPAQUE_LINK:
        {

        }
        case OSPFV2_LSA_OPAQUE_AREA:
        {

        }
        case OSPFV2_LSA_OPAQUE_AS:
        {

        }
        default:
            return std::nullopt;
    }
    return std::nullopt;
}
}
