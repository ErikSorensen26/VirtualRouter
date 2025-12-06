#include <DhcpClient.h>
#include <Interface.h>
#include <IPPacket.h>
#include <PacketBuilder.hpp>
#include <Interface.h>
#include <InterfaceConfigs.h>
#include <Global.h>
#include <VirtualRouter.h>
#include <DhcpTLVManager.hpp>
#include <DhcpInfo.hpp>
#include <Encryption.hpp>

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

    sendDhcpDiscover(currentInterface->getVRF()->global.getHostname(), mac);
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

    Global& global = currentInterface->getVRF()->global;
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
        case Variable::Dhcp::Type::offer:
            processDhcpOffer(dhcp, options);
            break;
        case Variable::Dhcp::Type::ack:
            (!acked.load(std::memory_order_relaxed) && offered.load(std::memory_order_relaxed))
                ? processDhcpAck(dhcp, options)
                : processDhcpInformAck(dhcp, options);
            break;
        case Variable::Dhcp::Type::nak:
            processDhcpNak(dhcp, options);
            break;
        case Variable::Dhcp::Type::decline:
            processDhcpDecline(dhcp, options);
            break;
        default:
            break;
    }
}

bool Protocol::DhcpClient::buildDhcpDiscover(PacketBuilder& builder, const uint8_t* mac, const std::string& hostname)
{
    // Reserve space (DHCP fixed header + rough TLV estimate)
    UDPPacket::reserveUDP(builder, AddressFamily::IPv4);
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
    dhcp.setServerName(Variable::Dhcp::serverHostName);
    dhcp.setBootFile(Variable::Dhcp::bootfile);
    dhcp.setMagicCookie(Variable::Dhcp::magicCookie);

    size_t mtuLimit = currentInterface->configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxClientSize = configs.maxSize.load(std::memory_order_relaxed);
    size_t maxAllowed = maxClientSize == 0 ? mtuLimit : std::min(mtuLimit, maxClientSize);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;

    // Add TLV options directly into trailing span
    const std::string* authKey = configs.getAuthKey();
    Dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, authKey);

    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::type, 1, &Variable::Dhcp::Type::discover);
    if (configs.clientID.size > 0)
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::clientID, configs.clientID.size, configs.clientID.data);
    uint8_t msgSize[2];
    writeU16(msgSize, configs.maxSize.load(std::memory_order_relaxed));
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::maxSize, 2, msgSize);
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::hostname, hostname.size(), reinterpret_cast<const uint8_t*>(hostname.data()));
    const uint8_t requestList[] = {
        Variable::Dhcp::Option::mask,
        Variable::Dhcp::Option::router,
        Variable::Dhcp::Option::domainServer,
        Variable::Dhcp::Option::hostname
    };
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::requestList, sizeof(requestList), requestList);

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(Variable::Dhcp::Option::overload, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp);

    tlv.tlv.append(Variable::Dhcp::end, 0, nullptr, 0);
    builder.addTLVSize(tlv.tlv.size());
    
    return true;
}

