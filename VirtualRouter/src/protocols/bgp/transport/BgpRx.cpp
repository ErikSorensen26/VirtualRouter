// BgpRx.cpp

// TODO: finish multi session
//
//make temp list of connections using connection id
//all neighbor af sessions will already be up because they go up once the base session goes up
//if session is already established, assume its a temp cid, get af, apply it to the session

#include "bgp/BgpProcess.h"
#include "packet/headers/BgpHeader.hpp"
#include "packet/headers/embedded/bgp/BgpOpenHeader.hpp"
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
                ok = processOpen(session, consumer.getId(), payload, error);
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

bool BgpRx::processOpen(Session& session, uint64_t cid, std::span<uint8_t> payload, Notification& error)
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

    uint8_t ridFirstOctet = static_cast<uint8_t>(peerRid >> 24);
    if (peerRid == 0 || peerRid == 0xFFFFFFFF || (ridFirstOctet >= 224 && ridFirstOctet <= 239))
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
    if (paramLen == 255 && payload.size() >= BgpOpenHeader::fixedSize + 2)
    {
        paramLen = readU16(open.getTrailData());
        extendedParamLen = true;
    }

    if (BgpOpenHeader::fixedSize + (extendedParamLen ? 2 : 0) + paramLen != payload.size())
    {
        error.code = BGP_NOTIFICATION_OPEN_UNSUPPORTED_PARAMETER;
        return false;
    }

    uint8_t* params = payload.data() + BgpOpenHeader::fixedSize + (extendedParamLen ? 2 : 0);

    // Resolve multi session before parsing capabilities
    Session* curSession = &session; // Handles session swap.
    if (auto* ms = session.getMultiSession(); ms && cid != session.getPrimaryConnection()->getId())
    {
        auto msAfi = resolveMultiSessionAf(std::span<uint8_t>{params, paramLen});
        if (!msAfi)
        {
            ms->close(cid);
            return false;
        }

        NegotiatedCapabilities& nc = session.getNegotiated();
        
        if (nc.multiSess && nc.multiSessionFamilies.contains(*msAfi) && nc.activeFamilies.contains(*msAfi))
        {
            curSession = ms->activateSession(cid, *msAfi);
            if (!curSession)
            {
                ms->close(cid);
                return false;
            }
        }
    }

    auto& peerCaps = curSession->getPeerCaps();
    peerCaps = {};

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

    if (!curSession || curSession->verifyConnection(cid))
        return false;

    uint32_t resolvedAs = peerAs2;
    if (curSession->getPeerCaps().asn32bit)
        resolvedAs = curSession->getPeerCaps().asn;
    else if (peerAs2 == static_cast<uint16_t>(kAsTrans))
        resolvedAs = kAsTrans;

    // Validate remote AS matches configuration.
    auto& sessCfg = curSession->getNeighbor().getConfigs();
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

    curSession->holdTime = peerHold;
    curSession->setPeerRid(peerRid);
    curSession->negotiateCapabilities();
    curSession->onOpenReceived();
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
    session.onKeepaliveReceived();
    return true;
}

bool BgpRx::processNotification(Session& session, std::span<uint8_t> payload, Notification& /*error*/)
{
    session.onNotificationReceived(payload);
    return true;
}

