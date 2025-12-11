#include <DhcpServer.h>
#include <IPPacket.h>
#include <Udp.h>
#include <PacketBuilder.hpp>
#include <Interface.h>
#include <InterfaceConfigs.h>
#include <TLVOptions.hpp>
#include <Encryption.hpp>
#include <DhcpInfo.hpp>
#include <Global.h>

void Protocol::DhcpServer::handlePacket(const DhcpHeader& dhcp, const uint8_t* sourceMac, Interface& iface)
{
    if (dhcp.getMagicCookie() != Variable::Dhcp::magicCookie)
        return;

    auto trail = dhcp.getTrail();
    std::vector<TLV8Option> options;
    if (!parseDhcpOptions(trail.data(), trail.size(), options)) return;

    uint8_t overload = 0;
    uint8_t msgType = 0;

    size_t clientMaxSize = configs.clientMaxSize.load(std::memory_order_relaxed);
    ClientID client;

    const TLV8Option* relayOpt = nullptr;
    const TLV8Option* authOpt = nullptr;

    for (const auto& opt : options)
    {
        switch (opt.type)
        {
            case Variable::Dhcp::Option::type: 
                if (opt.valueSize == 1)
                    msgType = opt.value[0];
                break;
            case Variable::Dhcp::Option::overload:
                if (opt.valueSize == 1)
                    overload = opt.value[0];
                break;
            case Variable::Dhcp::Option::maxSize:
                if (opt.valueSize == 2)
                    clientMaxSize = readU16(opt.value);
                break;
            case Variable::Dhcp::Option::clientID:
                client = { opt.value, opt.valueSize };
                break;
            case Variable::Dhcp::Option::relayAgentInfo:
                relayOpt = &opt;
                break;
            case Variable::Dhcp::Option::authentication:
                authOpt = &opt;
                break;
            default:
                break;
        }
    }

    if (msgType == 0) return;

    if (!client.data)
        client = { dhcp.getClientMac(), 6 };

    if (authManager.shouldEnforce(client))
    {
        if (!authOpt || (options.size() < 2 || options[options.size() - 2].type != Variable::Dhcp::Option::authentication) || !validateAuthentication(dhcp, client, authOpt->value))
            return;
    }

    if (options.empty() || options.back().type != Variable::Dhcp::end)
        return;

    if (overload & 1)
    {
        std::vector<TLV8Option> fileOptions;
        parseDhcpOptions(dhcp.raw->file, 128, fileOptions, true);
        options.insert(options.end() - 1, fileOptions.begin(), fileOptions.end());
    }

    if (overload & 2)
    {
        std::vector<TLV8Option> snameOptions;
        parseDhcpOptions(dhcp.raw->serverName, 64, snameOptions);
        options.insert(options.end(), snameOptions.begin(), snameOptions.end());
    }

    // Snooping
    if (!iface.configs.trusted.load(std::memory_order_relaxed))
    {
        bool allowed = false;
        uint16_t ifaceVlan = iface.configs.vlan.load(std::memory_order_relaxed);

        for (const auto& [vlan, size] : configs.snooping.vlans)
            if (ifaceVlan >= vlan && ifaceVlan <= vlan + *size.rbegin())
                allowed = true;

        if (!allowed)
        {
            bool isServerType = msgType == Variable::Dhcp::Type::offer ||
                                msgType == Variable::Dhcp::Type::ack ||
                                msgType == Variable::Dhcp::Type::nak;

            if (isServerType && !configs.snooping.allowUntrusted.load(std::memory_order_relaxed))
                return;

            if (configs.snooping.verifyMac.load(std::memory_order_relaxed))
                if (!sourceMac || std::memcmp(sourceMac, dhcp.getClientMac(), 6) == 0)
                    return; // MAC mismatch

            if (configs.snooping.verifyGiaddr.load(std::memory_order_relaxed))
            {
                uint32_t giaddr = readU32(dhcp.raw->giaddr);
                if (!iface.configs.ipv4.compareAddress(giaddr))
                    return;
            }

            if (configs.snooping.verifyRelay.load(std::memory_order_relaxed))
            {
                if (!relayOpt || relayOpt->valueSize < 2)
                    return;

                const uint8_t* data = relayOpt->value;
                size_t size = relayOpt->valueSize;

                bool circuitIDFound = false;
                bool removeIDFound = false;

                size_t offset = 0;
                while (offset + 2 <= size)
                {
                    uint8_t suboptType = data[offset];
                    uint8_t suboptLen = data[offset + 1];
                    offset += 2;

                    if (offset + suboptLen > size)
                        return;

                    const uint8_t* suboptValue = data + offset;
                    std::string id(reinterpret_cast<const char*>(suboptValue), suboptLen);

                    switch (suboptType)
                    {
                        case 1: // Circuit ID
                            if (configs.snooping.trustedCircuiteIDs.find(id) != configs.snooping.trustedCircuiteIDs.end())
                                circuitIDFound = true;
                            break;
                        case 2: // Remote ID
                            if (configs.snooping.trustedRemoteIDs.find(id) != configs.snooping.trustedRemoteIDs.end())
                                removeIDFound = true;
                            break;
                        default:
                            break;
                    }
                    offset += 2 + suboptLen;
                }
                if (!circuitIDFound && !removeIDFound)
                    return;
            }
        }
    }

    switch (msgType)
    {
        case Variable::Dhcp::Type::discover:
            processDiscover(dhcp, options, client, iface, clientMaxSize, relayOpt);
            break;
        case Variable::Dhcp::Type::request:
            processRequest(dhcp, options, client, iface, clientMaxSize, relayOpt);
            break;
        case Variable::Dhcp::Type::release:
            processRelease(dhcp, options, client, iface);
            break;
        case Variable::Dhcp::Type::inform:
            processInform(dhcp, options, client, iface, clientMaxSize, relayOpt);
            break;
        case Variable::Dhcp::Type::decline:
            processDecline(dhcp, options, client, iface);
            break;
        case Variable::Dhcp::Type::leaseQuery:
            processLeaseQuery(dhcp, options, client, iface, clientMaxSize, relayOpt);
            break;
        default:
            break;
    }
}

