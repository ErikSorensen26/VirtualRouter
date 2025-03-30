#include "IPPacket.h"
#include "Ethernet.h"
#include <Interface.h>

namespace Protocol
{
    void IPPacket::buildIp(Interface* iface, PacketInfo& packetInfo, const ByteString& destIp, ByteString const* sourceIp, ByteString const* destMac, uint8_t DSCP, uint8_t hopLimit, const ByteString& protocolType, bool reserved, bool dontFragment, bool moreFragment, uint16_t fragmentOffset)
    {
        if (!iface || iface->shutdownFlag.load(std::memory_order_relaxed)) return;

        // IPv6 Header creation
        if (destIp.size() == 16)
        {        
            {
                IPv6Header ip;
                {
                    std::shared_lock<std::shared_mutex> lock(iface->Get()->ipMutex);
                    ip.sourceAddress = (sourceIp && sourceIp->size() == 16) ? *sourceIp : iface->configs.ipv6.ipAddress;
                    ip.flowLabel = Functions::numToHex(iface->Get()->ipv6.ipv6FlowLabel, 5);
                }
                ip.version = "6";
                ip.trafficClass = Functions::numToHex(DSCP, 2);
                ip.payloadLength = ByteString(2, 0x00); // Will be calculated later
                ip.protocol = protocolType;
                ip.hopLimit = Functions::numToByte(hopLimit, 1);
                ip.destinationAddress = destIp;
                packetInfo.Layer3.insert(packetInfo.Layer3.begin(), ip);
            }
            Ethernet::build(iface, packetInfo, &destIp, destMac, Variable::Ethernet::ipv6);
        }
        else if (destIp.size() == 4)
        {
            {
                IPv4Header ip;
                {
                    std::shared_lock<std::shared_mutex> lock(iface->Get()->ipMutex);
                    ip.sourceAddress = (sourceIp && sourceIp->size() == 4) ? *sourceIp : iface->Get()->ipv4.ipAddress;
                }

                ip.version = ByteString("4", 1);
                ip.headerLength = ByteString("5", 1);
                ip.serviceField = Functions::numToByte(DSCP, 1);
                ip.totalLength = ByteString(2, 0x02);
                ip.identification = ByteString(2, 0x00);

                ip.fragmentFlag.reserved = ByteString(reserved ? "1" : "0", 1);
                ip.fragmentFlag.fragment = ByteString(dontFragment ? "1" : "0", 1);
                ip.fragmentFlag.moreFragment = ByteString(moreFragment ? "1" : "0", 1);
                ip.fragmentFlag.fragmentOffset = Functions::numToBin(fragmentOffset, 13);

                ip.TTL = Functions::numToByte(hopLimit, 1);
                ip.protocol = protocolType;
                ip.checksum = ByteString(2, 0x00);
                ip.destinationAddress = destIp;
                packetInfo.Layer3.insert(packetInfo.Layer3.begin(), ip);
            }
            Ethernet::build(iface, packetInfo, &destIp, destMac, Variable::Ethernet::ipv4);
        }
    }

    // Sets the UPD header in PacketInfo
    void IPPacket::buildUdp(Interface* iface, PacketInfo& packetInfo, const ByteString& destIp, ByteString const* sourceIp, ByteString const* destMac, uint8_t DSCP, uint8_t hopLimit, const ByteString& type,const ByteString& sourcePort,const ByteString& destinationPort, bool reserved, bool dontFragment, bool moreFragment, uint16_t fragmentOffset)
    {
        UdpHeader udp;
        udp.checksum = ByteString("\x00\x00", 2);
        udp.sourcePort = sourcePort;
        udp.destinationPort = destinationPort;
        packetInfo.Layer4.push_back(std::move(udp));

        buildIp(iface, packetInfo, destIp, sourceIp, destMac, DSCP, hopLimit, type, reserved, dontFragment, moreFragment, fragmentOffset);
    }
}