bool BgpRx::processUpdate(Session& session, std::span<uint8_t> payload, Notification& error)
{
    if  (payload.size() < 4)
    {
        error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST;
        return false;
    }

    IncomingUpdate update;

    size_t offset = 0;

    uint16_t withdrawnLen = readU16(payload.data() + offset);
    offset += 2;

    if (offset + withdrawnLen > payload.size())
    {
        error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_AS_PATH;
        return false;
    }

    update.withdrawnData = std::span<uint8_t>(payload.data() + offset, withdrawnLen);
    offset += withdrawnLen;

    // Path Attributes
    if (offset + 2 > payload.size())
    {
        error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST;
        return false;
    }

    uint16_t attrLen = readU16(payload.data() + offset);
    offset += 2;

    if (offset + attrLen > payload.size())
    {
        error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST;
        return false;
    }

    // Path attributes
    Attributes attrs;
    Path path;

    if (attrLen != 0)
    {
        if (!parsePathAttributes(session, {payload.data() + offset, attrLen}, update, error))
        {
            return error.code == 0; // Do not invalide if there is no error
        }
    }
    offset += attrLen;

    // Legacy NLRI selection
    if (update.afi.afi == BGP_AFI_IPV4 && update.afi.safi == BGP_SAFI_UNICAST)
        update.nlriData = std::span<uint8_t>(payload.data() + offset, payload.size() - offset);

    AddressFamilyVariant* af = session.getNeighbor().getProcess().findAddressFamily(update.afi);
    if (!af) // Af not enabled
    {
        error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST;
        return false;
    }

    return std::visit([&](auto&& fam) -> bool {
        return fam.onUpdateFromPeer(session, update, error);
    }, *af);
}

AfiSafi findMpAfiSafi(std::span<const uint8_t> attrData)
{
    const uint8_t* ptr = attrData.data();
    const uint8_t* end = ptr + attrData.size();

    while (ptr + 2 < end)
    {
        uint8_t flags = *ptr++;
        uint8_t type = *ptr++;

        uint16_t length = 0;
        if (flags & BGP_ATTR_FLAG_EXTENDED_LENGTH)
        {
            if (ptr + 2 > end) break;
            length = readU16(ptr);
            ptr += 2;
        }
        else
        {
            length = *ptr++;
        }

        if (type == BGP_ATTR_MP_REACH_NLRI || type == BGP_ATTR_MP_UNREACH_NLRI)
        {
            if (ptr + 3 <= end)
            {
                uint16_t afi = readU16(ptr);
                uint8_t safi = ptr[3];
                return {afi, safi};
            }
        }

        ptr += length;
    }

    return {BGP_AFI_IPV4, BGP_SAFI_UNICAST};
}

