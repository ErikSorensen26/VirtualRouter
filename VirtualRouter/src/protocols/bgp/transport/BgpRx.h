
// BgpRx.h

#ifndef BGP_TRANSMISSION_H
#define BGP_TRANSMISSION_H

#include <span>
#include <cstdint>

#include "bgp/BgpTypes.hpp"
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

class BgpRx
{
public:
    explicit BgpRx(BgpProcess& proc);

    BgpRx(const BgpRx&) = delete;
    BgpRx& operator=(const BgpRx&) = delete;
    BgpRx(BgpRx&&) noexcept = delete;
    BgpRx& operator=(BgpRx&&) noexcept = delete;

    void handleIncoming(Session& s, TCP::RxConsumer& c);

private:

    bool processOpen(Session& c, std::span<uint8_t> data, Notification& notification);
    bool processUpdate(Session& c, std::span<uint8_t> data, Notification& notification);
    bool processNotification(Session& c, std::span<uint8_t> data, Notification& notification);
    bool processKeepalive(Session& c, std::span<uint8_t> data, Notification& notification);
    bool processRouteRefresh(Session& c, std::span<uint8_t> data, Notification& notification);

    void parseCapabilities(std::span<uint8_t> data, Capabilities& out);

    template <typename N>
    bool parsePathAttributes(Session& session, std::span<uint8_t> data, PathAttribute<N>& attrs, Notification& error);

    template <typename N>
    bool parseNlriList(uint16_t afi, uint8_t safi, std::span<uint8_t> data, std::vector<N>& out, bool addPath, Notification& error);

private:
    BgpProcess& process;
};
}

#endif // BGP_TRANSMISSION_H
