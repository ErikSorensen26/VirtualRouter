// OspfArea.cpp

#include "OspfArea.h"
#include <OspfTopology.h>
#include <OspfInterface.h>
#include <OspfProcess.h>
#include <OspfTypes.hpp>
#include <OspfNeighbor.h>
#include <OspfTypes.hpp>
#include <OspfFletcher.hpp>
#include <Interface.h>
#include <InterfaceType.hpp>
#include <OspfNeighborTable.h>
#include <OspfNeighbor.h>
#include <variant>
#include <OspfRouteManager.h>
#include <type_traits>

#include "OspfOriginator.h"
#include <OspfOriginatorV2.h>
#include <OspfOriginatorV3.h>

namespace OSPF
{
OspfArea::OspfArea(Topology& base, uint32_t id, std::pmr::memory_resource* mr)
    : areaId(id),
      mr(mr ? mr : std::pmr::get_default_resource()),
      cfgs(base.getConfigs().areaInfo[id]),
      db(mr, base.process.getConfigs().maxLsa.load(std::memory_order_relaxed)),
      fq(base.process.getConfigs().maxFloodQueueDepth.load(std::memory_order_relaxed)),
      base(base),
      spfMgr(*this, base.process.tmgr),
      flags(base.getProcess().isV3)
{
    if (base.process.isV3)
        originator = new OspfOriginatorV3(*this);
    else
        originator = new OspfOriginatorV2(*this);
}

void OspfArea::clear()
{
    db.clear();
}

void OspfArea::releaseMemory()
{
    db.releaseMemory();
}

void OspfArea::runDCIntegrityScan()
{
    bool enabled = db.runDCIntegrityScan();
    bool old = dcCompatible.exchange(enabled, std::memory_order_acq_rel);
    if (old != enabled)
    {
        auto& ifaceMgr = base.process.getIfaceMgr();
        std::shared_lock<std::shared_mutex> lk(ifaceMgr.interfaceMutex);
        for (auto& [id, iface] : ifaceMgr.ospfInterfaceList)
        {
            if ((iface.configs.demandCircuit.load(std::memory_order_relaxed) ||
                iface.configs.floodReduction.load(std::memory_order_relaxed)) &&
                iface.floodReduction.load() != enabled)
            {
                iface.floodReduction.store(enabled, std::memory_order_release);
                // TODO: refresh interface
            }
        }
    }
}

void OspfArea::setFloodReduction(OspfInterface& iface)
{
    const bool enableFloodReduction =
        dcCompatible.load(std::memory_order_relaxed) && (
            iface.configs.floodReduction.load(std::memory_order_relaxed) ||
            iface.configs.demandCircuit.load(std::memory_order_relaxed)
        );

    if (iface.floodReduction.load(std::memory_order_relaxed) != enableFloodReduction)
    {
        iface.floodReduction.store(enableFloodReduction, std::memory_order_relaxed);
        // TODO: refresh interface
    }
}

template<typename Policy>
void OspfArea::flood()
{
    auto floodList = fq.tryDequeueBatch();
    auto& ifaceMgr = base.process.getIfaceMgr();
    std::shared_lock<std::shared_mutex> lock(ifaceMgr.interfaceMutex);
    for (auto& [id, iface] : ifaceMgr.ospfInterfaceList)
    {
        if (id.area == areaId && !iface.configs.databaseFilterAll.load(std::memory_order_relaxed))
            send(iface, floodList);
    }

    if (shouldRequestSpf.load(std::memory_order_relaxed))
        spfMgr.requestSpf<Policy>();
}

void OspfArea::send(OspfInterface& iface, std::vector<std::pair<FloodInfo, LsaRecordRef>>& records)
{
    auto& ntable = iface.getNTable();
    auto& dispatcher = iface.getDispatcher();

    if (iface.configs.networkType.load(std::memory_order_relaxed) == OSPF::InterfaceConfigs::NetworkType::BROADCAST)
    {
        std::shared_lock<std::shared_mutex> lock(ntable.mu);
        dispatcher.sendLSUpdate(nullptr, records);
    }
    else
    {
        std::shared_lock<std::shared_mutex> lock(ntable.mu);
        for (auto& [rid, nbr] : ntable.neighbors)
        {
            dispatcher.sendLSUpdate(&nbr, records);
        }
    }
}

template <typename Policy>
OspfArea::Result OspfArea::processLsa(const IncomingLsaContext& ctx, LsaBody& body)
{
    auto result = process(ctx, body);

    if (result.decision.action != InstallAction::REJECT_INVALID &&
        result.decision.action != InstallAction::IGNORE_OLDER &&
        result.decision.topologyChanged)
    {
        if (std::holds_alternative<typename Policy::ExternalLsa>(body))
        {
            base.distributeExternalLsa<Policy>(areaId, ctx, body);
        }
        else if (std::holds_alternative<typename Policy::InterNetworkLsa>(body))
        {
            auto res = RouteManager::deriveInterAreaNetwork<Policy>(*this, ctx.key, ctx.header, body);
            base.process.getRib().replaceRoute(areaId, res);
        }
        else if (std::holds_alternative<typename Policy::InterRouterLsa>(body))
        {
            RouteManager::deriveInterAreaRouter<Policy>(*this, ctx.key, ctx.header, body);
        }
    }

    return result;
}

OspfArea::Result OspfArea::process(const IncomingLsaContext& ctx, LsaBody& body)
{
    Result out{};

    const LsaRecord* existing = db.find(ctx.key);

    out.decision = evaluateIncomingLsa(existing, ctx, body);
    
    if (out.decision.action == InstallAction::REJECT_INVALID ||
        out.decision.action == InstallAction::IGNORE_OLDER)
    {
        return out;
    }

    const LsaRecordFlags lsaFlags = makeFlags(ctx);

    if (out.decision.action == InstallAction::IGNORE_DUPLICATE)
    {
        if (out.decision.shouldUpdateAgeOnly)
        {
            if (auto* r = db.find(ctx.key))
            {
                r->header.age = out.decision.newStoredAge;
                r->lastRefreshTime = std::chrono::steady_clock::now();
                r->flags = lsaFlags;
                out.record = r;
            }
        }
        else if (db.touchRefresh(ctx.key))
        {
            if (auto* r = db.find(ctx.key))
            {
                r->flags = lsaFlags;
                out.record = r;
            }
        }
        return out;
    }

    if (out.decision.action == InstallAction::FIGHT_BACK_SELF)
    {
        LsaRecord& rec = db.upsertMeta(ctx, lsaFlags);
        rec.body = body;
        out.record = &rec;
        out.decision.shouldFightBack = true;
    }
    else if (out.decision.action == InstallAction::FLUSH_MAX_AGE ||
        out.decision.shouldStoreReplace)
    {
        LsaRecord& rec = db.upsertMeta(ctx, lsaFlags);
        rec.body = body;
        out.record = &rec;
    }

    return out;
}

template <typename Policy>
void OspfArea::processSummaries(std::unordered_map<LsaKey, LsaBody>& summaries)
{
    constexpr uint16_t interNetwork = std::is_same_v<typename Policy::InterNetworkLsa, SummaryNetworkLsa>
        ? OSPFV2_LSA_SUM_NET : OSPFV3_LSA_INTER_AREA_PREFIX;

    db.forEachInType(interNetwork, [&](LsaKey& key, LsaRecord& record) {
        if (!summaries.contains(key))
            originator->processReoriginatedLsa<Policy>(key, std::forward<LsaBody>(record.body), false, true);
    });

    for (auto& [key, body] : summaries)
    {
        originator->processReoriginatedLsa<Policy>(key, std::forward<LsaBody>(body));
    }
}

void OspfArea::processExternalLsa(const IncomingLsaContext& ctx, LsaBody& body)
{
    bool expire = ctx.header.age == 3600;

    // Manage type 4 if needed
    originator->addExternal(ctx.key.advertisingRouter, ctx.key.linkStateId, expire);
    process(ctx, body);
}

void OspfArea::evaluateDecision(Result& result, const IncomingLsaContext& ctx)
{
    if (result.decision.shouldFlood && result.record)
        enqueueFlood(LsaRecordRef{ctx.key, *result.record}, ctx.info);
    if (result.decision.affectsSpfGraph && result.decision.topologyChanged && !shouldRequestSpf.load(std::memory_order_relaxed))
        shouldRequestSpf.store(true, std::memory_order_release);
}

bool OspfArea::compareLSASummary(const LsaHeader& hdr, const LsaKey& key) const
{
    const LsaRecord* existing = db.find(key);

    // Check if LSA is missing
    if (!existing)
    {
        return false;
    }

    const LsaCompareResult cmp = compareLsaHeaders(hdr, existing->header);
    if (cmp == LsaCompareResult::NEWER)
    {
        return false;
    }

    return true;
}

LsaRecordFlags OspfArea::makeFlags(const IncomingLsaContext& ctx) noexcept
{
    LsaRecordFlags f = LsaRecordFlags::NONE;
    if (ctx.selfOriginatedKey) f |= LsaRecordFlags::SELF_ORIGINATED;
    if (ctx.checksumValid) f |= LsaRecordFlags::CHECKSUM_VALID;
    return f;
}

void OspfArea::enqueueFlood(LsaRecordRef& record, FloodInfo info)
{
    fq.enqueue(record, info);
}

void OspfArea::enqueueFlood(LsaRecordRef&& record, FloodInfo info)
{
    fq.enqueue(record, info);
}

InstallResult OspfArea::evaluateIncomingLsa(const LsaRecord* existing, const IncomingLsaContext& ctx, const LsaBody& body)
{
    InstallResult out{};
    const uint16_t maxAge = base.process.getConfigs().maxAge.load(std::memory_order_relaxed);

    // RouterLsa, NetworkLsa, SummaryLsa, AsbrLsa
    out.affectsSpfGraph = ctx.key.lsaType >= 1 && ctx.key.lsaType <= 4;
        
    if (!ctx.checksumValid)
    {
        out.shouldAck = false;
        out.action = InstallAction::REJECT_INVALID;
        return out;
    }

    // Missing -> accept unless it is a pur flush
    if (!existing)
    {
        // Missing + MaxAge -> ACK only (not a flush)
        if (isMaxAge(ctx.header, maxAge))
        {
            out.action = InstallAction::FLUSH_MAX_AGE;
            out.shouldFlood = true;
            out.shouldStoreReplace = true;
            return out;
        }

        out.action = InstallAction::INSTALL_NEWER;
        out.shouldStoreReplace = true;
        out.shouldFlood = true;

        if (out.affectsSpfGraph)
            out.topologyChanged = true;

        return out;
    }

    out.compare = compareLsaHeaders(ctx.header, existing->header);

    switch (out.compare)
    {
        case LsaCompareResult::OLDER:
            out.action = InstallAction::IGNORE_OLDER;
            return out;
        case LsaCompareResult::SAME:
            out.action = InstallAction::IGNORE_DUPLICATE;
            return out;
        case LsaCompareResult::NEWER:
        {
            if (ctx.header.age == maxAge)
            {
                out.action = InstallAction::FLUSH_MAX_AGE;
                out.shouldStoreReplace = true;
                out.shouldFlood = true;

                if (out.affectsSpfGraph)
                    out.topologyChanged = true;

                return out;
            }

            if (ctx.selfOriginatedKey &&
                ctx.header.sequence >= existing->header.sequence)
            {
                out.action = InstallAction::FIGHT_BACK_SELF;
                out.shouldFightBack = true;
                out.shouldFlood = true;
                return out;
            }

            out.action = InstallAction::INSTALL_NEWER;
            out.shouldStoreReplace = true;
            out.shouldFlood = true;

            if (out.affectsSpfGraph)
            {
                if (!compareLsaBody(existing->body, body))
                    out.topologyChanged = true;
            }

            return out;
        }
    }
}

LsaCompareResult OspfArea::compareLsaHeaders(const LsaHeader& a, const LsaHeader& b) const
{
    uint16_t maxAge = base.process.getConfigs().maxAge.load(std::memory_order_relaxed);
    uint16_t maxAgeDiff = base.process.getConfigs().maxAgeDiff.load(std::memory_order_relaxed);

    if (a.sequence != b.sequence)
        return (a.sequence > b.sequence) ? LsaCompareResult::NEWER : LsaCompareResult::OLDER;

    if (a.checksum != b.checksum)
        return (a.checksum > b.checksum) ? LsaCompareResult::NEWER : LsaCompareResult::OLDER;

    const bool aMax = isMaxAge(a, maxAge);
    const bool bMax = isMaxAge(b, maxAge);

    if (aMax != bMax)
        return aMax ? LsaCompareResult::NEWER : LsaCompareResult::OLDER;

    const uint16_t diff = absDiffU16(a.age, b.age);
    if (diff > maxAgeDiff)
        return (a.age < b.age) ? LsaCompareResult::NEWER : LsaCompareResult::OLDER;

    return LsaCompareResult::SAME;
}

bool OspfArea::compareLsaBody(const LsaBody& a, const LsaBody& b)
{
    if (a.index() != b.index())
        return true;

    return std::visit(
        [](const auto& lhs, const auto& rhs) -> bool
        {
            if constexpr (requires { lhs == rhs; })
                return lhs == rhs;
            else
                return true;
        },
        a, b
    );
}

template OspfArea::Result OspfArea::processLsa<PolicyV2>(const IncomingLsaContext&, LsaBody&);
template OspfArea::Result OspfArea::processLsa<PolicyV3>(const IncomingLsaContext&, LsaBody&);

template void OspfArea::flood<PolicyV2>();
template void OspfArea::flood<PolicyV3>();
}
