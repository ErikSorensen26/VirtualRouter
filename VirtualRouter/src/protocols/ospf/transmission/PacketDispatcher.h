// PacketDispatcher.h

#ifndef PACKET_DISPATCHER_H
#define PACKET_DISPATCHER_H

#include <Registry.hpp>
#include <Ospfv2Header.hpp>
#include <LSDB.hpp>

#include <RouterLsaV2.hpp>
#include <NetworkLsaV2.hpp>
#include <SummaryNetworkLsa.hpp>
#include <SummaryRouterLsa.hpp>
#include <ExternalLsaV2.hpp>
#include <FloodTypes.hpp>

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
    virtual ~PacketDispatcher() = 0;

    virtual Config::OspfInterfaceBaseRegistry& getBaseConfigs();

    virtual void sendHello() = 0;
    virtual void sendUnicastHello(Neighbor& nbr) = 0;
    virtual void sendInitDBD(Neighbor& nbr) = 0;
    virtual bool sendDBD(Neighbor& nbr) = 0;
    virtual bool sendLSAck(Neighbor& nbr, std::vector<LsaRecordRef>& ) = 0;
    virtual bool sendReliableLSRequest(Neighbor& nbr, const std::vector<LsaKey>& dbds) = 0;
    virtual bool sendLSRequest(Neighbor& nbr, const std::vector<LsaKey>& keys) = 0;
    virtual bool sendReliableLSUpdate(Neighbor* nbr, std::vector<std::pair<FloodInfo, LsaRecordRef>>& keys) = 0;
    virtual bool sendLSUpdate(Neighbor* nbr, std::vector<std::pair<FloodInfo, LsaRecordRef>>& keys) = 0;

    virtual bool processOptions(uint32_t options);
    
    // TODO: finish demand citcuit in options

    virtual void retransmitDbd(Neighbor& nbr) = 0;
    bool retransmitLsu(Neighbor& nbr);
    bool retransmitLsr(Neighbor& nbr);

protected:

    virtual void transmit(PacketBuilder& pkt, const uint8_t* dest);

    uint16_t calculateAge(bool floodReduction, const LsaRecord& record);
    size_t addLinkLocalExtension(uint8_t* buf, bool restart);
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
        HeaderInfo(uint8_t* pload, size_t psize, uint16_t len, const IPAddress& neighborIp, const uint32_t rid, bool auth)
            : rid(rid), packetSize(psize), payloadSize(len), payload(pload), authEnabled(auth), neighborIp(neighborIp) {}

        uint32_t rid;
        const size_t packetSize{0};     ///< Actual header size.
        const size_t payloadSize{0};    ///< Defined payload length that OSPF defines.
        size_t offset{0};               ///< Current offset of parsing.
        uint8_t* payload{nullptr};      ///< Pointer to payload data starting after the common OSPFv2 Header.
        bool authEnabled{false};        ///< Indicates if this header has auth enabled

        const IPAddress& neighborIp;    ///< Sender IP.
        Neighbor* neighbor = nullptr;   ///< Neighbor object.
    };

    OspfInterface& iface;
    NeighborTable& ntable;
    AddressFamily af;
};
}

#endif // PACKET_DISPATCHER_H
