// OspfArea.cpp

#include "OspfArea.h"
#include <OspfTopology.h>
#include <OspfInterface.h>
#include <OspfProcess.h>
#include <OspfTypes.hpp>
#include <OspfNeighbor.h>
#include <OspfTypes.hpp>

namespace OSPF
{
OspfArea::OspfArea(Topology& base, uint32_t id, std::pmr::memory_resource* mr)
    : areaId(id),
      mr(mr ? mr : std::pmr::get_default_resource()),
      cfgs(base.getConfigs().areaInfo[id]),
      db(mr, base.process.getConfigs().maxLsa.load(std::memory_order_relaxed)),
      fq(base.process.getConfigs().maxFloodQueueDepth.load(std::memory_order_relaxed)),
      base(base) {}

void OspfArea::clear()
{
    db.clear();
}

void OspfArea::releaseMemory()
{
    db.releaseMemory();
}

void OspfArea::flood()
{
    auto floodList = fq.tryDequeueBatch();
    auto& ifaceMgr = base.process.getIfaceMgr();
    std::shared_lock<std::shared_mutex> lock(ifaceMgr.interfaceMutex);
    for (auto& [id, iface] : ifaceMgr.ospfInterfaceList)
    {
        if (id.area == areaId)
            send(iface, floodList);
    }
}

void OspfArea::send(OspfInterface& iface, std::vector<LsaRecordRef>& records)
{
    auto& ntable = iface.getNTable();
    auto& dispatcher = iface.getDispatcher();

    if (iface.configs->networkType.load(std::memory_order_relaxed) == OSPF::InterfaceConfigs::NetworkType::BROADCAST)
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

OspfArea::Result OspfArea::processLsa(const IncomingLsaContext& ctx, LsaBody& body)
{
    Result out{};

    const LsaRecord* existing = db.find(ctx.key);

    out.decision = evaluateIncomingLsa(existing, ctx);
    
    if (out.decision.action == InstallAction::REJECT_INVALID ||
        out.decision.action == InstallAction::IGNORE_OLDER)
    {
        return out;
    }

    const LsaRecordFlags flags = makeFlags(ctx);

    if (out.decision.action == InstallAction::IGNORE_DUPLICATE)
    {
        if (out.decision.shouldUpdateAgeOnly)
        {
            if (auto* r = db.find(ctx.key))
            {
                r->header.age = out.decision.newStoredAge;
                r->lastRefreshTime = std::chrono::steady_clock::now();
                r->flags = flags;
                out.record = r;
            }
        }
        else if (db.touchRefresh(ctx.key))
        {
            if (auto* r = db.find(ctx.key))
            {
                r->flags = flags;
                out.record = r;
            }
        }
        return out;
    }

    if (out.decision.action == InstallAction::FIGHT_BACK_SELF)
    {
        LsaRecord& rec = db.upsertMeta(ctx, flags);
        rec.body = std::move(body);
        out.record = &rec;

        out.decision.shouldFightBack = true;
    }
    else if (out.decision.action == InstallAction::FLUSH_MAX_AGE ||
        out.decision.shouldStoreReplace)
    {
        LsaRecord& rec = db.upsertMeta(ctx, flags);
        rec.body = std::move(body);
        out.record = &rec;

        if (out.decision.shouldFlood)
            enqueueFlood(LsaRecordRef{ctx.key, rec});
    }

    out.decision.shouldRunSpf = true;

    return out;
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

void OspfArea::enqueueFlood(LsaRecordRef& record)
{
    fq.enqueue(record);
}

void OspfArea::enqueueFlood(LsaRecordRef&& record)
{
    fq.enqueue(record);
}

InstallResult OspfArea::evaluateIncomingLsa(const LsaRecord* existing, const IncomingLsaContext& ctx)
{
    InstallResult out{};
    const uint16_t maxAge = base.process.getConfigs().maxAge.load(std::memory_order_relaxed);

    if (!ctx.checksumValid)
    {
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
}
