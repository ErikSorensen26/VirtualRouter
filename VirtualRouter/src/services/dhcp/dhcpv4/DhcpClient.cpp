// DhcpClient.cpp

#include <Global.h>
#include <VirtualRouter.h>

#include "DhcpClient.h"
#include "interface/Interface.h"
#include "infrastructure/IPPacket.h"
#include "udp/Udp.h"
#include "processing/PacketBuilder.hpp"
#include "DhcpTlvManager.hpp"
#include "dhcp/DhcpInfo.hpp"

#include "security/Encryption.hpp"

Protocol::DhcpClient::DhcpClient(Interface* iface, bool reduced) // Reduced is used for testing
    : currentInterface(iface), stopFlag(true)
{
    if (!reduced)
    {
        if (!iface->shutdownFlag.load(std::memory_order_relaxed))
        {
            initiate();
        }
    }
}

void Protocol::DhcpClient::initiate()
{
    uint8_t mac[6];
    currentInterface->configs.getMac(mac);

    sendDhcpDiscover(currentInterface->getVRF()->getGlobal().getHostname(), mac);
}

Protocol::DhcpClient::~DhcpClient()
{
    shutdown();
}

void Protocol::DhcpClient::shutdown()
{
    sendDhcpRelease();

    stopFlag.store(true, std::memory_order_release);
    offered.store(false);
    acked.store(false);

    Global& global = currentInterface->getVRF()->getGlobal();
    if (discoveryRetryTimerId) global.timeManager.cancelTimer(discoveryRetryTimerId);
    if (requestRetryTimerId) global.timeManager.cancelTimer(requestRetryTimerId);

    discoveryRetryTimerId = 0;
    requestRetryTimerId = 0;

    cancelLeaseTimers();
}

void Protocol::DhcpClient::handleDhcpPacket(const DhcpHeader& dhcp)
{
    const auto trail = dhcp.getTrail();
    std::vector<TLV8Option> options;
    if (!parseDhcpOptions(trail.data(), trail.size() - 1/*end option*/, options))
        return;

    uint8_t op = getOpcode(options);
    switch (op)
    {
        case DHCP_TYPE_OFFER:
            processDhcpOffer(dhcp, options);
            break;
        case DHCP_TYPE_ACK:
            (!acked.load(std::memory_order_relaxed) && offered.load(std::memory_order_relaxed))
                ? processDhcpAck(dhcp, options)
                : processDhcpInformAck(dhcp, options);
            break;
        case DHCP_TYPE_NAK:
            processDhcpNak(dhcp, options);
            break;
        case DHCP_TYPE_DECLINE:
            processDhcpDecline(dhcp, options);
            break;
        default:
            break;
    }
}

bool Protocol::DhcpClient::buildDhcpDiscover(PacketBuilder& builder, const uint8_t* mac, const std::string& hostname)
{
    // Reserve space (DHCP fixed header + rough TLV estimate)
    UDP::reserveUDP(builder, AddressFamily::IPv4);
    builder.reserveHeader(HeaderType::DHCP, DhcpHeader::fixedSize);

    auto* nextHeader = builder.nextBuildHeader();
    if (!nextHeader) return false;

    // Set Buffer and start constructing DHCP header
    DhcpHeader dhcp;
    dhcp.setBuffer(nextHeader->buffer);
    dhcp.setOpcode(0x01);
    dhcp.setHType(0x01); // Ethernet (only supported as of now)
    dhcp.setHLen(6);
    dhcp.setHops(0);

    generateDhcpTransid(dhcp.raw->xId);
    dhcp.setSecs(0);

    // Set flags (broadcast bit set)
    dhcp.raw->flags[0] = 0x80;
    dhcp.raw->flags[1] = 0x00;

    // Zero all addresses
    std::memset(dhcp.raw->ciaddr, 0, 16);

    dhcp.setClientMac(mac);
    dhcp.setServerName(DHCP_SERVER_HOSTNAME);
    dhcp.setBootFile(DHCP_BOOT_FILE);
    dhcp.setMagicCookie(DHCP_MAGIC_COOKIE);

    size_t mtuLimit = currentInterface->configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxClientSize = configs.maxSize.load(std::memory_order_relaxed);
    size_t maxAllowed = maxClientSize == 0 ? mtuLimit : std::min(mtuLimit, maxClientSize);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;

    // Add TLV options directly into trailing span
    const std::string* authKey = configs.getAuthKey();
    Dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, authKey);

    uint8_t type = DHCP_TYPE_DISCOVER;
    Dhcp::appendTLV(tlv, DHCP_OPTION_TYPE, 1, &type);
    if (configs.clientID.size > 0)
        Dhcp::appendTLV(tlv, DHCPV6_OPTION_CLIENT_ID, configs.clientID.size, configs.clientID.data);
    uint8_t msgSize[2];
    writeU16(msgSize, configs.maxSize.load(std::memory_order_relaxed));
    Dhcp::appendTLV(tlv, DHCP_OPTION_MAX_SIZE, 2, msgSize);
    Dhcp::appendTLV(tlv, DHCP_OPTION_HOSTNAME, hostname.size(), reinterpret_cast<const uint8_t*>(hostname.data()));
    const uint8_t requestList[] = {
        DHCP_OPTION_MASK,
        DHCP_OPTION_ROUTER,
        DHCP_OPTION_DOMAIN_SERVER,
        DHCP_OPTION_HOSTNAME
    };
    Dhcp::appendTLV(tlv, DHCP_OPTION_REQUEST_LIST, sizeof(requestList), requestList);

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(DHCP_OPTION_OVERLOAD, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp);

    tlv.tlv.append(DHCP_OPTION_END, 0, nullptr, 0);
    builder.addTLVSize(tlv.tlv.size());
    
    return true;
}