void Protocol::DhcpServer::sendOffer(
    Interface& iface,
    const uint8_t* transID,
    const ClientID& client,
    const uint8_t* chaddr,
    const uint8_t* giaddr,
    uint32_t ip,
    const uint8_t* destination,
    uint8_t* requests,
    size_t requestsSize,
    size_t clientMaxSize,
    const TLV8Option* relayInfo
)
{
    PacketBuilder builder(&iface);
    UDP::reserveUDP(builder, AddressFamily::IPv4);
    builder.reserveHeader(HeaderType::DHCP, DhcpHeader::fixedSize);

    auto* header = builder.nextBuildHeader();
    if (!header) return;

    DhcpHeader dhcp;
    dhcp.setBuffer(header->buffer);
    dhcp.setOpcode(0x02);
    dhcp.setHType(0x01); // Ethernet
    dhcp.setHLen(6);
    dhcp.setXid(transID);

    std::memset(dhcp.raw->ciaddr, 0, 4);
    writeU32(dhcp.raw->yiaddr, ip);
    dhcp.setRelayAgentIp(giaddr);
    dhcp.setServerName(Variable::Dhcp::serverHostName);
    dhcp.setBootFile(Variable::Dhcp::bootfile);
    dhcp.setClientMac(chaddr);
    dhcp.setMagicCookie(Variable::Dhcp::magicCookie);

    size_t mtuLimit = iface.configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxAllowed = clientMaxSize == 0 ? mtuLimit : std::min(mtuLimit, clientMaxSize);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;

    // Add TLV options directly into trailing span
    const std::string* authKey = authManager.getKeyForClient(client);
    Dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, authKey, requests, requestsSize);

    // Message Type
    tlv.tlv.append(Variable::Dhcp::Option::type, 1, &Variable::Dhcp::Type::offer, 1);

    // Server Identifier
    if (!Dhcp::appendTLV(tlv, Variable::Dhcp::Option::serverIdentifier, iface.configs.ipv4.getAddressInt()))
        return;

    // Lease Config
    Dhcp::DhcpNetwork* net = matchingNetwork(iface, dhcp);
    if (!net) return;

    uint32_t lease = net->configs.leaseTime.load(std::memory_order_relaxed);
    uint32_t t1 = (net->configs.t1Percentage.load(std::memory_order_relaxed) * lease) / 100;
    uint32_t t2 = (net->configs.t2Percentage.load(std::memory_order_relaxed) * lease) / 100;

    // Lease Time
    if (!Dhcp::appendTLV(tlv, Variable::Dhcp::Option::leaseTime, lease))
        return;

    // Lease T1
    if (!Dhcp::appendTLV(tlv, Variable::Dhcp::Option::renewalTime, t1))
        return;

    // Lease T2
    if (!Dhcp::appendTLV(tlv, Variable::Dhcp::Option::rebindingTime, t2))
        return;

    // Subnet Mask
    if (!Dhcp::appendTLV(tlv, Variable::Dhcp::Option::mask, net->configs.getSubnetMask()))
        return;

    // Router (Default Gateway)
    if (!Dhcp::appendTLV(tlv, Variable::Dhcp::Option::router, net->configs.getGateway()))
        return;

    // Dns Servers
    appendDnsServers(tlv, net->configs);

    // Domain Search List
    if (configs.offerSearchDomain && !net->configs.searchDomains.empty())
        appendDomainSearchList(tlv, net->configs.searchDomains);

    // Vendor-Specific
    appendVendorOptions(tlv, client, iface, net->configs);

    // Boot options
    appendBootOptions(tlv, net->configs);

    // Echo back option relay info
    if (relayInfo)
        if (!Dhcp::appendTLV(tlv, Variable::Dhcp::Option::relayAgentInfo, relayInfo->valueSize, relayInfo->value))
            return;

    if (requestsSize != 0)
        addRequestedOptions(tlv, *net, requests, requestsSize);

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(Variable::Dhcp::Option::overload, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp, client);

    tlv.tlv.append(Variable::Dhcp::end, 0, nullptr, 0);
    builder.addTLVSize(tlv.tlv.size());

    Interface* currentInterface = &iface;
    IPPacket::BuildIP ipBuild = {
        .iface = currentInterface,
        .packetInfo = builder,
        .destIp = destination,
        .destMac = chaddr,
        .hopLimit = 64,
        .protocolType = Variable::IP::udp
    };

    UDP::buildUdp(
        AddressFamily::IPv4,
        ipBuild,
        Variable::Udp::dhcpServer,
        Variable::Udp::dhcpClient
    );
}

void Protocol::DhcpServer::sendAck(
    Interface& iface,
    const uint8_t* transID,
    const ClientID& client,
    const uint8_t* chaddr,
    const uint8_t* giaddr,
    uint32_t ip,
    const uint8_t* destination,
    uint8_t* requests,
    size_t requestsSize,
    bool isRC,
    size_t clientMaxSize,
    const TLV8Option* relayInfo
)
{
    PacketBuilder builder(&iface);
    UDP::reserveUDP(builder, AddressFamily::IPv4);
    builder.reserveHeader(HeaderType::DHCP, DhcpHeader::fixedSize);

    auto* header = builder.nextBuildHeader();
    if (!header) return;

    DhcpHeader dhcp;
    dhcp.setBuffer(header->buffer);
    dhcp.setOpcode(0x02);
    dhcp.setHType(0x01);
    dhcp.setHLen(6);
    dhcp.setXid(transID);

    std::memset(dhcp.raw->ciaddr, 0, 4);
    writeU32(dhcp.raw->yiaddr, ip);
    dhcp.setRelayAgentIp(giaddr);
    dhcp.setServerName(Variable::Dhcp::serverHostName);
    dhcp.setBootFile(Variable::Dhcp::bootfile);
    dhcp.setClientMac(chaddr);
    dhcp.setMagicCookie(Variable::Dhcp::magicCookie);

    size_t mtuLimit = iface.configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxAllowed = std::min(clientMaxSize, mtuLimit);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;
    
    const std::string* authKey = authManager.getKeyForClient(client);
    Dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, authKey, requests, requestsSize);

    // Message type
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::type, 1, &Variable::Dhcp::Type::ack);

    // Server Identifier
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::serverIdentifier, iface.configs.ipv4.getAddressInt());

    // Lease config
    Dhcp::DhcpNetwork* net = matchingNetwork(iface, dhcp);
    if (!net) return;

    uint32_t lease = net->configs.leaseTime.load(std::memory_order_relaxed);
    uint32_t t1 = (net->configs.t1Percentage.load(std::memory_order_relaxed) * lease) / 100;
    uint32_t t2 = (net->configs.t2Percentage.load(std::memory_order_relaxed) * lease) / 100;

    // Lease Time
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::leaseTime, lease);

    // Lease T1
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::renewalTime, t1);

    // Lease T2
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::rebindingTime, t2);

    // Subnet Mask
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::mask, net->configs.getSubnetMask());
    
    // Router
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::router, net->configs.getGateway());

    // Dns Servers
    appendDnsServers(tlv, net->configs);

    // Domain Search List
    if (configs.offerSearchDomain.load(std::memory_order_relaxed) && !net->configs.searchDomains.empty())
        appendDomainSearchList(tlv, net->configs.searchDomains);

    // Vendor Options
    appendVendorOptions(tlv, client, iface, net->configs);

    // Boot options
    appendBootOptions(tlv, net->configs);
    
    // Echo relay option
    if (relayInfo)
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::relayAgentInfo, relayInfo->valueSize, relayInfo->value);

    // Option 55
    if (requestsSize != 0)
        addRequestedOptions(tlv, *net, requests, requestsSize);

    // Rapid commit
    if (isRC)
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::rapidCommit, 0, nullptr);

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(Variable::Dhcp::Option::overload, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp, client);

    // End
    tlv.tlv.append(Variable::Dhcp::end, 0, nullptr, 0);
    builder.addTLVSize(tlv.tlv.size());

    // Add to snooping table
    if (!configs.snooping.allowUntrusted.load(std::memory_order_relaxed))
    {
        Dhcp::SnoopingEntry entry;
        std::memcpy(entry.mac, chaddr, 6);
        entry.ip = ip;
        entry.interface = iface.configs.key;
        entry.expiration = std::chrono::steady_clock::now() + std::chrono::seconds(net->configs.leaseTime);

        std::lock_guard<std::mutex> lock(serverMutex);
        snoopingTable.emplace(readU48(entry.mac), entry);
    }

    Interface* currentInterface = &iface;
    IPPacket::BuildIP ipBuild = {
        .iface = currentInterface,
        .packetInfo = builder,
        .destIp = destination,
        .destMac = chaddr,
        .hopLimit = 64,
        .protocolType = Variable::IP::udp
    };

    UDP::buildUdp(
        AddressFamily::IPv4,
        ipBuild,
        Variable::Udp::dhcpServer,
        Variable::Udp::dhcpClient
    );
}

