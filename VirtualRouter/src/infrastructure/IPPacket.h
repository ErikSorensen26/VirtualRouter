// IPPacket.h

#ifndef IP_PACKET_H
#define IP_PACKET_H

#include <ByteString.hpp>
#include <PacketStructure.h>
#include <Logger.h>

class Interface;

namespace Protocol
{
    class IPPacket
    {
    public:
        // Sets the IPv4 header in PacketInfo
        static void buildIp(
            Interface* iface,
            PacketInfo& packetInfo, 
            const ByteString& destIp, 
            ByteString const* sourceIp, 
            ByteString const* destMac, 
            uint8_t DSCP, 
            uint8_t hopLimit,
            const ByteString& protocolType,
            bool reserved = false,
            bool dontFragment = true,
            bool moreFragment = false,
            uint16_t fragmentOffset = 0,
            uint32_t v6FlowLabel = 0
        );

        // Sets the UPD header in PacketInfo
        static void buildUdp(
            Interface* iface,
            PacketInfo& packetInfo, 
            const ByteString& destIp, 
            ByteString const* sourceIp, 
            ByteString const* destMac, 
            uint8_t DSCP, 
            uint8_t hopLimit, 
            const ByteString& type,
            const ByteString& sourcePort,
            const ByteString& destinationPort,
            bool reserved = false,
            bool dontFragment = true,
            bool moreFragment = false,
            uint16_t fragmentOffset = 0
        );
    };
}

#endif // IP_PACKET_H
