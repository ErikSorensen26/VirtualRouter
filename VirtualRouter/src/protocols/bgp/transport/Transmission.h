// Transmission.h

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
}

namespace BGP
{
class Session;
class BgpProcess;

class Transmission
{
public:
    explicit Transmission(BgpProcess& proc);

    Transmission(const Transmission&) = delete;
    Transmission& operator=(const Transmission&) = delete;
    Transmission(Transmission&&) noexcept = delete;
    Transmission& operator=(Transmission&&) noexcept = delete;

    void handleIncoming(Session& s, TCP::RxConsumer& c);

    void sendOpen(Session& session); 
    template <typename N>
    void sendUpdate(Session& session, const ParsedUpdate<N>& update);
    void sendNotification(Session& session, const Notification& notification);
    void sendKeepalive(Session& session);
    void sendRouteRefresh(Session& session, const AfiSafi& family);

private:
    bool dispatchMessage(Session& session, uint8_t type, std::span<const uint8_t> payload);

    bool processOpen(Session& c, std::span<uint8_t> data, Notification& notification);
    bool processUdpate(Session& c, std::span<uint8_t> data, Notification& notification);
    bool processNotification(Session& c, std::span<uint8_t> data, Notification& notification);
    bool processKeepalive(Session& c, std::span<uint8_t> data, Notification& notification);
    bool processRouteRefresh(Session& c, std::span<uint8_t> data, Notification& notification);

    template <typename N>
    bool parseNlriList(AddressFamily af, std::span<const uint8_t> data, std::vector<N>& out, Notification& error);
    template <typename N>
    bool parsePathAttributes(Session& session, std::span<const uint8_t> data, PathAttribute<N> attrs, Notification& error);

    template <typename N>
    static void appendNlriList(const std::vector<IPPrefix>& nlri, std::vector<uint8_t>& out);
    static void appendPathAttribute(uint8_t flags, uint8_t type, std::span<const uint8_t> value, std::vector<uint8_t>& out);
    template <typename N>
    static void encodeUpdatePayload(const ParsedUpdate<N>& update);

    static std::vector<uint8_t> makeMessage(uint8_t type, std::span<const uint8_t> payload);

    //Capabilities extractCapabilities(Session& c, const std::span<const uint8_t> data);
    //void extractPathAttributes(Session& c, const std::span<const uint8_t> data);

private:
    BgpProcess& process;
};
}

#endif // BGP_TRANSMISSION_H
