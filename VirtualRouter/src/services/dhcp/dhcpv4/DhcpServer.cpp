// DhcpServer.cpp

#include <Global.h>
#include <iostream>

#include "DhcpServer.h"
#include "infrastructure/IPPacket.h"
#include "udp/Udp.h"
#include "processing/PacketBuilder.hpp"
#include "interface/Interface.h"
#include "packet/TlvOptions.hpp"
#include "security/Encryption.hpp"
#include "dhcp/DhcpInfo.hpp"

namespace services::dhcp
{

void DhcpServer::handlePacket(const packet::DhcpHeader& dhcp, const uint8_t* sourceMac, interface::Interface& iface)
{
    if (dhcp.getMagicCookie() != DHCP_MAGIC_COOKIE)
        return;

    auto trail = dhcp.getTrail();
    std::vector<packet::TLV8Option> options;
    if (!parseDhcpOptions(trail.data(), trail.size(), options)) return;

    uint8_t overload = 0;
    uint8_t msgType = 0;

    size_t clientMaxSize = configs.clientMaxSize.load(std::memory_order_relaxed);
    ClientID client;

    const packet::TLV8Option* relayOpt = nullptr;
    const packet::TLV8Option* authOpt = nullptr;

    for (const auto& opt : options)
    {
        switch (opt.type)
        {
            case DHCP_OPTION_TYPE: 
                if (opt.valueSize == 1)
                    msgType = opt.value[0];
                break;
            case DHCP_OPTION_OVERLOAD:
                if (opt.valueSize == 1)
                    overload = opt.value[0];
                break;
            case DHCP_OPTION_MAX_SIZE:
                if (opt.valueSize == 2)
                    clientMaxSize = utils::read<uint16_t>(opt.value);
                break;
            case DHCP_OPTION_CLIENT_ID:
                client = { opt.value, opt.valueSize };
                break;
            case DHCP_OPTION_RELAY_AGENT_INFO:
                relayOpt = &opt;
                break;
            case DHCP_OPTION_AUTHENTICATION:
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
        if (!authOpt || (options.size() < 2 || options[options.size() - 2].type != DHCP_OPTION_AUTHENTICATION) || !validateAuthentication(dhcp, client, authOpt->value))
            return;
    }

    if (options.empty() || options.back().type != DHCP_OPTION_END)
        return;

    if (overload & 1)
    {
        std::vector<packet::TLV8Option> fileOptions;
        parseDhcpOptions(dhcp.raw->file, 128, fileOptions, true);
        options.insert(options.end() - 1, fileOptions.begin(), fileOptions.end());
    }

    if (overload & 2)
    {
        std::vector<packet::TLV8Option> snameOptions;
        parseDhcpOptions(dhcp.raw->serverName, 64, snameOptions);
        options.insert(options.end(), snameOptions.begin(), snameOptions.end());
    }

    // Snooping
    if (false) // trusted/vlan fields removed from InterfaceConfigs
    {
        bool allowed = false;
        uint16_t ifaceVlan = 0;

        for (const auto& [vlan, size] : configs.snooping.vlans)
            if (ifaceVlan >= vlan && ifaceVlan <= vlan + *size.rbegin())
                allowed = true;

        if (!allowed)
        {
            bool isServerType = msgType == DHCP_TYPE_OFFER ||
                                msgType == DHCP_TYPE_ACK ||
                                msgType == DHCP_TYPE_NAK;

            if (isServerType && !configs.snooping.allowUntrusted.load(std::memory_order_relaxed))
                return;

            if (configs.snooping.verifyMac.load(std::memory_order_relaxed))
                if (!sourceMac || std::memcmp(sourceMac, dhcp.getClientMac(), 6) == 0)
                    return; // MAC mismatch

            if (configs.snooping.verifyGiaddr.load(std::memory_order_relaxed))
            {
                uint32_t giaddr = utils::read<uint32_t>(dhcp.raw->giaddr);
                if (!iface.configs.ipv4.comparePrimaryAddress(giaddr))
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
        case DHCP_TYPE_DISCOVER:
            processDiscover(dhcp, options, client, iface, clientMaxSize, relayOpt);
            break;
        case DHCP_TYPE_REQUEST:
            processRequest(dhcp, options, client, iface, clientMaxSize, relayOpt);
            break;
        case DHCP_TYPE_RELEASE:
            processRelease(dhcp, options, client, iface);
            break;
        case DHCP_TYPE_INFORM:
            processInform(dhcp, options, client, iface, clientMaxSize, relayOpt);
            break;
        case DHCP_TYPE_DECLINE:
            processDecline(dhcp, options, client, iface);
            break;
        case DHCP_TYPE_LEASE_QUERY:
            processLeaseQuery(dhcp, options, client, iface, clientMaxSize, relayOpt);
            break;
        default:
            break;
    }
}

void DhcpServer::sendOffer(
    interface::Interface& iface,
    const uint8_t* transID,
    const ClientID& client,
    const uint8_t* chaddr,
    const uint8_t* giaddr,
    uint32_t ip,
    const uint8_t* destination,
    uint8_t* requests,
    size_t requestsSize,
    size_t clientMaxSize,
    const packet::TLV8Option* relayInfo
)
{
    processing::PacketBuilder builder(&iface);
    transport::udp::reserveUDP(builder, types::AddressFamily::IPv4);
    builder.reserveHeader(packet::HeaderType::DHCP, packet::DhcpHeader::fixedSize);

    auto* header = builder.nextBuildHeader();
    if (!header) return;

    packet::DhcpHeader dhcp;
    dhcp.setBuffer(header->buffer);
    dhcp.setOpcode(0x02);
    dhcp.setHType(0x01); // Ethernet
    dhcp.setHLen(6);
    dhcp.setXid(transID);

    std::memset(dhcp.raw->ciaddr, 0, 4);
    utils::write<uint32_t>(dhcp.raw->yiaddr, ip);
    dhcp.setRelayAgentIp(giaddr);
    dhcp.setServerName(DHCP_SERVER_HOSTNAME);
    dhcp.setBootFile(DHCP_BOOT_FILE);
    dhcp.setClientMac(chaddr);
    dhcp.setMagicCookie(DHCP_MAGIC_COOKIE);

    size_t mtuLimit = iface.configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxAllowed = clientMaxSize == 0 ? mtuLimit : std::min(mtuLimit, clientMaxSize);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;

    // Add TLV options directly into trailing span
    const std::string* authKey = authManager.getKeyForClient(client);
    dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, authKey, requests, requestsSize);

    // Message Type
    uint8_t type = DHCP_TYPE_OFFER;
    tlv.tlv.append(DHCP_OPTION_TYPE, 1, &type, 1);

    // Server Identifier
    if (!dhcp::appendTLV(tlv, DHCP_OPTION_SERVER_IDENTIFIER, iface.configs.ipv4.getPrimaryAddress().addr))
        return;

    // Lease Config
    dhcp::DhcpNetwork* net = matchingNetwork(iface, dhcp);
    if (!net) return;

    uint32_t lease = net->configs.leaseTime.load(std::memory_order_relaxed);
    uint32_t t1 = (net->configs.t1Percentage.load(std::memory_order_relaxed) * lease) / 100;
    uint32_t t2 = (net->configs.t2Percentage.load(std::memory_order_relaxed) * lease) / 100;

    // Lease Time
    if (!dhcp::appendTLV(tlv, DHCP_OPTION_LEASE_TIME, lease))
        return;

    // Lease T1
    if (!dhcp::appendTLV(tlv, DHCP_OPTION_RENEWAL_TIME, t1))
        return;

    // Lease T2
    if (!dhcp::appendTLV(tlv, DHCP_OPTION_REBINDING_TIME, t2))
        return;

    // Subnet Mask
    if (!dhcp::appendTLV(tlv, DHCP_OPTION_MASK, net->configs.getSubnetMask()))
        return;

    // Router (Default Gateway)
    if (!dhcp::appendTLV(tlv, DHCP_OPTION_ROUTER, net->configs.getGateway()))
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
        if (!dhcp::appendTLV(tlv, DHCP_OPTION_RELAY_AGENT_INFO, relayInfo->valueSize, relayInfo->value))
            return;

    if (requestsSize != 0)
        addRequestedOptions(tlv, *net, requests, requestsSize);

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(DHCP_OPTION_OVERLOAD, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp, client);

    tlv.tlv.append(DHCP_OPTION_END, 0, nullptr, 0);
    builder.addTLVSize(tlv.tlv.size());

    interface::Interface* currentInterface = &iface;
    infrastructure::ippacket::BuildIP ipBuild = {
        .iface = currentInterface,
        .packetInfo = builder,
        .destIp = types::IPAddress(destination, types::AddressFamily::IPv4),
        .destMac = utils::read<uint64_t, 6>(chaddr),
        .hopLimit = 64,
        .protocolType = IP_UDP
    };

    transport::udp::buildUdp(
        types::AddressFamily::IPv4,
        ipBuild,
        UDP_DHCP_SERVER,
        UDP_DHCP_CLIENT
    );
}

void DhcpServer::sendAck(
    interface::Interface& iface,
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
    const packet::TLV8Option* relayInfo
)
{
    processing::PacketBuilder builder(&iface);
    transport::udp::reserveUDP(builder, types::AddressFamily::IPv4);
    builder.reserveHeader(packet::HeaderType::DHCP, packet::DhcpHeader::fixedSize);

    auto* header = builder.nextBuildHeader();
    if (!header) return;

    packet::DhcpHeader dhcp;
    dhcp.setBuffer(header->buffer);
    dhcp.setOpcode(0x02);
    dhcp.setHType(0x01);
    dhcp.setHLen(6);
    dhcp.setXid(transID);

    std::memset(dhcp.raw->ciaddr, 0, 4);
    utils::write<uint32_t>(dhcp.raw->yiaddr, ip);
    dhcp.setRelayAgentIp(giaddr);
    dhcp.setServerName(DHCP_SERVER_HOSTNAME);
    dhcp.setBootFile(DHCP_BOOT_FILE);
    dhcp.setClientMac(chaddr);
    dhcp.setMagicCookie(DHCP_MAGIC_COOKIE);

    size_t mtuLimit = iface.configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxAllowed = std::min(clientMaxSize, mtuLimit);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;
    
    const std::string* authKey = authManager.getKeyForClient(client);
    dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, authKey, requests, requestsSize);

    // Message type
    uint8_t type = DHCP_TYPE_ACK;
    dhcp::appendTLV(tlv, DHCP_OPTION_TYPE, 1, &type);

    // Server Identifier
    dhcp::appendTLV(tlv, DHCP_OPTION_SERVER_IDENTIFIER, iface.configs.ipv4.getPrimaryAddress().addr);

    // Lease config
    dhcp::DhcpNetwork* net = matchingNetwork(iface, dhcp);
    if (!net) return;

    uint32_t lease = net->configs.leaseTime.load(std::memory_order_relaxed);
    uint32_t t1 = (net->configs.t1Percentage.load(std::memory_order_relaxed) * lease) / 100;
    uint32_t t2 = (net->configs.t2Percentage.load(std::memory_order_relaxed) * lease) / 100;

    // Lease Time
    dhcp::appendTLV(tlv, DHCP_OPTION_LEASE_TIME, lease);

    // Lease T1
    dhcp::appendTLV(tlv, DHCP_OPTION_RENEWAL_TIME, t1);

    // Lease T2
    dhcp::appendTLV(tlv, DHCP_OPTION_REBINDING_TIME, t2);

    // Subnet Mask
    dhcp::appendTLV(tlv, DHCP_OPTION_MASK, net->configs.getSubnetMask());
    
    // Router
    dhcp::appendTLV(tlv, DHCP_OPTION_ROUTER, net->configs.getGateway());

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
        dhcp::appendTLV(tlv, DHCP_OPTION_RELAY_AGENT_INFO, relayInfo->valueSize, relayInfo->value);

    // Option 55
    if (requestsSize != 0)
        addRequestedOptions(tlv, *net, requests, requestsSize);

    // Rapid commit
    if (isRC)
        dhcp::appendTLV(tlv, DHCP_OPTION_RAPID_COMMIT, 0, nullptr);

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(DHCP_OPTION_OVERLOAD, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp, client);

    // End
    tlv.tlv.append(DHCP_OPTION_END, 0, nullptr, 0);
    builder.addTLVSize(tlv.tlv.size());

    // Add to snooping table
    if (!configs.snooping.allowUntrusted.load(std::memory_order_relaxed))
    {
        dhcp::SnoopingEntry entry;
        std::memcpy(entry.mac, chaddr, 6);
        entry.ip = ip;
        entry.interface = iface.configs.key.getId();
        entry.expiration = std::chrono::steady_clock::now() + std::chrono::seconds(net->configs.leaseTime);

        std::lock_guard<std::mutex> lock(serverMutex);
        snoopingTable.emplace(utils::read<uint64_t, 6>(entry.mac), entry);
    }

    interface::Interface* currentInterface = &iface;
    infrastructure::ippacket::BuildIP ipBuild = {
        .iface = currentInterface,
        .packetInfo = builder,
        .destIp = types::IPAddress(destination, types::AddressFamily::IPv4),
        .destMac = utils::read<uint64_t, 6>(chaddr),
        .hopLimit = 64,
        .protocolType = IP_UDP
    };

    transport::udp::buildUdp(
        types::AddressFamily::IPv4,
        ipBuild,
        UDP_DHCP_SERVER,
        UDP_DHCP_CLIENT
    );
}

void DhcpServer::sendNak(
    interface::Interface& iface,
    const uint8_t* transID,
    const ClientID& client,
    const uint8_t* chaddr,
    const uint8_t* giaddr,
    size_t clientMaxSize,
    const packet::TLV8Option* relayInfo
)
{
    processing::PacketBuilder builder(&iface);
    transport::udp::reserveUDP(builder, types::AddressFamily::IPv4);
    builder.reserveHeader(packet::HeaderType::DHCP, packet::DhcpHeader::fixedSize);

    auto* header = builder.nextBuildHeader();
    if (!header) return;

    packet::DhcpHeader dhcp;
    dhcp.setBuffer(header->buffer);
    dhcp.setOpcode(0x02);
    dhcp.setHType(0x01);
    dhcp.setHLen(6);
    dhcp.setXid(transID);

    std::memset(dhcp.raw->ciaddr, 0, 12);
    dhcp.setRelayAgentIp(giaddr);

    dhcp.setServerName(DHCP_SERVER_HOSTNAME);
    dhcp.setBootFile(DHCP_BOOT_FILE);
    dhcp.setClientMac(chaddr);
    dhcp.setMagicCookie(DHCP_MAGIC_COOKIE);

    size_t mtuLimit = iface.configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxAllowed = std::min(clientMaxSize, mtuLimit);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;

    const std::string* authKey = authManager.getKeyForClient(client);
    dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, authKey);

    // Nak
    uint8_t type = DHCP_TYPE_NAK;
    dhcp::appendTLV(tlv, DHCP_OPTION_TYPE, 1, &type);

    // Server Identifier
    dhcp::appendTLV(tlv, DHCP_OPTION_SERVER_IDENTIFIER, iface.configs.ipv4.getPrimaryAddress().addr);

    // Echo relay option
    if (relayInfo)
        dhcp::appendTLV(tlv, DHCP_OPTION_RELAY_AGENT_INFO, relayInfo->valueSize, relayInfo->value);

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(DHCP_OPTION_OVERLOAD, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp, client);

    // End
    tlv.tlv.append(DHCP_OPTION_END, 0, nullptr, 0);
    builder.addTLVSize(tlv.tlv.size());

    bool hasGiaddr = giaddr && utils::read<uint32_t>(giaddr) != 0;
    types::IPAddress destIP = hasGiaddr ? types::IPAddress(giaddr, types::AddressFamily::IPv4) : types::IPAddress(IPV4_BROADCAST);

    interface::Interface* currentInterface = &iface;
    infrastructure::ippacket::BuildIP ipBuild = {
        .iface = currentInterface,
        .packetInfo = builder,
        .destIp = destIP,
        .destMac = utils::read<uint64_t, 6>(chaddr),
        .hopLimit = 64,
        .protocolType = IP_UDP
    };

    transport::udp::buildUdp(
        types::AddressFamily::IPv4,
        ipBuild,
        UDP_DHCP_SERVER,
        UDP_DHCP_CLIENT
    );
}

void DhcpServer::sendInformReply(
    interface::Interface& iface,
    const uint8_t* transID,
    const ClientID& client,
    const uint8_t* chaddr,
    const uint8_t* giaddr,
    const uint8_t* destination,
    uint8_t* requests,
    size_t requestsSize,
    size_t clientMaxSize,
    const packet::TLV8Option* relayInfo
)
{
    processing::PacketBuilder builder(&iface);
    transport::udp::reserveUDP(builder, types::AddressFamily::IPv4);
    builder.reserveHeader(packet::HeaderType::DHCP, packet::DhcpHeader::fixedSize);

    auto* header = builder.nextBuildHeader();
    if (!header) return;

    packet::DhcpHeader dhcp;
    dhcp.setBuffer(header->buffer);
    dhcp.setOpcode(0x02);
    dhcp.setHType(0x01);
    dhcp.setHLen(6);
    dhcp.setXid(transID);

    std::memset(dhcp.raw->ciaddr, 0, 12);
    dhcp.setRelayAgentIp(giaddr);
    dhcp.setServerName(DHCP_SERVER_HOSTNAME);
    dhcp.setBootFile(DHCP_BOOT_FILE);
    dhcp.setClientMac(chaddr);
    dhcp.setMagicCookie(DHCP_MAGIC_COOKIE);

    size_t mtuLimit = iface.configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxAllowed = std::min(clientMaxSize, mtuLimit);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;

    const std::string* authKey = authManager.getKeyForClient(client);
    dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, authKey);

