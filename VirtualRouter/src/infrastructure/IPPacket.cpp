#include "IPPacket.h"
#include "Ethernet.h"
#include <Interface.h>

namespace Protocol
{
    void IPPacket::buildIp(Interface* iface, PacketInfo& packetInfo, const ByteString& destIp, ByteString const* sourceIp, ByteString const* destMac, uint8_t DSCP, uint8_t hopLimit, const ByteString& protocolType, bool reserved, bool dontFragment, bool moreFragment, uint16_t fragmentOffset, uint32_t v6FlowLabel)
    {
        if (!iface || iface->shutdownFlag.load(std::memory_order_relaxed)) return;

        // IPv6 Header creation
        if (destIp.size() == 16)
        {        
            {
                IPv6Header ip;
                {
                    if (sourceIp && sourceIp->size() == 16)
                    {
                        ip.sourceAddress = *sourceIp;
                    }
                    else
                    {
                        // Pick best IP to use
                        std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
                        ByteString prefix = Functions::byteToBin(destIp.substr(0, 2));
                        if (prefix.substr(0, 10) == ByteString("1111111010"))
                        {
                            ip.sourceAddress = iface->configs.ipv6.linkLocalAddress.ip;
                        }
                        else if (prefix.substr(0, 3) == ByteString("001") && !iface->configs.ipv6.globalAddresses.empty())
                        {
                            ip.sourceAddress = iface->configs.ipv6.globalAddresses.front().ip;
                        }
                        else if (prefix.substr(0, 7) == ByteString("1111110") && !iface->configs.ipv6.uniqueLocalAddresses.empty())
                        {
                            ip.sourceAddress = iface->configs.ipv6.uniqueLocalAddresses.front().ip;
                        }
                        else
                        {
                            return; // Return if no matching scope is found
                        }
                    }
                }
                ip.flowLabel = Functions::numToHex(v6FlowLabel, 5);
                ip.version = "6";
                ip.trafficClass = Functions::numToHex(DSCP, 2);
                ip.payloadLength = ByteString(2, 0x00); // Will be calculated later
                ip.protocol = protocolType;
                ip.hopLimit = Functions::numToByte(hopLimit, 1);
                ip.destinationAddress = destIp;
                packetInfo.Layer3.insert(packetInfo.Layer3.begin(), std::move(ip));
            }
            Ethernet::build(iface, packetInfo, &destIp, destMac, Variable::Ethernet::ipv6);

            if (!dontFragment)
            {
                //TODO handle fragmentation
            }
        }
        else if (destIp.size() == 4)
        {
            {
                IPv4Header ip;
                {
                    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
                    ip.sourceAddress = (sourceIp && sourceIp->size() == 4) ? *sourceIp : iface->configs.ipv4.ipAddress;
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
                packetInfo.Layer3.insert(packetInfo.Layer3.begin(), std::move(ip));
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
