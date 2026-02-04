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
    : iface(iface), ntable(iface.getNTable()), af(iface.getProcess().getAF()) {}

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

uint16_t PacketDispatcher::addLinkLocalExtension(uint8_t* buf, bool restart)
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

void PacketDispatcher::sendReliableLSRequest(Neighbor& nbr, const std::vector<LsaKey>& dbds)
{
    auto lsrs = nbr.getRtr().lsrs();
    for (auto& key : dbds)
    {
        lsrs.add(key, key);
    }
    onLsrPacingTimer(nbr);
}

void PacketDispatcher::sendReliableLSUpdate(Neighbor* nbr, std::vector<std::pair<FloodInfo, LsaRecordRef>>& updates)
{
    bool filter = iface.getConfigs().get<Config::OspfInterface::DATABASE_FILTER>().load();
    bool floodReduction = iface.floodReduction.load(std::memory_order_relaxed);

    if (nbr)
    {
        auto& lsus = nbr->getRtr().lsus();
        for (const auto& [info, record] : updates)
        {
            if (!(filter || (floodReduction && info.reason == FloodReason::REFRESH)))
                lsus.add(record.key, record);
        }
    }
    else
    {
        for (const auto& [info, record] : updates)
        {
            if (!(filter || (floodReduction && info.reason == FloodReason::REFRESH)))
                multicastLsus.add(record.key, record);
        }

        for (auto& [_, neighbor] : iface.getNTable().neighbors)
        {
            auto& lsus = neighbor.getRtr().lsus();
            for (const auto& [info, record] : updates)
            {
                if (!(filter || (floodReduction && info.reason == FloodReason::REFRESH)))
                    lsus.add(record.key, record);
            }
            iface.getTimers().startLsuRetransmissionTimer(neighbor);
        }
    }

    onLsuPacingTimer(nbr);
}

void PacketDispatcher::onLsuRetransmissionTimer(Neighbor& nbr)
{
    auto& list = nbr.getRtr().lsus();
    list.beginRetransmitBurst();
    if (list.getActive())
        iface.getTimers().startLsuRetransmissionTimer(nbr);
    onLsuPacingTimer(&nbr);
}

void PacketDispatcher::onLsrRetransmissionTimer(Neighbor& nbr)
{
    auto& list = nbr.getRtr().lsrs();
    list.beginRetransmitBurst();
    if (list.getActive())
        iface.getTimers().startLsrRetransmissionTimer(nbr);
    onLsrPacingTimer(nbr);
}

void PacketDispatcher::onLsuPacingTimer(Neighbor* nbr)
{
    auto list = nbr ? nbr->getRtr().lsus() : multicastLsus;

    sendLSUpdate(nbr);

    if (list.burstActive()) 
        iface.getTimers().startLsuPacingTimer(nbr);
}

void PacketDispatcher::onLsrPacingTimer(Neighbor& nbr)
{
    auto list = nbr.getRtr().lsrs();

    sendLSRequest(nbr);

    if (list.burstActive()) 
        iface.getTimers().startLsrPacingTimer(nbr);
}
}