void Protocol::DhcpServer::sendNak(
    Interface& iface,
    const uint8_t* transID,
    const ClientID& client,
    const uint8_t* chaddr,
    const uint8_t* giaddr,
    size_t clientMaxSize,
    const TLV8Option* relayInfo
)
{
    PacketBuilder builder(&iface);
    UDP::reserveUDP(builder, AddressFamily::IPv4);
    builder.reserveHeader(HeaderType::DHCP, DhcpHeader::fixedSize);

    auto* header = builder.nextBuildHeader();
    if (!header) return;

    DhcpHeader dhcp;
    dhcp.setBuffer(header->buffer);
    dhcp.setOpcode(0x02);
    dhcp.setHType(0x01);
    dhcp.setHLen(6);
    dhcp.setXid(transID);

    std::memset(dhcp.raw->ciaddr, 0, 12);
    dhcp.setRelayAgentIp(giaddr);

    dhcp.setServerName(Variable::Dhcp::serverHostName);
    dhcp.setBootFile(Variable::Dhcp::bootfile);
    dhcp.setClientMac(chaddr);
    dhcp.setMagicCookie(Variable::Dhcp::magicCookie);

    size_t mtuLimit = iface.configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxAllowed = std::min(clientMaxSize, mtuLimit);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;

    const std::string* authKey = authManager.getKeyForClient(client);
    Dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, authKey);

    // Nak
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::type, 1, &Variable::Dhcp::Type::nak);

    // Server Identifier
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::serverIdentifier, iface.configs.ipv4.getAddressInt());

    // Echo relay option
    if (relayInfo)
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::relayAgentInfo, relayInfo->valueSize, relayInfo->value);

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(Variable::Dhcp::Option::overload, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp, client);

    // End
    tlv.tlv.append(Variable::Dhcp::end, 0, nullptr, 0);
    builder.addTLVSize(tlv.tlv.size());

    bool hasGiaddr = giaddr && std::memcmp(giaddr, Variable::IPv4::source, 4) != 0;
    const uint8_t* destIP = hasGiaddr ? giaddr : Variable::IPv4::broadcast;

    Interface* currentInterface = &iface;
    IPPacket::BuildIP ipBuild = {
        .iface = currentInterface,
        .packetInfo = builder,
        .destIp = destIP,
        .destMac = chaddr,
        .hopLimit = 64,
        .protocolType = Variable::IP::udp
    };

    UDP::buildUdp(
        AddressFamily::IPv4,
        ipBuild,
        Variable::Udp::dhcpServer,
        Variable::Udp::dhcpClient
    );
}

void Protocol::DhcpServer::sendInformReply(
    Interface& iface,
    const uint8_t* transID,
    const ClientID& client,
    const uint8_t* chaddr,
    const uint8_t* giaddr,
    const uint8_t* destination,
    uint8_t* requests,
    size_t requestsSize,
    size_t clientMaxSize,
    const TLV8Option* relayInfo
)
{
    PacketBuilder builder(&iface);
    UDP::reserveUDP(builder, AddressFamily::IPv4);
    builder.reserveHeader(HeaderType::DHCP, DhcpHeader::fixedSize);

    auto* header = builder.nextBuildHeader();
    if (!header) return;

    DhcpHeader dhcp;
    dhcp.setBuffer(header->buffer);
    dhcp.setOpcode(0x02);
    dhcp.setHType(0x01);
    dhcp.setHLen(6);
    dhcp.setXid(transID);

    std::memset(dhcp.raw->ciaddr, 0, 12);
    dhcp.setRelayAgentIp(giaddr);
    dhcp.setServerName(Variable::Dhcp::serverHostName);
    dhcp.setBootFile(Variable::Dhcp::bootfile);
    dhcp.setClientMac(chaddr);
    dhcp.setMagicCookie(Variable::Dhcp::magicCookie);

    size_t mtuLimit = iface.configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxAllowed = std::min(clientMaxSize, mtuLimit);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;

    const std::string* authKey = authManager.getKeyForClient(client);
    Dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, authKey);

    // ACK
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::type, 1, &Variable::Dhcp::Type::ack);

    // Server Identifier
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::serverIdentifier, iface.configs.ipv4.getAddressInt());

    Dhcp::DhcpNetwork* net = matchingNetwork(iface, dhcp);
    if (!net) return;

    // DNS
    appendDnsServers(tlv, net->configs);

    // Domain search list
    if (configs.offerSearchDomain && !net->configs.searchDomains.empty())
        appendDomainSearchList(tlv, net->configs.searchDomains);

    // Vendor Options
    appendVendorOptions(tlv, {chaddr, 6}, iface, net->configs);

    // Boot options
    appendBootOptions(tlv, net->configs);

    // Respect option 55 or fallback to override
    if (requestsSize != 0)
    {
        addRequestedOptions(tlv, *net, requests, requestsSize);
    }
    else if (configs.option55Override.load(std::memory_order_relaxed))
    {
        addRequestedOptions(tlv, *net, nullptr, 0);
    }

    // Echo relay option
    if (relayInfo)
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::relayAgentInfo, relayInfo->valueSize, relayInfo->value);

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(Variable::Dhcp::Option::overload, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp, client);

    // End
    tlv.tlv.append(Variable::Dhcp::end, 0, nullptr, 0);
    builder.addTLVSize(tlv.tlv.size());

    Interface* currentInterface = &iface;
    IPPacket::BuildIP ipBuild = {
        .iface = currentInterface,
        .packetInfo = builder,
        .destIp = destination,
        .destMac = chaddr,
        .hopLimit = 64,
        .protocolType = Variable::IP::udp
    };

    UDP::buildUdp(
        AddressFamily::IPv4,
        ipBuild,
        Variable::Udp::dhcpServer,
        Variable::Udp::dhcpClient
    );
}