bool Protocol::DhcpClient::buildDhcpRequest(PacketBuilder& builder, uint32_t transID, const std::string& hostname,
                                            uint32_t requestedIP, uint32_t serverID)
{
    // Reserve space (DHCP fixed header + rough TLV estimate)
    UDP::reserveUDP(builder, AddressFamily::IPv4);
    builder.reserveHeader(HeaderType::DHCP, DhcpHeader::fixedSize);

    auto* nextHeader = builder.nextBuildHeader();
    if (!nextHeader) return false;

    // Set Buffer and start constructing DHCP header
    DhcpHeader dhcp;
    dhcp.setBuffer(nextHeader->buffer);
    dhcp.setOpcode(0x01);
    dhcp.setHType(1);
    dhcp.setHLen(6);
    dhcp.setHops(0);

    dhcp.setXid(transID);
    dhcp.setSecs(0);

    // Set flags (broadcast bit set)
    dhcp.raw->flags[0] = 0x80;
    dhcp.raw->flags[1] = 0x00;

    // Zero all addresses
    std::memset(dhcp.raw->ciaddr, 0, 16);

    currentInterface->configs.getMac(dhcp.raw->chaddr);
    dhcp.setServerName(DHCP_SERVER_HOSTNAME);
    dhcp.setBootFile(DHCP_BOOT_FILE);
    dhcp.setMagicCookie(DHCP_MAGIC_COOKIE);

    size_t mtuLimit = currentInterface->configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxClientSize = configs.maxSize.load(std::memory_order_relaxed);
    size_t maxAllowed = maxClientSize == 0 ? mtuLimit : std::min(mtuLimit, maxClientSize);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;

    // Add TLV options directly into trailing span
    const std::string* authKey = configs.getAuthKey();
    Dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, authKey);

    uint8_t type = DHCP_TYPE_REQUEST;
    Dhcp::appendTLV(tlv, DHCP_OPTION_TYPE, 1, &type);
    if (configs.clientID.size > 0)
        Dhcp::appendTLV(tlv, DHCP_OPTION_CLIENT_ID, configs.clientID.size, configs.clientID.data);
    Dhcp::appendTLV(tlv, DHCP_OPTION_SERVER_IDENTIFIER, serverID);
    Dhcp::appendTLV(tlv, DHCP_OPTION_REQUEST_IP, requestedIP);

    uint32_t leaseTime = configs.leaseTime.load(std::memory_order_relaxed);
    if (configs.leaseTime.load(std::memory_order_relaxed) != 0)
    {
        Dhcp::appendTLV(tlv, DHCP_OPTION_LEASE_TIME, leaseTime);
        uint32_t renewTime = configs.renewalTime.load(std::memory_order_relaxed);
        uint32_t rebindTime = configs.rebindingTime.load(std::memory_order_relaxed);
        if (renewTime != 0)
            Dhcp::appendTLV(tlv, DHCP_OPTION_RENEWAL_TIME,renewTime);
        if (rebindTime != 0)
            Dhcp::appendTLV(tlv, DHCP_OPTION_REBINDING_TIME, rebindTime);
    }

    Dhcp::appendTLV(tlv, DHCP_OPTION_HOSTNAME, hostname.size(), reinterpret_cast<const uint8_t*>(hostname.data()));

    const uint8_t reqList[] = {
        DHCP_OPTION_MASK,
        DHCP_OPTION_ROUTER,
        DHCP_OPTION_DOMAIN_SERVER,
        DHCP_OPTION_DOMAIN_SEARCH,
        DHCP_OPTION_HOSTNAME,
        DHCP_OPTION_NETBIOS_SERVER,
        DHCP_OPTION_MTU,
        DHCP_OPTION_CLASSLESS_STATIC_ROUTE,
        DHCP_OPTION_NTP
    };
    Dhcp::appendTLV(tlv, DHCP_OPTION_REQUEST_LIST, sizeof(reqList), reqList);

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(DHCP_OPTION_OVERLOAD, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp);

    tlv.tlv.append(DHCP_OPTION_END, 0, nullptr, 0);
    builder.addTLVSize(tlv.tlv.size());
    
    return true;
}

