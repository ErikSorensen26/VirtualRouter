// EigrpPacketBuilder.h

#ifndef EIGRP_PACKET_BUILDER_H
#define EIGRP_PACKET_BUILDER_H

#include <optional>
#include "packet/headers/EigrpHeader.hpp"
#include <cstdint>
#include <vector>

namespace processing { class PacketBuilder; }
namespace types { struct IPAddress; }
namespace packet { class TLV16BufferManager; }

namespace routing::eigrp
{
class EigrpInterface;
class EigrpConfig;
struct RouteInfo;
struct OutgoingQuery;
enum class TLVType : uint16_t;

class EigrpPacketBuilder
{
public:
    static std::optional<packet::EigrpHeader> buildHeader(processing::PacketBuilder& packet,
        uint8_t opcode, uint32_t seq, uint32_t ack, uint16_t virId, uint32_t asn);
    static bool appendParameterTLV(packet::TLV16BufferManager& tlv, EigrpInterface& iface);
    static bool appendVersionTLV(packet::TLV16BufferManager& tlv);
    static bool appendMulticastSeqTLV(packet::TLV16BufferManager& tlv, uint32_t seq);
    static void appendAuthTLV(packet::TLV16BufferManager& tlv, EigrpInterface& iface);
    static bool appendStubTLV(packet::TLV16BufferManager& tlv, EigrpConfig& cfg);
    static size_t appendSequenceTLVs(packet::TLV16BufferManager& tlv, const std::vector<types::IPAddress>& neighbors);
    static size_t appendRoutes(EigrpInterface& iface, packet::TLV16BufferManager& tlv, const std::vector<const RouteInfo*>& routes, uint64_t bw, uint64_t delay, TLVType tlvVersion);
};
} // namespace routing

#endif // EIGRP_PACKET_BUILDER_H