void Protocol::DhcpServer::sendForceRenew(
    Interface& iface,
    const ClientID& client,
    const uint8_t* chaddr,
    uint32_t ip,
    const uint8_t* destination,
    size_t clientMaxSize,
    const TLV8Option* relayInfo
)
{
    PacketBuilder builder(&iface);
    UDP::reserveUDP(builder, AddressFamily::IPv4);
    builder.reserveHeader(HeaderType::DHCP, DhcpHeader::fixedSize);

    auto* header = builder.nextBuildHeader();
    if (!header) return;

    DhcpHeader dhcp;
    dhcp.setBuffer(header->buffer);
    dhcp.setOpcode(0x02);
    dhcp.setHType(0x01);
    dhcp.setHLen(6);

    generateDhcpTransid(dhcp.raw->xId);

    std::memset(dhcp.raw->ciaddr, 0, 16);
    writeU32(dhcp.raw->yiaddr, ip);

    dhcp.setServerName(Variable::Dhcp::serverHostName);
    dhcp.setBootFile(Variable::Dhcp::bootfile);
    dhcp.setClientMac(chaddr);
    dhcp.setMagicCookie(Variable::Dhcp::magicCookie);

    size_t mtuLimit = iface.configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxAllowed = std::min(clientMaxSize, mtuLimit);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;

    const std::string* authKey = authManager.getKeyForClient(client);
    if (!authKey) return; // ForceRenew can only run authenticated.
    Dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, true);

    // ForceRenew
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::type, 1, &Variable::Dhcp::Type::forceRenew);

    // Server Identifier
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::serverIdentifier, iface.configs.ipv4.getAddressInt());

    // Echo relay option
    if (relayInfo)
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::relayAgentInfo, relayInfo->valueSize, relayInfo->value);

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(Variable::Dhcp::Option::overload, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp, client);

    tlv.tlv.append(Variable::Dhcp::end, 0, nullptr, 0);
    builder.addTLVSize(tlv.tlv.size());

    Interface* currentInterface = &iface;
    IPPacket::BuildIP ipBuild = {
        .iface = currentInterface,
        .packetInfo = builder,
        .destIp = destination,
        .destMac = chaddr,
        .hopLimit = 64,
        .protocolType = Variable::IP::udp
    };

    UDP::buildUdp(
        AddressFamily::IPv4,
        ipBuild,
        Variable::Udp::dhcpServer,
        Variable::Udp::dhcpClient
    );
}

void Protocol::DhcpServer::sendLeaseQueryReply(
    Interface& iface,
    const uint8_t* transID,
    const ClientID& client,
    uint32_t clientIP,
    const uint8_t* chaddr,
    const uint8_t* giaddr,
    const TLV8Option* relayInfo,
    const IPv4LeaseManager::Lease* lease,
    uint8_t prefixLen,
    uint8_t responseType,
    size_t clientMaxSize
)
{
    PacketBuilder builder(&iface);
    
    UDP::reserveUDP(builder, AddressFamily::IPv4);
    builder.reserveHeader(HeaderType::DHCP, DhcpHeader::fixedSize);

    auto* nextHeader = builder.nextBuildHeader();
    if (!nextHeader) return;

    DhcpHeader dhcp;
    dhcp.setBuffer(nextHeader->buffer);
    dhcp.setOpcode(0x02);
    dhcp.setHType(0x01);
    dhcp.setHLen(6);
    dhcp.setHops(0);
    dhcp.setXid(transID);

    std::memset(dhcp.raw->ciaddr, 0, 16);
    dhcp.setClientMac(chaddr);

    dhcp.setServerName(Variable::Dhcp::serverHostName);
    dhcp.setBootFile(Variable::Dhcp::bootfile);
    dhcp.setClientMac(chaddr);
    dhcp.setMagicCookie(Variable::Dhcp::magicCookie);

    if (responseType == Variable::Dhcp::Type::leaseActive ||
        responseType == Variable::Dhcp::Type::leaseUnassigned)
    {
        writeU32(dhcp.raw->ciaddr, clientIP);
    }

    size_t mtuLimit = iface.configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxAllowed = clientMaxSize == 0 ? mtuLimit : std::min(mtuLimit, clientMaxSize);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;

    const std::string* authKey = authManager.getKeyForClient(client);
    Dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, authKey);

    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::type, 1, &responseType);

    if (relayInfo)
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::relayAgentInfo, relayInfo->valueSize, relayInfo->value);

    if (responseType == Variable::Dhcp::Type::leaseActive && lease)
    {
        // Lease time
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::leaseTime, lease->leaseTime);

        // Lease T1
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::renewalTime, lease->t1);

        // Lease T2
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::rebindingTime, lease->t2);

        // Mask
        uint8_t buf[4];
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::mask, 4, Functions::prefixToMask(buf, prefixLen, AddressFamily::IPv4));

        // Router
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::router, iface.configs.ipv4.getAddressInt());

        // Timestamp
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::timestamp, secondsSinceEpoch());
    }

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(Variable::Dhcp::Option::overload, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp, client);

    // End
    tlv.tlv.append(Variable::Dhcp::end, 0, nullptr, 0);
    builder.addTLVSize(tlv.tlv.size());

    Interface* currentInterface = &iface;
    IPPacket::BuildIP ipBuild = {
        .iface = currentInterface,
        .packetInfo = builder,
        .destIp = giaddr,
        .destMac = chaddr,
        .hopLimit = 64,
        .protocolType = Variable::IP::udp
    };

    UDP::buildUdp(
        AddressFamily::IPv4,
        ipBuild,
        Variable::Udp::dhcpServer,
        Variable::Udp::dhcpClient
    );
}

