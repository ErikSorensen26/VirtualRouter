// Retransmission.hpp

#ifndef RETRNASMISSION_HPP
#define RETRNASMISSION_HPP

#include "ospf/transmission/OspfPacket.hpp"
#include "ospf/database/LSDB.hpp"
#include "RetransmissionList.hpp"

namespace routing::ospf
{
class Neighbor;
class UnicastPacket;
struct LsaRecordRef;
struct LsaRecord;
struct LsaKey;

class Retransmission
{
public:
    Retransmission(OspfProcess& process, OspfInterface& iface)
        : outboundLsus(process, iface), outboundLsrs(process, iface) {}

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
} // namespace routing

#endif // RETRNASMISSION_HPP