bool Protocol::DhcpClient::buildDhcpRelease(PacketBuilder& builder)
{
    UDP::reserveUDP(builder, AddressFamily::IPv4);
    builder.reserveHeader(HeaderType::DHCP, DhcpHeader::fixedSize);

    auto* nextHeader = builder.nextBuildHeader();
    if (!nextHeader) return false;

    DhcpHeader dhcp;
    dhcp.setBuffer(nextHeader->buffer);
    dhcp.setOpcode(0x01);
    dhcp.setHType(0x01);
    dhcp.setHLen(6);
    dhcp.setHops(0);

    // New transaction ID or release
    generateDhcpTransid(dhcp.raw->xId);
    dhcp.setSecs(0);

    // No flags
    std::memset(dhcp.raw->flags, 0, 2);

    // Set ciaddr to current leased IP
    currentInterface->configs.ipv4.getPrimaryAddress(dhcp.raw->ciaddr);
    std::memset(dhcp.raw->yiaddr, 0, 12);

    currentInterface->configs.getMac(dhcp.raw->chaddr);
    dhcp.setServerName(DHCP_SERVER_HOSTNAME);
    dhcp.setBootFile(DHCP_BOOT_FILE);
    dhcp.setMagicCookie(DHCP_MAGIC_COOKIE);

    size_t mtuLimit = currentInterface->configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxClientSize = configs.maxSize.load(std::memory_order_relaxed);
    size_t maxAllowed = maxClientSize == 0 ? mtuLimit : std::min(mtuLimit, maxClientSize);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;

    // Add TLV options directly into trailing span
    const std::string* authKey = configs.getAuthKey();
    Dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, authKey);

    uint8_t type = DHCP_TYPE_RELEASE;
    Dhcp::appendTLV(tlv, DHCP_OPTION_TYPE, 1, &type);
    if (configs.clientID.size > 0)
        Dhcp::appendTLV(tlv, DHCP_OPTION_CLIENT_ID, configs.clientID.size, configs.clientID.data);
    if (configs.serverID.v4 != 0) 
        Dhcp::appendTLV(tlv, DHCP_OPTION_SERVER_IDENTIFIER, 4, configs.serverID.raw);

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(DHCP_OPTION_OVERLOAD, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp);

    tlv.tlv.append(DHCP_OPTION_END, 0, nullptr, 0);
    builder.addTLVSize(tlv.tlv.size());
    
    return true;
}