    // ACK
    uint8_t type = DHCP_TYPE_ACK;
    dhcp::appendTLV(tlv, DHCP_OPTION_TYPE, 1, &type);

    // Server Identifier
    dhcp::appendTLV(tlv, DHCP_OPTION_SERVER_IDENTIFIER, iface.configs.ipv4.getPrimaryAddress().addr);

    dhcp::DhcpNetwork* net = matchingNetwork(iface, dhcp);
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
        dhcp::appendTLV(tlv, DHCP_OPTION_RELAY_AGENT_INFO, relayInfo->valueSize, relayInfo->value);

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(DHCP_OPTION_OVERLOAD, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp, client);

    // End
    tlv.tlv.append(DHCP_OPTION_END, 0, nullptr, 0);
    builder.addTLVSize(tlv.tlv.size());

    interface::Interface* currentInterface = &iface;
    infrastructure::ippacket::BuildIP ipBuild = {
        .iface = currentInterface,
        .packetInfo = builder,
        .destIp = types::IPAddress(destination, types::AddressFamily::IPv4),
        .destMac = utils::read<uint64_t, 6>(chaddr),
        .hopLimit = 64,
        .protocolType = IP_UDP
    };

    transport::udp::buildUdp(
        types::AddressFamily::IPv4,
        ipBuild,
        UDP_DHCP_SERVER,
        UDP_DHCP_CLIENT
    );
}

