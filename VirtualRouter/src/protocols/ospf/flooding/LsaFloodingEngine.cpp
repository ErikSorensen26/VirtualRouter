// LsaFloodingEngine.cpp

#include "LsaFloodingEngine.h"
#include <Ospf.h>
#include <OspfTypes.hpp>

namespace OSPF
{
LsaFloodingEngine::LsaFloodingEngine(OspfProcess& base, std::pmr::memory_resource* mr)
    : mr(mr ? mr : std::pmr::get_default_resource()),
      db(mr, base.getConfigs().maxLsa.load(std::memory_order_relaxed)),
      fq(mr, base.getConfigs().maxFloodQueueDepth.load(std::memory_order_relaxed)),
      base(base) {}

void LsaFloodingEngine::clear()
{
    db.clear();
    fq.clear();
}

void LsaFloodingEngine::releaseMemory()
{
    db.releaseMemory();
    fq.clear();
}

LsaFloodingEngine::Result LsaFloodingEngine::processLsaHeader(const IncomingLsaContext& ctx)
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

    if (out.decision.action == InstallAction::REFRESH_SAME)
    {
        if (auto* r = db.find(ctx.key))
        {
            if (out.decision.shouldUpdateAgeOnly)
                r->header.age = out.decision.newStoredAge;

            r->lastRefreshTime = std::chrono::steady_clock::now();
            r->flags = flags;
            out.record = r;
        }
        return out;
    }

    LsaRecord& rec = db.upsertMeta(ctx.key, ctx.header, flags);
    rec.body.emplace<std::monostate>();
    out.record = &rec;

    if (out.decision.shouldFlood)
        enqueueFlood(ctx);

    return out;
}

LsaFloodingEngine::Result LsaFloodingEngine::processLsa(const IncomingLsaContext& ctx, LsaBody&& body)
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

    if (out.decision.action == InstallAction::REFRESH_SAME)
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

    if (out.decision.action == InstallAction::FLUSH_MAX_AGE ||
        out.decision.shouldStoreReplace)
    {
        LsaRecord& rec = db.upsertMeta(ctx.key, ctx.header, flags);
        rec.body = std::move(body);
        out.record = &rec;

        if (out.decision.shouldFlood)
            enqueueFlood(ctx);

        return out;
    }

    return out;
}

LsaRecordFlags LsaFloodingEngine::makeFlags(const IncomingLsaContext& ctx) noexcept
{
    LsaRecordFlags f = LsaRecordFlags::NONE;
    if (ctx.selfOriginatedKey) f |= LsaRecordFlags::SELF_ORIGINATED;
    if (ctx.checksumValid) f |= LsaRecordFlags::CHECKSUM_VALID;
    return f;
}

void LsaFloodingEngine::enqueueFlood(const IncomingLsaContext& ctx)
{
    fq.enqueue(FloodRequest{
        .key = ctx.key,
        .area = ctx.area,
        .incomingInterface = ctx.incomingInterface,
        .excludeIncoming = ctx.excludeIncoming
    });
}

InstallResult LsaFloodingEngine::evaluateIncomingLsa(const LsaRecord* existing, const IncomingLsaContext& ctx)
{
    InstallResult out{};
    uint16_t maxAge = base.getConfigs().maxAge.load(std::memory_order_relaxed);

    if (!ctx.checksumValid)
    {
        out.action = InstallAction::REJECT_INVALID;
        return out;
    }

    // Missing -> accept unless it is a pur flush
    if (!existing)
    {
        if (isMaxAge(ctx.header, maxAge))
        {
            out.action = InstallAction::FLUSH_MAX_AGE;
            out.compare = LsaCompareResult::NEWER;
            out.shouldAck = true;
            out.shouldFlood = false;
            out.shouldStoreReplace = false;
            out.shouldFlushExisting = false;
            return out;
        }

        out.action = InstallAction::INSTALL_NEWER;
        out.compare = LsaCompareResult::NEWER;
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
        {
            out.action = InstallAction::REFRESH_SAME;
            out.shouldAck = true;
            out.shouldFlood = false;

            if (ctx.header.age < existing->header.age)
            {
                out.shouldUpdateAgeOnly = true;
                out.newStoredAge = ctx.header.age;
            }
            return out;
        }
        case LsaCompareResult::NEWER:
        {
            if (isMaxAge(ctx.header, maxAge))
            {
                out.action = InstallAction::FLUSH_MAX_AGE;
                out.shouldFlood = true;
                out.shouldFlushExisting = true;
                out.shouldStoreReplace = false;
                return out;
            }

            if (ctx.selfOriginatedKey)
            {
                out.action = InstallAction::FIGHT_BACK_SELF;
                out.shouldStoreReplace = false;
                out.shouldFightBack = false;
                out.shouldFlood = false;
                return out;
            }

            // Normal newer install
            out.action = InstallAction::INSTALL_NEWER;
            out.shouldStoreReplace = true;
            out.shouldFlood = true;
            return out;
        }

        case LsaCompareResult::INCOMPARABLE:
        {
            if (ctx.selfOriginatedKey)
            {
                out.action = InstallAction::FIGHT_BACK_SELF;
                out.shouldStoreReplace = true;
                out.shouldFightBack = true;
                out.shouldFlood = false;
            }
            else
            {
                out.action = InstallAction::INSTALL_NEWER;
                out.shouldStoreReplace = true;
                out.shouldFlood = true;
            }
            return out;
        }
    }
}

LsaCompareResult LsaFloodingEngine::compareLsaHeaders(const LsaHeader& a, const LsaHeader& b) noexcept
{
    uint16_t maxAge = base.getConfigs().maxAge.load(std::memory_order_relaxed);
    uint16_t maxAgeDiff = base.getConfigs().maxAgeDiff.load(std::memory_order_relaxed);

    const bool aMax = isMaxAge(a, maxAge);
    const bool bMax = isMaxAge(b, maxAge);

    if (aMax != bMax)
        return aMax ? LsaCompareResult::OLDER : LsaCompareResult::NEWER;

    if (a.sequence != b.sequence)
        return (a.sequence > b.sequence) ? LsaCompareResult::NEWER : LsaCompareResult::OLDER;

    if (a.checksum != b.checksum)
        return (a.checksum > b.checksum) ? LsaCompareResult::NEWER : LsaCompareResult::OLDER;

    if (a.length != b.length)
        return (a.length > b.length) ? LsaCompareResult::NEWER : LsaCompareResult::OLDER;

    const uint16_t diff = absDiffU16(a.age, b.age);
    if (diff > maxAgeDiff)
        return (a.age < b.age) ? LsaCompareResult::NEWER : LsaCompareResult::OLDER;

    return LsaCompareResult::SAME;
}
}
