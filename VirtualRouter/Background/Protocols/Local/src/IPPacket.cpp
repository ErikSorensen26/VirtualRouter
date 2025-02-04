#include <IPPacket.h>
#include <Interface.h>

namespace Protocol
{
    IPPacket::IPPacket(Interface* iface) : currentInterface(iface) {}

    void IPPacket::setIPHeader(PacketInfo& packetInfo, const ByteString destIp, uint8_t DSCP, uint8_t hopLimit, ByteString type)
    {
        if (!currentInterface || !currentInterface->Get()) return;

        // IPv6 Header creation
        if (destIp.size() == 16)
        {        
            {
                std::shared_lock<std::shared_mutex> lock(currentInterface->Get()->ipMutex);
                IPv6Header ip;
                ip.version = "6";
                ip.trafficClass = Functions::numToHex(DSCP, 2);
                ip.flowLabel = Functions::numToHex(currentInterface->Get()->ipv6.ipv6FlowLabel, 5);
                ip.payloadLength = ByteString(2, 0x00); // Will be calculated later
                ip.protocol = type;
                ip.hopLimit = Functions::numToByte(hopLimit, 1);
                ip.sourceAddress = currentInterface->Get()->ipv6.ipAddress;
                ip.destinationAddress = destIp;
                packetInfo.Layer3.insert(packetInfo.Layer3.begin(), ip);
            }
            currentInterface->ethernet->setEthernetHeader(packetInfo, destIp, Variable::Ethernet::ipv6);
        }
        else if (destIp.size() == 4)
        {
            {
                std::shared_lock<std::shared_mutex> lock(currentInterface->Get()->ipMutex);
                IPv4Header ip;
                ip.version = ByteString("4", 1);
                ip.headerLength = ByteString("5", 1);
                ip.serviceField = Functions::numToByte(DSCP, 1);
                ip.totalLength = ByteString(2, 0x02);
                ip.identification = ByteString(2, 0x00);
                ip.fragmentFlag.reserved = ByteString("0", 1);
                ip.fragmentFlag.fragment = ByteString("0", 1);
                ip.fragmentFlag.moreFragment = ByteString("0", 1);
                ip.fragmentFlag.fragmentOffset = ByteString("0000000000000", 13);
                ip.TTL = Functions::numToByte(hopLimit, 1);
                ip.protocol = type;
                ip.checksum = ByteString(2, 0x00);
                ip.sourceAddress = currentInterface->Get()->ipv4.ipAddress;
                ip.destinationAddress = destIp;
                packetInfo.Layer3.insert(packetInfo.Layer3.begin(), ip);
            }
            currentInterface->ethernet->setEthernetHeader(packetInfo, destIp, Variable::Ethernet::ipv4);
        }
    }
}
