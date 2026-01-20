// v3PacketDispatcher.h

#ifndef PACKET_DISPATCHER_V3_H
#define PACKET_DISPATCHER_V3_H

#include <Ospfv3Header.hpp>
#include <LSDB.hpp>
#include <PacketDispatcher.h>

#include <RouterLsaV3.hpp>
#include <NetworkLsaV3.hpp>
#include <InterAreaPrefixLsa.hpp>
#include <InterAreaRouterLsa.hpp>
#include <ExternalLsaV3.hpp>
#include <LinkLsa.hpp>
#include <IntraAreaPrefixLsa.hpp>

struct IPAddress;

namespace OSPF
{
class OspfInterface;
class Neighbor;

class PacketDispatcherV3 : PacketDispatcher
{
public:
    PacketDispatcherV3(OspfInterface& iface) : PacketDispatcher(iface) {}

    void handleIncoming(const Ospfv3Header& ospfHeader, const uint8_t* neighborIp, bool multicast);

private:
    bool processOptions(uint32_t options) override;

    struct HeaderInfo
    { HeaderInfo(const Ospfv3Header& ospf, const IPAddress& neighborIp, const uint32_t rid) : rid(rid), ospf(ospf), neighborIp(neighborIp) {
            payloadSize = ospf.getPacketLen();
            payload = ospf.getTrail().data();
        }

        uint32_t rid;
        size_t payloadSize{0};      ///< Defined payload length that OSPF defines.
        size_t offset{0};           ///< Current offset of parsing.
        uint8_t* payload{nullptr};  ///< Pointer to payload data starting after the common OSPFv2 Header.

        const Ospfv3Header& ospf;   ///< OSPFv3 header.
        const IPAddress& neighborIp; ///< Sender IP.
        Neighbor* neighbor = nullptr; ///< Neighbor object.
    };
    
    void processHello(HeaderInfo& info);
    void processDBD(HeaderInfo& info);
    void processLSAck(HeaderInfo& info);
    void processLSRequest(HeaderInfo& info);
    void processLSUpdate(HeaderInfo& info);

    void processLLSDataBlock(HeaderInfo& info);

    bool sendLSAck(std::vector<LsaRecord*>& records);

    std::optional<LsaBody> buildLsaBody(uint8_t type, const uint8_t* buf, uint16_t len);
};
}

#endif // V3_PACKET_DISPATCHER_H