void Protocol::DhcpServer::processDiscover(
    const DhcpHeader& dhcp,
    std::vector<TLV8Option>& options,
    ClientID& client,
    Interface& iface,
    size_t clientMaxSize,
    const TLV8Option* relayInfo
)
{
    if (dhcp.raw->hLen != 6)
        return;
    
    Dhcp::DhcpNetwork* net = matchingNetwork(iface, dhcp);
    if (!net)
        return;

    // Extract Client ID
    uint8_t msgType = 0;
    uint8_t* requestOptions = nullptr;
    size_t requestOptionSize = 0;
    uint32_t requestedIp = 0;
    bool rapidCommit = false;
    bool broadcast = (dhcp.getFlags() & 0x8000) == 0;

    for (auto& opt : options)
    {
        switch (opt.type)
        {
            case Variable::Dhcp::Option::type:
                if (opt.valueSize == 1)
                    msgType = opt.value[0];
                break;
            case Variable::Dhcp::Option::requestList:
                requestOptionSize = opt.valueSize;
                requestOptions = opt.value;
                break;
            case Variable::Dhcp::Option::rapidCommit:
                if (opt.valueSize == 0)
                    rapidCommit = configs.rapidCommit.load(std::memory_order_relaxed);
                break;
            case Variable::Dhcp::Option::requestIP:
                requestedIp = readU32(opt.value);
            default:
                break;
        }
    }

    if (msgType != Variable::Dhcp::Type::discover)
        return;

    if (configs.limitLeases.load(std::memory_order_relaxed))
        if (net->leaseManager->size() >= configs.leasesPerInterface.load(std::memory_order_relaxed))
            return;

    auto leaseParams = [&]() -> std::tuple<uint32_t, uint32_t, uint32_t>
    {
        uint32_t leaseTime = net->configs.leaseTime.load(std::memory_order_relaxed);
        uint32_t t1 = (net->configs.t1Percentage.load(std::memory_order_relaxed) * leaseTime) / 100;
        uint32_t t2 = (net->configs.t2Percentage.load(std::memory_order_relaxed) * leaseTime) / 100;
        return { leaseTime, t1, t2 };
    };

    auto getIp = [&]() -> uint32_t
    {
        if (configs.checkForConflict.load(std::memory_order_relaxed))
        {
            for (int retry = 0; retry < configs.conflictRetry.load(std::memory_order_relaxed); ++retry)
            {
                uint32_t ip = 0;
                if (rapidCommit)
                {
                    auto leaseTime = leaseParams();
                    if (requestedIp != 0 && net->leaseManager->hasLease(client, requestedIp))
                    {
                        ip = requestedIp;
                    }
                    else if (requestedIp != 0)
                    {
                        bool success = net->leaseManager->createLeaseFromReq(
                            client,
                            requestedIp,
                            std::get<0>(leaseTime),
                            std::get<1>(leaseTime),
                            std::get<2>(leaseTime)
                        );
                        if (success)
                            ip = requestedIp;
                    }

                    if (ip == 0)
                    {
                        ip = net->leaseManager->createLease(
                            client,
                            std::get<0>(leaseTime),
                            std::get<1>(leaseTime),
                            std::get<2>(leaseTime)
                        );
                    }
                }
                else
                {
                    if (requestedIp != 0 && net->leaseManager->hasLease(client, requestedIp))
                    {
                        ip = requestedIp;
                    }
                    else if (requestedIp != 0)
                    {
                        bool success = net->pool->allocateRequestedTemporaryIP(
                            requestedIp,
                            client,
                            configs.offerExpiration.load(std::memory_order_relaxed)
                        );
                        if (success)
                            ip = requestedIp;
                    }

                    if (ip == 0)
                    {
                        ip = net->pool->allocateTemporaryIP(
                            client,
                            configs.offerExpiration.load(std::memory_order_relaxed)
                        );
                    }
                }

                if (ip == 0) return 0;

                if (configs.pingTimeout.load(std::memory_order_relaxed) > 0)
                {
                    bool isUse = false;
                    for (int attempt = 0; attempt < configs.pingRetryCount.load(std::memory_order_relaxed); ++attempt)
                    {
                        //TODO Placeholder: real ping check, set isUse = true
                        //isUse = true;
                        //break;
                    }

                    if (!isUse)
                        return ip;

                    if (configs.logConflicts)
                        std::cout << "[DHCP] Conflict IP " << ip << " is in use." << std::endl;

                    net->pool->setConflicted(ip, configs.conflictResolution.load(std::memory_order_relaxed));
                    std::this_thread::sleep_for(std::chrono::seconds(configs.conflictInterval.load(std::memory_order_relaxed)));
                }
                else return ip;
            }
            return 0;
        }
        else
        {
            if (rapidCommit)
            {
                auto leaseTime = leaseParams();
                return net->leaseManager->createLease(client, std::get<0>(leaseTime), std::get<1>(leaseTime), std::get<2>(leaseTime));
            }
            else
            {
                return net->pool->allocateTemporaryIP(client, configs.offerExpiration.load(std::memory_order_relaxed));
            }
        }
    };

    const uint8_t* destination = nullptr;
    bool hasRelay = std::memcmp(dhcp.getRelayAgentIP(), Variable::IPv4::source, 4);

    if (hasRelay)
        destination = dhcp.getRelayAgentIP();
    else if (broadcast || dhcp.getClientIP() == 0)
        destination = Variable::IPv4::broadcast;
    else
        destination = dhcp.getClientIP();

    uint32_t offeredIP = getIp();
    if (offeredIP == 0)
    {
        sendNak(iface, dhcp.getXid(), client, dhcp.getClientMac(), dhcp.getRelayAgentIP(), clientMaxSize, relayInfo);
        return;
    }

    if (rapidCommit)
    {
        sendAck(
            iface,
            dhcp.getXid(),
            client,
            dhcp.getClientMac(),
            dhcp.getRelayAgentIP(),
            offeredIP,
            destination,
            requestOptions,
            requestOptionSize,
            true,
            clientMaxSize,
            relayInfo
        );
    }
    else
    {
        sendOffer(
            iface,
            dhcp.getXid(),
            client,
            dhcp.getClientMac(),
            dhcp.getRelayAgentIP(),
            offeredIP,
            destination,
            requestOptions,
            requestOptionSize,
            clientMaxSize,
            relayInfo
        );
    }
}