bool Protocol::DhcpClient::buildDhcpRequest(PacketBuilder& builder, uint32_t transID, const std::string& hostname,
                                            uint32_t requestedIP, uint32_t serverID)
{
    // Reserve space (DHCP fixed header + rough TLV estimate)
    UDPPacket::reserveUDP(builder, AddressFamily::IPv4);
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
    dhcp.setServerName(Variable::Dhcp::serverHostName);
    dhcp.setBootFile(Variable::Dhcp::bootfile);
    dhcp.setMagicCookie(Variable::Dhcp::magicCookie);

    size_t mtuLimit = currentInterface->configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxClientSize = configs.maxSize.load(std::memory_order_relaxed);
    size_t maxAllowed = maxClientSize == 0 ? mtuLimit : std::min(mtuLimit, maxClientSize);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;

    // Add TLV options directly into trailing span
    const std::string* authKey = configs.getAuthKey();
    Dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, authKey);

    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::type, 1, &Variable::Dhcp::Type::request);
    if (configs.clientID.size > 0)
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::clientID, configs.clientID.size, configs.clientID.data);
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::serverIdentifier, serverID);
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::requestIP, requestedIP);

    uint32_t leaseTime = configs.leaseTime.load(std::memory_order_relaxed);
    if (configs.leaseTime.load(std::memory_order_relaxed) != 0)
    {
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::leaseTime, leaseTime);
        uint32_t renewTime = configs.renewalTime.load(std::memory_order_relaxed);
        uint32_t rebindTime = configs.rebindingTime.load(std::memory_order_relaxed);
        if (renewTime != 0)
            Dhcp::appendTLV(tlv, Variable::Dhcp::Option::renewalTime, renewTime);
        if (rebindTime != 0)
            Dhcp::appendTLV(tlv, Variable::Dhcp::Option::rebindingTime, rebindTime);
    }

    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::hostname, hostname.size(), reinterpret_cast<const uint8_t*>(hostname.data()));

    const uint8_t reqList[] = {
        Variable::Dhcp::Option::mask,
        Variable::Dhcp::Option::router,
        Variable::Dhcp::Option::domainServer,
        Variable::Dhcp::Option::domainSearch,
        Variable::Dhcp::Option::hostname,
        Variable::Dhcp::Option::netbiosNameServer,
        Variable::Dhcp::Option::mtu,
        Variable::Dhcp::Option::classlessStateRoute,
        Variable::Dhcp::Option::ntp
    };
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::requestList, sizeof(reqList), reqList);

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(Variable::Dhcp::Option::overload, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp);

    tlv.tlv.append(Variable::Dhcp::end, 0, nullptr, 0);
    builder.addTLVSize(tlv.tlv.size());
    
    return true;
}

bool Protocol::DhcpClient::buildDhcpRelease(PacketBuilder& builder)
{
    UDPPacket::reserveUDP(builder, AddressFamily::IPv4);
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
    currentInterface->configs.ipv4.getAddress(dhcp.raw->ciaddr);
    std::memset(dhcp.raw->yiaddr, 0, 12);

    currentInterface->configs.getMac(dhcp.raw->chaddr);
    dhcp.setServerName(Variable::Dhcp::serverHostName);
    dhcp.setBootFile(Variable::Dhcp::bootfile);
    dhcp.setMagicCookie(Variable::Dhcp::magicCookie);

    size_t mtuLimit = currentInterface->configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxClientSize = configs.maxSize.load(std::memory_order_relaxed);
    size_t maxAllowed = maxClientSize == 0 ? mtuLimit : std::min(mtuLimit, maxClientSize);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;

    // Add TLV options directly into trailing span
    const std::string* authKey = configs.getAuthKey();
    Dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, authKey);

    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::type, 1, &Variable::Dhcp::Type::release);
    if (configs.clientID.size > 0)
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::clientID, configs.clientID.size, configs.clientID.data);
    if (configs.serverID.v4 != 0) 
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::serverIdentifier, 4, configs.serverID.raw);

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(Variable::Dhcp::Option::overload, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp);

    tlv.tlv.append(Variable::Dhcp::end, 0, nullptr, 0);
    builder.addTLVSize(tlv.tlv.size());
    
    return true;
}

