// EigrpPacketBuilder.cpp

#include "EigrpPacketBuilder.h"
#include "eigrp/core/Eigrp.h"
#include "processing/PacketBuilder.hpp"
#include "eigrp/core/EigrpConfig.h"
#include "eigrp/EigrpTypes.hpp"
#include "eigrp/interface/AuthHandler.h"
#include "eigrp/interface/EigrpInterface.h"
#include "eigrp/topology/TopologyTable.h"
#include "TLVBuilder.h"
#include "Neighbor.h"

namespace EIGRP
{
std::optional<EigrpHeader> EigrpPacketBuilder::buildHeader(PacketBuilder& packet,
    uint8_t opcode, uint32_t seq, uint32_t ack, uint16_t virId, uint32_t asn
)
{
    EigrpHeader e = packet.reserveAndBuildHeader<EigrpHeader>(HeaderType::EIGRP);
    if (!e.buffer) return std::nullopt;

    e.setVersion(2);
    e.setOpcode(opcode);
    e.setSequence(seq);
    e.setAck(ack);
    e.setVirtualRouterId(virId);
    e.setAutonomousSystem(asn);
    e.setFlagInit(false);
    e.setFlagCondRecv(false);
    e.setFlagRestart(false);
    e.setFlagEndOfTable(false);
    return e;
}

void EigrpPacketBuilder::appendAuthTLV(TLV16BufferManager& tlv, EigrpInterface& iface)
{
    if (!iface.isAuthEnabled()) return;
    auto* buf = tlv.getNextValBuf(52); // max: SHA256 (20 + 32)
    if (!buf) return;
    uint16_t valSize = iface.getAuth().buildAuthTLV(buf);
    if (valSize == 0) return;
    tlv.append(EIGRP_OPTION_AUTHENTICATION, static_cast<uint16_t>(valSize + 4), nullptr, valSize);
}

bool EigrpPacketBuilder::appendStubTLV(TLV16BufferManager& tlv, EigrpConfig& cfg)
{
    if (!cfg.stubEnabled()) return false;
    TLVBuilder::encodeStubOption(tlv.getNextValBuf(), cfg.getStubConfig());
    tlv.append(EIGRP_OPTION_STUB, 6, nullptr, 2);
    return true;
}

size_t EigrpPacketBuilder::appendRoutes(EigrpInterface& iface, TLV16BufferManager& tlv, const std::vector<const RouteInfo*>& routes, uint64_t bw, uint64_t delay, TLVType tlvVersion)
{
    size_t appended = 0;
    for (auto* r : routes)
    {
        auto* buf = tlv.getNextValBuf();
        TLVBuilder::RouteType type = r->routeInfo.routeType == RouteType::EXTERNAL
            ? static_cast<TLVBuilder::RouteType>(static_cast<uint16_t>(tlvVersion) | 3)
            : static_cast<TLVBuilder::RouteType>(static_cast<uint16_t>(tlvVersion) | 2);
        uint16_t len = TLVBuilder::encodeRouteOption(iface, buf, tlv.maxSize(), r, bw, delay, type);
        if (len == 0) break;

        if (tlv.append(static_cast<uint16_t>(type), len + 4, nullptr, len))
            appended++;
        else break;
    }
    return appended;
}

bool EigrpPacketBuilder::appendParameterTLV(TLV16BufferManager& tlv, EigrpInterface& iface)
{
    uint8_t* val = tlv.getNextValBuf(8);
    if (!val) return false;
    if (iface.getRtp().pendingPeerTermination.load(std::memory_order_relaxed))
    {
        std::memset(val, 0, 6);
        writeU16(val + 6, iface.configs.get<Config::EigrpInterface::HOLD_TIME>().load());
        iface.getRtp().pendingPeerTermination.store(false, std::memory_order_release);
    }
    else
    {
        KValue k = iface.getBase().getGlobalConfigMgr().getKValues();
        TLVBuilder::calculateParameters(val, k, iface.configs.get<Config::EigrpInterface::HOLD_TIME>().load());
    }
    return tlv.append(EIGRP_OPTION_PARAMETER, 12, nullptr, 8);
}

bool EigrpPacketBuilder::appendVersionTLV(TLV16BufferManager& tlv)
{
    uint8_t* val = tlv.getNextValBuf(4);
    if (!val) return false;
    writeU16(val, EIGRP_VERSION_RELEASE);
    writeU16(val + 2, EIGRP_VERSION_TLS);
    return tlv.append(EIGRP_OPTION_VERSION, 8, nullptr, 4);
}

size_t EigrpPacketBuilder::appendSequenceTLVs(TLV16BufferManager& tlv, const std::vector<IPAddress>& neighbors)
{
    if (neighbors.empty()) return 0;

    const uint8_t ipSize = static_cast<uint8_t>(neighbors.front().isIPv6() ? 16 : 4);
    size_t maxFit = (tlv.maxSize() - tlv.size() - 13) / ipSize;
    size_t amount = std::min(maxFit, neighbors.size());
    if (amount == 0) return 0;

    uint16_t payloadLength = static_cast<uint16_t>((amount * ipSize) + 1);
    uint16_t tlvLength = payloadLength + 4;

    auto* buf = tlv.getNextValBuf(payloadLength);
    if (!buf) return 0;

    buf[0] = ipSize;

    size_t offset = 1;
    for (size_t i = 0; i < amount; i++)
    {
        const auto& ip = neighbors[i];
        if (ip.isIPv4()) {
            writeU32(buf + offset, ip.v4());
        } else {
            writeU128(buf + offset, ip.v6());
        }
        offset += ipSize;
    }
    
    tlv.append(EIGRP_OPTION_SEQUENCE, tlvLength, nullptr, payloadLength);
    return amount;
}

bool EigrpPacketBuilder::appendMulticastSeqTLV(TLV16BufferManager& tlv, uint32_t seq)
{
    uint8_t* val = tlv.getNextValBuf(4);
    if (!val) return false;
    writeU32(val, seq);
    return tlv.append(EIGRP_OPTION_MULTICAST_SEQUENCE, 8, nullptr, 4);
}
}
