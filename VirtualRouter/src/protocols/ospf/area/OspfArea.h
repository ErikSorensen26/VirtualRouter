// OspfArea.h

#ifndef OSPF_AREA_H
#define OSPF_AREA_H

#include <memory_resource>

#include <LsdbTable.h>
#include <SpfManager.h>
#include "FloodQueue.hpp"
#include "FloodTypes.hpp"
#include "OspfFlagManager.hpp"
#include "OspfOriginator.h"

namespace OSPF
{
struct AreaConfigs;
struct OspfPath;
class Topology;
class OspfInterface;

constexpr inline bool isMaxAge(const LsaHeader& h, uint16_t maxAge) noexcept
{
    return h.age >= maxAge;
}

constexpr inline uint16_t absDiffU16(uint16_t a, uint16_t b) noexcept
{
    return (a >= b) ? static_cast<uint16_t>(a - b) : static_cast<uint16_t>(b - a);
}

class OspfArea
{
public:
    struct Result final
    {
        InstallResult decision{};
        LsaRecord* record{nullptr};
    };

    explicit OspfArea(Topology& base, uint32_t area, std::pmr::memory_resource* mr = std::pmr::get_default_resource());

    // Getters
    LsdbTable& lsdb() noexcept { return db; }
    const LsdbTable& lsdb() const noexcept { return db; }
    const Topology& topology() const noexcept { return base; }
    Topology& topology() { return base; }
    AreaConfigs& getConfigs() { return cfgs; }
    const AreaConfigs& getConfigs() const noexcept { return cfgs; }
    FloodQueue& floodQueue() noexcept { return fq; }
    const FloodQueue& floodQueue() const noexcept { return fq; } OspfFlagManager& getFlags() { return flags; }
    const OspfFlagManager& getFlags() const noexcept { return flags; }
    const SpfManager& getSpfManager() const noexcept { return spfMgr; }

    void clear();
    void releaseMemory();

    // Flooding
    template<typename Policy>
    void flood();
    bool hasPendingFlood() const noexcept { return !fq.empty(); }
    void send(OspfInterface& iface, std::vector<LsaRecordRef>& records);
    std::vector<LsaRecordRef> tryDequeueFlood() { return fq.tryDequeueBatch(); }

    // Processing
    template <typename Policy>
    Result processLsa(const IncomingLsaContext& ctx, LsaBody&& body);
    void processExternalLsa(const IncomingLsaContext& ctx, LsaBody&& body);
    template <typename Policy>
    void processReoriginatedLsa(const LsaKey& key, LsaBody&& body, bool expire = false);
    void evaluateDecision(Result& decision, const IncomingLsaContext& ctx);
    bool compareLSASummary(const LsaHeader& hdr, const LsaKey& key) const;

    static LsaRecordFlags makeFlags(const IncomingLsaContext& ctx) noexcept;

    const uint32_t areaId;

protected:
    std::pmr::memory_resource* mr{nullptr};

    std::atomic<bool> shouldRequestSpf;
    std::atomic<uint8_t> options;

    AreaConfigs& cfgs;
    LsdbTable db;
    FloodQueue fq;
    Topology& base;
    SpfManager spfMgr;
    OspfFlagManager flags;

    OspfOriginator* originator{nullptr};

private:
    Result process(const IncomingLsaContext& ctx, LsaBody&& body);

    void enqueueFlood(LsaRecordRef& record);
    void enqueueFlood(LsaRecordRef&& record);

    InstallResult evaluateIncomingLsa(const LsaRecord* existing, const IncomingLsaContext& ctx, const LsaBody& body);
    LsaCompareResult compareLsaHeaders(const LsaHeader& a, const LsaHeader& b) const;
    bool compareLsaBody(const LsaBody& a, const LsaBody& b);
};
}

#endif // OSPF_LSA_FLOODING_ENGINE_H
