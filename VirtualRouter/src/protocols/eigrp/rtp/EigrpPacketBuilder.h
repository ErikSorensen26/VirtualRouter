// EigrpPacketBuilder.h

#ifndef EIGRP_PACKET_BUILDER_H
#define EIGRP_PACKET_BUILDER_H

#include <optional>
#include <cstdint>
#include <vector>

struct EigrpHeader;
class TLV16BufferManager;
class PacketBuilder;
struct IPAddress;

namespace Eigrp
{
class EigrpInterface;
class EigrpConfig;
struct RouteInfo;
struct OutgoingQuery;
enum class TLVType : uint16_t;

class EigrpPacketBuilder
{
public:
    static std::optional<EigrpHeader> buildHeader(PacketBuilder& packet,
        uint8_t opcode, uint32_t seq, uint32_t ack, uint16_t virId, uint16_t asn);
    static bool appendParameterTLV(TLV16BufferManager& tlv, EigrpInterface& iface);
    static bool appendVersionTLV(TLV16BufferManager& tlv);
    static bool appendMulticastSeqTLV(TLV16BufferManager& tlv, uint32_t seq);
    static void appendAuthTLV(TLV16BufferManager& tlv, EigrpInterface& iface);
    static bool appendStubTLV(TLV16BufferManager& tlv, EigrpConfig& cfg);
    static size_t appendSequenceTLVs(TLV16BufferManager& tlv, const std::vector<IPAddress>& neighbors);
    static size_t appendRoutes(EigrpInterface& iface, TLV16BufferManager& tlv, const std::vector<const RouteInfo*>& routes, uint64_t bw, uint64_t delay, TLVType tlvVersion);
};
}


#endif // EIGRP_PACKET_BUILDER_H
