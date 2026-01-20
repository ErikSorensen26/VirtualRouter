// Retransmission.h

#ifndef RETRNASMISSION_H
#define RETRNASMISSION_H

#include <atomic>
#include <vector>
#include <optional>
#include <OspfPacket.hpp>
#include <FloodTypes.hpp>

namespace OSPF
{
class Neighbor;
class UnicastPacket;
struct LsaRecordRef;
struct LsaRecord;
struct LsaKey;

class Retransmission
{
public:
    std::atomic<uint32_t> dbdTimerId;
    std::atomic<uint32_t> lsrTimerId;
    std::atomic<uint32_t> lsuTimerId;

    // Reliability
    std::vector<std::pair<FloodInfo, LsaRecordRef>>::iterator addLsu(LsaRecordRef& ref);
    std::vector<LsaKey>::iterator addLsr(const LsaKey& key);

    bool hasLsu(LsaKey& key);
    bool hasLsr(LsaKey& key);

    std::optional<LsaRecordRef> getLsu(const LsaKey& key);
    void eraseLsu(LsaKey& key);
    void eraseLsr(LsaKey& key);

    void clearLsu();
    void clearLsr();

    bool getLsuActive();
    bool getLsrActive();

    std::mutex& getRelMtx() { return reliableMtx; }
    const std::vector<std::pair<FloodInfo, LsaRecordRef>>& getLsu() const { return outboundLsus; }
    std::vector<std::pair<FloodInfo, LsaRecordRef>>& getLsu() { return outboundLsus; }
    const std::vector<LsaKey>& getLsr() const { return outboundLsrs; }

    std::mutex reliableMtx;
    UnicastPacket dbdPacket;

private:
    // Reliability
    std::vector<std::pair<FloodInfo, LsaRecordRef>> outboundLsus;
    std::vector<LsaKey> outboundLsrs;
};
}

#endif // Retransmission
