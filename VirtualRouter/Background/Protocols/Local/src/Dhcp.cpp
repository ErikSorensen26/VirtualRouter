#include "../include/Dhcp.h"
#include <Interface.h>
#include <random>

namespace Protocol {
    // Constructor for the DhcpClient class, initializes with a reference to an Interface object
    DhcpClient::DhcpClient(Interface& CurrentInterface) : currentInterface(&CurrentInterface) 
    {
        // Nothing Listed
    }

    // Generates a random DHCP transaction ID
    ByteString DhcpClient::generateDhcpTransid() 
    {
        std::random_device rd; 
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> dis(0, UINT32_MAX);
        uint32_t transid = dis(gen);
        std::stringstream ss;
        ss << std::hex << std::setw(8) << std::setfill('0') << transid;
        return Functions::hexToByte(ss.str()); 
    }

    // Initializes DHCP, sends discover requests, handles offers, and sends requests and acknowledgments
    void DhcpClient::InitializeDhcp(ByteString& hardwareAddress) 
    {
        bool run = true;

        while (run) 
        {
            bool offer = true;
            bool sideload = true;

            // Loop to handle DHCP discover and offer
            while (offer) 
            {
                // Check if lease time has expired
                if (leaseStart + Functions::byteToNum(ByteString("0x00", 1) + currentInterface->Get()->dhcp.leaseTime) < secondsSinceEpoch()) 
                {
                    std::string hostname = Global::getInstance().getHostname();
                    PacketInfo dhcpPacket = dhcpBody(hardwareAddress); 
                    PacketInfo discoverInfo = dhcpDiscover(dhcpPacket,hostname , hardwareAddress);
                    currentInterface->enqueuePacket(discoverInfo); 
                    std::this_thread::sleep_for(std::chrono::seconds(2)); 

                    std::lock_guard<std::mutex> lock(dhcpMutex);
                    if (!dhcpOffer.Layer2.empty())
                    {
                        offer = false; 
                        PacketInfo dhcpBody2 = dhcpBody(hardwareAddress);

                        if (!dhcpOffer.Layer5.empty())
                        { 
                            std::any header = dhcpOffer.Layer5[0];

                            // Check and extract DHCP header
                            if (header.has_value() && header.type() == typeid(DhcpHeader)) 
                            {
                                DhcpHeader* dhcp = std::any_cast<DhcpHeader>(&header); 
                                if (dhcp) 
                                {
                                    ExtractOptions(dhcp->options);
                                    PacketInfo requestInfo = dhcpRequest(dhcpBody2, *dhcp, hostname, hardwareAddress, Variable::IPv4::source, Variable::IPv4::source); // Create DHCP request packet
                                    currentInterface->enqueuePacket(requestInfo); 
                                }
                            }
                        }
                    }
                }
                else 
                {
                    offer = false; 
                }
            }

            // Loop to handle DHCP acknowledgment
            offer = true;
            while (offer) 
            {
                if (!dhcpAck.Layer2.empty())
                { 
                    std::any header = dhcpOffer.Layer5[0]; 
                    if (header.has_value() && header.type() == typeid(DhcpHeader)) 
                    {
                        DhcpHeader* dhcp = std::any_cast<DhcpHeader>(&header); 
                        if (dhcp) 
                        {
                            dhcpAck.Layer2.clear(); 
                            offer = false; 
                            for (auto opt : dhcp->options) 
                            { 
                                if (opt.option == Variable::Dhcp::Option::type && opt.value == Variable::Dhcp::Type::nak) 
                                {
                                    sideload = false; 
                                }
                            }
                            if (sideload) 
                            {
                                ExtractOptions(dhcp->options); 
                                currentInterface->setIPv4(dhcp->yourClientIP, currentInterface->Get()->dhcp.subnetMask);
                                leaseStart = secondsSinceEpoch(); 
                            }
                        }
                    }
                }
            }

            // Loop to handle DHCP lease renewal
            while (sideload) 
            {
                if (leaseStart + Functions::byteToNum(currentInterface->Get()->dhcp.renewalTime) < secondsSinceEpoch()) 
                {   
                    ByteString dhcpIP = currentInterface->Get()->ipv4.ipAddress;
                    DhcpHeader header; 
                    header.yourClientIP = dhcpIP; 
                    header.transID = generateDhcpTransid();
                    std::string hostname = Global::getInstance().getHostname();
                    PacketInfo dhcpPacket = dhcpBody(hardwareAddress); 
                    PacketInfo requestInfo = dhcpRequest(dhcpPacket, header, hostname, hardwareAddress, dhcpIP, currentInterface->Get()->dhcp.dhcpServer); // Create DHCP request packet
                    currentInterface->enqueuePacket(requestInfo); 
                    sideload = false;
                    acked = false;
                }
            }
        }
    }

