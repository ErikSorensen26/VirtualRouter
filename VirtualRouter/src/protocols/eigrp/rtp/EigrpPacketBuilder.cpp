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
    std::memset(&packet, 0, sizeof(packet));

    packet.reserveHeader(HeaderType::EIGRP, EigrpHeader::fixedSize);
    auto* hdr = packet.nextBuildHeader();
    if (!hdr) return std::nullopt;

    EigrpHeader e;
    e.setBuffer(hdr->buffer);
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
    if (!iface.configs->authKey.fullyEnabled.load(std::memory_order_relaxed)) return;
    auto* buf = tlv.getNextValBuf(0);
    uint8_t len = iface.getAuth().buildAuthTLV(buf);
    tlv.append(Variable::Eigrp::Option::authentication, len, nullptr, len);
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
            ? static_cast<TLVBuilder::RouteType>(static_cast<uint8_t>(tlvVersion) ^ 1)
            : static_cast<TLVBuilder::RouteType>(tlvVersion);
        uint16_t len = TLVBuilder::encodeRouteOption(buf, tlv.maxSize(), r, bw, delay, type);
        if (len == 0) break;

        if (tlv.append(static_cast<uint16_t>(type), len + 4, nullptr, len))
            appended++;
        else break;
    }
    return appended;
}

size_t EigrpPacketBuilder::appendQueries(TLV16BufferManager& tlv, const std::vector<OutgoingQuery*>& queries, uint32_t seq, uint64_t bw, uint64_t delay, TLVType tlvVersion)
{
    size_t appended = 0;
    for (auto* q : queries)
    {
        auto* buf = tlv.getNextValBuf();
        TLVBuilder::RouteType type = q->route->originRoute->routeInfo.routeType == RouteType::EXTERNAL
            ? static_cast<TLVBuilder::RouteType>(static_cast<uint8_t>(tlvVersion) ^ 1)
            : static_cast<TLVBuilder::RouteType>(tlvVersion);
        uint16_t len = TLVBuilder::encodeRouteOption(buf, tlv.maxSize(), q->route->originRoute, bw, delay, type);
        if (len == 0) break;

        if (tlv.append(static_cast<uint8_t>(type), len + 4, nullptr, len))
        {
            appended++;
            q->querySequence = seq;
        }
        else break;
    }
    return appended;
}

size_t EigrpPacketBuilder::appendSIAQueries(TLV16BufferManager& tlv, const std::vector<OutgoingQuery*>& queries, uint32_t seq, uint64_t bw, uint64_t delay, TLVType tlvVersion)
{
    size_t appended = 0;
    for (auto* q : queries)
    {
        auto* buf = tlv.getNextValBuf();
        TLVBuilder::RouteType type = q->route->originRoute->routeInfo.routeType == RouteType::EXTERNAL
            ? static_cast<TLVBuilder::RouteType>(static_cast<uint8_t>(tlvVersion) ^ 1)
            : static_cast<TLVBuilder::RouteType>(tlvVersion);
        uint16_t len = TLVBuilder::encodeRouteOption(buf, tlv.maxSize(), q->route->originRoute, bw, delay, type);
        if (len == 0) break;

        if (tlv.append(static_cast<uint8_t>(type), len + 4, nullptr, len))
        {
            appended++;
            q->siaSequence = seq;
        }
        else break;
    }
    return appended;
}

bool EigrpPacketBuilder::appendParameterTLV(TLV16BufferManager& tlv, EigrpInterface& iface)
{
    uint8_t* val = tlv.getNextValBuf(8);
    if (!val) return false;
    EigrpConfigs::KValue k = iface.getBase().getGlobalConfigMgr().getKValues();
    TLVBuilder::calculateParameters(val, k, iface.configs->holdTime.load(std::memory_order_relaxed));
    return tlv.append(Variable::Eigrp::Option::parameter, 12, nullptr, 8);
}

bool EigrpPacketBuilder::appendVersionTLV(TLV16BufferManager& tlv)
{
    uint8_t* val = tlv.getNextValBuf(4);
    if (!val) return false;
    writeU16(val, Variable::Eigrp::Version::release);
    writeU16(val + 4, Variable::Eigrp::Version::tls);
    return tlv.append(Variable::Eigrp::Option::version, 8, nullptr, 4);
}

bool EigrpPacketBuilder::appendSequenceTLV(TLV16BufferManager& tlv, const IPAddress& ip)
{
    uint8_t ipSize = static_cast<uint8_t>(ip.isV6 ? 16 : 4);
    uint8_t* val = tlv.getNextValBuf(1 + ipSize);
    if (!val) return false;
    val[4] = ipSize;
    std::memcpy(val + 1, ip.raw, ipSize);
    return tlv.append(Variable::Eigrp::Option::sequence, 5 + ipSize, nullptr, 1 + ipSize);
}

bool EigrpPacketBuilder::appendMulticastSeqTLV(TLV16BufferManager& tlv, uint32_t seq)
{
    uint8_t* val = tlv.getNextValBuf(4);
    if (!val) return false;
    writeU32(val, seq);
    return tlv.append(Variable::Eigrp::Option::multicastSequence, 8, nullptr, 4);
}
}