bool Protocol::DhcpClient::buildDhcpInform(PacketBuilder& builder, const std::string& hostname, const uint8_t* mac)
{
    UDP::reserveUDP(builder, AddressFamily::IPv4);
    builder.reserveHeader(HeaderType::DHCP, DhcpHeader::fixedSize);

    auto* nextHeader = builder.nextBuildHeader();
    if (!nextHeader) return false;

    DhcpHeader dhcp;
    dhcp.setBuffer(nextHeader->buffer);
    dhcp.setOpcode(0x01);
    dhcp.setHType(0x01);
    dhcp.setHLen(6);
    dhcp.setHops(0);

    generateDhcpTransid(dhcp.raw->xId);
    dhcp.setSecs(0);

    // Flags: none
    std::memset(dhcp.raw->flags, 0, 2);

    // Set ciaddr to client IP
    currentInterface->configs.ipv4.getPrimaryAddress(dhcp.raw->ciaddr);
    std::memset(dhcp.raw->yiaddr, 0, 12);

    dhcp.setClientMac(mac);
    dhcp.setServerName(DHCP_SERVER_HOSTNAME);
    dhcp.setBootFile(DHCP_BOOT_FILE);
    dhcp.setMagicCookie(DHCP_MAGIC_COOKIE);

    size_t mtuLimit = currentInterface->configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxClientSize = configs.maxSize.load(std::memory_order_relaxed);
    size_t maxAllowed = maxClientSize == 0 ? mtuLimit : std::min(mtuLimit, maxClientSize);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;

    // Add TLV options directly into trailing span
    const std::string* authKey = configs.getAuthKey();
    Dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, authKey);

    uint8_t type = DHCP_TYPE_INFORM;
    Dhcp::appendTLV(tlv, DHCP_OPTION_TYPE, 1, &type);
    if (configs.clientID.size > 0)
        Dhcp::appendTLV(tlv, DHCPV6_OPTION_CLIENT_ID, configs.clientID.size, configs.clientID.data);
    Dhcp::appendTLV(tlv, DHCP_OPTION_HOSTNAME, hostname.size(), reinterpret_cast<const uint8_t*>(hostname.data()));
    
    const uint8_t reqList[] = {
        DHCP_OPTION_DOMAIN_SERVER,
        DHCP_OPTION_DOMAIN_SEARCH,
        DHCP_OPTION_NTP,
        DHCP_OPTION_ROUTER
    };
    Dhcp::appendTLV(tlv, DHCP_OPTION_REQUEST_LIST, sizeof(reqList), reqList);

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(DHCP_OPTION_OVERLOAD, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp);

    tlv.tlv.append(DHCP_OPTION_END, 0, nullptr, 0);
    builder.addTLVSize(tlv.tlv.size());

    return true;
}

void Protocol::DhcpClient::sendDhcpDiscover(const std::string& hostname, const uint8_t* mac)
{
    PacketBuilder builder(currentInterface);
    if (!buildDhcpDiscover(builder, mac, hostname))
        return;

    IPPacket::BuildIP ipBuild = {
        .iface = currentInterface,
        .packetInfo = builder,
        .destIp = IPV4_BROADCAST,
        .sourceIp = IPV4_SOURCE,
        .hopLimit = 64,
        .protocolType = IP_UDP
    };

    UDP::buildUdp(
        AddressFamily::IPv4,
        ipBuild,
        UDP_DHCP_CLIENT,
        UDP_DHCP_SERVER
    );

    // Retry scheduling
    uint32_t timerId = currentInterface->getVRF()->getGlobal().timeManager.addTimer(
        std::chrono::steady_clock::now() + std::chrono::seconds(4),
        [this, hostname](uint32_t) {
            if (!offered.load(std::memory_order_relaxed) && !stopFlag.load(std::memory_order_relaxed)) {
                uint8_t mac[6];
                currentInterface->configs.getMac(mac);
                sendDhcpDiscover(hostname, mac);
            }
        }
    );

    discoveryRetryTimerId.store(timerId, std::memory_order_release);
}

void Protocol::DhcpClient::sendDhcpRequest(uint32_t transID, const std::string& hostname, uint32_t requestedIp, uint32_t serverId)
{
    PacketBuilder builder(currentInterface);
    if (!buildDhcpRequest(builder, transID, hostname, requestedIp, serverId))
        return;

    {
        IPPacket::BuildIP ipBuild = {
            .iface = currentInterface,
            .packetInfo = builder,
            .destIp = IPV4_BROADCAST,
            .sourceIp = IPV4_SOURCE,
            .hopLimit = 64,
            .protocolType = IP_UDP
        };

        UDP::buildUdp(
            AddressFamily::IPv4,
            ipBuild,
            UDP_DHCP_CLIENT,
            UDP_DHCP_SERVER
        );
    }

    // Retry
    uint32_t timerId = currentInterface->getVRF()->getGlobal().timeManager.addTimer(
        std::chrono::steady_clock::now() + std::chrono::seconds(4),
        [this, transID, hostname, requestedIp, serverId](uint32_t) {
            if (!acked.load(std::memory_order_relaxed) && !stopFlag.load(std::memory_order_relaxed)) {
                sendDhcpRequest(transID, hostname, requestedIp, serverId);
            }
        }
    );

    requestRetryTimerId.store(timerId, std::memory_order_relaxed);
}

