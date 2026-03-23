// OspfPacket.hpp

#ifndef OSPF_PACKET_HPP
#define OSPF_PACKET_HPP

#include <cstring>
#include <IPAddress.h>

#include "packet/StaticHeader.hpp"

namespace routing::ospf
{
class Neighbor;

class UnicastPacket
{
public:
    UnicastPacket() = default;
    UnicastPacket(uint8_t* buf, size_t len, const types::IPAddress& dest)
        : packet(buf, len), destination(dest) {}

    // Default copy constructor and assignment operator
    UnicastPacket(const UnicastPacket&) = default;
    UnicastPacket& operator=(const UnicastPacket&) = default;

    // Default move constructor and move assignment operator
    UnicastPacket(UnicastPacket&&) = default;
    UnicastPacket& operator=(UnicastPacket&&) = default;

    void reset()
    {
        packet = packet::StaticHeader{};
        sequence = 0;
    }

    uint32_t sequence;

    packet::StaticHeader packet;
    types::IPAddress destination;
};
} // namespace routing

#endif

