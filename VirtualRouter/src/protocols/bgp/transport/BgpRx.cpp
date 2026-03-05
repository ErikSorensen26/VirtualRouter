// BgpRx.cpp

#include <Functions.h>

#include "bgp/BgpProcess.h"
#include "packet/headers/BgpHeader.hpp"
#include "packet/headers/embedded/bgp/BgpOpenHeader.hpp"
#include "packet/TlvOptions.hpp"
#include "bgp/session/Session.h"
#include "bgp/neighbor/Neighbor.h"
#include "tcp/rx/RxConsumer.h"
#include "BgpRx.h"

namespace BGP
{
void BgpRx::handleIncoming(Session& session, TCP::RxConsumer& consumer)
{
    auto& buf = consumer.get();

    while (true)
    {
        if (buf.size() < BgpHeader::fixedSize) break;

        BgpHeader hdr;
        hdr.setBuffer(const_cast<uint8_t*>(buf.data()));

        // Validate marker.
        if (std::memcmp(hdr.getMarker(), BGP_MARKER, 16) != 0)
        {
            Notification error;
            error.code = BGP_NOTIFICATION_HEADER_CONNECTION_NOT_SYNCED;
            session.sendNotification(error);
            session.postEvent(FsmEvent::BGP_HEADER_ERR);
            return;
        }

        uint16_t length = hdr.getLength();

        // Minimum message length = 19 (header only)
        if (length < BgpHeader::fixedSize)
        {
            Notification error;
            error.code = BGP_NOTIFICATION_HEADER_BAD_MESSAGE_LENGTH;
            error.data.resize(2);
            writeU16(error.data.data(), length);
            session.sendNotification(error);
            session.postEvent(FsmEvent::BGP_HEADER_ERR);
            return;
        }

        // Extended message: normal cap is 4096; if negotiated, allow 65535.
        uint16_t maxLen = session.getNegotiated().extendedMessage
            ? kExtendedMessageLen
            : kMaxMessageLen;
        if (length > maxLen)
        {
            Notification error;
            error.code = BGP_NOTIFICATION_HEADER_BAD_MESSAGE_LENGTH;
            error.data.resize(2);
            writeU16(error.data.data(), length);
            session.sendNotification(error);
            session.postEvent(FsmEvent::BGP_HEADER_ERR);
            return;
        }

        if (buf.size() < length) break; // Wait for more data.

        hdr.setTrailSize(static_cast<size_t>(length - BgpHeader::fixedSize));
        std::span<uint8_t> payload(buf.data() + BgpHeader::fixedSize, length - BgpHeader::fixedSize);

        Notification error;
        bool ok = true;

        switch (hdr.getType())
        {
            case BGP_TYPE_OPEN:
            {
                ok = processOpen(session, payload, error);
                if (!ok)
                {
                    session.sendNotification(error);
                    session.postEvent(FsmEvent::BGP_OPEN_MSG_ERR);
                }
                break;
            }
            case BGP_TYPE_UPDATE:
            {
                ok = processUpdate(session, payload, error);
                if (!ok)
                {
                    session.sendNotification(error);
                    session.postEvent(FsmEvent::UPDATE_MSG_ERR);
                }
                break;
            }
            case BGP_TYPE_NOTIFICATION:
            {
                ok = processNotification(session, payload, error);
                break;
            }
            case BGP_TYPE_KEEPALIVE:
            {
                ok = processKeepalive(session, payload, error);
                if (!ok)
                {
                    session.sendNotification(error);
                    session.postEvent(FsmEvent::BGP_HEADER_ERR);
                }
                break;
            }
            case BGP_TYPE_ROUTE_REFRESH:
            {
                ok = processRouteRefresh(session, payload, error);
                if (!ok)
                {
                    session.sendNotification(error);
                }
                break;
            }
            default:
            {
                error.code = BGP_NOTIFICATION_HEADER_BAD_MESSAGE_TYPE;
                error.data.push_back(hdr.getType());
                session.sendNotification(error);
                session.postEvent(FsmEvent::BGP_HEADER_ERR);
                return;
            }
        }

        consumer.commit(length);
        if (!ok) return;
    }
}

bool BgpRx::processOpen(Session& session, std::span<uint8_t> payload, Notification& error)
{
    if (payload.size() < BgpOpenHeader::fixedSize)
    {
        error.code = BGP_NOTIFICATION_HEADER_BAD_MESSAGE_LENGTH;
        return false;
    }

    BgpOpenHeader open;
    open.setBuffer(payload.data());
    open.setTrailSize(payload.size() - BgpOpenHeader::fixedSize);

    // Version check
    if (open.getVersion() != BGP_VERSION)
    {
        error.code = BGP_NOTIFICATION_OPEN_UNSUPPORTED_VERSION;
        error.data.resize(2);
        writeU16(error.data.data(), BGP_VERSION);
        return false;
    }

    // BGP Identifier: must not be 0 or multicast
    uint32_t peerRid = open.getIdentifier();

    if (peerRid == 0 || Functions::isMulticast(open.getIdentifierBuf(), AddressFamily::IPv4))
    {
        error.code = BGP_NOTIFICATION_OPEN_BAD_IDENTIFIER;
        return false;
    }

    // Hold time: 0 or >= 3
    uint16_t peerHold = open.getHoldTime();
    if (peerHold == 1 || peerHold == 2)
    {
        error.code = BGP_NOTIFICATION_OPEN_UNACCEPTABLE_HOLD;
        return false;
    }

    // Minimum hold time (if configured).
    auto& baseCfg = session.getBaseConfig();
    auto& minHtOpt = baseCfg.get<Config::BgpTransportBase::MINIMUM_HOLDTIME>();
    if (minHtOpt.hasValue())
    {
        uint16_t minHt = minHtOpt.load();
        uint16_t negotiatedHold = (peerHold == 0) ? 0
            : std::min(session.holdTime, peerHold);
        if (negotiatedHold != 0 && negotiatedHold < minHt)
        {
            error.code = BGP_NOTIFICATION_OPEN_UNACCEPTABLE_HOLD;
            return false;
        }
    }

    // Peer AS
    uint16_t peerAs2 = open.getAsNumber();

    // Parameter length boundary check.
    bool extendedParamLen = false;
    uint16_t paramLen = open.getParameterLen();
    if (paramLen == 255 && payload.size() + 2 >= BgpOpenHeader::fixedSize)
    {
        paramLen = readU16(open.getTrailData());
        extendedParamLen = true;
    }

    if (BgpOpenHeader::fixedSize + (extendedParamLen ? 2 : 0) + paramLen != payload.size())
    {
        error.code = BGP_NOTIFICATION_OPEN_UNSUPPORTED_PARAMETER;
        return false;
    }

    // Parse optional parameters
    auto& peerCaps = session.getPeerCaps();
    peerCaps = {};

    uint8_t* params = payload.data() + BgpOpenHeader::fixedSize + (extendedParamLen ? 2 : 0);
    size_t pos = 0;
    while (pos + 2 <= static_cast<size_t>(paramLen))
    {
        uint8_t pType = params[pos];
        uint8_t pLen = params[pos + 1];
        if (pos + 2 + pLen > static_cast<size_t>(paramLen))
        {
            error.code = BGP_NOTIFICATION_OPEN_UNSUPPORTED_PARAMETER;
            return false;
        }
        if (pType == BGP_PARAMETER_CAPABILITY)
        {
            parseCapabilities(std::span<uint8_t>(params + pos + 2, pLen), peerCaps);
        }
        pos += 2 + pLen;
    }

    uint32_t resolvedAs = peerAs2;
    if (peerCaps.asn32bit)
        resolvedAs = peerCaps.asn;
    else if (peerAs2 == static_cast<uint16_t>(kAsTrans))
        resolvedAs = kAsTrans;

    // Validate remote AS matches configuration.
    auto& sessCfg = session.getNeighbor().getConfigs();
    auto& remAsOpt = sessCfg.get<Config::BgpNeighborSession::REMOTE_AS>();
    if (remAsOpt.hasValue())
    {
        uint32_t cfgAs = remAsOpt.load();
        if (cfgAs != 0 && resolvedAs != cfgAs)
        {
            error.code = BGP_NOTIFICATION_OPEN_BAD_PEER_AS;
            return false;
        }
    }

    session.holdTime = peerHold;
    session.setPeerRid(peerRid);

    Notification negError;
    session.negotiateCapabilities(negError);

    session.onOpenReceived(payload, error);
    return true;
}

bool BgpRx::processKeepalive(Session& session, std::span<uint8_t> payload, Notification& error)
{
    if (!payload.empty())
    {
        error.code = BGP_NOTIFICATION_HEADER_BAD_MESSAGE_LENGTH;
        error.data.resize(2);
        writeU16(error.data.data(), static_cast<uint16_t>(BgpHeader::fixedSize));
        return false;
    }
    return session.onKeepaliveReceived(payload, error);
}

bool BgpRx::processNotification(Session& session, std::span<uint8_t> payload, Notification& error)
{
    if (payload.size() < 4)
    {
        error.code = BGP_NOTIFICATION_REFRESH_INVALID_LENGTH;
        return false;
    }
    return session.onRouteRefreshReceived(payload, error);
}

bool BgpRx::processUpdate(Session& session, std::span<uint8_t> payload, Notification& error)
{
    if (payload.size() < 4)
    {
        error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST;
        return false;
    }

    size_t offset = 0;

    // Withdrawn routes length
    uint16_t withdrawnLen = readU16(payload.data() + offset);
    offset += 2;
    if (offset + withdrawnLen > payload.size())
    {
        error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST;
        return false;
    }

    std::span<uint8_t> withdrawnData(payload.data() + offset, withdrawnLen);
    offset += withdrawnLen;

    // Path attributes length
    if (offset + 2 > payload.size())
    {
        error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST;
        return false;
    }
    uint16_t attrLen = readU16(payload.data() + offset);
    offset += 2;
    if (offset + attrLen > payload.size())
    {
        error.code + BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST;
        return false;
    }

    std::span<uint8_t> attrData(payload.data() + offset, attrLen);
    offset += attrLen;

    // Legacy IPv4 unicast NLRI
    std::span<uint8_t> nlriData(payload.data() + offset, payload.size() - offset);

    // Parse into ParsedUpdate<IPPrefix>
    ParsedUpdate<IPPrefix> update;

    // Withdrawn routes (legacy IPv4 unicast).
    if (!parseNlriList<IPPrefix>(BGP_AFI_IPV4, BGP_SAFI_UNICAST, withdrawnData, update.withdrawn, false, error))
    {
        return false;
    }

    // Path attributes.
    PathAttribute<IPPrefix> attrs;
}

void BgpRx::parseCapabilities(std::span<uint8_t> data, Capabilities& out)
{
    size_t idx = 0;
    while (idx + 2 <= data.size())
    {
        uint8_t type = data[idx];
        uint8_t len = data[idx + 1];

        if (idx + 2 + len > data.size()) return;

        const uint8_t* p = data.data() + idx + 2;

        switch (type)
        {
            case BGP_CAPABILITY_MULTIPROTOCOL:
            {
                if (len == 4)
                {
                    uint16_t afi = readU16(p);
                    uint8_t safi = p[3];
                    out.mpFamilies.push_back({afi, safi});
                }
                break;
            }
            case BGP_CAPABILITY_ROUTE_REFRESH:
            {
                if (len == 0) out.routeRefresh = true;
                break;
            }
            case BGP_CAPABILITY_ENHANCED_ROUTE_REFRESH:
            {
                if (len == 0) out.enhancedRouteRefresh = true;
                break;
            }
            case BGP_CAPABILITY_OUTBOUND_FILTER:
            {
                if (len >= 5)
                {
                    uint16_t afi = readU16(p);
                    uint8_t safi = p[2];
                    uint8_t count = p[3];
                    size_t pos = 4;
                    for (uint8_t i = 0; i < count && pos + 2 <= static_cast<size_t>(len); ++i)
                    {
                        out.orfEntries.push_back({{afi, safi}, p[pos], p[pos + 1]});
                        pos += 2;
                    }
                    out.outboundRouteFiltering = true;
                }
                break;
            }
            case BGP_CAPABILITY_EXTENDED_NEXT_HOP:
            {
                for (size_t pos = 0; pos + 6 <= static_cast<size_t>(len); pos += 6)
                {
                    uint16_t nlriAfi = readU16(p + pos);
                    uint8_t nlriSafi = p[pos + 2];
                    uint16_t nhAfi = readU16(p + pos + 4);
                    out.extendedNextHopEntries.push_back({{nlriAfi, nlriSafi}, nhAfi});
                }
                if (len >= 6) out.extendedNextHop = true;
                break;
            }
            case BGP_CAPABILITY_EXTENDED_MESSAGE:
            {
                if (len == 0) out.extendedMessage = true;
                break;
            }
            case BGP_CAPABILITY_BGP_SEC:
            {
                if (len == 3)
                {
                    uint16_t afi = readU16(p);
                    uint8_t safi = p[2];
                    out.bgpsecFamilies.push_back({afi, safi});
                    out.bgpsec = true;
                }
                break;
            }
            case BGP_CAPABILITY_MULTIPLE_LABELS:
            {
                if (len >= 3)
                {
                    uint16_t afi = readU16(p);
                    uint8_t safi = p[2];
                    out.labeledFamilies.push_back({afi, safi});
                    out.multipleLabels = true;
                }
                break;
            }
            case BGP_CAPABILITY_GRACEFUL_RESTART:
            {
                if (len >= 2)
                {
                    uint16_t flagsTime = readU16(p);
                    out.gracefulRestart = true;
                    out.restarting = (flagsTime & 0x8000) != 0;
                    out.restartTime = flagsTime & 0x0FFF;

                    for (size_t pos = 2; pos + 4 <= static_cast<size_t>(len); pos += 4)
                    {
                        uint16_t afi = readU16(p + pos);
                        uint8_t safi = p[pos + 2];
                        uint8_t flags = p[pos + 3];
                        out.gracefulFamilies.push_back({{afi, safi}, (flags & 0x80) != 0});
                    }
                }
                break;
            }
            case BGP_CAPABILITY_32_BIT_AS:
            {
                if (len == 4)
                {
                    out.asn32bit = true;
                    out.asn = readU32(p);
                }
                break;
            }
            case BGP_CAPABILITY_ADD_PATH:
            {
                for (size_t pos = 0; pos + 4 <= static_cast<size_t>(len); pos += 4)
                {
                    uint16_t afi = readU16(p + pos);
                    uint8_t safi = p[pos + 2];
                    uint8_t mode = p[pos + 3];
                    out.addPathFamilies.push_back({{afi, safi}, mode});
                }
                if (len >= 4) out.addPath = true;
                break;
            }
            case BGP_CAPABILITY_LLGR:
            {
                for (size_t pos = 0; pos + 7 <= static_cast<size_t>(len); pos += 7)
                {
                    uint16_t afi = readU16(p + pos);
                    uint8_t safi = p[pos + 2];
                    uint8_t flags = p[pos + 3];
                    uint32_t staleTime = readU24(p + pos + 4);
                    out.llgrFamilies.push_back({{afi, safi}, staleTime, flags});
                }
                if (len >= 7) out.llgr = true;
                break;
            }
            default:
            {
                break;
            }
        }

        idx += 2 + len;
    }
}

Capabilities BgpRx::parseCapabilities(std::span<uint8_t> data, Capabilities& out)
{
    Capabilities cc;
    auto& capabilities = cc;

    size_t idx = 0;

    while (idx + 2 <= data.size())
    {
        uint8_t type = data[idx];
        uint8_t size = data[idx + 1];

        if (idx + 2 + size > data.size())
            return;

        const uint8_t* payload = data.data() + idx + 2;
        
        switch (type)
        {
            case BGP_CAPABILITY_MULTIPROTOCOL:
                if (size == 4)
                {
                    uint16_t afi = readU16(payload);
                    uint8_t safi = payload[3];

                    capabilities.mpFamilies.push_back({ afi, safi });
                }
                break;
            case BGP_CAPABILITY_ROUTE_REFRESH:
                if (size == 0)
                {
                    capabilities.routeRefresh = true;
                }
                break;
            case BGP_CAPABILITY_OUTBOUND_FILTER:
                if (size >= 5)
                {
                    uint16_t afi = readU16(payload);
                    uint8_t safi = payload[2];
                    uint8_t count = payload[3];

                    size_t pos = 4;

                    for (uint8_t i = 0; i < count && pos + 2 <= size; ++i)
                    {
                        uint8_t orfType = payload[pos];
                        uint8_t sendReceive = payload[pos + 1];

                        capabilities.orfEntries.push_back({
                            afi, safi, orfType, sendReceive
                        });

                        pos += 2;
                    }

                    capabilities.outboundRouteFiltering = true;
                }
                break;
            case BGP_CAPABILITY_EXTENDED_NEXT_HOP:
                if (size == 5)
                {
                    uint16_t nlriAfi = readU16(payload);
                    uint8_t safi = payload[2];
                    uint16_t nhAfi = readU16(payload + 3);

                    capabilities.extendedNextHopEntries.push_back({
                        nlriAfi, safi, nhAfi
                    });

                    capabilities.extendedNextHop = true;
                }
                break;
            case BGP_CAPABILITY_EXTENDED_MESSAGE:
                if (size == 0)
                    capabilities.extendedMessage = true;
                break;
            case BGP_CAPABILITY_BGP_SEC:
                if (size == 3)
                {
                    uint16_t afi = readU16(payload);
                    uint8_t safi = payload[2];

                    capabilities.bgpsecFamilies.push_back({ afi, safi });
                    capabilities.bgpsec = true;
                }
                break;
            case BGP_CAPABILITY_MULTIPLE_LABELS:
                if (size == 4)
                {
                    uint16_t afi = readU16(payload);
                    uint8_t safi = payload[3];

                    capabilities.labeledFamilies.push_back({ afi, safi });
                    capabilities.multipleLabels = true;
                }
                break;
            case BGP_CAPABILITY_GRACEFUL_RESTART:
                if (size >= 2)
                {
                    uint16_t flagsTime = readU16(payload);

                    capabilities.gracefulRestart = true;
                    capabilities.restarting = (flagsTime & 0x8000) != 0;
                    capabilities.restartTime = flagsTime & 0x0FFF;

                    size_t pos = 2;

                    while (pos + 4 <= size)
                    {
                        uint16_t afi = readU16(payload + pos);
                        uint8_t safi = payload[pos + 2];
                        uint8_t flags = payload[pos + 3];

                        capabilities.gracefulFamilies.push_back({
                            afi, safi, (flags & 0x80) != 0
                        });

                        pos += 4;
                    }
                }
                break;
            case BGP_CAPABILITY_32_BIT_AS:
                if (size == 4)
                {
                    capabilities.asn32bit = true;
                    capabilities.asn = readU32(payload);
                }
                break;
            case BGP_CAPABILITY_ADD_PATH:
                if (size >= 4)
                {
                    size_t pos = 0;

                    while (pos + 4 <= size)
                    {
                        uint16_t afi = readU16(payload + pos);
                        uint8_t safi = payload[pos + 2];
                        uint8_t mode = payload[pos + 3];

                        capabilities.addPathFamilies.push_back({
                            afi, safi, mode
                        });

                        pos += 4;
                    }

                    capabilities.addPath = true;
                }
                break;
            case BGP_CAPABILITY_ENHANCED_ROUTE_REFRESH:
                if (size == 0)
                    capabilities.enhancedRouteRefresh = true;
                break;
            case BGP_CAPABILITY_LLGR:
                if (size >= 7)
                {
                    size_t pos = 0;

                    while (pos + 7 <= size)
                    {
                        uint16_t afi = readU16(payload + pos);
                        uint8_t safi = payload[pos + 2];

                        uint32_t staleTime = readU24(payload + pos + 3);

                        capabilities.llgrFamilies.push_back({
                            afi, safi, staleTime
                        });

                        pos += 7;
                    }

                    capabilities.llgr = true;
                }
                break;
            default: break;
        }

        idx += 2 + size;
    }
}

void extractPathAttributes
}

