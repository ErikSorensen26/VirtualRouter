// EigrpPacketBuilder.cpp

// TODO support uint32_t asn
#include <EigrpPacketBuilder.h>
#include <PacketBuilder.hpp>
#include <EigrpTypes.hpp>
#include <AuthHandler.h>
#include <EigrpInterface.h>
#include <EigrpConfig.h>
#include <Eigrp.h>
#include "TLVBuilder.h"
#include "Neighbor.h"

namespace Eigrp
{
std::optional<EigrpHeader> EigrpPacketBuilder::buildHeader(PacketBuilder& packet,
                               uint8_t opcode,
                               uint32_t seq,
                               uint32_t ack,
                               uint16_t virId,
                               uint16_t asn)
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
    if (!iface.configs->auth.fullyEnabled.load(std::memory_order_relaxed)) return;
    auto* buf = tlv.getNextValBuf(36);
    iface.getAuth().buildAuthTLV(buf);
    tlv.append(Variable::Eigrp::Option::authentication, 40, nullptr, 36);
}

bool EigrpPacketBuilder::appendStubTLV(TLV16BufferManager& tlv, EigrpConfig& cfg)
{
    if (!cfg.stubEnabled()) return false;
    TLVBuilder::encodeStubOption(tlv.getNextValBuf(), cfg.getStubConfig());
    tlv.append(Variable::Eigrp::Option::stub, 6, nullptr, 2);
    return true;
}

size_t EigrpPacketBuilder::appendRoutes(TLV16BufferManager& tlv, const std::vector<const RouteInfo*>& routes, uint64_t bw, uint64_t delay, TLVType tlvVersion)
{
    size_t appended = 0;
    for (auto* r : routes)
    {
        auto* buf = tlv.getNextValBuf();
        TLVBuilder::RouteType type = r->routeInfo.routeType == RouteType::EXTERNAL
            ? static_cast<TLVBuilder::RouteType>(static_cast<uint16_t>(tlvVersion) | 3)
            : static_cast<TLVBuilder::RouteType>(static_cast<uint16_t>(tlvVersion) | 2);
        uint16_t len = TLVBuilder::encodeRouteOption(buf, tlv.maxSize(), r, bw, delay, type);
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
        std::memset(val, 255, 6);
        writeU16(val + 6, iface.configs->holdTime.load(std::memory_order_relaxed));
        iface.getRtp().pendingPeerTermination.store(false, std::memory_order_release);
    }
    else
    {
        EigrpConfigs::KValue k = iface.getBase().getGlobalConfigMgr().getKValues();
        TLVBuilder::calculateParameters(val, k, iface.configs->holdTime.load(std::memory_order_relaxed));
    }
    return tlv.append(Variable::Eigrp::Option::parameter, 12, nullptr, 8);
}

bool EigrpPacketBuilder::appendVersionTLV(TLV16BufferManager& tlv)
{
    uint8_t* val = tlv.getNextValBuf(4);
    if (!val) return false;
    writeU16(val, Variable::Eigrp::Version::release);
    writeU16(val + 2, Variable::Eigrp::Version::tls);
    return tlv.append(Variable::Eigrp::Option::version, 8, nullptr, 4);
}

size_t EigrpPacketBuilder::appendSequenceTLVs(TLV16BufferManager& tlv, const std::vector<IPAddress>& neighbors)
{
    if (neighbors.empty()) return 0;

    const uint8_t ipSize = static_cast<uint8_t>(neighbors.front().isV6 ? 16 : 4);
    size_t maxFit = (tlv.maxSize() - tlv.size() - 13) / ipSize;
    size_t amount = std::min(maxFit, neighbors.size());
    if (amount == 0) return 0;

    uint16_t payloadLength = static_cast<uint16_t>((amount * ipSize) + 1);
    uint16_t tlvLength = payloadLength + 4;

    auto* buf = tlv.getNextValBuf(payloadLength);
    if (!buf) return 0;

    buf[0] = ipSize;

    size_t offset = 1;
    for (int i = 0; i < amount; i++)
    {
        const auto& ip = neighbors[i];
        std::memcpy(buf + offset, ip.raw, ipSize);
        offset += ipSize;
    }
    
    tlv.append(Variable::Eigrp::Option::sequence, tlvLength, nullptr, payloadLength);
    return amount;
}

bool EigrpPacketBuilder::appendMulticastSeqTLV(TLV16BufferManager& tlv, uint32_t seq)
{
    uint8_t* val = tlv.getNextValBuf(4);
    if (!val) return false;
    writeU32(val, seq);
    return tlv.append(Variable::Eigrp::Option::multicastSequence, 8, nullptr, 4);
}
}