void Protocol::DhcpClient::sendDhcpRelease()
{
    if (!acked.load(std::memory_order_acquire)) return;

    PacketBuilder builder(currentInterface);

    if (!buildDhcpRelease(builder)) return;

    uint8_t ip[4];
    currentInterface->configs.ipv4.getPrimaryAddress(ip);

    IPPacket::BuildIP ipBuild = {
        .iface = currentInterface,
        .packetInfo = builder,
        .destIp = configs.serverID.raw,
        .sourceIp = ip,
        .hopLimit = 64,
        .protocolType = IP_UDP
    };

    UDP::buildUdp(
        AddressFamily::IPv4,
        ipBuild,
        UDP_DHCP_CLIENT,
        UDP_DHCP_SERVER
    );
}

bool Protocol::DhcpClient::processDhcpOffer(const DhcpHeader& dhcp, std::vector<TLV8Option>& options)
{
    if (dhcp.getMagicCookie() != DHCP_MAGIC_COOKIE) return false;

    uint8_t msgType = 0;
    const uint8_t* serverIdPtr = nullptr;
    const uint8_t* auth = nullptr;

    for (const auto& opt : options)
    {
        switch (opt.type)
        {
            case DHCP_OPTION_TYPE:
                if (opt.length == 1)
                    msgType = opt.value[0];
                break;
            case DHCP_OPTION_SERVER_IDENTIFIER:
                if (opt.length == 4)
                    serverIdPtr = opt.value;
                break;
            case DHCP_OPTION_AUTHENTICATION:
                if (opt.length >= 20)
                    auth = opt.value;
                break;
            default:
                break;
        }
    }

    if (auth && !validateAuthentication(dhcp, auth))
        return false;

    if (msgType != DHCP_TYPE_OFFER || !serverIdPtr)
        return false;

    uint32_t requestedAddress = readU32(dhcp.raw->yiaddr);
    uint32_t dhcpServerId = readU32(serverIdPtr);
    uint32_t transId = readU32(dhcp.raw->xId);

    offered.store(true, std::memory_order_release);
    Global& global = currentInterface->getVRF()->getGlobal();
    global.timeManager.cancelTimer(discoveryRetryTimerId.load(std::memory_order_relaxed));

    sendDhcpRequest(
        transId,
        global.getHostname(),
        requestedAddress,
        dhcpServerId
    );

    return true;
}

bool Protocol::DhcpClient::processDhcpAck(const DhcpHeader& dhcp, std::vector<TLV8Option>& options)
{
    if (dhcp.getMagicCookie() != DHCP_MAGIC_COOKIE) return false;

    bool type = false;
    uint32_t serverId = 0, leaseTime = 0, subnetMask = 0, gateway = 0;
    uint32_t t1 = 0, t2 = 0;
    const uint8_t* auth = nullptr;

    for (const auto& opt : options)
    {
        switch(opt.type)
        {
            case DHCP_OPTION_TYPE:
                if (opt.length == 1 && opt.value[0] == DHCP_TYPE_ACK)
                    type = true;
                break;
            case DHCP_OPTION_SERVER_IDENTIFIER:
                if (opt.length == 4)
                    serverId = readU32(opt.value);
                break;
            case DHCP_OPTION_LEASE_TIME:
                if (opt.length == 4)
                    leaseTime = readU32(opt.value);
                break;
            case DHCP_OPTION_RENEWAL_TIME:
                if (opt.length == 4)
                    t1 = readU32(opt.value);
                break;
            case DHCP_OPTION_REBINDING_TIME:
                if (opt.length == 4)
                    t2 = readU32(opt.value);
                break;
            case DHCP_OPTION_MASK:
                if (opt.length == 4)
                    subnetMask = readU32(opt.value);
                break;
            case DHCP_OPTION_ROUTER:
                if (opt.length >= 4)
                    gateway = readU32(opt.value);
                break;
            case DHCP_OPTION_AUTHENTICATION:
                if (opt.length >= 20)
                    auth = opt.value;
                break;
            default:
                break;
        }
    }

    // Validate required options
    if (!type || serverId == 0 || leaseTime == 0 || subnetMask == 0 || gateway == 0 || (auth && !validateAuthentication(dhcp, auth)))
        return false;
    
    // Apply configs
    currentInterface->setIPv4(readU32(dhcp.raw->yiaddr), std::popcount(subnetMask));
    configs.router.v4 = gateway;
    configs.serverID.v4 = serverId;
    configs.leaseTime.store(leaseTime);
    configs.leaseStart.store(std::chrono::steady_clock::now());

    processOptionalOption(options);

    currentInterface->getVRF()->getGlobal().timeManager.cancelTimer(requestRetryTimerId.load(std::memory_order_relaxed));
    requestRetryTimerId.store(0, std::memory_order_release);
    acked.store(true, std::memory_order_release);

    scheduleLeaseTimers(t1, t2, leaseTime);
    return true;
}