    // Creates a DHCP packet body with Ethernet, IP, and UDP headers
    PacketInfo DhcpClient::dhcpBody(ByteString& hardwareAddress)
    {
        PacketInfo dhcpPacket;

        EthernetHeader eth;
        IPv4Header ip;
        UdpHeader udp;

        eth.destinationMac = Variable::Mac::broadcast; 
        eth.sourceMac = hardwareAddress; 
        eth.type = Variable::Ethernet::ipv4;

        dhcpPacket.Layer2.push_back(eth);

        ip.version = "4";
        ip.headerLength = "5";
        ip.serviceField = std::string("\x00", 1);
        ip.totalLength = std::string("\x00\x00", 2); 
        ip.identification = std::string("\x00\x00", 2); 
        ip.fragmentFlag.reserved = "0"; 
        ip.fragmentFlag.fragment = "0";
        ip.fragmentFlag.moreFragment = "0";
        ip.fragmentFlag.fragment = "0000000000000";
        ip.TTL = std::string("\x10", 1);
        ip.protocol = Variable::IP::udp; 
        ip.checksum = std::string("\x00\x00", 2);
        ip.sourceAddress = Variable::IPv4::source;
        ip.destinationAddress = Variable::IPv4::broadcast;

        dhcpPacket.Layer3.push_back(ip);

        udp.sourcePort = Variable::Udp::Dhcp::source;
        udp.destinationPort = Variable::Udp::Dhcp::destination;
        udp.length = std::string("\x01\x00", 2);
        udp.checksum = std::string("\x00\x00", 2); 

        dhcpPacket.Layer4.push_back(udp); 
        return dhcpPacket; 
    }

    // Creates a DHCP discover packet
    PacketInfo DhcpClient::dhcpDiscover(PacketInfo packet, std::string& hostname, ByteString& hardwareAddress) 
    {
        DhcpHeader dhcp;

        dhcp.boot = Variable::Dhcp::Type::discover; 
        dhcp.hardwareType = std::string("\x01", 1);
        dhcp.hardwareAddressLength = std::string("\x06", 1);
        dhcp.hops = std::string("\x00", 1);
        dhcp.transID = generateDhcpTransid(); 
        dhcp.secondsElapsed = std::string("\x00\x00", 2);
        dhcp.bootpFlags.broadcast = "0";
        dhcp.bootpFlags.reserved = "000000000000000";
        dhcp.clientIP = Variable::IPv4::source; 
        dhcp.yourClientIP = Variable::IPv4::source;
        dhcp.nextServerIP = Variable::IPv4::source;
        dhcp.relayAgentIP = Variable::IPv4::source;
        dhcp.clientMacAddress = hardwareAddress;
        dhcp.clientHardwareAddressPadding = Variable::Dhcp::clientHardwareAddressPadding;
        dhcp.serverHostName = Variable::Dhcp::serverHostName;
        dhcp.bootFile = Variable::Dhcp::bootfile;
        dhcp.magicCookie = Variable::Dhcp::magicCookie; 

        dhcp.options.resize(4);

        dhcp.options[0].option = Variable::Dhcp::Option::type;
        dhcp.options[0].length = std::string("\x01", 1); 
        dhcp.options[0].value = Variable::Dhcp::Type::discover;

        dhcp.options[1].option = Variable::Dhcp::Option::clientID;
        dhcp.options[1].length = std::string("\x06", 1);
        dhcp.options[1].value = hardwareAddress;

        dhcp.options[2].option = Variable::Dhcp::Option::maxSize; 
        dhcp.options[2].length = std::string("\x02", 1); 
        dhcp.options[2].value = std::string("\x02\x40", 2);

        dhcp.options[3].option = Variable::Dhcp::Option::hostname;
        dhcp.options[3].length = Functions::numToByte(static_cast<uint32_t>(hostname.length()));
        dhcp.options[3].value = hostname; 

        dhcp.end = Variable::Dhcp::end; 

        packet.Layer5.push_back(dhcp);

        return packet; 
    }

