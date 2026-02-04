// PacketDispatcherV3.h

#ifndef V3_PACKET_DISPATCHER_H
#define V3_PACKET_DISPATCHER_H

#include <Ospfv3Header.hpp>
#include <OspfInterface.h>
#include <LSDB.hpp>
#include <PacketDispatcher.h>

#include <RouterLsaV3.hpp>
#include <NetworkLsaV3.hpp>
#include <SummaryNetworkLsa.hpp>
#include <SummaryRouterLsa.hpp>
#include <ExternalLsaV3.hpp>

struct IPAddress;
class PacketBuilder;

struct Ospfv3HelloHeader;
struct Ospfv3DBDHeader;
struct Ospfv3LSRHeader;
struct Ospfv3LSAHeader;

namespace OSPF
{
class OspfInterface;
class Neighbor;
class NeighborTable;

class PacketDispatcherV3 : public PacketDispatcher
{
public:
    PacketDispatcherV3(OspfInterface& iface, Config::Reference<Config::OspfInterfaceBaseRegistry>& configs);

    Config::OspfInterfaceBaseRegistry& getBaseConfigs() override;

    void handleIncoming(const Ospfv3Header& ospfHeader, const uint8_t* neighborIp, bool multicast);

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

    void finalizeHeader(Ospfv3Header& hdr, OspfBuilder& builder, bool lls = false);

    void transmitReliable(PacketBuilder& pkt, Neighbor* neighbor, Ospfv3Header& header);
    bool setupDbd(Neighbor& neighbor, Ospfv3Header& pkt);

    uint16_t getMtu();

    std::optional<Ospfv3Header> buildHeader(PacketBuilder& builder, uint8_t type);
    std::optional<Ospfv3HelloHeader> buildHello(OspfBuilder builder, bool lls);
    std::optional<Ospfv3DBDHeader> buildDBD(OspfBuilder& builder, Neighbor& nbr, bool lls);
    std::optional<Ospfv3LSAHeader> buildLSAHeader(OspfBuilder& builder, const LsaKey& key, const LsaRecord& record, bool floodReduction);
    std::optional<Ospfv3LSAHeader> buildCopyLSAHeader(OspfBuilder& builder, const LsaKey& key, const LsaRecord& record);

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

    std::optional<LsaBody> buildLsaBody(uint8_t type, const uint8_t* buf, uint16_t len);

    Config::Reference<Config::OspfInterfaceBaseRegistry> baseConfigs;
    Config::Reference<Config::OspfInterfaceRegistry> configs;
};
}

#endif // V3_PACKET_DISPATCHER_H