bool Protocol::DhcpClient::buildDhcpInform(PacketBuilder& builder, const std::string& hostname, const uint8_t* mac)
{
    UDPPacket::reserveUDP(builder, AddressFamily::IPv4);
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
    currentInterface->configs.ipv4.getAddress(dhcp.raw->ciaddr);
    std::memset(dhcp.raw->yiaddr, 0, 12);

    dhcp.setClientMac(mac);
    dhcp.setServerName(Variable::Dhcp::serverHostName);
    dhcp.setBootFile(Variable::Dhcp::bootfile);
    dhcp.setMagicCookie(Variable::Dhcp::magicCookie);

    size_t mtuLimit = currentInterface->configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxClientSize = configs.maxSize.load(std::memory_order_relaxed);
    size_t maxAllowed = maxClientSize == 0 ? mtuLimit : std::min(mtuLimit, maxClientSize);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;

    // Add TLV options directly into trailing span
    const std::string* authKey = configs.getAuthKey();
    Dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, authKey);

    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::type, 1, &Variable::Dhcp::Type::inform);
    if (configs.clientID.size > 0)
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::clientID, configs.clientID.size, configs.clientID.data);
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::hostname, hostname.size(), reinterpret_cast<const uint8_t*>(hostname.data()));
    
    const uint8_t reqList[] = {
        Variable::Dhcp::Option::domainServer,
        Variable::Dhcp::Option::domainSearch,
        Variable::Dhcp::Option::ntp,
        Variable::Dhcp::Option::router
    };
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::requestList, sizeof(reqList), reqList);

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(Variable::Dhcp::Option::overload, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp);

    tlv.tlv.append(Variable::Dhcp::end, 0, nullptr, 0);
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
        .destIp = Variable::IPv4::broadcast,
        .sourceIp = Variable::IPv4::source,
        .hopLimit = 64,
        .protocolType = Variable::IP::udp
    };

    UDPPacket::buildUdp(
        AddressFamily::IPv4,
        ipBuild,
        Variable::Udp::dhcpClient,
        Variable::Udp::dhcpServer
    );

    // Retry scheduling
    uint32_t timerId = currentInterface->getVRF()->global.timeManager.addTimer(
        std::chrono::steady_clock::now() + std::chrono::seconds(4),
        [this, hostname]() {
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
            .destIp = Variable::IPv4::broadcast,
            .sourceIp = Variable::IPv4::source,
            .hopLimit = 64,
            .protocolType = Variable::IP::udp
        };

        UDPPacket::buildUdp(
            AddressFamily::IPv4,
            ipBuild,
            Variable::Udp::dhcpClient,
            Variable::Udp::dhcpServer
        );
    }

    // Retry
    uint32_t timerId = currentInterface->getVRF()->global.timeManager.addTimer(
        std::chrono::steady_clock::now() + std::chrono::seconds(4),
        [this, transID, hostname, requestedIp, serverId]() {
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
    currentInterface->configs.ipv4.getAddress(ip);

    IPPacket::BuildIP ipBuild = {
        .iface = currentInterface,
        .packetInfo = builder,
        .destIp = configs.serverID.raw,
        .sourceIp = ip,
        .hopLimit = 64,
        .protocolType = Variable::IP::udp
    };

    UDPPacket::buildUdp(
        AddressFamily::IPv4,
        ipBuild,
        Variable::Udp::dhcpClient,
        Variable::Udp::dhcpServer
    );
}

bool Protocol::DhcpClient::processDhcpOffer(const DhcpHeader& dhcp, std::vector<TLV8Option>& options)
{
    if (dhcp.getMagicCookie() != Variable::Dhcp::magicCookie) return false;

    uint8_t msgType = 0;
    const uint8_t* serverIdPtr = nullptr;
    uint8_t* auth = nullptr;

    for (const auto& opt : options)
    {
        switch (opt.type)
        {
            case Variable::Dhcp::Option::type:
                if (opt.length == 1)
                    msgType = opt.value[0];
                break;
            case Variable::Dhcp::Option::serverIdentifier:
                if (opt.length == 4)
                    serverIdPtr = opt.value;
                break;
            case Variable::Dhcp::Option::authentication:
                if (opt.length >= 20)
                    auth = opt.value;
                break;
            default:
                break;
        }
    }

    if (auth && !validateAuthentication(dhcp, auth))
        return false;

    if (msgType != Variable::Dhcp::Type::offer || !serverIdPtr)
        return false;

    uint32_t requestedAddress = readU32(dhcp.raw->yiaddr);
    uint32_t dhcpServerId = readU32(serverIdPtr);
    uint32_t transId = readU32(dhcp.raw->xId);

    offered.store(true, std::memory_order_release);
    Global& global = currentInterface->getVRF()->global;
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
    if (dhcp.getMagicCookie() != Variable::Dhcp::magicCookie) return false;

    bool type = false;
    uint32_t serverId = 0, leaseTime = 0, subnetMask = 0, gateway = 0;
    uint32_t t1 = 0, t2 = 0;
    uint8_t* auth = nullptr;

    for (const auto& opt : options)
    {
        switch(opt.type)
        {
            case Variable::Dhcp::Option::type:
                if (opt.length == 1 && opt.value[0] == Variable::Dhcp::Type::ack)
                    type = true;
                break;
            case Variable::Dhcp::Option::serverIdentifier:
                if (opt.length == 4)
                    serverId = readU32(opt.value);
                break;
            case Variable::Dhcp::Option::leaseTime:
                if (opt.length == 4)
                    leaseTime = readU32(opt.value);
                break;
            case Variable::Dhcp::Option::renewalTime:
                if (opt.length == 4)
                    t1 = readU32(opt.value);
                break;
            case Variable::Dhcp::Option::rebindingTime:
                if (opt.length == 4)
                    t2 = readU32(opt.value);
                break;
            case Variable::Dhcp::Option::mask:
                if (opt.length == 4)
                    subnetMask = readU32(opt.value);
                break;
            case Variable::Dhcp::Option::router:
                if (opt.length >= 4)
                    gateway = readU32(opt.value);
                break;
            case Variable::Dhcp::Option::authentication:
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

    currentInterface->getVRF()->global.timeManager.cancelTimer(requestRetryTimerId.load(std::memory_order_relaxed));
    requestRetryTimerId.store(0, std::memory_order_release);
    acked.store(true, std::memory_order_release);

    scheduleLeaseTimers(t1, t2, leaseTime);
    return true;
}

bool Protocol::DhcpClient::processDhcpNak(const DhcpHeader& dhcp, std::vector<TLV8Option>& options, bool isDecline)
{
    if (dhcp.getMagicCookie() != Variable::Dhcp::magicCookie) return false;

    uint8_t msgType = 0;
    uint8_t* auth = nullptr;
    
    for (const auto& opt : options)
    {
        if (opt.type == Variable::Dhcp::Option::type)
        {
            msgType = opt.value[0];
            break;
        }
        if (opt.type == Variable::Dhcp::Option::authentication)
        {
            auth = opt.value;
        }
    }

    if ((msgType != (isDecline ? Variable::Dhcp::Type::decline : Variable::Dhcp::Type::nak)) || (auth && !validateAuthentication(dhcp, auth)))
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
    sendDhcpDiscover(currentInterface->getVRF()->global.getHostname(), mac);

    return true;
}

bool Protocol::DhcpClient::processDhcpDecline(const DhcpHeader& dhcp, std::vector<TLV8Option>& options)
{
    return processDhcpNak(dhcp, options, true);
}

bool Protocol::DhcpClient::processDhcpInformAck(const DhcpHeader& dhcp, std::vector<TLV8Option>& options)
{
    if (dhcp.getMagicCookie() != Variable::Dhcp::magicCookie) return false;

    uint8_t msgType = 0;
    uint8_t* auth = nullptr;
    
    for (const auto& opt : options)
    {
        if (opt.type == Variable::Dhcp::Option::type)
        {
            msgType = opt.value[0];
            break;
        }
        if (opt.type == Variable::Dhcp::Option::authentication)
        {
            auth = opt.value;
        }
    }

    if (msgType != Variable::Dhcp::Type::ack || !(auth && validateAuthentication(dhcp, auth)))
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
            case Variable::Dhcp::Option::domainServer:
                for (size_t i = 0; i + 4 <= opt.length; i += 4)
                    configs.dnsServers.emplace_back(opt.value + i, AddressFamily::IPv4);
                break;

            case Variable::Dhcp::Option::domainName:
                configs.domainName.assign(reinterpret_cast<const char*>(opt.value), opt.valueSize);
                break;

            case Variable::Dhcp::Option::domainSearch:
                //TODO full domain parsing
                //configs.domainSearch.assign(reinterpret_cast<const char*>(opt.value), opt.valueSize);
                break;

            case Variable::Dhcp::Option::ntp:
                for (size_t i = 0; i + 4 <= opt.length; i += 4)
                    configs.ntpServers.emplace_back(opt.value + 1, AddressFamily::IPv4);
                break;

            case Variable::Dhcp::Option::classlessStateRoute:
            case 249:
                //TODO decode classless static route
                break;

            case Variable::Dhcp::Option::netbiosNameServer:
                for (size_t i = 0; i + 4 <= opt.length; i += 4)
                    configs.winsServers.emplace_back(opt.value + i, AddressFamily::IPv4);
                break;

            case Variable::Dhcp::Option::mtu:
                if (opt.length == 2)
                    configs.mtu.store(readU16(opt.value), std::memory_order_release);
                break;
            
            case Variable::Dhcp::Option::hostname:
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

    Global& global = currentInterface->getVRF()->global;

    renewTimerId = global.timeManager.addTimer(
        now + std::chrono::seconds(renewTime),
        [this]() { sendRenew(); });

    rebindTimerId = global.timeManager.addTimer(
        now + std::chrono::seconds(rebindTime),
        [this]() { sendRebind(); });

    expireTimerId = global.timeManager.addTimer(
        now + std::chrono::seconds(lease),
        [this]() { expireLease(); });
}

void Protocol::DhcpClient::cancelLeaseTimers()
{
    Global& global = currentInterface->getVRF()->global;
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
    UDPPacket::reserveUDP(builder, AddressFamily::IPv4);
    builder.reserveHeader(HeaderType::DHCP, DhcpHeader::fixedSize);
    auto* next = builder.nextBuildHeader();
    if (!next) return;

    DhcpHeader dhcp;
    dhcp.setBuffer(next->buffer);
    dhcp.setOpcode(Variable::Dhcp::Type::request);
    dhcp.setHType(0x01);
    dhcp.setHLen(6);
    dhcp.setHops(0);
    generateDhcpTransid(dhcp.raw->xId);
    dhcp.setSecs(0);
    std::memset(dhcp.raw->flags, 0, 2);

    currentInterface->configs.ipv4.getAddress(dhcp.raw->ciaddr);
    std::memset(dhcp.raw->yiaddr, 0, 12);

    uint8_t mac[6];
    currentInterface->configs.getMac(mac);
    dhcp.setServerName(Variable::Dhcp::serverHostName);
    dhcp.setBootFile(Variable::Dhcp::bootfile);
    dhcp.setMagicCookie(Variable::Dhcp::magicCookie);

    size_t mtuLimit = currentInterface->configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxClientSize = configs.maxSize.load(std::memory_order_relaxed);
    size_t maxAllowed = maxClientSize == 0 ? mtuLimit : std::min(mtuLimit, maxClientSize);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;

    // Add TLV options directly into trailing span
    const std::string* authKey = configs.getAuthKey();
    Dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, authKey);

    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::type, 1, &Variable::Dhcp::Type::request);
    if (configs.clientID.size > 0)
        Dhcp::appendTLV(tlv, Variable::Dhcp::Option::clientID, configs.clientID.size, configs.clientID.data);
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::requestIP, currentInterface->configs.ipv4.getAddress());

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(Variable::Dhcp::Option::overload, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp);

    tlv.tlv.append(Variable::Dhcp::end, 0, nullptr, 0);
    builder.addTLVSize(tlv.tlv.size());

    uint8_t ip[4];

    IPPacket::BuildIP ipBuild = {
        .iface = currentInterface,
        .packetInfo = builder,
        .destIp = configs.serverID.raw,
        .sourceIp = currentInterface->configs.ipv4.getAddress(ip),
        .hopLimit = 64,
        .protocolType = Variable::IP::udp
    };

    UDPPacket::buildUdp(
        AddressFamily::IPv4,
        ipBuild,
        Variable::Udp::dhcpClient,
        Variable::Udp::dhcpServer
    );
}

