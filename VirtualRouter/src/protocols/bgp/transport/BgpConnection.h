// BgpConnection.h

#ifndef BGP_CONNECTION_H
#define BGP_CONNECTION_H

#include <span>
#include <cstdint>

#include "tcp/Connection.h"

struct BgpHeader;

namespace BGP
{
class Neighbor;
class BgpProcess;

class Connection
{
public:
    Connection(Neighbor& nbr, TCP::Connection& c) noexcept;
    Connection(Neighbor& nbr) noexcept;

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    Connection(Connection&&) noexcept = delete;
    Connection& operator=(Connection&&) noexcept = delete;

    ~Connection();

    void handleIncoming(const std::span<const uint8_t>& data);

    void sendOpen(); 
    void sendUpdate();
    void sendNotification();
    void sendKeepalive();
    void sendRouteRefresh();

private:

    Neighbor& neighbor;
    TCP::Connection connection;
};
}

#endif // BGP_TRANSMISSION_H
