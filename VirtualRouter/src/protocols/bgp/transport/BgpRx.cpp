// BgpRx.cpp

#include <Functions.h>

#include "bgp/BgpProcess.h"
#include "packet/headers/BgpHeader.hpp"
#include "packet/headers/embedded/bgp/BgpOpenHeader.hpp"
#include "packet/TlvOptions.hpp"
#include "bgp/session/Session.h"
#include "Transmission.h"

namespace BGP
{
void Transmission::handleIncoming(Session& c, const std::span<uint8_t> data)
{
    if (data.size() > 4096)
        return; // Invalid size

    for (size_t idx = 0; idx <= data.size();)
    {
        BgpHeader hdr;
        hdr.setBuffer(data.data() + idx);

        if (std::memcmp(hdr.getMarker(), BGP_MARKER, 16) != 0)
        {
            // XXX: Send notification
            
            return;
        }

        if (hdr.getLength() >= 19)
        {
            // XXX: send notification
            return;
        }

        hdr.setTrailSize(hdr.getLength() - BgpHeader::fixedSize);

        switch (hdr.getType())
        {
            case BGP_TYPE_OPEN:
                processOpen(c, hdr.getTrail());
                break;
            case BGP_TYPE_UPDATE:
                processUdpate(c, hdr.getTrail());
                break;
            case BGP_TYPE_NOTIFICATION:
                processNotification(c, hdr.getTrail());
                break;
            case BGP_TYPE_KEEP_ALIVE:
                processNotification(c, hdr.getTrail());
                break;
            case BGP_TYPE_ROUTE_REFRESH:
                processRouteRefresh(c, hdr.getTrail());
                break;
            default:
            {
                // XXX: send notification
                return;
            }
        }
    }
}

void Transmission::processOpen(Session& c, std::span<uint8_t> data)
{
    BgpOpenHeader open;
    open.setBuffer(data.data());

    if (open.getVersion() != BGP_VERSION)
    {
        // XXX: Notification (open error)
        return;
    }

    if (open.getAsNumber() != process.asNumber)
    {
        // XXX: Notification (open error)
        return;
    }
    // TODO: support 32 bit as

    uint16_t minHold = std::min(c.holdTime, open.getHoldTime());
    if (minHold < c.getConfigs().get<Config::BgpBase::MINIMUM_HOLDTIME>().load())
    {
        // XXX: Notification (invalid holdtime)
        return;
    }

    uint32_t id = open.getIdentifier();
    if (id == 0 || Functions::isMulticast(open.getIdentifierBuf(), AddressFamily::IPv4))
    {
        // XXX: Notification (invalid id)
    }

    if (BgpOpenHeader::fixedSize + open.getParameterLen() != data.size())
        return;

    std::vector<TLV8Option> parameterOpts;
    parseBgpOpenParameters(open.getTrailData(), open.getParameterLen(), parameterOpts);

    for (auto& opt : parameterOpts)
    {
        switch (opt.type)
        {
            case BGP_PARAMETER_CAPABILITY:
                extractCapabilities(c, opt.asSpan());
                break;
            default:
                break;
        }
    }
}

void Transmission::processUdpate(Session& c, std::span<uint8_t> data)
{

}

void Transmission::processNotification(Session& c, std::span<uint8_t> data)
{

}

void Transmission::processKeepalive(Session& c, std::span<uint8_t> data)
{

}

void Transmission::processRouteRefresh(Session& c, std::span<uint8_t> data)
{

}

Capabilities Transmission::extractCapabilities(Session& c, const std::span<const uint8_t> data)
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

