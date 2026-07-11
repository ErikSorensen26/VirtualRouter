// PacketDispatcher.cpp

#include "PacketDispatcher.h"
#include "OspfPacket.hpp"
#include "OspfFletcher.hpp"
#include "ospf/interface/OspfInterface.h"
#include "ospf/OspfProcess.h"
#include "ospf/neighbor/Neighbor.h"
#include "ospf/OspfTypes.hpp"

#define SUPPORT_RESYNC false

namespace routing::ospf
{
PacketDispatcher::PacketDispatcher(OspfInterface& iface)
    : multicastLsus(iface, iface.getProcessConfigs()), iface(iface), ntable(iface.ntable), af(iface.area.process.af) {}

PacketDispatcher::~PacketDispatcher() = default;

template <typename Policy>
bool PacketDispatcher::processOptions(uint32_t options, Neighbor& nbr)
{
    (void)nbr; // For OSPFV3

    uint32_t flags = iface.flags.getFlags();

    bool ignore = getIfaceBaseConfigs().get<config::OspfInterfaceBase::BASE>().get().get<config::OspfInterface::DEMAND_CIRCUIT_IGNORE>().load();
    if (iface.demandCircuit == OspfInterface::DcDecision::UNDECIDED && !ignore)
    {
        if (InterfaceFlagManager::getDemandCircuits(options) && InterfaceFlagManager::getDemandCircuits(flags) &&
            iface.configs.get<config::OspfInterface::NETWORK>().load() == config::ospf::NetworkType::POINT_TO_POINT)
            iface.demandCircuit = OspfInterface::DcDecision::ENABLED;
        else
            iface.demandCircuit = OspfInterface::DcDecision::DISABLED;
    }

    if (AreaFlagManager::getExternalRouting(flags) != AreaFlagManager::getExternalRouting(options))
        return false;
    if (AreaFlagManager::getNssa(flags) != AreaFlagManager::getNssa(options))
        return false;

    if constexpr (std::is_same_v<Policy, PolicyV2>)
    {
        // Disable opaque if neighbor does not advertise O-bit support
        if (iface.opaqueEnabled.load(std::memory_order_relaxed) && !AreaFlagManager::getOpaque(options))
            iface.opaqueEnabled.store(false, std::memory_order_relaxed);
        if (AreaFlagManager::getAddressFamilySupport(flags) != AreaFlagManager::getAddressFamilySupport(options) && iface.area.process.af == types::AddressFamily::IPv4)
            return false;
    }
    else if constexpr (std::is_same_v<Policy, PolicyV3>)
    {
        if (auto r = AreaFlagManager::getRouterBit(options); r != nbr.isTransit.load(std::memory_order_relaxed))
            nbr.isTransit.store(r, std::memory_order_release);
    }

    return true;
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

    age += iface.configs.get<config::OspfInterface::TRANSMIT_DELAY>().load();

    if (age > OSPF_MAX_AGE) age = OSPF_MAX_AGE;

    bool dnaOut = age != 3600 && floodReduction;

    return static_cast<uint16_t>(age) | (dnaOut ? 0x8000 : 0);
}

uint16_t PacketDispatcher::addLinkLocalExtension(uint8_t* buf, bool restart)
{
    utils::writeU32(buf, 0x00000000); // Checksum and size not calculated yet
    utils::writeU16(buf + 4, 0x0001); // Ext TLV type
    utils::writeU16(buf + 6, 0x0004); // Ext TLV size

    uint32_t options = 0x0000;
    if (SUPPORT_RESYNC)
        options |= 0x0001; // Resync flag
    if (restart)
        options |= 0x0002; // Restart signal
    
    utils::writeU32(buf + 8, options);

    return 12;
}

void PacketDispatcher::addLinkLocalChecksum(uint8_t* buf)
{
    utils::writeU16(buf + 2, 0x0003); // 12 (default)
    ChecksumFletcher check;
    check.addBytes(buf, 12);
    utils::writeU16(buf, check.finalize());
}

void PacketDispatcher::sendReliableLsr(Neighbor& nbr, const std::vector<LsaKey>& dbds)
{
    auto lsrs = nbr.getRtr().lsrs();
    for (auto& key : dbds)
    {
        lsrs.add(key, key);
    }
    onLsrPacingTimer(nbr);
}

void PacketDispatcher::sendReliableLsu(Neighbor* nbr, std::vector<std::pair<FloodInfo, LsaRecordRef>>& updates)
{
    bool filter = iface.configs.get<config::OspfInterface::DATABASE_FILTER>().load();
    bool floodReduction = iface.floodReduction;

    if (nbr)
    {
        auto& lsus = nbr->getRtr().lsus();
        for (const auto& [info, record] : updates)
        {
            if (!(filter || (floodReduction && info.reason == FloodReason::REFRESH)))
                lsus.add(record.key, record);
        }
        lsus.beginRetransmitBurst();
    }
    else
    {
        for (const auto& [info, record] : updates)
        {
            if (!(filter || (floodReduction && info.reason == FloodReason::REFRESH)))
                multicastLsus.add(record.key, record);
        }
        multicastLsus.beginRetransmitBurst();

        // Add lsu updates to neighbors
        iface.ntable.forEach([this, &updates](uint32_t, Neighbor& nbr) {
            addLsaRetransmissions(nbr.getRtr().lsus(), updates);
            iface.tmgr.startLsuRetransmissionTimer(nbr);
        });
    }

    onLsuPacingTimer(nbr);
}

void PacketDispatcher::onLsuRetransmissionTimer(Neighbor& nbr)
{
    auto& list = nbr.getRtr().lsus();
    list.beginRetransmitBurst();
    if (list.getActive())
        iface.tmgr.startLsuRetransmissionTimer(nbr);
    onLsuPacingTimer(&nbr);
}

void PacketDispatcher::onLsrRetransmissionTimer(Neighbor& nbr)
{
    auto& list = nbr.getRtr().lsrs();
    list.beginRetransmitBurst();
    if (list.getActive())
        iface.tmgr.startLsrRetransmissionTimer(nbr);
    onLsrPacingTimer(nbr);
}

void PacketDispatcher::onLsuPacingTimer(Neighbor* nbr)
{
    auto list = nbr ? nbr->getRtr().lsus() : multicastLsus;

    sendLsu(nbr);

    if (list.burstActive()) 
        iface.tmgr.startLsuPacingTimer(nbr);
}

void PacketDispatcher::onLsrPacingTimer(Neighbor& nbr)
{
    auto list = nbr.getRtr().lsrs();

    sendLsr(nbr);

    if (list.burstActive()) 
        iface.tmgr.startLsrPacingTimer(nbr);
}

void PacketDispatcher::addLsaRetransmissions(RetransmissionList<LsaKey, LsaRecordRef>& lsuList, std::vector<std::pair<FloodInfo, LsaRecordRef>>& updates)
{
    bool filter = iface.configs.get<config::OspfInterface::DATABASE_FILTER>().load();
    bool floodReduction = iface.floodReduction;

    for (const auto& [info, record] : updates)
    {
        if (!(filter || (floodReduction && info.reason == FloodReason::REFRESH)))
            lsuList.add(record.key, record);
    }
    lsuList.beginRetransmitBurst();
}

template bool PacketDispatcher::processOptions<PolicyV2>(uint32_t, Neighbor&);
template bool PacketDispatcher::processOptions<PolicyV3>(uint32_t, Neighbor&);
} // namespace routing
