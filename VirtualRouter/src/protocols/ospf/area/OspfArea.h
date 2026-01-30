// OspfArea.h

#ifndef OSPF_AREA_H
#define OSPF_AREA_H

#include <memory_resource>

#include <Registry.hpp>
#include <LsdbTable.h>
#include <SpfManager.h>
#include "FloodQueue.hpp"
#include "FloodTypes.hpp"
#include "OspfFlagManager.h"
#include "OspfOriginator.h"

namespace OSPF
{
struct OspfPath;
class OspfProcess;
class OspfInterface;

constexpr inline bool isMaxAge(const LsaHeader& h, uint16_t maxAge) noexcept
{
    return h.age >= maxAge;
}

constexpr inline uint16_t absDiffU16(uint16_t a, uint16_t b) noexcept
{
    return (a >= b) ? static_cast<uint16_t>(a - b) : static_cast<uint16_t>(b - a);
}

struct CalcResults
{
    uint16_t checksum;
    uint16_t size;
};

template<typename Policy>
CalcResults runLsaCalculations(const LsaHeader& hdr, const LsaKey& key, const LsaBody& body)
{
    CalcResults res;

    std::visit([&](auto& lsa) {
        using T = std::decay_t<decltype(lsa)>;
        if constexpr (!std::is_same_v<T, std::monostate>)
        {
            // Finalize length
            res.size = 20 + lsa.size();

            ChecksumFletcher check;

            if constexpr (std::is_same_v<Policy, PolicyV2>)
                check.add(hdr.options);

            check.addU16(key.lsaType);
            check.addU32(key.linkStateId);
            check.addU32(key.advertisingRouter);
            check.addU32(hdr.sequence);
            check.addU16(res.size); // Length

            lsa.appendChecksum(check);

            res.checksum = check.finalize();
        }
    }, body);

    return res;
}

class OspfArea
{
public:
    struct Result final
    {
        FloodReason reason;
        InstallResult decision{};
        LsaRecord* record{nullptr};
    };

    struct OspfAreaRange
    {
        // Config
        bool notAdvertise;
        std::optional<uint32_t> costOverride;

        // Runtime
        uint32_t contributorCount = 0;
        uint32_t computedMetric = 0;

        std::optional<uint32_t> summary = std::nullopt;
        bool discardPresent = false;

        std::unordered_set<LsaKey> suppressed;
    };

    explicit OspfArea(OspfProcess& base, uint32_t area, std::pmr::memory_resource* mr = std::pmr::get_default_resource());

    // Getters
    LsdbTable& lsdb() noexcept { return db; }
    const LsdbTable& lsdb() const noexcept { return db; }
    const OspfProcess& process() const noexcept { return base; }
    OspfProcess& process() { return base; }
    Config::OspfAreaRegistry& getConfigs() { return configs.get(); }
    const Config::OspfAreaRegistry& getConfigs() const noexcept { return configs.get(); }
    FloodQueue& floodQueue() noexcept { return fq; }
    const FloodQueue& floodQueue() const noexcept { return fq; }
    AreaFlagManager& getFlags() { return flags; }
    const AreaFlagManager& getFlags() const noexcept { return flags; }
    const SpfManager& getSpfManager() const noexcept { return spfMgr; }
    OspfOriginator& getOriginator() { return originator; }

    // Flooding
    template<typename Policy>
    void flood();
    void injectDefaultStubRoute();
    bool hasPendingFlood() const noexcept { return !fq.empty(); }
    void send(OspfInterface& iface, std::vector<std::pair<FloodInfo, LsaRecordRef>>& records);
    std::vector<std::pair<FloodInfo, LsaRecordRef>> tryDequeueFlood() { return fq.tryDequeueBatch(); }

    // Processing
    template <typename Policy>
    std::optional<Result> processLsa(IncomingLsaContext& ctx, LsaBody& body);
    template <typename Policy>
    void processSummaries(std::unordered_map<LsaKey, LsaBody>& summaries);
    void processExternalLsa(IncomingLsaContext& ctx, LsaBody& body);
    void evaluateDecision(Result& decision, const IncomingLsaContext& ctx);
    bool compareLSASummary(const LsaHeader& hdr, const LsaKey& key) const;

    // Range
    void syncRangeConfig();
    void syncRangeRuntime(const std::vector<std::pair<IPPrefix, OspfPath>>& pathList);
    void syncRangeSuppression(const std::unordered_set<IPPrefix>& ranges);
    void suppressInterAreaPrefix(const IPPrefix& prefix) const;
    std::unordered_set<IPPrefix> getRanges();

    // Other
    void clear();
    void releaseMemory();
    void runDCIntegrityScan();
    void setFloodReduction(OspfInterface& iface);

    static LsaRecordFlags makeFlags(const IncomingLsaContext& ctx) noexcept;

protected:
    std::pmr::memory_resource* mr{nullptr};

    std::atomic<bool> shouldRequestSpf;
    std::atomic<uint8_t> options;

    Config::Reference<Config::OspfAreaRegistry> configs;

    std::mutex rangeMu;
    std::unordered_map<IPPrefix, OspfAreaRange> ranges;

    LsdbTable db;
    FloodQueue fq;
    OspfProcess& base;
    SpfManager spfMgr;
    AreaFlagManager flags;

    OspfOriginator& originator;
private:
    Result process(IncomingLsaContext& ctx, LsaBody& body);

    void enqueueFlood(LsaRecordRef& record, FloodInfo info);
    void enqueueFlood(LsaRecordRef&& record, FloodInfo info);

    // Ranges
    std::unordered_map<IPPrefix, std::pair<uint32_t, uint32_t>> computeRangeContributors(const std::vector<std::pair<IPPrefix, OspfPath>>& intraAreaRoutes, const std::unordered_map<IPPrefix, OspfAreaRange>& ranges);
    template <typename Policy>
    void applyRange(OspfAreaRange& r);
    template <typename Policy>
    void withdrawRange(OspfAreaRange& r);

    InstallResult evaluateIncomingLsa(const LsaRecord* existing, IncomingLsaContext& ctx, const LsaBody& body);
    LsaCompareResult compareLsaHeaders(const LsaHeader& a, const LsaHeader& b) const;
    bool compareLsaBody(const LsaBody& a, const LsaBody& b);

public:
    const AreaType type;
    const uint32_t areaId;

    std::atomic<bool> dcCompatible{true};
};
}

#endif // OSPF_LSA_FLOODING_ENGINE_H