void Protocol::DhcpServer::processRequest(
    const DhcpHeader& dhcp,
    std::vector<TLV8Option>& options,
    ClientID& client,
    Interface& iface,
    size_t clientMaxSize,
    const TLV8Option* relayInfo
)
{
    if (dhcp.raw->hLen != 6)
        return;

    Dhcp::DhcpNetwork* net = matchingNetwork(iface, dhcp);
    if (!net)
        return;

    uint8_t msgType = 0;
    size_t requestOptionSize = 0;
    uint8_t* requestOptions = nullptr;
    const uint8_t* requestIP = nullptr;
    bool broadcast = (dhcp.getFlags() & 0x8000) == 0;

    for (const auto& opt : options)
    {
        switch (opt.type)
        {
            case Variable::Dhcp::Option::type:
                if (opt.valueSize == 1)
                    msgType = opt.value[0];
                break;
            case Variable::Dhcp::Option::requestList:
                requestOptionSize = opt.valueSize;
                requestOptions = opt.value;
                break;
            case Variable::Dhcp::Option::requestIP:
                if (opt.valueSize == 4)
                    requestIP = opt.value;
                break;
            default:
                break;
        }
    }
    
    if (msgType != Variable::Dhcp::Type::request)
        return;

    const uint8_t* destination = nullptr;
    bool hasRelay = std::memcmp(dhcp.getRelayAgentIP(), Variable::IPv4::source, 4) != 0;

    if (hasRelay) 
        destination = dhcp.getRelayAgentIP();
    else if (broadcast || dhcp.getClientIPInt() == 0)
        destination = Variable::IPv4::broadcast;
    else
        destination = dhcp.getClientIP();

    uint32_t leaseTime = net->configs.leaseTime.load(std::memory_order_relaxed);
    uint32_t t1 = (net->configs.t1Percentage.load(std::memory_order_relaxed) * leaseTime) / 100;
    uint32_t t2 = (net->configs.t2Percentage.load(std::memory_order_relaxed) * leaseTime) / 100;

    // Handle renewals via ciaddr
    if (dhcp.getClientIPInt() != 0)
    {
        uint32_t ip = dhcp.getClientIPInt();
        if (net->leaseManager->hasLease(client, ip))
        {
            net->leaseManager->renewLease(client, leaseTime, t1, t2);
            sendAck(iface,
                dhcp.getXid(),
                client,
                dhcp.getClientMac(),
                dhcp.getRelayAgentIP(),
                dhcp.getClientIPInt(),
                destination,
                requestOptions,
                requestOptionSize,
                false,
                clientMaxSize,
                relayInfo
            );
            return;
        }
    }
    else if (requestIP)
    {
        uint32_t reqIp = readU32(requestIP);
        
        if (net->leaseManager->createLeaseFromTemp(client, reqIp, leaseTime, t1, t2))
        {
            sendAck(
                iface,
                dhcp.getXid(),
                client,
                dhcp.getClientMac(),
                dhcp.getRelayAgentIP(),
                reqIp,
                destination,
                requestOptions,
                requestOptionSize,
                false,
                clientMaxSize,
                relayInfo
            );
            return;
        }
    }

    // No valid lease or bad request - send NAK
    sendNak(
        iface,
        dhcp.getXid(),
        client,
        dhcp.getClientMac(),
        dhcp.getRelayAgentIP(),
        clientMaxSize,
        relayInfo
    );
}

void Protocol::DhcpServer::processDecline(
    const DhcpHeader& dhcp,
    std::vector<TLV8Option>& options,
    ClientID& client,
    Interface& iface
)
{
    if (dhcp.raw->hLen != 6)
        return;
    
    Dhcp::DhcpNetwork* net = matchingNetwork(iface, dhcp);
    if (!net)
        return;

    for (const auto& opt : options)
    {
        if (opt.type == Variable::Dhcp::Option::clientID)
        {
            client = {opt.value, opt.valueSize};
            break;
        }
    }

    if (!client.data)
        client = {dhcp.getClientMac(), 6};

    uint32_t declinedIP = dhcp.getClientIPInt();
    if (declinedIP == 0)
        return;

    if (!net->leaseManager->hasLease(client, declinedIP))
        return; // Only process if the client has/had this lease

    net->leaseManager->declineLease(client, declinedIP);

    if (configs.logConflicts)
    {
        //TODO
    }
}

void Protocol::DhcpServer::processRelease(
    const DhcpHeader& dhcp,
    std::vector<TLV8Option>& options,
    ClientID& client,
    Interface& iface
)
{
    if (dhcp.raw->hLen != 6)
        return;

    Dhcp::DhcpNetwork* net = matchingNetwork(iface, dhcp);
    if (!net)
        return;

    for (const auto& opt : options)
    {
        if (opt.type == Variable::Dhcp::Option::clientID)
        {
            client = {opt.value, opt.valueSize};
            break;
        }
    }

    if (!client.data)
        client = {dhcp.getClientMac(), 6};

    uint32_t releaseIP = dhcp.getClientIPInt();
    if (releaseIP == 0)
        return; // Nothing to release

    if (!net->leaseManager->hasLease(client, releaseIP))
        return; // Only release if the lease was actually owned

    net->leaseManager->releaseLease(client);
}

void Protocol::DhcpServer::processInform(
    const DhcpHeader& dhcp,
    std::vector<TLV8Option>& options,
    ClientID& client,
    Interface& iface,
    size_t clientMaxSize,
    const TLV8Option* relayInfo
)
{
    if (dhcp.raw->hLen != 6)
        return;

    Dhcp::DhcpNetwork* net = matchingNetwork(iface, dhcp);
    if (!net)
        return;

    uint8_t* requestOptions = nullptr;
    size_t requestOptionSize = 0;

    for (const auto& opt : options)
    {
        switch (opt.type)
        {
            case Variable::Dhcp::Option::requestList:
                requestOptionSize = opt.valueSize;
                requestOptions = opt.value;
                break;
            default:
                break;
        }
    }

    // Must have a valid ciaddr
    if (dhcp.getClientIPInt() == 0)
        return;

    const uint8_t* destination = nullptr;
    bool hasRelay = std::memcmp(dhcp.getRelayAgentIP(), Variable::IPv4::source, 4) != 0;

    if (hasRelay)
        destination = dhcp.getRelayAgentIP();
    else if (dhcp.getClientIP() == 0)
        destination = Variable::IPv4::broadcast;
    else
        destination = dhcp.getClientIP();

    sendInformReply(
        iface,
        dhcp.getXid(),
        client,
        dhcp.getClientMac(),
        dhcp.getRelayAgentIP(),
        destination,
        requestOptions,
        requestOptionSize,
        clientMaxSize,
        relayInfo
    );
}

