// ReliablePacket.hpp

#ifndef RELIABLE_PACKET_HPP
#define RELIABLE_PACKET_HPP

#include <cstring>
#include <chrono>
#include <IPAddress.hpp>
#include <EigrpHeader.hpp>
#include <StaticHeader.hpp>
#include <unordered_map>

namespace Eigrp
{
class Neighbor;
struct ReliableInfo
{
    std::chrono::steady_clock::time_point sendTime; ///< Time the packet was sent.
    uint8_t retransmissionCount = 0; ///< Number of retransmissions.
    uint32_t timerId = 0; ///< Timer ID for retransmission.
    uint32_t sequence = 0;
};

class UnicastReliablePacket
{
public:
    UnicastReliablePacket() = default;
    UnicastReliablePacket(EigrpHeader& builder, const IPAddress& dest)
        : packet(builder.buffer, builder.fixedSize + builder.getTrail().size()), destination(dest) {}

    // Default copy constructor and copy assignment operator
    UnicastReliablePacket(const UnicastReliablePacket&) = default;
    UnicastReliablePacket& operator=(const UnicastReliablePacket&) = default;
    
    // Default move constructor and move assignment operator
    UnicastReliablePacket(UnicastReliablePacket&&) = default;
    UnicastReliablePacket& operator=(UnicastReliablePacket&&) = default;

    ReliableInfo info;

    StaticHeader packet;
    IPAddress destination;
};

class MulticastReliablePacket
{
public:
    MulticastReliablePacket() = default;
    MulticastReliablePacket(EigrpHeader& builder, std::unordered_map<Neighbor*, ReliableInfo>& nbrs)
        : packet(builder.buffer, builder.fixedSize + builder.getTrail().size()), neighbors(std::move(nbrs)) {}

    // Default copy constructor and copy assignment operator
    MulticastReliablePacket(const MulticastReliablePacket&) = default;
    MulticastReliablePacket& operator=(const MulticastReliablePacket&) = default;

    // Default move constructor and move assignment operator
    MulticastReliablePacket(MulticastReliablePacket&&) = default;
    MulticastReliablePacket& operator=(MulticastReliablePacket&&) = default;

    StaticHeader packet;
    std::unordered_map<Neighbor*, ReliableInfo> neighbors;
};
}

#endif