void DhcpServer::sendForceRenew(
    interface::Interface& iface,
    const ClientID& client,
    const uint8_t* chaddr,
    uint32_t ip,
    const uint8_t* destination,
    size_t clientMaxSize,
    const packet::TLV8Option* relayInfo
)
{
    processing::PacketBuilder builder(&iface);
    transport::udp::reserveUDP(builder, types::AddressFamily::IPv4);
    builder.reserveHeader(packet::HeaderType::DHCP, packet::DhcpHeader::fixedSize);

    auto* header = builder.nextBuildHeader();
    if (!header) return;

    packet::DhcpHeader dhcp;
    dhcp.setBuffer(header->buffer);
    dhcp.setOpcode(0x02);
    dhcp.setHType(0x01);
    dhcp.setHLen(6);

    generateDhcpTransid(dhcp.raw->xId);

    std::memset(dhcp.raw->ciaddr, 0, 16);
    utils::write<uint32_t>(dhcp.raw->yiaddr, ip);

    dhcp.setServerName(DHCP_SERVER_HOSTNAME);
    dhcp.setBootFile(DHCP_BOOT_FILE);
    dhcp.setClientMac(chaddr);
    dhcp.setMagicCookie(DHCP_MAGIC_COOKIE);

    size_t mtuLimit = iface.configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxAllowed = std::min(clientMaxSize, mtuLimit);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;

    const std::string* authKey = authManager.getKeyForClient(client);
    if (!authKey) return; // ForceRenew can only run authenticated.
    dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, true);

    // ForceRenew
    uint8_t type = DHCP_TYPE_FORCE_RENEW;
    dhcp::appendTLV(tlv, DHCP_OPTION_TYPE, 1, &type);

    // Server Identifier
    dhcp::appendTLV(tlv, DHCP_OPTION_SERVER_IDENTIFIER, iface.configs.ipv4.getPrimaryAddress().addr);

    // Echo relay option
    if (relayInfo)
        dhcp::appendTLV(tlv, DHCP_OPTION_RELAY_AGENT_INFO, relayInfo->valueSize, relayInfo->value);

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(DHCP_OPTION_OVERLOAD, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp, client);

    tlv.tlv.append(DHCP_OPTION_END, 0, nullptr, 0);
    builder.addTLVSize(tlv.tlv.size());

    interface::Interface* currentInterface = &iface;
    infrastructure::ippacket::BuildIP ipBuild = {
        .iface = currentInterface,
        .packetInfo = builder,
        .destIp = types::IPAddress(destination, types::AddressFamily::IPv4),
        .destMac = utils::read<uint64_t, 6>(chaddr),
        .hopLimit = 64,
        .protocolType = IP_UDP
    };

    transport::udp::buildUdp(
        types::AddressFamily::IPv4,
        ipBuild,
        UDP_DHCP_SERVER,
        UDP_DHCP_CLIENT
    );
}

