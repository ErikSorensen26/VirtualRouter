// v2PacketDispatcher.h

#ifndef V2_PACKET_DISPATCHER_H
#define V2_PACKET_DISPATCHER_H

#include "packet/headers/Ospfv2Header.hpp"
#include "ospf/interface/OspfInterface.h"
#include "ospf/database/LSDB.hpp"
#include "ospf/transmission/PacketDispatcher.h"

struct IPAddress;
class PacketBuilder;

struct Ospfv2HelloHeader;
struct Ospfv2DBDHeader;
struct Ospfv2LSRHeader;
struct Ospfv2LSAHeader;

namespace OSPF
{
class OspfInterface;
class Neighbor;
class NeighborTable;

class PacketDispatcherV2 : public PacketDispatcher
{
public:
    PacketDispatcherV2(OspfInterface& iface, Config::Reference<Config::OspfInterfaceBaseRegistry>& configs);

    Config::OspfInterfaceBaseRegistry& getBaseConfigs() override;

    void handleIncoming(const Ospfv2Header& ospfHeader, const uint8_t* neighborIp, bool multicast);

    void sendHello() override;
    void sendUnicastHello(Neighbor& nbr) override;
    void sendInitDBD(Neighbor& nbr) override;
    bool sendDBD(Neighbor& nbr) override;
    bool sendLSAck(Neighbor& nbr, std::vector<LsaRecordRef>& records) override;

    bool sendLSRequest(Neighbor& nbr) override;
    bool sendLSUpdate(Neighbor* nbr) override;

    void onDbdRetransmissionTimer(Neighbor& nbr) override;

private:
    void transmit(PacketBuilder& pkt, const uint8_t* dest = nullptr) override;

    bool processOptions(uint32_t options, Neighbor& nbr) override;

    void finalizeHeader(Ospfv2Header& hdr, OspfBuilder& builder, bool lls = false);

    void transmitReliable(PacketBuilder& pkt, Neighbor* neighbor, Ospfv2Header& header);
    bool setupDbd(Neighbor& neighbor, Ospfv2Header& pkt);

    uint16_t getMtu();

    std::optional<Ospfv2Header> buildHeader(PacketBuilder& builder, uint8_t type);
    std::optional<Ospfv2HelloHeader> buildHello(OspfBuilder builder, bool lls);
    std::optional<Ospfv2DBDHeader> buildDBD(OspfBuilder& builder, Neighbor& nbr, bool lls);
    std::optional<Ospfv2LSAHeader> buildLSAHeader(OspfBuilder& builder, const LsaKey& key, const LsaRecord& record, bool floodReduction);
    std::optional<Ospfv2LSAHeader> buildCopyLSAHeader(OspfBuilder& builder, const LsaKey& key, const LsaRecord& record);

    std::optional<PacketBuilder> buildLSRequest(Neighbor& nbr);
    std::optional<PacketBuilder> buildLSUpdate(Neighbor* nbr);

    size_t addLSRequests(OspfBuilder& builder, Neighbor& nbr);
    size_t addLSUpdates(OspfBuilder& builder, Neighbor* nbr);
    size_t addLSAcks(OspfBuilder& builder, std::span<LsaRecordRef>& acks);

    void buildDescriptions(OspfBuilder& builder, Neighbor& nbr);
    bool buildLSABody(OspfBuilder& builder, LsaRecord& body, uint8_t type);

private:
    
    void processHello(HeaderInfo& info, bool unicast);
    void processDBD(HeaderInfo& info);
    void processLSAck(HeaderInfo& info);
    void processLSRequest(HeaderInfo& info);
    void processLSUpdate(HeaderInfo& info);

    void processLLSDataBlock(PacketDispatcher::HeaderInfo& info);

    bool buildLLSAuthentication(OspfBuilder& info, uint16_t llsSize, uint32_t seq, uint8_t* secret);
    void buildOspfSimpleAuthentication(Ospfv2Header& header, uint64_t secret);
    bool buildOspfCryptoAuthentication(OspfBuilder& info, Ospfv2Header& hdr, uint32_t seq, uint8_t id, uint8_t* secret);

    bool processOspfSimpleAuthentication(const Ospfv2Header& hdr);
    bool processOspfCryptoAuthentication(HeaderInfo& info, const Ospfv2Header& hdr);

    std::optional<LsaBody> buildLsaBody(uint8_t type, const uint8_t* buf, uint16_t len);

    Config::Reference<Config::OspfInterfaceBaseRegistry> baseConfigs;
    Config::Reference<Config::OspfInterfaceRegistry> configs;
};
}

#endif // V2_PACKET_DISPATCHER_H
