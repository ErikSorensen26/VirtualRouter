
// BgpRx.h

#ifndef BGP_RX_H
#define BGP_RX_H

#include <span>
#include <cstdint>

#include "bgp/BgpTypes.hpp"
#include "bgp/session/Session.h"
#include "bgp/features/Capabilities.h"
#include "bgp/rib/RibTypes.hpp"

struct BgpHeader;
class PacketBuilder;
namespace TCP
{
class RxConsumer;
class Connection;
}

namespace BGP
{
class Session;
class BgpProcess;
struct Capabilities;

struct IncomingUpdate
{
    Attributes attrs;
    Path path;
    AfiSafi afi = { BGP_AFI_IPV4, BGP_SAFI_UNICAST };

    std::span<uint8_t> withdrawnData;
    std::span<uint8_t> nlriData;
};

class BgpRx
{
public:
    BgpRx() = delete;

    static void handleIncoming(Session& s, TCP::RxConsumer& c);

    template <typename N>
    static bool processUpdate(Session& c, IncomingUpdate& uinfo, ParsedUpdate<typename N::Nlri>& update, Notification& notification);

private:

    static bool processOpen(Session& c, std::span<uint8_t> data, Notification& notification);
    static bool processUpdate(Session& c, std::span<uint8_t> data, Notification& notification);
    static bool processNotification(Session& c, std::span<uint8_t> data, Notification& notification);
    static bool processKeepalive(Session& c, std::span<uint8_t> data, Notification& notification);
    static bool processRouteRefresh(Session& c, std::span<uint8_t> data, Notification& notification);

    static void parseCapabilities(std::span<uint8_t> data, Capabilities& out);
    static bool parsePathAttributes(Session& session, std::span<uint8_t> data, IncomingUpdate& uinfo, Notification& error);
};

template <typename N>
bool BgpRx::processUpdate(Session& session, IncomingUpdate& uinfo, ParsedUpdate<typename N::Nlri>& update, Notification& error)
{
    const bool addPath = session.getNegotiated().addPathFamilies.end() !=
        std::find_if(session.getNegotiated().addPathFamilies.begin(),
                     session.getNegotiated().addPathFamilies.end(),
                     [](const auto& ap) { return ap.family == N::afi; });

    // Withdrawn NLRI
    for (size_t pos = 0; pos < uinfo.withdrawnData.size();)
    {
        if (addPath)
        {
            if (pos + 4 > uinfo.withdrawnData.size())
            {
                error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST;
                return false;
            }
            pos += 4;
        }

        typename N::Nlri nlri{};
        size_t consumed = N::decodeNlri(uinfo.withdrawnData.data() + pos, nlri);
        if (consumed == 0 || pos + consumed > uinfo.withdrawnData.size())
        {
            error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST;
            return false;
        }
        update.withdrawn.push_back(nlri);
        pos += consumed;
    }

    // Legacy IPv4 NLRI

    for (size_t pos = 0; pos < uinfo.nlriData.size();)
    {
        if (addPath)
        {
            if (pos + 4 > uinfo.nlriData.size())
            {
                error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST;
                return false;
            }
            pos += 4;
        }

        typename N::Nlri nlri{};
        size_t consumed = N::decodeNlri(uinfo.nlriData.data() + pos, nlri);
        if (consumed == 0 || pos + consumed > uinfo.nlriData.size())
        {
            error.code = BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST;
            return false;
        }
        update.announcements.push_back(nlri);
        pos += consumed;
    }

    update.attrs = PathAttribute{uinfo.attrs, uinfo.path};
    return true;
}
}

#endif // BGP_RX_H