bool Protocol::DhcpClient::processDhcpNak(const DhcpHeader& dhcp, std::vector<TLV8Option>& options, bool isDecline)
{
    if (dhcp.getMagicCookie() != DHCP_MAGIC_COOKIE) return false;

    uint8_t msgType = 0;
    const uint8_t* auth = nullptr;
    
    for (const auto& opt : options)
    {
        if (opt.type == DHCP_OPTION_TYPE)
        {
            msgType = opt.value[0];
            break;
        }
        if (opt.type == DHCP_OPTION_AUTHENTICATION)
        {
            auth = opt.value;
        }
    }

    if ((msgType != (isDecline ? DHCP_TYPE_DECLINE : DHCP_TYPE_NAK)) || (auth && !validateAuthentication(dhcp, auth)))
        return false;

    // Reset state
    currentInterface->removeIPv4();
    configs.serverID.v4 = 0;
    configs.router.v4 = 0;
    configs.leaseTime.store(0, std::memory_order_release);
    acked.store(false, std::memory_order_release);
    offered.store(false, std::memory_order_release);

    // Cancel timers
    cancelLeaseTimers();

    // Restart discovery
    uint8_t mac[6];
    currentInterface->configs.getMac(mac);
    sendDhcpDiscover(currentInterface->getVRF()->getGlobal().getHostname(), mac);

    return true;
}

bool Protocol::DhcpClient::processDhcpDecline(const DhcpHeader& dhcp, std::vector<TLV8Option>& options)
{
    return processDhcpNak(dhcp, options, true);
}

bool Protocol::DhcpClient::processDhcpInformAck(const DhcpHeader& dhcp, std::vector<TLV8Option>& options)
{
    if (dhcp.getMagicCookie() != DHCP_MAGIC_COOKIE) return false;

    uint8_t msgType = 0;
    const uint8_t* auth = nullptr;
    
    for (const auto& opt : options)
    {
        if (opt.type == DHCP_OPTION_TYPE)
        {
            msgType = opt.value[0];
            break;
        }
        if (opt.type == DHCP_OPTION_AUTHENTICATION)
        {
            auth = opt.value;
        }
    }

    if (msgType != DHCP_TYPE_ACK || !(auth && validateAuthentication(dhcp, auth)))
        return false;

    processOptionalOption(options);
    return true;
}

void Protocol::DhcpClient::processOptionalOption(const std::vector<TLV8Option>& opts)
{
    for (const auto& opt : opts)
    {
        switch (opt.type)
        {
            case DHCP_OPTION_DOMAIN_SERVER:
                for (size_t i = 0; i + 4 <= opt.length; i += 4)
                    configs.dnsServers.emplace_back(opt.value + i, AddressFamily::IPv4);
                break;

            case DHCP_OPTION_DOMAIN_NAME:
                configs.domainName.assign(reinterpret_cast<const char*>(opt.value), opt.valueSize);
                break;

            case DHCP_OPTION_DOMAIN_SEARCH:
                //TODO full domain parsing
                //configs.domainSearch.assign(reinterpret_cast<const char*>(opt.value), opt.valueSize);
                break;

            case DHCP_OPTION_NTP:
                for (size_t i = 0; i + 4 <= opt.length; i += 4)
                    configs.ntpServers.emplace_back(opt.value + 1, AddressFamily::IPv4);
                break;

            case DHCP_OPTION_CLASSLESS_STATIC_ROUTE:
            case 249:
                //TODO decode classless static route
                break;

            case DHCP_OPTION_NETBIOS_SERVER:
                for (size_t i = 0; i + 4 <= opt.length; i += 4)
                    configs.winsServers.emplace_back(opt.value + i, AddressFamily::IPv4);
                break;

            case DHCP_OPTION_MTU:
                if (opt.length == 2)
                    configs.mtu.store(readU16(opt.value), std::memory_order_release);
                break;
            
            case DHCP_OPTION_HOSTNAME:
                configs.hostname.assign(reinterpret_cast<const char*>(opt.value), opt.valueSize);
            default:
                break;
        }
    }
}

