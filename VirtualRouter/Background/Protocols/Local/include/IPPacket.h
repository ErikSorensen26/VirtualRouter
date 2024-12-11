#pragma once

#include <ByteString.hpp>
#include <PacketStructure.h>
#include <Interface.h>
#include <Logger.h>

namespace Protocol
{
    class IPPacket
    {
    public:
        // Constructor
        IPPacket(Interface& iface);

        // Sets the IPv4 header in PacketInfo
        void setIPHeader(PacketInfo& packetInfo, const ByteString destIp, int DSCP, int hopLimit, const ByteString type);
    private:
        // Current Interface
        Interface& currentInterface;
    };
}
