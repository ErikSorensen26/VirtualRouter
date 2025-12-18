// LsaFloodingEngine.h

#ifndef OSPF_LSA_FLOODING_ENGINE_H
#define OSPF_LSA_FLOODING_ENGINE_H

#include <memory_resource>

#include <LsdbTable.h>
#include "FloodQueue.h"
#include "FloodTypes.hpp"

namespace OSPF
{
class OspfProcess;

constexpr inline bool isMaxAge(const LsaHeader& h, uint16_t maxAge) noexcept
{
    return h.age >= maxAge;
}

constexpr inline uint16_t absDiffU16(uint16_t a, uint16_t b) noexcept
{
    return (a >= b) ? static_cast<uint16_t>(a - b) : static_cast<uint16_t>(b - a);
}

class LsaFloodingEngine
{
public:
    struct IncomingLsaContext final
    {
        const LsaKey& key;
        const LsaHeader& header;
        
        bool checksumValid{false};
        bool selfOriginatedKey{false};

        uint32_t area{0};
        uint32_t incomingInterface{0};
        bool excludeIncoming{true};
    };

    struct Result final
    {
        InstallResult decision{};
        LsaRecord* record{nullptr};
    };

    explicit LsaFloodingEngine(OspfProcess& base, std::pmr::memory_resource* mr = std::pmr::get_default_resource());

    LsdbTable& lsdb() noexcept { return db; }
    const LsdbTable& lsdb() const noexcept { return db; }

    FloodQueue& floodQueue() noexcept { return fq; }
    const FloodQueue& floodQueue() const noexcept { return fq; }

    void clear();
    void releaseMemory();

    bool hasPendingFlood() const noexcept { return !fq.empty(); }
    bool tryDequeueFlood(FloodRequest& out) { return fq.tryDequeue(out); }

    Result processLsaHeader(const IncomingLsaContext& ctx);
    Result processLsa(const IncomingLsaContext& ctx, LsaBody&& body);

private:
    std::pmr::memory_resource* mr{nullptr};
    LsdbTable db;
    FloodQueue fq;

    OspfProcess& base;

private:
    static LsaRecordFlags makeFlags(const IncomingLsaContext& ctx) noexcept;
    void enqueueFlood(const IncomingLsaContext& ctx);

    InstallResult evaluateIncomingLsa(const LsaRecord* existing, const IncomingLsaContext& ctx);
    LsaCompareResult compareLsaHeaders(const LsaHeader& a, const LsaHeader& b) noexcept;
};
}

#endif // OSPF_LSA_FLOODING_ENGINE_H
