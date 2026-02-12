// PacketDispatcher.h

#ifndef PACKET_DISPATCHER_H
#define PACKET_DISPATCHER_H

#include "configs/registry/router/OspfInterfaceRegistry.h"
#include "ospf/database/LSDB.hpp"
#include "ospf/neighbor/RetransmissionList.hpp"

struct IPAddress;
class PacketBuilder;

struct Ospfv2HelloHeader;
struct Ospfv2DBDHeader;
struct Ospfv2LSRHeader;
struct Ospfv2LSAHeader;

namespace OSPF
{
class UnicastPacket;
class OspfInterface;
class Neighbor;
class NeighborTable;

class PacketDispatcher
{
public:
    PacketDispatcher(OspfInterface& iface);
    virtual ~PacketDispatcher();

    virtual Config::OspfInterfaceBaseRegistry& getBaseConfigs() = 0;

    virtual void sendHello() = 0;
    virtual void sendUnicastHello(Neighbor& nbr) = 0;
    virtual void sendInitDBD(Neighbor& nbr) = 0;
    virtual bool sendDBD(Neighbor& nbr) = 0;
    virtual bool sendLSAck(Neighbor& nbr, std::vector<LsaRecordRef>& ) = 0;

    void sendReliableLSRequest(Neighbor& nbr, const std::vector<LsaKey>& dbds);
    void sendReliableLSUpdate(Neighbor* nbr, std::vector<std::pair<FloodInfo, LsaRecordRef>>& keys);

    virtual void onDbdRetransmissionTimer(Neighbor& nbr) = 0;
    void onLsuRetransmissionTimer(Neighbor& nbr);
    void onLsrRetransmissionTimer(Neighbor& nbr);

    void onLsuPacingTimer(Neighbor* nbr);
    void onLsrPacingTimer(Neighbor& nbr);

    RetransmissionList<LsaKey, LsaRecordRef>& getMulticastLsu() { return multicastLsus; }

protected:
    virtual bool sendLSRequest(Neighbor& nbr) = 0;
    virtual bool sendLSUpdate(Neighbor* nbr) = 0;

    virtual void transmit(PacketBuilder& pkt, const uint8_t* dest) = 0;

    virtual bool processOptions(uint32_t options, Neighbor& nbr) = 0;

    uint16_t calculateAge(bool floodReduction, const LsaRecord& record);
    uint16_t addLinkLocalExtension(uint8_t* buf, bool restart);
    void addLinkLocalChecksum(uint8_t* buf);

    struct OspfBuilder
    {
        PacketBuilder& pkt;
        uint8_t* buf = nullptr;
        size_t offset;
        size_t maxSize;

        bool hasRoom(size_t s)
            { return (offset + s) <= maxSize; }
        uint8_t* getBuf()
            { return buf + offset; }
    };

    struct HeaderInfo
    {
        HeaderInfo(uint8_t* pload, size_t psize, uint16_t len, uint8_t authType, const IPAddress& neighborIp, const uint32_t rid)
            : rid(rid), packetSize(psize), payloadSize(len), payload(pload), authType(authType), neighborIp(neighborIp) {}

        uint32_t rid;
        const size_t packetSize{0};     ///< Actual header size.
        const size_t payloadSize{0};    ///< Defined payload length that OSPF defines.
        size_t offset{0};               ///< Current offset of parsing.
        uint8_t* payload{nullptr};      ///< Pointer to payload data starting after the common OSPFv2 Header.
        uint8_t authType{0};            ///< Auth type.
        uint8_t authSize{0};            ///< Size of auth for LLS to use.

        const IPAddress& neighborIp;    ///< Sender IP.
        Neighbor* neighbor = nullptr;   ///< Neighbor object.
    };

    RetransmissionList<LsaKey, LsaRecordRef> multicastLsus;

    OspfInterface& iface;
    NeighborTable& ntable;
    AddressFamily af;
};
}

#endif // PACKET_DISPATCHER_H
