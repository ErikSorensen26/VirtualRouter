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
        // Constructor
        IPPacket(Interface* iface);

        // Sets the IPv4 header in PacketInfo
        void setIPHeader(PacketInfo& packetInfo, const ByteString destIp, uint8_t DSCP, uint8_t hopLimit, const ByteString type);
    private:
        // Current Interface
        Interface* currentInterface;
    };
}

#endif // IP_PACKET_H
