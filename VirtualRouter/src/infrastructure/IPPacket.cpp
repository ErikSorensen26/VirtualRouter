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

                        uint8_t firstByte = destIp[0];
                        uint8_t secondByte = destIp[1];

                        // Multicast (FF00::/8)
                        if (firstByte == 0xFF)
                        {
                            uint8_t scope = secondByte & 0x0F; // Low nibble = bits 12-15
                        
                            if (scope == 0x01 || scope == 0x02)
                            {
                                if (iface->configs.ipv6.linkLocalAddress)
                                    ip.sourceAddress = iface->configs.ipv6.linkLocalAddress->ip;
                            }
                            else if (scope == 0x05 || scope == 0x08) // Site/org-local
                            {
                                if (!iface->configs.ipv6.uniqueLocalAddresses.empty())
                                    ip.sourceAddress = iface->configs.ipv6.uniqueLocalAddresses.front()->ip;
                            }
                            else if (scope == 0x0E) // Global scope
                            {
                                if (!iface->configs.ipv6.globalAddresses.empty())
                                    ip.sourceAddress = iface->configs.ipv6.globalAddresses.front()->ip;
                            }
                            else
                            {
                                return; // Return if no matching scope is found
                            }
                        }
                        // Global Unicast (2000::/3 = 001xxxxx)
                        else if ((firstByte & 0b11100000) == 0b00100000)
                        {
                            if (!iface->configs.ipv6.globalAddresses.empty())
                                ip.sourceAddress = iface->configs.ipv6.globalAddresses.front()->ip;
                        }
                        // Unique Local (FC00::/7 = 1111110x)
                        else if ((firstByte & 0b11111110) == 0b11111100)
                        {
                            if (!iface->configs.ipv6.uniqueLocalAddresses.empty())
                                ip.sourceAddress = iface->configs.ipv6.uniqueLocalAddresses.front()->ip;
                        }
                        // Link-local (FE80::/10 = 1111111010xxxxxxx)
                        else if ((firstByte & 0b11111100) == 0b11111100 && (secondByte & 0b00001111) == 0x00)
                        {
                            if (iface->configs.ipv6.linkLocalAddress)
                                ip.sourceAddress = iface->configs.ipv6.linkLocalAddress->ip;
                        }
                        // Loopback (::1)
                        else if (std::all_of(destIp.begin(), destIp.end() - 1, [](uint8_t b) { return b == 0; }) && destIp[15] == 1)
                        {
                            ip.sourceAddress = ByteString(16, 0x00); // 16 bytes initialized to 0
                            ip.sourceAddress[15] = 0x01;             // Set last byte to 1 using bitwise operator
                        }
                        else
                        {
                            return; // Unsuported or unconfigured destination
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
