#include "../include/Dhcp.h"

using namespace std;

namespace Protocol {
    // Constructor for the DhcpClient class, initializes with a reference to an Interface object
    DhcpClient::DhcpClient(Interface& CurrentInterface) : currentInterface(&CurrentInterface) 
    {
        // Nothing Listed
    }

    // Generates a random DHCP transaction ID
    std::string DhcpClient::generateDhcpTransid() 
    {
        std::random_device rd; 
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> dis(0, UINT32_MAX);
        uint32_t transid = dis(gen);
        std::stringstream ss;
        ss << std::hex << std::setw(8) << std::setfill('0') << transid;
        return ss.str(); 
    }

    // Initializes DHCP, sends discover requests, handles offers, and sends requests and acknowledgments
    void DhcpClient::InitializeDhcp(string& hostname, string& hardwareAddress) 
    {
        bool run = true;
        PacketInfo dhcpBody = DhcpBody(hostname, hardwareAddress, 312); 

        while (run) 
        {
            bool offer = true;
            bool sideload = true;

            // Loop to handle DHCP discover and offer
            while (offer) 
            {
                // Check if lease time has expired
                if (leaseStart + function->hexToNum("00" + function->byteToHex(currentInterface->interfaceInfo.dhcp.leaseTime)) < secondsSinceEpoch()) 
                {
                    PacketInfo discoverInfo = DhcpDiscover(dhcpBody, hostname, hardwareAddress);
                    string discoverPacket = Encapsulate(discoverInfo);
                    currentInterface->packetOutQueue.enqueue(discoverPacket); 
                    std::this_thread::sleep_for(std::chrono::seconds(4)); 

                    std::lock_guard<std::mutex> lock(dhcpMutex);
                    if (!dhcpOffer.Layer2.empty())
                    {
                        offer = false; 
                        PacketInfo dhcpBody2 = DhcpBody(hostname, hardwareAddress, 340); 

                        if (!dhcpOffer.Layer5.empty())
                        { 
                            std::any header = dhcpOffer.Layer5[0];

                            // Check and extract DHCP header
                            if (header.has_value() && header.type() == typeid(dhcpHeader)) 
                            {
                                dhcpHeader* dhcp = std::any_cast<dhcpHeader>(&header); 
                                if (dhcp) 
                                {
                                    ExtractOptions(dhcp->options);
                                    PacketInfo requestInfo = DhcpRequest(dhcpBody2, *dhcp, hostname, hardwareAddress, variable.ip.source, variable.ip.source); // Create DHCP request packet
                                    string requestPacket = Encapsulate(requestInfo);
                                    currentInterface->packetOutQueue.enqueue(requestPacket); 
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
                    if (header.has_value() && header.type() == typeid(dhcpHeader)) 
                    {
                        dhcpHeader* dhcp = std::any_cast<dhcpHeader>(&header); 
                        if (dhcp) 
                        {
                            dhcpAck.Layer2.clear(); 
                            offer = false; 
                            for (auto opt : dhcp->options) 
                            { 
                                if (opt.option == variable.dhcp.options.type && opt.value == variable.dhcp.type.nac) 
                                {
                                    sideload = false; 
                                }
                            }
                            if (sideload) 
                            {
                                ExtractOptions(dhcp->options); 
                                currentInterface->setIPv4(function->byteToHex(dhcp->yourClientIP), function->byteToHex(currentInterface->interfaceInfo.dhcp.subnetMask));
                                leaseStart = secondsSinceEpoch(); 
                            }
                        }
                    }
                }
            }

            // Loop to handle DHCP lease renewal
            while (sideload) 
            {
                if (leaseStart + function->hexToNum(function->byteToHex(currentInterface->interfaceInfo.dhcp.renewalTime)) < secondsSinceEpoch()) 
                {   
                    string dhcpIP = currentInterface->Get().ip;
                    dhcpHeader header; 
                    header.yourClientIP = dhcpIP; 
                    header.transID = function->hexToByte(generateDhcpTransid()); 
                    PacketInfo requestInfo = DhcpRequest(dhcpBody, header, hostname, hardwareAddress, dhcpIP, currentInterface->interfaceInfo.dhcp.dhcpServer); // Create DHCP request packet
                    string requestPacket = Encapsulate(requestInfo); 
                    currentInterface->packetOutQueue.enqueue(requestPacket); 
                    sideload = false;
                    acked = false;
                }
            }
        }
    }

    // Creates a DHCP packet body with Ethernet, IP, and UDP headers
    PacketInfo DhcpClient::DhcpBody(string& hostname, string& hardwareAddress, int length) 
    {
        PacketInfo dhcpPacket;

        ethernetHeader eth;
        ipv4Header ip;
        udpHeader udp;

        eth.destinationMac = variable.mac.broadcast; 
        eth.sourceMac = function->hexToByte(hardwareAddress); 
        eth.type = variable.ethernet.ipv4;

        dhcpPacket.Layer2.push_back(eth);

        ip.version = variable.ipv4.GetValue();
        ip.headerLength = "5";
        ip.serviceField = function->hexToByte("00");
        ip.totalLength = function->hexToByte("0000"); 
        ip.identification = function->hexToByte("0000"); 
        ip.fragmentFlag.reserved = "0"; 
        ip.fragmentFlag.fragment = "0";
        ip.fragmentFlag.moreFragment = "0";
        ip.fragmentFlag.fragment = "0000000000000";
        ip.TTL = function->hexToByte("10");
        ip.protocol = variable.ipv4.udp; 
        ip.checksum = function->hexToByte("0000");
        ip.sourceAddress = variable.ip.source;
        ip.destinationAddress = variable.ip.broadcast;

        dhcpPacket.Layer3.push_back(ip);

        udp.sourcePort = variable.udp.dhcp.source;
        udp.destinationPort = variable.udp.dhcp.destination;
        udp.length = function->hexToByte("0000");
        udp.checksum = function->hexToByte("0000"); 

        dhcpPacket.Layer4.push_back(udp); 
        return dhcpPacket; 
    }

    // Creates a DHCP discover packet
    PacketInfo DhcpClient::DhcpDiscover(PacketInfo packet, string& hostname, string& hardwareAddress) 
    {
        dhcpHeader dhcp;

        dhcp.boot = variable.dhcp.type.discover; 
        dhcp.hardwareType = function->hexToByte("01");
        dhcp.hardwareAddressLength = function->hexToByte("06");
        dhcp.hops = function->hexToByte("00");
        dhcp.transID = function->hexToByte(generateDhcpTransid()); 
        dhcp.secondsElapsed = function->hexToByte("0000");
        dhcp.bootpFlags.broadcast = "0";
        dhcp.bootpFlags.reserved = "000000000000000";
        dhcp.clientIP = variable.ip.source; 
        dhcp.yourClientIP = variable.ip.source;
        dhcp.nextServerIP = variable.ip.source;
        dhcp.relayAgentIP = variable.ip.source;
        dhcp.clientMacAddress = function->hexToByte(hardwareAddress);
        dhcp.clientHardwareAddressPadding = variable.dhcp.clientHardwareAddressPadding;
        dhcp.serverHostName = variable.dhcp.serverHostName;
        dhcp.bootFile = variable.dhcp.bootfile;
        dhcp.magicCookie = variable.dhcp.magicCookie; 

        dhcp.options.resize(4);

        dhcp.options[0].option = variable.dhcp.options.type;
        dhcp.options[0].length = function->hexToByte("01"); 
        dhcp.options[0].value = variable.dhcp.type.discover;

        dhcp.options[1].option = variable.dhcp.options.clientID;
        dhcp.options[1].length = function->hexToByte("06"); 
        dhcp.options[1].value = function->hexToByte(hardwareAddress);

        dhcp.options[2].option = variable.dhcp.options.maxSize; 
        dhcp.options[2].length = function->hexToByte("02"); 
        dhcp.options[2].value = function->hexToByte("0240");

        dhcp.options[3].option = variable.dhcp.options.hostname;
        dhcp.options[3].length = function->hexToByte(function->intToHex(hostname.length())); 
        dhcp.options[3].value = hostname; 

        dhcp.end = variable.dhcp.end; 

        packet.Layer5.push_back(dhcp);

        return packet; 
    }

    PacketInfo DhcpClient::DhcpRequest(PacketInfo packet, dhcpHeader& header, string& hostname, string& hardwareAddress, string& requestedIP, string& serverID) 
    {
        dhcpHeader dhcp;

        dhcp.boot = variable.dhcp.type.discover; 
        dhcp.hardwareType = function->hexToByte("01"); 
        dhcp.hardwareAddressLength = function->hexToByte("06"); 
        dhcp.hops = function->hexToByte("00"); 
        dhcp.transID = header.transID; 
        dhcp.secondsElapsed = function->hexToByte("0000"); 
        dhcp.bootpFlags.broadcast = "0"; 
        dhcp.bootpFlags.reserved = "000000000000000"; 
        dhcp.clientIP = variable.ip.source; 
        dhcp.yourClientIP = header.yourClientIP; 
        dhcp.nextServerIP = variable.ip.source; 
        dhcp.relayAgentIP = variable.ip.source; 
        dhcp.clientMacAddress = function->hexToByte(hardwareAddress); 
        dhcp.clientHardwareAddressPadding = variable.dhcp.clientHardwareAddressPadding; 
        dhcp.serverHostName = variable.dhcp.serverHostName; 
        dhcp.bootFile = variable.dhcp.bootfile; 
        dhcp.magicCookie = variable.dhcp.magicCookie; 

        dhcp.options.resize(7); 

        dhcp.options[0].option = variable.dhcp.options.type; 
        dhcp.options[0].length = function->hexToByte("01"); 
        dhcp.options[0].value = variable.dhcp.type.request; 

        dhcp.options[1].option = variable.dhcp.options.clientID;
        dhcp.options[1].length = function->hexToByte("06");
        dhcp.options[1].value = function->hexToByte(hardwareAddress); 

        dhcp.options[2].option = variable.dhcp.options.serverIdentifier;
        dhcp.options[2].length = function->hexToByte("04");
        dhcp.options[2].value = currentInterface->interfaceInfo.dhcp.dhcpServer;

        dhcp.options[3].option = variable.dhcp.options.requestIP;
        dhcp.options[3].length = function->hexToByte("04");
        dhcp.options[3].value = header.yourClientIP; 

        dhcp.options[4].option = variable.dhcp.options.leaseTime; 
        dhcp.options[4].length = function->hexToByte(function->intToHex(currentInterface->interfaceInfo.dhcp.leaseTime.size())); 
        dhcp.options[4].value = currentInterface->interfaceInfo.dhcp.leaseTime;

        dhcp.options[5].option = variable.dhcp.options.hostname; 
        dhcp.options[5].length = function->hexToByte(function->intToHex(hostname.length())); 
        dhcp.options[5].value = hostname; 

        dhcp.options[6].option = variable.dhcp.options.requestList; 
        dhcp.options[6].length = function->hexToByte("0d"); 
        dhcp.options[6].value = variable.dhcp.options.mask + 
            variable.dhcp.options.broadcast + 
            variable.dhcp.options.timeOffset + 
            variable.dhcp.options.router + 
            variable.dhcp.options.domainName + 
            variable.dhcp.options.domainServer +
            variable.dhcp.options.domainSearch +
            variable.dhcp.options.hostname +
            variable.dhcp.options.netbiosNameServer +
            variable.dhcp.options.netbiosScope +
            variable.dhcp.options.mtu +
            variable.dhcp.options.classlessStateRoute + 
            variable.dhcp.options.ntp; 

        dhcp.end = variable.dhcp.end; 

        packet.Layer5.push_back(dhcp);

        return packet; 
    }

    // Method to extract DHCP options from a response
    void DhcpClient::ExtractOptions(vector<dhcpHeader::Option> options) 
    {
        for (auto opt : options) 
        {
            if (opt.option == variable.dhcp.options.serverIdentifier) 
            {
                currentInterface->interfaceInfo.dhcp.dhcpServer = opt.value; 
            } 
            else if (opt.option == variable.dhcp.options.leaseTime) 
            {
                currentInterface->interfaceInfo.dhcp.leaseTime = opt.value; 
            } 
            else if (opt.option == variable.dhcp.options.renewalTime) 
            {
                currentInterface->interfaceInfo.dhcp.renewalTime = opt.value; 
            } 
            else if (opt.option == variable.dhcp.options.rebindingTime) 
            {
                currentInterface->interfaceInfo.dhcp.rebindingTime = opt.value; 
            } 
            else if (opt.option == variable.dhcp.options.mask) 
            { 
                currentInterface->interfaceInfo.dhcp.subnetMask = opt.value; 
            } 
            else if (opt.option == variable.dhcp.options.broadcast) 
            {
                currentInterface->interfaceInfo.dhcp.broadcast = opt.value; 
            } 
            else if (opt.option == variable.dhcp.options.domainServer) 
            {
                currentInterface->interfaceInfo.dhcp.dnsServer.push_back(opt.value); 
            } 
            else if (opt.option == variable.dhcp.options.router) 
            {
                currentInterface->interfaceInfo.dhcp.router = opt.value; 
            }
        }
    }

    // Placeholder for a method to handle DHCP packets
    void DhcpClient::DhcpPacket(const dhcpHeader* header, string& type)
    {
        // Implementation needed
    }
}