void DhcpServer::sendLeaseQueryReply(
    interface::Interface& iface,
    const uint8_t* transID,
    const ClientID& client,
    uint32_t clientIP,
    const uint8_t* chaddr,
    const uint8_t* giaddr,
    const packet::TLV8Option* relayInfo,
    const IPv4LeaseManager::Lease* lease,
    uint8_t prefixLen,
    uint8_t responseType,
    size_t clientMaxSize
)
{
    processing::PacketBuilder builder(&iface);
    
    transport::udp::reserveUDP(builder, types::AddressFamily::IPv4);
    builder.reserveHeader(packet::HeaderType::DHCP, packet::DhcpHeader::fixedSize);

    auto* nextHeader = builder.nextBuildHeader();
    if (!nextHeader) return;

    packet::DhcpHeader dhcp;
    dhcp.setBuffer(nextHeader->buffer);
    dhcp.setOpcode(0x02);
    dhcp.setHType(0x01);
    dhcp.setHLen(6);
    dhcp.setHops(0);
    dhcp.setXid(transID);

    std::memset(dhcp.raw->ciaddr, 0, 16);
    dhcp.setClientMac(chaddr);

    dhcp.setServerName(DHCP_SERVER_HOSTNAME);
    dhcp.setBootFile(DHCP_BOOT_FILE);
    dhcp.setClientMac(chaddr);
    dhcp.setMagicCookie(DHCP_MAGIC_COOKIE);

    if (responseType == DHCP_TYPE_LEASE_ACTIVE ||
        responseType == DHCP_TYPE_LEASE_UNASSIGNED)
    {
        utils::write<uint32_t>(dhcp.raw->ciaddr, clientIP);
    }

    size_t mtuLimit = iface.configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxAllowed = clientMaxSize == 0 ? mtuLimit : std::min(mtuLimit, clientMaxSize);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;

    const std::string* authKey = authManager.getKeyForClient(client);
    dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, authKey);

    dhcp::appendTLV(tlv, DHCP_OPTION_TYPE, 1, &responseType);

    if (relayInfo)
        dhcp::appendTLV(tlv, DHCP_OPTION_RELAY_AGENT_INFO, relayInfo->valueSize, relayInfo->value);

    if (responseType == DHCP_TYPE_LEASE_ACTIVE && lease)
    {
        // Lease time
        dhcp::appendTLV(tlv, DHCP_OPTION_LEASE_TIME, lease->leaseTime);

        // Lease T1
        dhcp::appendTLV(tlv, DHCP_OPTION_RENEWAL_TIME, lease->t1);

        // Lease T2
        dhcp::appendTLV(tlv, DHCP_OPTION_REBINDING_TIME, lease->t2);

        // Mask
        dhcp::appendTLV(tlv, DHCP_OPTION_MASK, types::v4Mask(prefixLen));

        // Router
        dhcp::appendTLV(tlv, DHCP_OPTION_ROUTER, iface.configs.ipv4.getPrimaryAddress().addr);

        // Timestamp
        dhcp::appendTLV(tlv, DHCP_OPTION_TIMESTAMP, secondsSinceEpoch());
    }

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(DHCP_OPTION_OVERLOAD, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp, client);

    // End
    tlv.tlv.append(DHCP_OPTION_END, 0, nullptr, 0);
    builder.addTLVSize(tlv.tlv.size());

    interface::Interface* currentInterface = &iface;
    infrastructure::ippacket::BuildIP ipBuild = {
        .iface = currentInterface,
        .packetInfo = builder,
        .destIp = types::IPAddress(giaddr, types::AddressFamily::IPv4),
        .destMac = utils::read<uint64_t, 6>(chaddr),
        .hopLimit = 64,
        .protocolType = IP_UDP
    };

    transport::udp::buildUdp(
        types::AddressFamily::IPv4,
        ipBuild,
        UDP_DHCP_SERVER,
        UDP_DHCP_CLIENT
    );
}