    PacketInfo DhcpClient::dhcpRequest(PacketInfo packet, DhcpHeader& header, std::string& hostname, ByteString hardwareAddress, ByteString requestedIP, ByteString serverID) 
    {
        DhcpHeader dhcp;

        dhcp.boot = Variable::Dhcp::Type::discover; 
        dhcp.hardwareType = std::string("\x01", 1); 
        dhcp.hardwareAddressLength = std::string("\x06", 1); 
        dhcp.hops = std::string("\x00", 1); 
        dhcp.transID = header.transID; 
        dhcp.secondsElapsed = std::string("\x00\x00", 2); 
        dhcp.bootpFlags.broadcast = "0"; 
        dhcp.bootpFlags.reserved = "000000000000000"; 
        dhcp.clientIP = Variable::IPv4::source; 
        dhcp.yourClientIP = header.yourClientIP; 
        dhcp.nextServerIP = Variable::IPv4::source; 
        dhcp.relayAgentIP = Variable::IPv4::source; 
        dhcp.clientMacAddress = hardwareAddress; 
        dhcp.clientHardwareAddressPadding = Variable::Dhcp::clientHardwareAddressPadding; 
        dhcp.serverHostName = Variable::Dhcp::serverHostName; 
        dhcp.bootFile = Variable::Dhcp::bootfile; 
        dhcp.magicCookie = Variable::Dhcp::magicCookie; 

        dhcp.options.resize(7); 

        dhcp.options[0].option = Variable::Dhcp::Option::type; 
        dhcp.options[0].length = std::string("\x01", 1); 
        dhcp.options[0].value = Variable::Dhcp::Type::request; 

        dhcp.options[1].option = Variable::Dhcp::Option::clientID;
        dhcp.options[1].length = std::string("\x06", 1);
        dhcp.options[1].value = hardwareAddress; 

        dhcp.options[2].option = Variable::Dhcp::Option::serverIdentifier;
        dhcp.options[2].length = std::string("\x04", 1);
        dhcp.options[2].value = currentInterface->Get()->dhcp.dhcpServer;

        dhcp.options[3].option = Variable::Dhcp::Option::requestIP;
        dhcp.options[3].length = std::string("\x04", 1);
        dhcp.options[3].value = header.yourClientIP; 

        dhcp.options[4].option = Variable::Dhcp::Option::leaseTime; 
        dhcp.options[4].length = Functions::numToByte(static_cast<uint32_t>(currentInterface->Get()->dhcp.leaseTime.size()));
        dhcp.options[4].value = currentInterface->Get()->dhcp.leaseTime;

        dhcp.options[5].option = Variable::Dhcp::Option::hostname; 
        dhcp.options[5].length = Functions::numToByte(static_cast<uint32_t>(hostname.length()));
        dhcp.options[5].value = hostname; 

        dhcp.options[6].option = Variable::Dhcp::Option::requestList; 
        dhcp.options[6].length = std::string("\x0d", 1);
        dhcp.options[6].value = Variable::Dhcp::Option::mask + 
            Variable::Dhcp::Option::broadcast + 
            Variable::Dhcp::Option::timeOffset + 
            Variable::Dhcp::Option::router + 
            Variable::Dhcp::Option::domainName + 
            Variable::Dhcp::Option::domainServer +
            Variable::Dhcp::Option::domainSearch +
            Variable::Dhcp::Option::hostname +
            Variable::Dhcp::Option::netbiosNameServer +
            Variable::Dhcp::Option::netbiosScope +
            Variable::Dhcp::Option::mtu +
            Variable::Dhcp::Option::classlessStateRoute + 
            Variable::Dhcp::Option::ntp; 

        dhcp.end = Variable::Dhcp::end; 

        packet.Layer5.push_back(dhcp);

        return packet; 
    }

    // Method to extract DHCP options from a response
    void DhcpClient::ExtractOptions(std::vector<DhcpHeader::Option> options) 
    {
        for (auto opt : options) 
        {
            if (opt.option == Variable::Dhcp::Option::serverIdentifier) 
            {
                currentInterface->Get()->dhcp.dhcpServer = opt.value;
            } 
            else if (opt.option == Variable::Dhcp::Option::leaseTime) 
            {
                currentInterface->Get()->dhcp.leaseTime = opt.value;
            } 
            else if (opt.option == Variable::Dhcp::Option::renewalTime) 
            {
                currentInterface->Get()->dhcp.renewalTime = opt.value;
            } 
            else if (opt.option == Variable::Dhcp::Option::rebindingTime) 
            {
                currentInterface->Get()->dhcp.rebindingTime = opt.value;
            } 
            else if (opt.option == Variable::Dhcp::Option::mask) 
            { 
                currentInterface->Get()->dhcp.subnetMask = Functions::byteMaskToNum(opt.value);
            } 
            else if (opt.option == Variable::Dhcp::Option::broadcast) 
            {
                currentInterface->Get()->dhcp.broadcast = opt.value;
            } 
            else if (opt.option == Variable::Dhcp::Option::domainServer) 
            {
                currentInterface->Get()->dhcp.dnsServer.push_back(opt.value);
            } 
            else if (opt.option == Variable::Dhcp::Option::router) 
            {
                currentInterface->Get()->dhcp.router = opt.value;
            }
        }
    }

    // Placeholder for a method to handle DHCP packets
    void DhcpClient::DhcpPacket(const DhcpHeader* header, ByteString& type)
    {
        // Implementation needed
    }
}
