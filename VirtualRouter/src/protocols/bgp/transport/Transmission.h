// Transmission.h

#ifndef BGP_TRANSMISSION_H
#define BGP_TRANSMISSION_H

#include <span>
#include <cstdint>

struct BgpHeader;
class PacketBuilder;

namespace BGP
{
class Session;
class BgpProcess;
class 

class Transmission
{
public:
    Transmission(BgpProcess& proc);

    Transmission(const Transmission&) = delete;
    Transmission& operator=(const Transmission&) = delete;

    Transmission(Transmission&&) noexcept = delete;
    Transmission& operator=(Transmission&&) noexcept = delete;

    ~Transmission();

    void handleIncoming(Session& c, std::span<uint8_t> data);

    void sendOpen(); 
    void sendUpdate();
    void sendNotification();
    void sendKeepalive();
    void sendRouteRefresh();

private:

    void processOpen(Session& c, std::span<uint8_t> data);
    void processUdpate(Session& c, std::span<uint8_t> data);
    void processNotification(Session& c, std::span<uint8_t> data);
    void processKeepalive(Session& c, std::span<uint8_t> data);
    void processRouteRefresh(Session& c, std::span<uint8_t> data);

    Capabilities extractCapabilities(Session& c, const std::span<const uint8_t> data);
    void extractPathAttributes(Session& c, const std::span<const uint8_t> data);

    BgpProcess& process;
};
}

#endif // BGP_TRANSMISSION_H
