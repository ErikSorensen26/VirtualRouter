// Retransmission.h

#ifndef RETRNASMISSION_H
#define RETRNASMISSION_H

#include <atomic>
#include <vector>
#include <optional>
#include <OspfPacket.hpp>

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
    std::vector<LsaRecordRef>::iterator addLsu(LsaRecordRef& ref);
    std::vector<LsaKey>::iterator addLsr(const LsaKey& key);

    bool hasLsu(LsaKey& key);
    bool hasLsr(LsaKey& key);

    std::optional<LsaRecordRef> moveLsu(const LsaKey& key);
    void eraseLsr(LsaKey& key);

    bool getLsuActive();
    bool getLsrActive();

    void retransmitDbd();

    std::mutex& getRelMtx() { return reliableMtx; }
    const std::vector<LsaRecordRef>& getLsu() const { return outboundLsus; }
    const std::vector<LsaKey>& getLsr() const { return outboundLsrs; }

    std::mutex reliableMtx;
    UnicastPacket dbdPacket;

private:
    // Reliability
    std::vector<LsaRecordRef> outboundLsus;
    std::vector<LsaKey> outboundLsrs;
};
}

#endif // Retransmission
