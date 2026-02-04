// Retransmission.hpp

#ifndef RETRNASMISSION_HPP
#define RETRNASMISSION_HPP

#include <OspfPacket.hpp>
#include <FloodTypes.hpp>
#include <LSDB.hpp>
#include "RetransmissionList.hpp"

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
    uint32_t dbdTimerId;

    // Reliability
    RetransmissionList<LsaKey, LsaRecordRef>& lsus() { return outboundLsus; }
    RetransmissionList<LsaKey, LsaKey>& lsrs() { return outboundLsrs; };

    bool getDbdActive() { return dbdTimerId != 0; }
    UnicastPacket dbdPacket;

private:
    // Reliability
    RetransmissionList<LsaKey, LsaRecordRef> outboundLsus;
    RetransmissionList<LsaKey, LsaKey> outboundLsrs;
};
}

#endif // RETRNASMISSION_HPP