void Protocol::DhcpClient::scheduleLeaseTimers(uint32_t t1, uint32_t t2, uint32_t lease)
{
    auto now = std::chrono::steady_clock::now();
    auto renewTime = t1 ? t1 : lease / 2;
    auto rebindTime = t2 ? t2 : (lease * 875) / 1000;

    Global& global = currentInterface->getVRF()->getGlobal();

    renewTimerId = global.timeManager.addTimer(
        now + std::chrono::seconds(renewTime),
        [this](uint32_t) { sendRenew(); });

    rebindTimerId = global.timeManager.addTimer(
        now + std::chrono::seconds(rebindTime),
        [this](uint32_t) { sendRebind(); });

    expireTimerId = global.timeManager.addTimer(
        now + std::chrono::seconds(lease),
        [this](uint32_t) { expireLease(); });
}

void Protocol::DhcpClient::cancelLeaseTimers()
{
    Global& global = currentInterface->getVRF()->getGlobal();
    if (renewTimerId) global.timeManager.cancelTimer(renewTimerId);
    if (rebindTimerId) global.timeManager.cancelTimer(rebindTimerId);
    if (expireTimerId) global.timeManager.cancelTimer(expireTimerId);
    renewTimerId = 0;
    rebindTimerId = 0;
    expireTimerId = 0;
}

void Protocol::DhcpClient::sendRenew()
{
    PacketBuilder builder(currentInterface);
    UDP::reserveUDP(builder, AddressFamily::IPv4);
    builder.reserveHeader(HeaderType::DHCP, DhcpHeader::fixedSize);
    auto* next = builder.nextBuildHeader();
    if (!next) return;

    DhcpHeader dhcp;
    dhcp.setBuffer(next->buffer);
    dhcp.setOpcode(DHCP_TYPE_REQUEST);
    dhcp.setHType(0x01);
    dhcp.setHLen(6);
    dhcp.setHops(0);
    generateDhcpTransid(dhcp.raw->xId);
    dhcp.setSecs(0);
    std::memset(dhcp.raw->flags, 0, 2);

    currentInterface->configs.ipv4.getPrimaryAddress(dhcp.raw->ciaddr);
    std::memset(dhcp.raw->yiaddr, 0, 12);

    uint8_t mac[6];
    currentInterface->configs.getMac(mac);
    dhcp.setServerName(DHCP_SERVER_HOSTNAME);
    dhcp.setBootFile(DHCP_BOOT_FILE);
    dhcp.setMagicCookie(DHCP_MAGIC_COOKIE);

    size_t mtuLimit = currentInterface->configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxClientSize = configs.maxSize.load(std::memory_order_relaxed);
    size_t maxAllowed = maxClientSize == 0 ? mtuLimit : std::min(mtuLimit, maxClientSize);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;

    // Add TLV options directly into trailing span
    const std::string* authKey = configs.getAuthKey();
    Dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, authKey);

    uint8_t type = DHCP_TYPE_REQUEST;
    Dhcp::appendTLV(tlv, DHCP_OPTION_TYPE, 1, &type);
    if (configs.clientID.size > 0)
        Dhcp::appendTLV(tlv, DHCP_OPTION_CLIENT_ID, configs.clientID.size, configs.clientID.data);
    Dhcp::appendTLV(tlv, DHCP_OPTION_REQUEST_IP, currentInterface->configs.ipv4.getPrimaryAddress());

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(DHCP_OPTION_OVERLOAD, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp);

    tlv.tlv.append(DHCP_OPTION_END, 0, nullptr, 0);
    builder.addTLVSize(tlv.tlv.size());

    uint8_t ip[4];

    IPPacket::BuildIP ipBuild = {
        .iface = currentInterface,
        .packetInfo = builder,
        .destIp = configs.serverID.raw,
        .sourceIp = currentInterface->configs.ipv4.getPrimaryAddress(ip),
        .hopLimit = 64,
        .protocolType = IP_UDP
    };

    UDP::buildUdp(
        AddressFamily::IPv4,
        ipBuild,
        UDP_DHCP_CLIENT,
        UDP_DHCP_SERVER
    );
}

