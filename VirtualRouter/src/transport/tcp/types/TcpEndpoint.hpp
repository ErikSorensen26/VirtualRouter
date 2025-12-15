// TcpEndpoint.h

#ifndef TCP_ENDPOINT_H
#define TCP_ENDPOINT_H

#include <cstdint>
#include <AddressFamily.hpp>
#include <IPAddress.hpp>
#include <functional>

namespace TCP
{
using TcpPort = uint16_t;

struct TcpEndpoint final
{
    IPAddress address;
    TcpPort port{0};

    bool isWildcardAddress() const noexcept
    {
        return address.isUnspecified();
    }
    
    bool operator==(const TcpEndpoint& other) noexcept
    {
        return address == other.address && port == other.port;
    }

    bool operator!=(const TcpEndpoint& other) noexcept
    {
        return address != other.address || port != other.port;
    }
};
}

namespace std
{
template <>
struct hash<TCP::TcpEndpoint>
{
    size_t operator()(const TCP::TcpEndpoint& k) const noexcept
    {
        // Start with IP hash values
        size_t h = std::hash<IPAddress>{}(k.address);
        size_t p = static_cast<size_t>(k.port);

        p ^= h + 0x9e3779b7f4a7c15ull + (p << 6) + (p >> 2);
        
        return p;
    }
};
}

#endif // TCP_ENDPOINT_H