std::optional<AfiSafi> BgpRx::resolveMultiSessionAf(std::span<uint8_t> data)
{
    size_t pos = 0;

    std::optional<AfiSafi> multiProtocol;
    std::optional<AfiSafi> multiSession;

    while (pos + 2 <= static_cast<size_t>(data.size()))
    {
        uint8_t pType = data[pos];
        uint8_t pLen = data[pos + 1];

        if (pos + 2 + pLen > data.size())
            return std::nullopt;

        if (pType == BGP_PARAMETER_CAPABILITY)
        {
            size_t idx = 0;

            while (idx + 2 <= data.size())
            {
                uint8_t type = data[idx];
                uint8_t len = data[idx + 1];

                if (idx + 2 + len > data.size())
                    return std::nullopt;

                const uint8_t* p = data.data() + idx + 2;

                switch (type)
                {
                    case BGP_CAPABILITY_MULTIPROTOCOL:
                    {
                        if (len == 4)
                        {
                            uint16_t afi = readU16(p);
                            uint8_t safi = p[3];
                            if (multiProtocol.has_value())
                                return std::nullopt;
                            multiProtocol.emplace(afi, safi);
                        }
                        break;
                    }
                    case BGP_CAPABILITY_MULTI_SESSION:
                    {
                        for (size_t i = 0; i + 4 <= static_cast<size_t>(len); i += 4)
                        {
                            uint16_t afi = readU16(p + i);
                            uint8_t safi = p[i + 2];
                            if (multiSession.has_value())
                                return std::nullopt;
                            multiProtocol.emplace(afi, safi);
                        }
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
        pos += 2 + pLen;
    }

    if (multiProtocol.has_value() && multiSession.has_value() && *multiProtocol == multiSession)
        return *multiSession;
    return std::nullopt;
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
            case BGP_CAPABILITY_MULTI_SESSION:
            {
                for (size_t pos = 0; pos + 4 <= static_cast<size_t>(len); pos += 4)
                {
                    uint16_t afi = readU16(p + pos);
                    uint8_t safi = p[pos + 2];
                    out.multiSessionFamilies.push_back({afi, safi});
                }
                if (len >= 4) out.multiSess = true;
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
            case BGP_CAPABILITY_FQDN:
            {
                if (len >= 1)
                {
                    uint8_t hnLen = p[0];
                    if (1 + hnLen <= len)
                    {
                        out.hostname.assign(reinterpret_cast<const char*>(p + 1), hnLen);
                        size_t domainOff = 1 + hnLen;
                        if (domainOff + 1 <= static_cast<size_t>(len))
                        {
                            uint8_t domLen = p[domainOff];
                            if (domainOff + 1 + domLen <= static_cast<size_t>(len))
                                out.domain.assign(reinterpret_cast<const char*>(p + domainOff + 1), domLen);
                        }
                        out.fqdn = true;
                    }
                }
                break;
            }
            case BGP_CAPABILITY_LINK_LOCAL_NEXT_HOP:
            {
                if (len == 0) out.linkLocalNextHop = true;
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

bool BgpRx::processRouteRefresh(Session& session, std::span<uint8_t> payload, Notification& error)
{
    // RFC 2918: 4-byte body — AFI (2), Reserved/Subtype (1), SAFI (1)
    if (payload.size() < 4)
    {
        error.code = BGP_NOTIFICATION_HEADER_BAD_MESSAGE_LENGTH;
        error.data.resize(2);
        writeU16(error.data.data(), static_cast<uint16_t>(BgpHeader::fixedSize + payload.size()));
        return false;
    }

    AfiSafi family;
    family.afi  = readU16(payload.data());
    uint8_t subtype = payload[2]; // enhanced-RR subtype (BORR/EORR) or ORF when-to-refresh
    family.safi = payload[3];

    // Parse ORF TLVs if present and negotiated (RFC 5291/5292)
    std::vector<OrfPrefixEntry> orfEntries;
    if (payload.size() > 4 && session.getNegotiated().canReceiveOrf(family, BGP_ORF_TYPE_PREFIX_LIST))
    {
        size_t pos = 4;
        while (pos + 3 <= payload.size())
        {
            uint8_t  orfType = payload[pos];
            uint16_t orfLen  = readU16(payload.data() + pos + 1);
            pos += 3;
            if (pos + orfLen > payload.size()) break;

            if (orfType == BGP_ORF_TYPE_PREFIX_LIST)
            {
                size_t epos = 0;
                while (epos < orfLen)
                {
                    uint8_t actionMatch = payload[pos + epos++];
                    uint8_t action = (actionMatch >> 6) & 0x03;
                    uint8_t match  = actionMatch & 0x01;

                    OrfPrefixEntry e{};
                    e.action = action;
                    e.match  = match;

                    if (action == BGP_ORF_ACTION_REMOVE_ALL)
                    {
                        orfEntries.push_back(e);
                        continue;
                    }

                    if (epos + 6 > orfLen) break;
                    e.sequence = readU32(payload.data() + pos + epos); epos += 4;
                    e.minLen   = payload[pos + epos++];
                    e.maxLen   = payload[pos + epos++];

                    if (epos >= orfLen) break;
                    uint8_t prefixLen   = payload[pos + epos++];
                    uint8_t prefixBytes = static_cast<uint8_t>((prefixLen + 7) / 8);
                    if (epos + prefixBytes > orfLen) break;

                    e.prefix.prefixLength = prefixLen;
                    if (family.afi == BGP_AFI_IPV6) {
                        e.prefix.setV6(readBytes<__uint128_t>(payload.data() + pos + epos, prefixBytes));
                    } else {
                        e.prefix.setV4(readBytes<uint32_t>(payload.data() + pos + epos, prefixBytes));
                    }
                    epos += prefixBytes;

                    orfEntries.push_back(e);
                }
            }
            pos += orfLen;
        }
    }

    if (!orfEntries.empty())
    {
        session.getNeighbor().getAfNeighbor(family).updateOrfFilter(orfEntries);
        if (subtype == BGP_ORF_WHEN_IMMEDIATE)
        {
            AddressFamilyVariant* af = session.getNeighbor().getProcess().findAddressFamily(family);
            if (af)
                std::visit([&](auto&& fam) { fam.refreshPeer(session); }, *af);
        }
    }
    else
    {
        AddressFamilyVariant* af = session.getNeighbor().getProcess().findAddressFamily(family);
        if (af)
        {
            if (subtype == BGP_ROUTE_REFRESH_BORR)
                std::visit([&](auto&& fam) { fam.onPeerBorr(session); }, *af);
            else if (subtype == BGP_ROUTE_REFRESH_EORR)
                std::visit([&](auto&& fam) { fam.onPeerEorr(session); }, *af);
            else
                std::visit([&](auto&& fam) { fam.refreshPeer(session); }, *af);
        }
    }

    session.onRouteRefreshReceived();
    return true;
}

bool BgpRx::parsePathAttributes(Session& session, std::span<uint8_t> data, IncomingUpdate& uinfo, Notification& error)
{
    size_t pos = 0;

    Attributes& attrs = uinfo.attrs;
    Path& path = uinfo.path;

    bool sawOrigin = false;
    bool sawAsPath = false;
    bool sawNextHop = false;

    std::optional<AfiSafi> reachAfi;
    std::optional<AfiSafi> unreachAfi;

    while (pos < data.size())
    {
        if (pos + 2 > data.size())
        {
            error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST;
            return false;
        }

        uint8_t flags = data[pos];
        uint8_t type = data[pos + 1];
        pos += 2;

        bool optional = (flags & BGP_ATTR_FLAG_OPTIONAL) != 0;
        bool transitive = (flags & BGP_ATTR_FLAG_TRANSITIVE) != 0;
        bool partial = (flags & BGP_ATTR_FLAG_PARTIAL) != 0;
        bool extendedLength = (flags & BGP_ATTR_FLAG_EXTENDED_LENGTH) != 0;

        uint32_t attrLen = 0;
        if (extendedLength)
        {
            if (pos + 2 > data.size())
            {
                error.code = BGP_NOTIFICATION_UPDATE_ATTR_LENGTH;
                return false;
            }
            attrLen = readU16(data.data() + pos);
            pos += 2;
        }
        else
        {
            if (pos + 1 > data.size())
            {
                error.code = BGP_NOTIFICATION_UPDATE_ATTR_LENGTH;;
                return false;
            }
            attrLen = data[pos];
            pos += 1;
        }

        if (pos + attrLen > data.size())
        {
            error.code = BGP_NOTIFICATION_UPDATE_ATTR_LENGTH;
            return false;
        }

        std::span<uint8_t> val(data.data() + pos, attrLen);
        pos += attrLen;

        {
            // NOTE: 1,2,3,4,8,14,15,16 not allowed 
            auto& attrRanges = session.getNeighbor().getAttrRanges();
            if (attrRanges.discard.test(type))
                continue;
            if (attrRanges.withdraw.test(type))
                return false;
        }

        auto wellKnownFlagError = [&]() -> bool
        {
            error.code = BGP_NOTIFICATION_UPDATE_ATTR_FLAG;
            error.data.assign(val.begin(), val.end());
            return false;
        };

        switch (type)
        {
            // ORIGIN
            case BGP_ATTR_ORIGIN:
            {
                if (optional || !transitive) return wellKnownFlagError();
                if (attrLen != 1)
                {
                    error.code = BGP_NOTIFICATION_UPDATE_ATTR_LENGTH;
                    return false;
                }
                uint orig = val[0];
                if (orig > BGP_ORIGIN_INCOMPLETE)
                {
                    error.code = BGP_NOTIFICATION_UPDATE_INVALID_ORIGIN;
                    return false;
                }
                attrs.origin = orig;
                sawOrigin = true;
                break;
            }
            // AS PATH
            case BGP_ATTR_AS_PATH:
            {
                if (optional || !transitive) return wellKnownFlagError();
                size_t ap = 0;
                bool use4byte = session.getNegotiated().asn32bit;
                uint8_t asnBytes = use4byte ? 4 : 2;

                while (ap + 2 <= attrLen)
                {
                    uint8_t segType = val[ap];
                    uint8_t segLen = val[ap + 1];
                    ap += 2;

                    if (ap + segLen + asnBytes > attrLen)
                    {
                        error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_AS_PATH;
                        return false;
                    }

                    AsPathSegment seg;
                    seg.segmentType = segType;
                    for (uint8_t i = 0; i < segLen; ++i)
                    {
                        uint32_t asn = use4byte
                            ? readU32(val.data() + ap + i * 4)
                            : readU16(val.data() + ap + i * 2);
                        seg.asns.push_back(asn);
                    }
                    ap += segLen * asnBytes;
                    attrs.asPath.push_back(std::move(seg));
                }
                sawAsPath = true;
                break;
            }

            // NEXT HOP
            case BGP_ATTR_NEXT_HOP:
            {
                if (optional || !transitive) return wellKnownFlagError();
                if (attrLen != 4)
                {
                    error.code = BGP_NOTIFICATION_UPDATE_ATTR_LENGTH;
                    return false;
                }
                path.nextHop = IPAddress(readU32(val.data()));
                sawNextHop = true;
                break;
            }

            // MULTI EXIT DISC
            case BGP_ATTR_MULTI_EXIT_DISC:
            {
                if (attrLen != 4)
                {
                    error.code = BGP_NOTIFICATION_UPDATE_ATTR_LENGTH;
                    return false;
                }
                attrs.med = readU32(val.data());
                break;
            }

            // LOCAL PREF
            case BGP_ATTR_LOCAL_PREF:
            {
                if (optional || !transitive) return wellKnownFlagError();
                if (attrLen != 4)
                {
                    error.code = BGP_NOTIFICATION_UPDATE_ATTR_LENGTH;
                    return false;
                }
                attrs.localPref = readU32(val.data());
                break;
            }

            // AGGREGATOR
            case BGP_ATTR_AGGREGATOR:
            {
                bool use4byte = session.getNegotiated().asn32bit;
                size_t expected = use4byte ? 8 : 6;
                if (attrLen != expected)
                {
                    error.code = BGP_NOTIFICATION_UPDATE_ATTR_LENGTH;
                    return false;
                }
                Aggregator agg;
                if (use4byte)
                {
                    agg.asn = readU32(val.data());
                    agg.speaker = IPAddress(readU32(val.data() + 4));
                }
                else
                {
                    agg.asn = readU16(val.data());
                    agg.speaker = IPAddress(readU32(val.data() + 2));
                }
                attrs.asAggregator = std::move(agg);
                break;
            }

            // COMMUNITIES
            case BGP_ATTR_COMMUNITIES:
            {
                if (attrLen % 4 != 0)
                {
                    error.code = BGP_NOTIFICATION_UPDATE_ATTR_LENGTH;
                    return false;
                }
                for (uint32_t i = 0; i < attrLen; i += 4)
                    attrs.communities.push_back(readU32(val.data() + i));
                break;
            }

            // ORIGINATOR ID
            case BGP_ATTR_ORIGINATOR_ID:
            {
                if (attrLen != 4)
                {
                    error.code = BGP_NOTIFICATION_UPDATE_ATTR_LENGTH;
                    return false;
                }
                attrs.originatorId = readU32(val.data());
                break;
            }

            // CLUSTER LIST
            case BGP_ATTR_CLUSTER_LIST:
            {
                if (attrLen % 4 != 0)
                {
                    error.code = BGP_NOTIFICATION_UPDATE_ATTR_LENGTH;
                    return false;
                }
                for (uint32_t i = 0; i < attrLen; i += 4)
                    attrs.clusterList.push_back(readU32(val.data() + i));
                break;
            }

            // MP REACH NLRI
            case BGP_ATTR_MP_REACH_NLRI:
            {
                if (attrLen < 4)
                {
                    error.code = BGP_NOTIFICATION_UPDATE_ATTR_LENGTH;
                    return false;
                }
                uint16_t afi = readU16(val.data());
                uint8_t safi = val[2];
                uint8_t nhLen = val[3];
                if (4 + nhLen > attrLen)
                {
                    error.code = BGP_NOTIFICATION_UPDATE_ATTR_LENGTH;
                    return false;
                }

                reachAfi = AfiSafi{afi, safi};
                if (unreachAfi.has_value() && *unreachAfi != *reachAfi)
                {
                    error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST;
                    return false;
                }

                if (afi == BGP_AFI_IPV6)
                {
                    if (nhLen == 16)
                    {
                        std::memcpy(&path.nextHop.raw, val.data() + 4, 16);
                        sawNextHop = true;
                    }
                    else if (nhLen == 32)
                    {
                        std::memcpy(&path.nextHop.raw, val.data() + 4, 16);
                        IPAddress linkLocal;
                        std::memcpy(&linkLocal.raw, val.data() + 20, 16);
                        path.linkLocal = linkLocal;
                        sawNextHop = true;
                    }
                }
                else if (nhLen == 4)
                {
                    path.nextHop = IPAddress(readU32(val.data() + 4));
                    sawNextHop = true;
                }
                else if (nhLen == 12)
                {
                    path.rd = readU64(val.data() + 4);
                    path.nextHop = IPAddress(readU32(val.data() + 12));
                    sawNextHop = true;
                }

                // SNPA (skip).
                size_t mpPos = 4 + nhLen;
                if (mpPos >= attrLen) break;
                uint8_t snpaCount = val[mpPos++];
                for (uint8_t i = 0; i < snpaCount && mpPos < attrLen; ++i)
                {
                    uint8_t snpaLen = val[mpPos++];
                    mpPos += (snpaLen + 1) / 2;
                }

                // NLRI
                uinfo.nlriData = std::span<uint8_t>(val.data() + mpPos, attrLen - mpPos);
                break;
            }

            // MP UNREACH NLRI
            case BGP_ATTR_MP_UNREACH_NLRI:
            {
                if (attrLen < 3)
                {
                    error.code = BGP_NOTIFICATION_UPDATE_ATTR_LENGTH;
                    return false;
                }
                uint16_t afi = readU16(val.data());
                uint8_t safi = val[2];

                unreachAfi = AfiSafi{afi, safi};
                if (reachAfi.has_value() && *reachAfi != *unreachAfi)
                {
                    error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST;
                    return false;
                }

                uinfo.withdrawnData = std::span<uint8_t>(val.data() + 3, attrLen - 3);
                break;
            }

            // AS4 PATH
            case BGP_ATTR_AS4_PATH:
            {
                size_t ap = 0;
                while (ap + 2 <= attrLen)
                {
                    uint8_t segType = val[ap];
                    uint8_t segLen = val[ap + 1];
                    ap += 2;
                    if (ap + segLen * 4 > attrLen) break;
                    AsPathSegment seg;
                    seg.segmentType = segType;
                    for (uint8_t i = 0; i < segLen; ++i)
                        seg.asns.push_back(readU32(val.data() + ap + i * 4));
                    ap += segLen * 4;
                    attrs.as4Path.push_back(std::move(seg));
                }
                break;
            }

            // AS4 AGGREGATOR
            case BGP_ATTR_AS4_AGGREGATOR:
            {
                if (attrLen != 8)
                {
                    error.code = BGP_NOTIFICATION_UPDATE_ATTR_LENGTH;
                    return false;
                }
                Aggregator agg;
                agg.asn = readU32(val.data());
                agg.speaker = IPAddress(readU32(val.data() + 4));
                attrs.as4Aggregator = agg;
                break;
            }

            // AIGP
            case BGP_ATTR_AIGP:
            {
                if (attrLen >= 11 && val[0] == 1 && readU16(val.data() + 1) == 11)
                {
                    attrs.aigp = readU64(val.data() + 3);
                }
                break;
            }

            // LARGE COMMUNITIES
            case BGP_ATTR_LARGE_COMMUNITIES:
            {
                if (attrLen % 12 != 0)
                {
                    error.code = BGP_NOTIFICATION_UPDATE_ATTR_LENGTH;
                    return false;
                }
                for (uint32_t i = 0; i < attrLen; i += 12)
                {
                    std::array<uint32_t, 3> lc;
                    lc[0] = readU32(val.data() + i);
                    lc[1] = readU32(val.data() + i + 4);
                    lc[2] = readU32(val.data() + i + 8);
                    attrs.largeCommunities.push_back(lc);
                }
                break;
            }

            // UNKNOWN ATTRIBUTES
            default:
            {
                if (!optional)
                {
                    error.code = BGP_NOTIFICATION_UPDATE_UNRECOGNIZED_ATTR;
                    error.data.assign(val.begin(), val.end());
                    return false;
                }
                if (transitive)
                {
                    UnknownAttribute ua;
                    ua.flags = flags | BGP_ATTR_FLAG_PARTIAL;
                    ua.type = type;
                    ua.value.assign(val.begin(), val.end());
                    attrs.unknownTransitive.push_back(std::move(ua));
                }
                break;
            }
        }
    }

    // AS4 path reconstruction (RFC 4893 §4.2.3)
    // If the peer doesn't support 4-byte ASN but sent AS4_PATH, merge them.
    if (!session.getNegotiated().asn32bit && !attrs.as4Path.empty())
    {
        // Count total effective AS hops in each path
        auto countHops = [](const std::vector<AsPathSegment>& segs) -> size_t {
            size_t n = 0;
            for (const auto& s : segs)
                n += (s.segmentType == BGP_AS_SET) ? (s.asns.empty() ? 0u : 1u) : s.asns.size();
            return n;
        };

        const size_t asLen  = countHops(attrs.asPath);
        const size_t as4Len = countHops(attrs.as4Path);

        // Keep the leftmost (asLen - as4Len) hops from asPath (added by 2-byte AS speakers),
        // then append the full as4Path for the 4-byte portion.
        std::vector<AsPathSegment> merged;
        size_t toKeep = (asLen > as4Len) ? (asLen - as4Len) : 0;

        for (const auto& seg : attrs.asPath)
        {
            if (toKeep == 0) break;
            size_t segHops = (seg.segmentType == BGP_AS_SET)
                ? (seg.asns.empty() ? 0u : 1u)
                : seg.asns.size();

            if (segHops <= toKeep)
            {
                merged.push_back(seg);
                toKeep -= segHops;
            }
            else
            {
                AsPathSegment partial = seg;
                partial.asns.resize(toKeep);
                merged.push_back(std::move(partial));
                toKeep = 0;
            }
        }

        for (const auto& seg : attrs.as4Path)
            merged.push_back(seg);

        attrs.asPath = std::move(merged);
        attrs.as4Path.clear();
    }

    // Determine the address family after all attributes have been parsed
    if (reachAfi.has_value())
        uinfo.afi = *reachAfi;
    else if (unreachAfi.has_value())
        uinfo.afi = *unreachAfi;
    else
        uinfo.afi = AfiSafi{ BGP_AFI_IPV4, BGP_SAFI_UNICAST };

    (void)sawOrigin;
    (void)sawAsPath;
    (void)sawNextHop;

    return true;
}
}

