// v2PacketDispatcher.h

#ifndef V2_PACKET_DISPATCHER_H
#define V2_PACKET_DISPATCHER_H

#include <Ospfv2Header.hpp>
#include <OspfInterface.h>
#include <LSDB.hpp>
#include <PacketDispatcher.h>

#include <RouterLsaV2.hpp>
#include <NetworkLsaV2.hpp>
#include <SummaryNetworkLsa.hpp>
#include <SummaryRouterLsa.hpp>
#include <ExternalLsaV2.hpp>

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
    PacketDispatcherV2(OspfInterface& iface) : PacketDispatcher(iface) {}

    void handleIncoming(const Ospfv2Header& ospfHeader, const uint8_t* neighborIp, bool multicast);

    void sendHello() override;
    void sendUnicastHello(Neighbor& nbr) override;
    void sendInitDBD(Neighbor& nbr) override;
    bool sendDBD(Neighbor& nbr) override;
    bool sendLSAck(Neighbor& nbr, std::vector<LsaRecordRef>& records) override;
    bool sendReliableLSRequest(Neighbor& nbr, const std::vector<LsaKey>& dbds) override;
    bool sendLSRequest(Neighbor& nbr, const std::vector<LsaKey>& keys) override;
    bool sendReliableLSUpdate(Neighbor* nbr, std::vector<std::pair<FloodInfo, LsaRecordRef>>& keys) override;
    bool sendLSUpdate(Neighbor* nbr, std::vector<std::pair<FloodInfo, LsaRecordRef>>& keys) override;

    void retransmitDbd(Neighbor& nbr) override;

private:
    bool processOptions(uint32_t options, bool isStatic = false) override;

    void finalizeHeader(Ospfv2Header& hdr, OspfBuilder& builder, bool lls = false);

    void transmitReliable(PacketBuilder& pkt, Neighbor* neighbor, Ospfv2Header& header);
    bool setupDbd(Neighbor& neighbor, Ospfv2Header& pkt);
    void setupLsu(Neighbor& neighbor, std::vector<LsaRecordRef>& records);
    void setupLsr(Neighbor& neighbor, const std::vector<LsaKey>& records);
    void setupMulticastLsu(std::vector<LsaRecordRef>& records);

    uint16_t getMtu();

    std::optional<Ospfv2Header> buildHeader(PacketBuilder& builder, uint8_t type);
    std::optional<Ospfv2HelloHeader> buildHello(OspfBuilder builder, bool lls);
    std::optional<Ospfv2DBDHeader> buildDBD(OspfBuilder& builder, Neighbor& nbr, bool lls);
    std::optional<Ospfv2LSAHeader> buildLSAHeader(OspfBuilder& builder, const LsaKey& key, const LsaRecord& record);

    std::deque<PacketBuilder> buildLSRequestList(const std::vector<LsaKey>& records);
    std::deque<PacketBuilder> buildLSUpdateList(std::vector<std::pair<FloodInfo, LsaRecordRef>>& records, std::vector<LsaRecordRef>& sentKeys);

    size_t buildLSRequest(OspfBuilder& builder, std::span<const LsaKey>& key);
    size_t buildLSAck(OspfBuilder& builder, std::span<LsaRecordRef>& acks);
    size_t buildLSUpdate(OspfBuilder& builder, std::vector<LsaRecordRef>& sentKeys, std::span<std::pair<FloodInfo, LsaRecordRef>>& keys);

    void buildDescriptions(OspfBuilder& builder, Neighbor& nbr);
    bool buildLSABody(OspfBuilder& builder, LsaRecord& body, uint8_t type);

private:
    
    void processHello(HeaderInfo& info, bool unicast);
    void processDBD(HeaderInfo& info);
    void processLSAck(HeaderInfo& info);
    void processLSRequest(HeaderInfo& info);
    void processLSUpdate(HeaderInfo& info);

    void processLLSDataBlock(HeaderInfo& info);

    std::optional<LsaBody> buildLsaBody(uint8_t type, const uint8_t* buf, uint16_t len);
};
}

#endif // V2_PACKET_DISPATCHER_H