void Protocol::DhcpServer::processLeaseQuery(
    const DhcpHeader& dhcp,
    std::vector<TLV8Option>& options,
    ClientID& client,
    Interface& iface,
    size_t clientMaxSize,
    const TLV8Option* relayInfo
)
{
    if (dhcp.raw->hLen != 6)
        return;

    Dhcp::DhcpNetwork* net = matchingNetwork(iface, dhcp);
    if (!net) return;

    uint32_t queryIP = 0;

    for (const auto& opt : options)
    {
        switch (opt.type)
        {
            case Variable::Dhcp::Option::requestIP:
                if (opt.valueSize == 4)
                    queryIP = readU32(opt.value);
                break;
            default:
                break;
        }
    }

    std::optional<IPv4LeaseManager::Lease> lease = net->leaseManager->getLease(client, queryIP);
    
    uint8_t responseType = Variable::Dhcp::Type::leaseUnknown;

    if (lease.has_value() && lease.value().expiry > std::chrono::steady_clock::now())
    {
        responseType = Variable::Dhcp::Type::leaseActive;
    }
    else if (queryIP && net->pool->withinRange(queryIP))
    {
        responseType = Variable::Dhcp::Type::leaseUnassigned;
    }

    IPv4LeaseManager::Lease* leasePtr = lease.has_value()
        ? &lease.value()
        : nullptr;

    sendLeaseQueryReply(
        iface,
        dhcp.getXid(),
        client,
        queryIP,
        dhcp.getClientMac(),
        dhcp.getRelayAgentIP(),
        relayInfo,
        leasePtr,
        net->configs.getPrefixLen(),
        responseType,
        clientMaxSize
    );
}

void Protocol::DhcpServer::appendDnsServers(Dhcp::DhcpTLVManager& tlv, const Dhcp::DhcpNetworkConfig& network)
{
    bool override = configs.updateDNS.override.load(std::memory_order_relaxed);
    bool both = configs.updateDNS.both.load(std::memory_order_relaxed);
    bool before = configs.updateDNS.before.load(std::memory_order_relaxed);

    const auto& localList = network.dnsServers;
    const auto& globalList = configs.globalDnsServers;

    std::vector<uint32_t> finalList;

    if (override)
    {
        finalList = globalList;
    }
    else if (both)
    {
        finalList = localList;
        finalList.insert(finalList.end(), globalList.begin(), globalList.end());
    }
    else if (before)
    {
        finalList = globalList;
        finalList.insert(finalList.end(), localList.begin(), localList.end());
    }
    else
    {
        finalList = localList;
    }

    uint8_t buffer[255];

    size_t offset = 0;
    for (const uint32_t& ip : finalList)
    {
        if (offset < 52)
        {
            writeU32(buffer + offset, ip);
            offset += 4;
        }
        else break;
    }

    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::domainServer, offset, buffer);
}

void Protocol::DhcpServer::appendNtpServers(Dhcp::DhcpTLVManager& tlv, const Dhcp::DhcpNetworkConfig& network)
{
    if (network.ntpServers.size() == 0) return;
    uint8_t buffer[255];

    size_t offset = 0;
    for (const uint32_t& ip : network.ntpServers)
    {
        if (offset < 52)
        {
            writeU32(buffer + offset, ip);
            offset += 4;
        }
        else break;
    }

    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::ntp, offset, buffer);
}

void Protocol::DhcpServer::appendAuthOptions(TLV8BufferManager& tlv, const DhcpHeader& dhcp, const ClientID& clientID)
{
    const std::string* key = authManager.getKeyForClient(clientID);
    if (!key) return;

    uint8_t* authOpt = tlv.getNextValBuf(27);
    if (!authOpt) return;
    tlv.append(Variable::Dhcp::Option::authentication, 27, nullptr, 27);

    authOpt[0] = 1;
    authOpt[1] = 1;
    authOpt[2] = 0;

    uint64_t counter = authManager.getReplayCounter(clientID) + 1;
    writeU64(authOpt + 3, counter);

    // Zero the digest field
    std::memset(authOpt + 11, 0, 16);

    // Build HMAC input over full packet assuming end option is not yet added
    Authentication::generateHMAC(
        authOpt + 11,
        dhcp.buffer,
        DhcpHeader::fixedSize + tlv.size(),
        reinterpret_cast<const uint8_t*>(key->data()),
        key->size(),
        Authentication::HmacType::MD5
    );

    authManager.getReplayCounter(clientID) = counter;
}

bool Protocol::DhcpServer::validateAuthentication(const DhcpHeader& dhcp, const ClientID& clientID, const uint8_t* data)
{
    if (!data) return !authManager.shouldEnforce(clientID);

    // Check fields
    if (data[0] != 1 || data[1] != 1 || data[2] != 0)
        return false;

    // Extract replay counter
    uint64_t counter = readU64(data + 3);
    if (counter <= authManager.getReplayCounter(clientID))
        return false;

    const std::string* key = authManager.getKeyForClient(clientID);
    if (!key) return false;

    // Copy out hash
    uint8_t receivedHash[16];
    std::memcpy(receivedHash, data + 11, 16);
    std::memset(const_cast<uint8_t*>(data + 11), 0, 16); // Set hash to 0s
    
    uint8_t computedHash[16];
    Authentication::generateHMAC(
        computedHash,
        dhcp.buffer,
        DhcpHeader::fixedSize + dhcp.getTrail().size() - 2,
        reinterpret_cast<const uint8_t*>(key->data()),
        key->size(),
        Authentication::HmacType::MD5
    );

    if (std::memcmp(computedHash, receivedHash, 16) != 0)
        return false;

    authManager.getReplayCounter(clientID) = counter;
    return true;
}

void Protocol::DhcpServer::appendVendorOptions(Dhcp::DhcpTLVManager& tlv, const ClientID& client, const Interface& iface, const Dhcp::DhcpNetworkConfig& configs)
{
    if (!configs.vendorClassID.empty())
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::vendorClassID, configs.vendorClassID.size(), reinterpret_cast<const uint8_t*>(configs.vendorClassID.data()));

    uint8_t buffer[256];
    size_t offset = 0;

    // PXE Discovery control (sub 6)
    if (configs.pxeDiscoveryControl.has_value() && offset + 3 <= sizeof(buffer))
    {
        buffer[offset++] = 6;
        buffer[offset++] = 1;
        buffer[offset++] = configs.pxeDiscoveryControl.value();
    }

    // PXE Boot Servers (sub 8)
    if (!configs.pxeBootServers.empty())
    {
        size_t count = std::min(configs.pxeBootServers.size(), static_cast<size_t>((255 - 2 - offset) / 4));
        buffer[offset++] = 8;
        buffer[offset++] = static_cast<uint8_t>(count * 4);
        for (size_t i = 0; i < count; ++i)
            writeU32(buffer + offset + i * 4, configs.pxeBootServers[i]);
        offset += count * 4;
    }

    if (offset > 0)
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::vendorSpecific, offset, buffer);
}