void Protocol::DhcpClient::sendRebind()
{
    PacketBuilder builder(currentInterface);
    UDPPacket::reserveUDP(builder, AddressFamily::IPv4);
    builder.reserveHeader(HeaderType::DHCP, DhcpHeader::fixedSize);
    auto* next = builder.nextBuildHeader();
    if (!next) return;

    DhcpHeader dhcp;
    dhcp.setBuffer(next->buffer);
    dhcp.setOpcode(Variable::Dhcp::Type::request);
    dhcp.setHType(0x01);
    dhcp.setHLen(6);
    dhcp.setHops(0);
    generateDhcpTransid(dhcp.raw->xId);
    dhcp.setSecs(0);
    dhcp.raw->flags[0] = 0x80;
    dhcp.raw->flags[1] = 0x00;

    currentInterface->configs.ipv4.getAddress(dhcp.raw->ciaddr);
    std::memset(dhcp.raw->yiaddr, 0, 12);

    uint8_t mac[6];
    currentInterface->configs.getMac(mac);
    dhcp.setClientMac(mac);
    dhcp.setServerName(Variable::Dhcp::serverHostName);
    dhcp.setBootFile(Variable::Dhcp::bootfile);
    dhcp.setMagicCookie(Variable::Dhcp::magicCookie);

    size_t mtuLimit = currentInterface->configs.ipv4.mtu.load(std::memory_order_relaxed);
    size_t maxClientSize = configs.maxSize.load(std::memory_order_relaxed);
    size_t maxAllowed = maxClientSize == 0 ? mtuLimit : std::min(mtuLimit, maxClientSize);
    size_t tlvLimit = maxAllowed - builder.bufferOffset;

    // Add TLV options directly into trailing span
    const std::string* authKey = configs.getAuthKey();
    Dhcp::DhcpTLVManager tlv(dhcp, tlvLimit, authKey);

    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::type, 1, &Variable::Dhcp::Type::request);
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::clientID, 6, mac);
    Dhcp::appendTLV(tlv, Variable::Dhcp::Option::requestIP, currentInterface->configs.ipv4.getAddress());

    if (tlv.file)
    {
        uint8_t overload = tlv.sname ? 3 : 2;
        tlv.tlv.append(Variable::Dhcp::Option::overload, 1, &overload, 1);
    }

    if (authKey)
        appendAuthOptions(tlv.tlv, dhcp);

    tlv.tlv.append(Variable::Dhcp::end, 0, nullptr, 0);
    builder.addTLVSize(tlv.tlv.size());

    uint8_t ip[4];

    IPPacket::BuildIP ipBuild = {
        .iface = currentInterface,
        .packetInfo = builder,
        .destIp = Variable::IPv4::broadcast,
        .sourceIp = currentInterface->configs.ipv4.getAddress(ip),
        .hopLimit = 64,
        .protocolType = Variable::IP::udp
    };

    UDPPacket::buildUdp(
        AddressFamily::IPv4,
        ipBuild,
        Variable::Udp::dhcpClient,
        Variable::Udp::dhcpServer
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
    sendDhcpDiscover(currentInterface->getVRF()->global.getHostname(), mac);
}

void Protocol::DhcpClient::appendAuthOptions(TLV8BufferManager& tlv, const DhcpHeader& dhcp)
{
    const std::string* key = configs.getAuthKey();
    if (!key) return;

    uint8_t* authOpt = tlv.getNextValBuf(27);
    if (!authOpt) return;
    tlv.append(Variable::Dhcp::Option::authentication, 27, nullptr, 27);

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
        if (opt.type == Variable::Dhcp::Option::type)
            return *opt.value;
    }
    return 0;
}
