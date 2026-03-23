// PacketDispatcherV3.h

#ifndef V3_PACKET_DISPATCHER_H
#define V3_PACKET_DISPATCHER_H

#include "packet/headers/Ospfv3Header.hpp"
#include "ospf/interface/OspfInterface.h"
#include "ospf/database/LSDB.hpp"
#include "ospf/transmission/PacketDispatcher.h"

namespace processing { class PacketBuilder; }
namespace types { struct IPAddress; }
namespace packet { struct Ospfv3HelloHeader; struct Ospfv3DBDHeader; struct Ospfv3LSRHeader; struct Ospfv3LSAHeader; }

namespace routing::ospf
{
class OspfInterface;
class Neighbor;
class NeighborTable;

class PacketDispatcherV3 : public PacketDispatcher
{
public:
    PacketDispatcherV3(OspfInterface& iface, config::Reference<config::OspfInterfaceBaseRegistry>& configs);

    config::OspfInterfaceBaseRegistry& getBaseConfigs() override;

    void handleIncoming(const packet::Ospfv3Header& ospfHeader, const uint8_t* neighborIp, bool multicast);

    void sendHello() override;
    void sendUnicastHello(Neighbor& nbr) override;
    void sendInitDBD(Neighbor& nbr) override;
    bool sendDBD(Neighbor& nbr) override;
    bool sendLSAck(Neighbor& nbr, std::vector<LsaRecordRef>& records) override;

    bool sendLSRequest(Neighbor& nbr) override;
    bool sendLSUpdate(Neighbor* nbr) override;

    void onDbdRetransmissionTimer(Neighbor& nbr) override;

private:
    void transmit(processing::PacketBuilder& pkt, const types::IPAddress* dest = nullptr) override;

    bool processOptions(uint32_t options, Neighbor& nbr) override;

    void finalizeHeader(packet::Ospfv3Header& hdr, OspfBuilder& builder, bool lls = false);

    void transmitReliable(processing::PacketBuilder& pkt, Neighbor* neighbor, packet::Ospfv3Header& header);
    bool setupDbd(Neighbor& neighbor, packet::Ospfv3Header& pkt);

    uint16_t getMtu();

    std::optional<packet::Ospfv3Header> buildHeader(processing::PacketBuilder& builder, uint8_t type);
    std::optional<packet::Ospfv3HelloHeader> buildHello(OspfBuilder builder, bool lls);
    std::optional<packet::Ospfv3DBDHeader> buildDBD(OspfBuilder& builder, Neighbor& nbr, bool lls);
    std::optional<packet::Ospfv3LSAHeader> buildLSAHeader(OspfBuilder& builder, const LsaKey& key, const LsaRecord& record, bool floodReduction);
    std::optional<packet::Ospfv3LSAHeader> buildCopyLSAHeader(OspfBuilder& builder, const LsaKey& key, const LsaRecord& record);

    std::optional<processing::PacketBuilder> buildLSRequest(Neighbor& nbr);
    std::optional<processing::PacketBuilder> buildLSUpdate(Neighbor* nbr);

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

    std::optional<LsaBody> buildLsaBody(uint16_t type, const uint8_t* buf, uint16_t len);

    config::Reference<config::OspfInterfaceBaseRegistry> baseConfigs;
    config::Reference<config::OspfInterfaceRegistry> configs;
};
} // namespace routing

#endif // V3_PACKET_DISPATCHER_H