void Protocol::DhcpClient::sendRebind()
{
    PacketBuilder builder(currentInterface);
    UDP::reserveUDP(builder, AddressFamily::IPv4);
    builder.reserveHeader(HeaderType::DHCP, DhcpHeader::fixedSize);
    auto* next = builder.nextBuildHeader();
    if (!next) return;

    DhcpHeader dhcp;
    dhcp.setBuffer(next->buffer);
    dhcp.setOpcode(DHCP_TYPE_REQUEST);
    dhcp.setHType(0x01);
    dhcp.setHLen(6);
    dhcp.setHops(0);
    generateDhcpTransid(dhcp.raw->xId);
    dhcp.setSecs(0);
    dhcp.raw->flags[0] = 0x80;
    dhcp.raw->flags[1] = 0x00;

    currentInterface->configs.ipv4.getPrimaryAddress(dhcp.raw->ciaddr);
    std::memset(dhcp.raw->yiaddr, 0, 12);

    uint8_t mac[6];
    currentInterface->configs.getMac(mac);
    dhcp.setClientMac(mac);
    dhcp.setServerName(DHCP_SERVER_HOSTNAME);
    dhcp.setBootFile(DHCP_BOOT_FILE);
    dhcp.setMagicCookie(DHCP_MAGIC_COOKIE);

    size_t mtuLimit = currentInterface->configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxClientSize = configs.maxSize.load(std::memory_order_relaxed);
    size_t maxAllowed = maxClientSize == 0 ? mtuLimit : std::min(mtuLimit, maxClientSize);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;

    // Add TLV options directly into trailing span
    const std::string* authKey = configs.getAuthKey();
    Dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, authKey);

    uint8_t type = DHCP_TYPE_REQUEST;
    Dhcp::appendTLV(tlv, DHCP_OPTION_TYPE, 1, &type);
    Dhcp::appendTLV(tlv, DHCP_OPTION_CLIENT_ID, 6, mac);
    Dhcp::appendTLV(tlv, DHCP_OPTION_REQUEST_IP, currentInterface->configs.ipv4.getPrimaryAddress());

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(DHCP_OPTION_OVERLOAD, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp);

    tlv.tlv.append(DHCP_OPTION_END, 0, nullptr, 0);
    builder.addTLVSize(tlv.tlv.size());

    uint8_t ip[4];

    IPPacket::BuildIP ipBuild = {
        .iface = currentInterface,
        .packetInfo = builder,
        .destIp = IPV4_BROADCAST,
        .sourceIp = currentInterface->configs.ipv4.getPrimaryAddress(ip),
        .hopLimit = 64,
        .protocolType = IP_UDP
    };

    UDP::buildUdp(
        AddressFamily::IPv4,
        ipBuild,
        UDP_DHCP_CLIENT,
        UDP_DHCP_SERVER
    );
}

void Protocol::DhcpClient::expireLease()
{
    currentInterface->removeIPv4();
    configs.serverID.v4 = 0;
    configs.router.v4 = 0;
    configs.leaseTime.store(0);
    configs.leaseStart.store({});

    acked.store(false);
    offered.store(false);

    renewTimerId = 0;
    rebindTimerId = 0;
    expireTimerId = 0;

    // Optionally restart discovery if auto-renew
    uint8_t mac[6];
    currentInterface->configs.getMac(mac);
    sendDhcpDiscover(currentInterface->getVRF()->getGlobal().getHostname(), mac);
}

void Protocol::DhcpClient::appendAuthOptions(TLV8BufferManager& tlv, const DhcpHeader& dhcp)
{
    const std::string* key = configs.getAuthKey();
    if (!key) return;

    uint8_t* authOpt = tlv.getNextValBuf(27);
    if (!authOpt) return;
    tlv.append(DHCP_OPTION_AUTHENTICATION, 27, nullptr, 27);

    authOpt[0] = 1;
    authOpt[1] = 1;
    authOpt[2] = 0;

    uint64_t counter = configs.lastReplayCounter.load(std::memory_order_relaxed);
    writeU64(authOpt + 3, counter + 1);

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

    configs.lastReplayCounter.store(counter + 1, std::memory_order_release);
}

bool Protocol::DhcpClient::validateAuthentication(const DhcpHeader& dhcp, const uint8_t* data)
{
    const std::string* key = configs.getAuthKey();
    if (!key) return true;

    // Check fields
    if (data[0] != 1 || data[1] != 1 || data[2] != 0)
        return false;

    // Extract replay counter
    uint64_t counter = readU64(data + 3);
    if (counter <= configs.lastReplayCounter.load(std::memory_order_relaxed))
        return false;

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

    configs.lastReplayCounter.store(counter, std::memory_order_relaxed);
    return true;
}

uint8_t Protocol::DhcpClient::getOpcode(std::vector<TLV8Option>& options)
{
    for (const auto& opt : options)
    {
        if (opt.type == DHCP_OPTION_TYPE)
            return *opt.value;
    }
    return 0;
}