void DhcpServer::processDiscover(
    const packet::DhcpHeader& dhcp,
    std::vector<packet::TLV8Option>& options,
    ClientID& client,
    interface::Interface& iface,
    size_t clientMaxSize,
    const packet::TLV8Option* relayInfo
)
{
    if (dhcp.raw->hLen != 6)
        return;
    
    dhcp::DhcpNetwork* net = matchingNetwork(iface, dhcp);
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
            case DHCP_OPTION_TYPE:
                if (opt.valueSize == 1)
                    msgType = opt.value[0];
                break;
            case DHCP_OPTION_REQUEST_LIST:
                requestOptionSize = opt.valueSize;
                requestOptions = opt.value;
                break;
            case DHCP_OPTION_RAPID_COMMIT:
                if (opt.valueSize == 0)
                    rapidCommit = configs.rapidCommit.load(std::memory_order_relaxed);
                break;
            case DHCP_OPTION_REQUEST_IP:
                requestedIp = utils::read<uint32_t>(opt.value);
            default:
                break;
        }
    }

    if (msgType != DHCP_TYPE_DISCOVER)
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

    static const uint8_t BROADCAST_BYTES[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t* destination = nullptr;
    bool hasRelay = utils::read<uint32_t>(dhcp.getRelayAgentIP()) != 0;

    if (hasRelay)
        destination = dhcp.getRelayAgentIP();
    else if (broadcast || dhcp.getClientIP() == nullptr)
        destination = BROADCAST_BYTES;
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

void DhcpServer::processRequest(
    const packet::DhcpHeader& dhcp,
    std::vector<packet::TLV8Option>& options,
    ClientID& client,
    interface::Interface& iface,
    size_t clientMaxSize,
    const packet::TLV8Option* relayInfo
)
{
    if (dhcp.raw->hLen != 6)
        return;

    dhcp::DhcpNetwork* net = matchingNetwork(iface, dhcp);
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
            case DHCP_OPTION_TYPE:
                if (opt.valueSize == 1)
                    msgType = opt.value[0];
                break;
            case DHCP_OPTION_REQUEST_LIST:
                requestOptionSize = opt.valueSize;
                requestOptions = opt.value;
                break;
            case DHCP_OPTION_REQUEST_IP:
                if (opt.valueSize == 4)
                    requestIP = opt.value;
                break;
            default:
                break;
        }
    }
    
    if (msgType != DHCP_TYPE_REQUEST)
        return;

    static const uint8_t BROADCAST_BYTES[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t* destination = nullptr;
    bool hasRelay = utils::read<uint32_t>(dhcp.getRelayAgentIP()) != 0;

    if (hasRelay)
        destination = dhcp.getRelayAgentIP();
    else if (broadcast || dhcp.getClientIPInt() == 0)
        destination = BROADCAST_BYTES;
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
        uint32_t reqIp = utils::read<uint32_t>(requestIP);
        
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

void DhcpServer::processDecline(
    const packet::DhcpHeader& dhcp,
    std::vector<packet::TLV8Option>& options,
    ClientID& client,
    interface::Interface& iface
)
{
    if (dhcp.raw->hLen != 6)
        return;
    
    dhcp::DhcpNetwork* net = matchingNetwork(iface, dhcp);
    if (!net)
        return;

    for (const auto& opt : options)
    {
        if (opt.type == DHCP_OPTION_CLIENT_ID)
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

void DhcpServer::processRelease(
    const packet::DhcpHeader& dhcp,
    std::vector<packet::TLV8Option>& options,
    ClientID& client,
    interface::Interface& iface
)
{
    if (dhcp.raw->hLen != 6)
        return;

    dhcp::DhcpNetwork* net = matchingNetwork(iface, dhcp);
    if (!net)
        return;

    for (const auto& opt : options)
    {
        if (opt.type == DHCP_OPTION_CLIENT_ID)
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

void DhcpServer::processInform(
    const packet::DhcpHeader& dhcp,
    std::vector<packet::TLV8Option>& options,
    ClientID& client,
    interface::Interface& iface,
    size_t clientMaxSize,
    const packet::TLV8Option* relayInfo
)
{
    if (dhcp.raw->hLen != 6)
        return;

    dhcp::DhcpNetwork* net = matchingNetwork(iface, dhcp);
    if (!net)
        return;

    uint8_t* requestOptions = nullptr;
    size_t requestOptionSize = 0;

    for (const auto& opt : options)
    {
        switch (opt.type)
        {
            case DHCP_OPTION_REQUEST_LIST:
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

    static const uint8_t BROADCAST_BYTES[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t* destination = nullptr;
    bool hasRelay = utils::read<uint32_t>(dhcp.getRelayAgentIP()) != 0;

    if (hasRelay)
        destination = dhcp.getRelayAgentIP();
    else if (dhcp.getClientIP() == nullptr)
        destination = BROADCAST_BYTES;
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

void DhcpServer::processLeaseQuery(
    const packet::DhcpHeader& dhcp,
    std::vector<packet::TLV8Option>& options,
    ClientID& client,
    interface::Interface& iface,
    size_t clientMaxSize,
    const packet::TLV8Option* relayInfo
)
{
    if (dhcp.raw->hLen != 6)
        return;

    dhcp::DhcpNetwork* net = matchingNetwork(iface, dhcp);
    if (!net) return;

    uint32_t queryIP = 0;

    for (const auto& opt : options)
    {
        switch (opt.type)
        {
            case DHCP_OPTION_REQUEST_IP:
                if (opt.valueSize == 4)
                    queryIP = utils::read<uint32_t>(opt.value);
                break;
            default:
                break;
        }
    }

    std::optional<IPv4LeaseManager::Lease> lease = net->leaseManager->getLease(client, queryIP);
    
    uint8_t responseType = DHCP_TYPE_LEASE_UNKNOWN;

    if (lease.has_value() && lease.value().expiry > std::chrono::steady_clock::now())
    {
        responseType = DHCP_TYPE_LEASE_ACTIVE;
    }
    else if (queryIP && net->pool->withinRange(queryIP))
    {
        responseType = DHCP_TYPE_LEASE_UNASSIGNED;
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

void DhcpServer::appendDnsServers(dhcp::DhcpTLVManager& tlv, const dhcp::DhcpNetworkConfig& network)
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
            utils::write<uint32_t>(buffer + offset, ip);
            offset += 4;
        }
        else break;
    }

    dhcp::appendTLV(tlv, DHCP_OPTION_DOMAIN_SERVER, offset, buffer);
}

void DhcpServer::appendNtpServers(dhcp::DhcpTLVManager& tlv, const dhcp::DhcpNetworkConfig& network)
{
    if (network.ntpServers.size() == 0) return;
    uint8_t buffer[255];

    size_t offset = 0;
    for (const uint32_t& ip : network.ntpServers)
    {
        if (offset < 52)
        {
            utils::write<uint32_t>(buffer + offset, ip);
            offset += 4;
        }
        else break;
    }

    dhcp::appendTLV(tlv, DHCP_OPTION_NTP, offset, buffer);
}

void DhcpServer::appendAuthOptions(packet::TLV8BufferManager& tlv, const packet::DhcpHeader& dhcp, const ClientID& clientID)
{
    const std::string* key = authManager.getKeyForClient(clientID);
    if (!key) return;

    uint8_t* authOpt = tlv.getNextValBuf(27);
    if (!authOpt) return;
    tlv.append(DHCP_OPTION_AUTHENTICATION, 27, nullptr, 27);

    authOpt[0] = 1;
    authOpt[1] = 1;
    authOpt[2] = 0;

    uint64_t counter = authManager.getReplayCounter(clientID) + 1;
    utils::write<uint64_t>(authOpt + 3, counter);

    // Zero the digest field
    std::memset(authOpt + 11, 0, 16);

    // Build HMAC input over full packet assuming end option is not yet added
    security::authentication::generateHMAC(
        authOpt + 11,
        dhcp.buffer,
        packet::DhcpHeader::fixedSize + tlv.size(),
        reinterpret_cast<const uint8_t*>(key->data()),
        key->size(),
        security::authentication::HmacType::MD5
    );

    authManager.getReplayCounter(clientID) = counter;
}

bool DhcpServer::validateAuthentication(const packet::DhcpHeader& dhcp, const ClientID& clientID, const uint8_t* data)
{
    if (!data) return !authManager.shouldEnforce(clientID);

    // Check fields
    if (data[0] != 1 || data[1] != 1 || data[2] != 0)
        return false;

    // Extract replay counter
    uint64_t counter = utils::read<uint64_t>(data + 3);
    if (counter <= authManager.getReplayCounter(clientID))
        return false;

    const std::string* key = authManager.getKeyForClient(clientID);
    if (!key) return false;

    // Copy out hash
    uint8_t receivedHash[16];
    std::memcpy(receivedHash, data + 11, 16);
    std::memset(const_cast<uint8_t*>(data + 11), 0, 16); // Set hash to 0s
    
    uint8_t computedHash[16];
    security::authentication::generateHMAC(
        computedHash,
        dhcp.buffer,
        packet::DhcpHeader::fixedSize + dhcp.getTrail().size() - 2,
        reinterpret_cast<const uint8_t*>(key->data()),
        key->size(),
        security::authentication::HmacType::MD5
    );

    if (std::memcmp(computedHash, receivedHash, 16) != 0)
        return false;

    authManager.getReplayCounter(clientID) = counter;
    return true;
}

void DhcpServer::appendVendorOptions(dhcp::DhcpTLVManager& tlv, const ClientID& client, const interface::Interface& iface, const dhcp::DhcpNetworkConfig& configs)
{
    if (!configs.vendorClassID.empty())
        dhcp::appendTLV(tlv, DHCP_OPTION_VENDOR_CLASS_ID, configs.vendorClassID.size(), reinterpret_cast<const uint8_t*>(configs.vendorClassID.data()));

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
            utils::write<uint32_t>(buffer + offset + i * 4, configs.pxeBootServers[i]);
        offset += count * 4;
    }

    if (offset > 0)
        dhcp::appendTLV(tlv, DHCP_OPTION_VENDOR_SPECIFIC, offset, buffer);
}

void DhcpServer::appendBootOptions(dhcp::DhcpTLVManager& tlv, const dhcp::DhcpNetworkConfig& configs)
{
    // Option 66
    if (!configs.tftpServerName.empty())
    {
        const auto* data = reinterpret_cast<const uint8_t*>(configs.tftpServerName.data());
        dhcp::appendTLV(tlv, DHCP_OPTION_TFTP_SERVER_NAME, configs.tftpServerName.size(), data);
    }

    // Option 67
    if (!configs.bootFileName.empty())
    {
        const auto* data = reinterpret_cast<const uint8_t*>(configs.bootFileName.data());
        dhcp::appendTLV(tlv, DHCP_OPTION_BOOT_FILE, configs.bootFileName.size(), data);
    }

    // Option 150
    if (!configs.tftpServers.empty())
    {
        size_t count = std::min(configs.tftpServers.size(), static_cast<size_t>((255 / 4)));
        uint8_t ipData[255];
        for (size_t i = 0; i < count; ++i)
            utils::write<uint32_t>(ipData + i * 4, configs.tftpServers[i]);
        dhcp::appendTLV(tlv, DHCP_OPTION_TFTP_SERVERS, count * 4, ipData);
    }
}

DhcpNetwork* DhcpServer::matchingNetwork(const interface::Interface& iface, const packet::DhcpHeader& dhcp) const
{
    auto findMatchingNetworkAgainstIP([&](types::IPv4Address ip) -> dhcp::DhcpNetwork* {
        for (auto& [_, config] : networks)
        {
            auto network = config->configs.getNetworkID();
            if (config->pool->init.load(std::memory_order_relaxed) && network.contains(ip))
            {
                return config;
            }
        }
        return nullptr;
    });

    // Match based on the relay agent IP (if present)
    auto relayMatch = findMatchingNetworkAgainstIP(types::IPv4Address(dhcp.getRelayAgentIP()));
    if (relayMatch)
    {
        return relayMatch;
    }

    // Fall back to client IP matching
    auto clientMatch = findMatchingNetworkAgainstIP(types::IPv4Address(dhcp.getClientIP()));
    if (clientMatch)
    {
        return clientMatch;
    }

    return nullptr; // No match found
}

DhcpNetwork* DhcpServer::addPool(const std::string& poolName)
{
    std::lock_guard<std::mutex> lock(serverMutex);
    // Create a new pool and lease for the dhcp network.
    networks.emplace(poolName, new dhcp::DhcpNetwork(timeManager, configs));
}

void DhcpServer::removePool(std::string& poolName)
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

void DhcpServer::addRequestedOptions(dhcp::DhcpTLVManager& tlv, dhcp::DhcpNetwork& network, const uint8_t* requestList, size_t requestsSize)
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
            case DHCP_OPTION_MASK:
                dhcp::appendTLV(tlv, opt, network.configs.getSubnetMask());
                break;
            case DHCP_OPTION_BROADCAST:
                dhcp::appendTLV(tlv, opt, network.pool->broadcast.load(std::memory_order_relaxed));
                break;
            case DHCP_OPTION_ROUTER:
                dhcp::appendTLV(tlv, opt, network.configs.getGateway());
                break;
            case DHCP_OPTION_DOMAIN_NAME:
                if (network.configs.domainName.size() > 0)
                    dhcp::appendTLV(tlv, opt, network.configs.domainName.size(), reinterpret_cast<const uint8_t*>(network.configs.domainName.data()));
                break;
            case DHCP_OPTION_MTU:
                if (network.configs.mtu.load(std::memory_order_relaxed) != 0)
                {
                    utils::write<uint16_t>(buffer, network.configs.mtu.load(std::memory_order_relaxed));
                    dhcp::appendTLV(tlv, opt, 2, buffer);
                }
                break;
            case DHCP_OPTION_NTP:
                appendNtpServers(tlv, network.configs);
                break;
            case DHCP_OPTION_HOSTNAME:
                {
                    std::string hostname = global.getHostname();
                    dhcp::appendTLV(tlv, opt, hostname.size(), reinterpret_cast<uint8_t*>(hostname.data()));
                }
                break;
            case DHCP_OPTION_LEASE_TIME:
                dhcp::appendTLV(tlv, opt, lease);
                break;
            case DHCP_OPTION_RENEWAL_TIME:
                dhcp::appendTLV(tlv, opt, (network.configs.t1Percentage.load(std::memory_order_relaxed) * lease) / 100);
                break;
            case DHCP_OPTION_REBINDING_TIME:
                dhcp::appendTLV(tlv, opt, (network.configs.t2Percentage.load(std::memory_order_relaxed) * lease) / 100);
                break;
            default:
                break;
        }
    }
}

void DhcpServer::appendDomainSearchList(dhcp::DhcpTLVManager& tlv, const std::vector<std::string>& domains)
{
    
}

bool DhcpNetworkConfig::updateNetwork(types::IPPrefix& prefix, uint32_t& gateway)
{
    return true;
}

types::IPPrefix DhcpNetworkConfig::getNetworkID() const
{
    return types::IPPrefix();
}

uint8_t* DhcpNetworkConfig::getNetwork(uint8_t* out) const
{
    return nullptr;
}

uint32_t DhcpNetworkConfig::getNetwork() const
{
    return 0;
}

uint8_t* DhcpNetworkConfig::getGateway(uint8_t* out) const
{
    return nullptr;
}

uint32_t DhcpNetworkConfig::getGateway() const
{
    return 0;
}

uint8_t DhcpNetworkConfig::getPrefixLen() const
{
    return 0;
}

uint8_t* DhcpNetworkConfig::getSubnetMask(uint8_t* out)
{
    return nullptr;
}

uint32_t DhcpNetworkConfig::getSubnetMask()
{
    return 0;
}

} // namespace services
