// OspfArea.h

#ifndef OSPF_AREA_H
#define OSPF_AREA_H

#include <memory_resource>

#include <LsdbTable.h>
#include "FloodQueue.hpp"
#include "FloodTypes.hpp"

namespace OSPF
{
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

    LsdbTable& lsdb() noexcept { return db; }
    const LsdbTable& lsdb() const noexcept { return db; }

    void flood();
    void send(OspfInterface& iface, std::vector<LsaRecordRef>& records);

    FloodQueue& floodQueue() noexcept { return fq; }
    const FloodQueue& floodQueue() const noexcept { return fq; }

    void clear();
    void releaseMemory();

    bool hasPendingFlood() const noexcept { return !fq.empty(); }
    std::vector<LsaRecordRef> tryDequeueFlood() { return fq.tryDequeueBatch(); }

    Result processLsa(const IncomingLsaContext& ctx, LsaBody& body);
    bool compareLSASummary(const LsaHeader& hdr, const LsaKey& key) const;

    const uint32_t areaId;

private:
    std::pmr::memory_resource* mr{nullptr};

    LsdbTable db;
    FloodQueue fq;
    Topology& base;

private:
    static LsaRecordFlags makeFlags(const IncomingLsaContext& ctx) noexcept;
    void enqueueFlood(LsaRecordRef& record);
    void enqueueFlood(LsaRecordRef&& record);

    InstallResult evaluateIncomingLsa(const LsaRecord* existing, const IncomingLsaContext& ctx);
    LsaCompareResult compareLsaHeaders(const LsaHeader& a, const LsaHeader& b) const;
};
}

#endif // OSPF_LSA_FLOODING_ENGINE_H