void Protocol::DhcpServer::appendBootOptions(Dhcp::DhcpTLVManager& tlv, const Dhcp::DhcpNetworkConfig& configs)
{
    // Option 66
    if (!configs.tftpServerName.empty())
    {
        const auto* data = reinterpret_cast<const uint8_t*>(configs.tftpServerName.data());
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::tftpServerName, configs.tftpServerName.size(), data);
    }

    // Option 67
    if (!configs.bootFileName.empty())
    {
        const auto* data = reinterpret_cast<const uint8_t*>(configs.bootFileName.data());
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::bootfile, configs.bootFileName.size(), data);
    }

    // Option 150
    if (!configs.tftpServers.empty())
    {
        size_t count = std::min(configs.tftpServers.size(), static_cast<size_t>((255 / 4)));
        uint8_t ipData[255];
        for (size_t i = 0; i < count; ++i)
            writeU32(ipData + i * 4, configs.tftpServers[i]);
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::tftpServers, count * 4, ipData);
    }
}

Protocol::Dhcp::DhcpNetwork* Protocol::DhcpServer::matchingNetwork(const Interface& iface, const DhcpHeader& dhcp) const
{
    auto findMatchingNetworkAgainstIP([&](const uint8_t* ip) -> Dhcp::DhcpNetwork* {
        for (auto& [_, config] : networks)
        {
            auto network = config->configs.getNetworkID();
            if (config->pool->init.load(std::memory_order_relaxed), Functions::compareNetworkWithIp(network.addr, ip, network.prefixLength, AddressFamily::IPv4))
            {
                return config;
            }
        }
        return nullptr;
    });

    // Match based on the relay agent IP (if present)
    auto relayMatch = findMatchingNetworkAgainstIP(dhcp.getRelayAgentIP());
    if (relayMatch)
    {
        return relayMatch;
    }

    // Fall back to client IP matching
    auto clientMatch = findMatchingNetworkAgainstIP(dhcp.getClientIP());
    if (clientMatch)
    {
        return clientMatch;
    }

    return nullptr; // No match found
}

Protocol::Dhcp::DhcpNetwork* Protocol::DhcpServer::addPool(const std::string& poolName)
{
    std::lock_guard<std::mutex> lock(serverMutex);
    // Create a new pool and lease for the dhcp network.
    networks.emplace(poolName, new Dhcp::DhcpNetwork(timeManager, configs));
}

void Protocol::DhcpServer::removePool(std::string& poolName)
{
    std::lock_guard<std::mutex> lock(serverMutex);
    // Remove network config.
    auto it = networks.find(poolName);
    if (it != networks.end())
    {
        delete it->second;
        networks.erase(it);
    }
}

void Protocol::DhcpServer::addRequestedOptions(Dhcp::DhcpTLVManager& tlv, Dhcp::DhcpNetwork& network, const uint8_t* requestList, size_t requestsSize)
{
    if (!requestList || requestsSize == 0)
        return;

    uint8_t* buffer = nullptr;

    for (size_t i = 0; i < requestsSize; ++i)
    {
        uint8_t opt = requestList[i];
        uint32_t lease = network.configs.leaseTime.load(std::memory_order_relaxed);

        switch (opt)
        {
            case Variable::Dhcp::Option::mask:
                Dhcp::appendTLV(tlv, opt, network.configs.getSubnetMask());
                break;
            case Variable::Dhcp::Option::broadcast:
                Dhcp::appendTLV(tlv, opt, network.pool->broadcast.load(std::memory_order_relaxed));
                break;
            case Variable::Dhcp::Option::router:
                Dhcp::appendTLV(tlv, opt, network.configs.getGateway());
                break;
            case Variable::Dhcp::Option::domainName:
                if (network.configs.domainName.size() > 0)
                    Dhcp::appendTLV(tlv, opt, network.configs.domainName.size(), reinterpret_cast<const uint8_t*>(network.configs.domainName.data()));
                break;
            case Variable::Dhcp::Option::mtu:
                if (network.configs.mtu.load(std::memory_order_relaxed) != 0)
                {
                    writeU16(buffer, network.configs.mtu.load(std::memory_order_relaxed));
                    Dhcp::appendTLV(tlv, opt, 2, buffer);
                }
                break;
            case Variable::Dhcp::Option::ntp:
                appendNtpServers(tlv, network.configs);
                break;
            case Variable::Dhcp::Option::hostname:
                {
                    std::string hostname = global.getHostname();
                    Dhcp::appendTLV(tlv, opt, hostname.size(), reinterpret_cast<uint8_t*>(hostname.data()));
                }
                break;
            case Variable::Dhcp::Option::leaseTime:
                Dhcp::appendTLV(tlv, opt, lease);
                break;
            case Variable::Dhcp::Option::renewalTime:
                Dhcp::appendTLV(tlv, opt, (network.configs.t1Percentage.load(std::memory_order_relaxed) * lease) / 100);
                break;
            case Variable::Dhcp::Option::rebindingTime:
                Dhcp::appendTLV(tlv, opt, (network.configs.t2Percentage.load(std::memory_order_relaxed) * lease) / 100);
                break;
            default:
                break;
        }
    }
}

void Protocol::DhcpServer::appendDomainSearchList(Dhcp::DhcpTLVManager& tlv, const std::vector<std::string>& domains)
{
    
}

bool Protocol::Dhcp::DhcpNetworkConfig::updateNetwork(IPPrefix& prefix, uint32_t& gateway)
{
    return true;
}

IPPrefix Protocol::Dhcp::DhcpNetworkConfig::getNetworkID() const
{
    return IPPrefix();
}

uint8_t* Protocol::Dhcp::DhcpNetworkConfig::getNetwork(uint8_t* out) const
{
    return nullptr;
}

uint32_t Protocol::Dhcp::DhcpNetworkConfig::getNetwork() const
{
    return 0;
}

uint8_t* Protocol::Dhcp::DhcpNetworkConfig::getGateway(uint8_t* out) const
{
    return nullptr;
}

uint32_t Protocol::Dhcp::DhcpNetworkConfig::getGateway() const
{
    return 0;
}

uint8_t Protocol::Dhcp::DhcpNetworkConfig::getPrefixLen() const
{
    return 0;
}

uint8_t* Protocol::Dhcp::DhcpNetworkConfig::getSubnetMask(uint8_t* out)
{
    return nullptr;
}

uint32_t Protocol::Dhcp::DhcpNetworkConfig::getSubnetMask()
{
    return 0;
}
