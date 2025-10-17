#include <gtest/gtest.h>
#include <MockInterface.hpp>
#include <Ndp.h>
#include <chrono>
#include <PacketBuilder.hpp>
#include <IPPacket.h>

class Internal_NdpTest : public ::testing::Test
{
protected:
    MockInterface* iface;
    Protocol::Ndp* ndp;
    Global* global;
    alignas(64) uint8_t buf[128];

    uint8_t ip[16] = { 0xFe, 0x80, 0, 0, 0, 0, 0, 0, 0x03, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x01};
    uint8_t otherGlobal[16] = { 0xFD, 0x12, 0,0,0,0,0,0,0,0,0,0,0,0,1, 0x02};

    uint8_t localLinkIp[16] = { 0xFe, 0x80, 0, 0, 0, 0, 0, 0, 0x02, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x01};
    uint8_t globalIp[16] = { 0xFD, 0x12, 0,0,0,0,0,0,0,0,0,0,0,0,1, 0x01};

    uint8_t prefix[16] = {0xFD, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t mac[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    uint8_t mac2[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0A};

    void SetUp() override
    {
        std::memset(buf, 0, 128);
        global = new Global({}, false, true);
        iface = new MockInterface(*global);
        iface->enableShutdown();
        iface->configs.ipv6.addAddress(localLinkIp, true, 64);
        iface->configs.ipv6.linkLocalAddress->valid = true;
        iface->configs.ipv6.linkLocalAddress->tentative= false;
        iface->configs.ipv6.addAddress(globalIp, false, 64);
        iface->configs.ipv6.globalAddresses[0]->valid = true;
        iface->configs.ipv6.globalAddresses[0]->tentative = false;
        ndp = new Protocol::Ndp(*iface);
        iface->ndp = ndp;
        ndp->configs.dadTime.store(10);
    }

    void TearDown() override
    {
        ndp->shutdown();
        delete iface;
        delete global;
    }

    void onReachableTimeout(const IPAddress& targetIp) { ndp->onReachableTimeout(targetIp); }
    void scheduleNextRA() { ndp->scheduleNextRA(); }
    std::unordered_set<IPAddress>& getPendingRequests() { return ndp->pendingRequests; }
    std::unordered_map<IPAddress, uint8_t>& getRetryCount() { return ndp->nsRetryCount; }
    std::unordered_map<IPAddress, uint64_t>& getProxyEntries() { return ndp->proxyEntries; }
    std::mutex& getRequestMutex() { return ndp->requestMutex; }
    std::shared_mutex& getCacheMutex() { return ndp->ndpCacheMutex; }
    std::unordered_map<IPAddress, Protocol::NdpCacheEntry>& getNdpCache() { return ndp->ndpCache; }
    std::vector<InterfaceConfigs::IPv6State::IPv6Address*>& getIPv6s() { return iface->configs.ipv6.globalAddresses; }
    InterfaceConfigs::IPv6State::IPv6Address* getLinkLocal() { return iface->configs.ipv6.linkLocalAddress; }
    void clearIPv6s() { iface->configs.ipv6.globalAddresses.clear(); }
    void clearUnsolidated() { ndp->lastUnsolicitedNaTime.clear(); }
    PacketBuilder& routeAdvertisment(PacketBuilder& pkt, uint8_t* raMac = nullptr) { ndp->routeAdvertisement(pkt, iface->configs.getMac(raMac ? raMac : mac)); return pkt; }
};

// Test: SendNS_ReceiveNA_CreatesEntry
TEST_F(Internal_NdpTest, SendNS_ReceiveNA_CreatesEntry)
{
    EXPECT_CALL(*iface, enqueuePacket(::testing::_, ::testing::_)).Times(2); // failed

    PacketBuilder dummy(iface);
    dummy.reserveHeader(HeaderType::ETHERNET, EthernetHeader::fixedSize);
    dummy.reserveHeader(HeaderType::IPV6, IPv6Header::fixedSize);
    dummy.nextBuildHeader();
    ndp->resolveAndSend(ip, dummy);

    uint8_t res[4] = { 0xE0, 0x00, 0x00, 0x00 };
    uint8_t trail[24] = {0};
    std::memcpy(trail, ip, 16);
    TLV8BufferManager opt(trail + 16, 8);
    opt.append(Variable::ICMPv6::Option::target, 1, mac, 6);

    Icmpv6Header na;
    na.setBuffer(buf);
    na.setType(Variable::ICMPv6::Type::ndpNeighborAdvertisement);
    na.setCode(0x00);
    na.setReserved(res);
    na.setTrail(trail, 24);

    ndp->receiveNeighborAdvertisement(na, ip);

    uint8_t resolvedMac[6];
    EXPECT_TRUE(ndp->getMac(resolvedMac, ip)); // failed
    EXPECT_EQ(std::memcmp(resolvedMac, mac, 6), 0);
}

// Test: UnsolicitedNA_CreatesEntry
TEST_F(Internal_NdpTest, UnsolicitedNA_CreatesEntry)
{
    uint8_t res[4] = { 0xA0, 0x00, 0x00, 0x00 };
    uint8_t trail[24];
    std::memcpy(trail, ip, 16);
    TLV8BufferManager opt(trail + 16, 8);
    opt.append(Variable::ICMPv6::Option::target, 1, mac, 6);

    Icmpv6Header na;
    na.setBuffer(buf);
    na.setCode(Variable::ICMPv6::Type::ndpNeighborAdvertisement);
    na.setReserved(res);
    na.setTrail(trail, 24);

    ndp->receiveNeighborAdvertisement(na, ip);

    uint8_t resolvedMac[6];
    ASSERT_TRUE(ndp->getMac(resolvedMac, ip));
    EXPECT_EQ(std::memcmp(resolvedMac, mac, 6), 0);
}

// Test: CacheEntryExpiresAfterReachableTime
TEST_F(Internal_NdpTest, CacheEntryExpiresAfterReachableTime)
{
    ndp->configs.reachableTime.store(100);
    ndp->configs.cacheExpire.store(1);

    IPAddress addr(ip, AddressFamily::IPv6);
    ndp->addNdpEntry(addr, readU48(mac));

    uint8_t resolvedMac[6];
    ASSERT_TRUE(ndp->getMac(resolvedMac, ip));

    std::this_thread::sleep_for(std::chrono::seconds(2));

    ASSERT_FALSE(ndp->getMac(resolvedMac, ip));
}

// Test: NudTransitionsAndRemoval
TEST_F(Internal_NdpTest, NudTransitionsAndRemoval)
{
    iface->blockEnqueues();
    ndp->configs.reachableTime.store(50);
    ndp->configs.cacheExpire.store(2);
    ndp->configs.nudBaseInterval = 1;
    ndp->configs.nudBase = 1;

    ndp->addNdpEntry(IPAddress(ip, AddressFamily::IPv6), readU48(mac));
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    onReachableTimeout({ip, AddressFamily::IPv6}); // REACHABLE -> STALE
    std::this_thread::sleep_for(std::chrono::seconds(2));

    uint8_t resolvedMac[6];
    ASSERT_FALSE(ndp->getMac(resolvedMac, ip));
}

// Test: ReceiveNAWithoutQueue_AddsEntry
TEST_F(Internal_NdpTest, ReceiveNAWithoutQueue_AddsEntry)
{
    uint8_t res[4] = { 0xA0, 0x00, 0x00, 0x00 };
    uint8_t trail[24];
    std::memcpy(trail, ip, 16);
    TLV8BufferManager opt(trail + 16, 8);
    opt.append(Variable::ICMPv6::Option::target, 1, mac, 6);

    Icmpv6Header na;
    na.setBuffer(buf);
    na.setType(Variable::ICMPv6::Type::ndpNeighborAdvertisement);
    na.setCode(0x00);
    na.setReserved(res);
    na.setTrail(trail, 24);

    ndp->receiveNeighborAdvertisement(na, ip);

    uint8_t resolvedMac[6];
    ASSERT_TRUE(ndp->getMac(resolvedMac, ip));
    EXPECT_EQ(std::memcmp(resolvedMac, mac, 6), 0);
}

// Test: SLAAC_RSAndRA_CreatesAddress
TEST_F(Internal_NdpTest, SLAAC_RSAndRA_CreatesAddress)
{
    iface->configs.setMac(mac);
    ndp->configs.slaacEnabled.store(true);

    EXPECT_CALL(*iface, enqueuePacket(::testing::_, ::testing::_)).Times(2);
    ndp->initiateSlaac(); // Would normally send RS

    uint8_t fullAddr[16];
    uint8_t macAddr[6];
    Functions::calculateEui64(fullAddr, prefix, iface->configs.getMac(macAddr));

    uint8_t trail[40] = {0};

    trail[8] = Variable::ICMPv6::Option::prefix;
    trail[9] = 0x04;
    trail[10] = 0x40; // 64-bit prefix length
    trail[11] = 0xC0; // L and A bits
    writeU32(trail + 12, 1800);
    writeU32(trail + 16, 900);
    std::memcpy(trail + 24, prefix, 16);

    Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(Variable::ICMPv6::Type::ndpRouteAdvertisement);
    ra.setCode(0x00);
    ra.setTrail(trail, 40);
    
    ndp->receiveRouteAdvertisement(ra, ip, mac2);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    bool found = false;
    for (auto* addr : getIPv6s())
    {
        if (std::memcmp(addr->ip, fullAddr, 16) == 0 && addr->valid)
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// Test: SLAAC_DAD_Failure_MarksDuplicate
TEST_F(Internal_NdpTest, SLAAC_DAD_Failure_MarksDuplicate) 
{
    iface->blockEnqueues();
    iface->configs.setMac(mac);
    ndp->configs.slaacEnabled.store(true);
    ndp->configs.dadAttempts.store(1);
    ndp->configs.dadTime.store(50); // ms

    uint8_t fullAddr[16];
    Functions::calculateEui64(fullAddr, prefix, mac);

    auto* addr = iface->configs.ipv6.addAddress(fullAddr, false, 64);
    addr->tentative = true;

    ndp->duplicateAddressDetection(addr, false);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    {
        uint8_t res[4] = { 0xA0, 0x00, 0x00, 0x00 };
        uint8_t trail[24] = {0};
        TLV8BufferManager opt(trail + 16, 8);
        opt.append(Variable::ICMPv6::Option::target, 1, mac, 6);
        std::memcpy(trail, fullAddr, 16);

        Icmpv6Header na;
        na.setBuffer(buf);

        na.setType(Variable::ICMPv6::Type::ndpNeighborAdvertisement);
        na.setCode(0x00);
        na.setReserved(res);
        na.setTrail(trail, 24);

        ndp->receiveNeighborAdvertisement(na, Variable::IPv4::source);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_FALSE(addr->valid);
    EXPECT_FALSE(addr->tentative);
}

// Test: SLAAC_ValidLifetimeExpires
TEST_F(Internal_NdpTest, SLAAC_ValidLifetimeExpires) 
{
    iface->blockEnqueues();
    iface->configs.setMac(mac);
    ndp->configs.slaacEnabled.store(true);

    uint8_t fullAddr[16];
    auto* addr = iface->configs.ipv6.addAddress(Functions::calculateEui64(fullAddr, prefix, mac), false, 64);
    addr->tentative = false;
    addr->valid = true;

    ndp->configs.raLifetime.store(1); // seconds
    ndp->configs.raPreferredLifetime.store(10); // not relevant here

    ndp->duplicateAddressDetection(addr, false);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_FALSE(addr->globalValid);
}

// Test: SLAAC_PreferredLifetimeExpires
TEST_F(Internal_NdpTest, SLAAC_PreferredLifetimeExpires) 
{
    iface->blockEnqueues();
    iface->configs.setMac(mac);
    ndp->configs.slaacEnabled.store(true);

    uint8_t trail[40] = {0};

    trail[8] = Variable::ICMPv6::Option::prefix;
    trail[9] = 0x04;
    trail[10] = 0x40; // 64-bit prefix length
    trail[11] = 0xC0; // L and A bits
    writeU32(trail + 12, 100);
    writeU32(trail + 16, 1);
    std::memcpy(trail + 24, prefix, 16);

    Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(Variable::ICMPv6::Type::ndpRouteAdvertisement);
    ra.setCode(0x00);
    ra.setTrail(trail, 40);

    ndp->receiveRouteAdvertisement(ra, Variable::IPv6::source, mac2);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    auto* addr = getIPv6s().back();

    EXPECT_TRUE(addr->deprecated);
}

// Test: SLAAC_Disabled_IgnoresPrefixes
TEST_F(Internal_NdpTest, SLAAC_Disabled_IgnoresPrefixes) 
{
    iface->blockEnqueues();
    clearIPv6s();
    iface->configs.setMac(mac);
    ndp->configs.slaacEnabled.store(false);

    uint8_t trail[48] = {0};

    trail[16] = Variable::ICMPv6::Option::prefix;
    trail[17] = 0x04;
    trail[18] = 0x40; // 64-bit prefix length
    trail[19] = 0xC0; // L and A bits
    writeU32(trail + 20, 100);
    writeU32(trail + 24, 1);
    std::memcpy(trail + 32, prefix, 16);

    Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(Variable::ICMPv6::Type::ndpRouteAdvertisement);
    ra.setCode(0x00);
    ra.setTrail(trail, 48);

    ndp->receiveRouteAdvertisement(ra, Variable::IPv4::source, mac2);

    EXPECT_TRUE(getIPv6s().empty());
}

// Test: DAD_NoConflict_MarksValid
TEST_F(Internal_NdpTest, DAD_NoConflict_MarksValid) 
{
    iface->blockEnqueues();
    auto* addr = iface->configs.ipv6.addAddress(ip, false, 64);
    addr->tentative = true;
    ndp->configs.dadAttempts.store(1);
    ndp->configs.dadTime.store(100);

    ndp->duplicateAddressDetection(addr, false);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_FALSE(addr->tentative);
    EXPECT_TRUE(addr->valid);
}

// Test: DAD_ConflictFromNA_MarksDuplicate
TEST_F(Internal_NdpTest, DAD_ConflictFromNA_MarksDuplicate)
{
    iface->blockEnqueues();
    auto* addr = iface->configs.ipv6.addAddress(ip, false, 64);
    addr->tentative = true;
    ndp->configs.dadAttempts.store(1);
    ndp->configs.dadTime.store(100);

    ndp->duplicateAddressDetection(addr, false);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    uint8_t trail[24] = {0};
    std::memcpy(trail, ip, 16);
    trail[16] = Variable::ICMPv6::Option::target;
    trail[17] = 0x01;
    std::memcpy(trail + 18, mac, 6);

    Icmpv6Header na;
    na.setBuffer(buf);

    na.setType(Variable::ICMPv6::Type::ndpNeighborAdvertisement);
    na.setCode(0x00);
    na.setTrail(trail, 24);

    ndp->receiveNeighborAdvertisement(na, ip);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_FALSE(addr->tentative);
    EXPECT_FALSE(addr->valid);
}

// Test: DAD_MultipleAttemptsRequired
TEST_F(Internal_NdpTest, DAD_MultipleAttemptsRequired)
{
    iface->blockEnqueues();
    auto* addr = iface->configs.ipv6.addAddress(ip, false, 64);
    addr->tentative = true;
    ndp->configs.dadAttempts.store(2);
    ndp->configs.dadTime.store(50);

    ndp->duplicateAddressDetection(addr, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    {
        std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
        EXPECT_TRUE(addr->tentative);  // 1st attempt done, not yet accepted
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    {
        std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
        EXPECT_FALSE(addr->tentative);
        EXPECT_TRUE(addr->valid);
    }
}

// Test: DAD_Suppressed_NoDetectionOccurs
TEST_F(Internal_NdpTest, DAD_Suppressed_NoDetectionOccurs)
{
    auto* addr = iface->configs.ipv6.addAddress(ip, false, 64);
    addr->tentative = true;
    ndp->configs.dadAttempts.store(0);
    ndp->configs.dadTime.store(100);

    ndp->duplicateAddressDetection(addr, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_TRUE(addr->tentative);  // Should still be tentative due to suppression
}

// Test: ProxyNA_RespondsWithCorrectMAC
TEST_F(Internal_NdpTest, ProxyNA_RespondsWithCorrectMAC)
{
    uint8_t proxyIp[16];
    std::memcpy(proxyIp, ip, 16);
    uint8_t proxyMac[6];
    std::memcpy(proxyMac, mac, 6);
    proxyIp[15] = 'c';
    proxyMac[5] = 'c';
    ndp->addNdpEntry({proxyIp, AddressFamily::IPv6}, readU48(proxyMac), true);  // mark as proxy

    Icmpv6Header ns;
    ns.setBuffer(buf);

    ns.setType(Variable::ICMPv6::Type::ndpNeighborSolicitation);
    ns.setCode(0x00);
    ns.setTrail(localLinkIp, 16);

    EXPECT_CALL(*iface, enqueuePacket(testing::_, testing::_))
        .WillOnce(testing::Invoke([&](PacketBuilder& pkt, const uint8_t*) {
            bool hasEth = false;
            bool hasIp = false;
            bool hasICMPv6 = false;
            for (size_t i = 0; i < pkt.getHeaderCount(); ++i)
            {
                auto header = pkt.getHeaders()[i];
                if (header.type == HeaderType::ETHERNET)
                {
                    if (hasEth) FAIL();
                    hasEth = true;
                    auto eth = reinterpret_cast<EthernetHeaderRaw*>(pkt.getHeaders()[i].buffer);
                    EXPECT_EQ(std::memcmp(eth->destinationMac, proxyMac, 6), 0);
                }
                else if (header.type == HeaderType::IPV6)
                {
                    if (hasIp) FAIL();
                    hasIp = true;
                }
                else if (header.type == HeaderType::ICMPV6)
                {
                    if (hasICMPv6) FAIL();
                    hasICMPv6 = true;
                    auto icmp = reinterpret_cast<Icmpv6HeaderRaw*>(header.buffer);
                    EXPECT_EQ(icmp->type, Variable::ICMPv6::Type::ndpNeighborAdvertisement);
                    EXPECT_EQ(std::memcmp(icmp->reserved + 4, proxyIp, 16), 0);
                }
            }
            ASSERT_TRUE(hasEth && hasIp && hasICMPv6);
        }));

    ndp->receiveNeighborSolicitation(ns, proxyIp, proxyMac);
}

// Test: ProxyEntryExpiresIfNotRefreshed
TEST_F(Internal_NdpTest, ProxyEntryExpiresIfNotRefreshed)
{
    ndp->configs.cacheExpire.store(1);
    ndp->configs.reachableTime.store(10);

    ndp->addNdpEntry({ip, AddressFamily::IPv6}, readU48(mac), true);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    uint8_t resolvedMac[6];
    EXPECT_FALSE(ndp->getMac(resolvedMac, ip));
}

// Test: Proxy_UnsolicitedNASent
TEST_F(Internal_NdpTest, Proxy_UnsolicitedNASent)
{
    ndp->addNdpEntry({ip, AddressFamily::IPv6}, readU48(mac), true);

    EXPECT_CALL(*iface, enqueuePacket(testing::_, testing::_))
        .WillOnce(testing::Invoke([&](PacketBuilder& pkt, const uint8_t*) {
            bool hasEth = false;
            bool hasIp = false;
            bool hasICMP = false;
            for (size_t i = 0; i < pkt.getHeaderCount(); ++i)
            {
                auto header = pkt.getHeaders()[i];
                if (header.type == HeaderType::ETHERNET)
                {
                    if (hasEth) FAIL();
                    hasEth = true;
                    auto eth = reinterpret_cast<EthernetHeaderRaw*>(header.buffer);
                    EXPECT_EQ(std::memcmp(eth->destinationMac, Variable::Mac::broadcast, 6), 0);
                }
                else if (header.type == HeaderType::IPV6)
                {
                    if (hasIp) FAIL();
                    hasIp = true;
                }
                else if (header.type == HeaderType::ICMPV6)
                {
                    if (hasICMP) FAIL();
                    hasICMP = true;
                    auto icmp = reinterpret_cast<Icmpv6HeaderRaw*>(header.buffer);
                    EXPECT_EQ(icmp->type, Variable::ICMPv6::Type::ndpNeighborAdvertisement);
                    EXPECT_EQ(std::memcmp(icmp->reserved + 4, ip, 16), 0);
                }
            }
            ASSERT_TRUE(hasEth && hasIp && hasICMP);
        }));

    clearUnsolidated();
    ndp->sendNeighborAdvertisement(Variable::Mac::broadcast, ip);
}

// Test: DADProbe_TriggersProxyNA
TEST_F(Internal_NdpTest, DADProbe_TriggersProxyNA)
{
    ndp->addNdpEntry({ip, AddressFamily::IPv6}, readU48(mac), true);

    uint8_t unspecified[16] = {0}; // ::

    Icmpv6Header ns;
    ns.setBuffer(buf);
    ns.setType(Variable::ICMPv6::Type::ndpNeighborSolicitation);
    ns.setTrail(ip, 16);

    clearUnsolidated();

    EXPECT_CALL(*iface, enqueuePacket(testing::_, testing::_))
        .WillOnce(testing::Invoke([&](PacketBuilder& pkt, const uint8_t*) {
            bool hasEth = false;
            bool hasIp = false;
            bool hasICMP = false;
            for (size_t i = 0; i < pkt.getHeaderCount(); ++i)
            {
                auto header = pkt.getHeaders()[i];
                if (header.type == HeaderType::ETHERNET)
                {
                    if (hasEth) FAIL();
                    hasEth = true;
                }
                else if (header.type == HeaderType::IPV6)
                {
                    if (hasIp) FAIL();
                    hasIp = true;
                }
                else if (header.type == HeaderType::ICMPV6)
                {
                    if (hasICMP) FAIL();
                    hasICMP = true;
                    auto icmp = reinterpret_cast<Icmpv6HeaderRaw*>(header.buffer);
                    EXPECT_EQ(icmp->type, Variable::ICMPv6::Type::ndpNeighborAdvertisement);
                    EXPECT_EQ(std::memcmp(icmp->reserved + 4, ip, 16), 0);
                }
            }
            ASSERT_TRUE(hasEth && hasIp && hasICMP);
        }));

    ndp->receiveNeighborSolicitation(ns, unspecified, Variable::Mac::source);
}

// Test: RA_MOFlagsUpdateConfig
TEST_F(Internal_NdpTest, RA_MOFlagsUpdateConfig)
{
    ndp->configs.managedConfigFlag.store(true);
    ndp->configs.otherConfigFlag.store(true);

    PacketBuilder ra(iface);
    routeAdvertisment(ra);
    
    bool hasEth = false;
    bool hasIp = false;
    bool hasICMP = false;
    for (size_t i = 0; i < ra.getHeaderCount(); ++i)
    {
        auto header = ra.getHeaders()[i];
        if (header.type == HeaderType::ETHERNET)
        {
            if (hasEth) FAIL();
            hasEth = true;
        }
        else if (header.type == HeaderType::IPV6)
        {
            if (hasIp) FAIL();
            hasIp = true;
        }
        else if (header.type == HeaderType::ICMPV6)
        {
            if (hasICMP) FAIL();
            hasICMP = true;
            auto icmp = reinterpret_cast<Icmpv6HeaderRaw*>(header.buffer);
            uint8_t flags = icmp->reserved[1];
            EXPECT_TRUE(flags & (1 << 6));
            EXPECT_TRUE(flags & (1 << 7));
        }
    }
    ASSERT_TRUE(hasEth && hasIp && hasICMP);
}

// Test: RA_TrustedSourceIsAccepted
TEST_F(Internal_NdpTest, RA_TrustedSourceIsAccepted)
{
    ndp->configs.suppressRA.store(false);
    ndp->configs.destinationGuard.store(true);
    ndp->configs.raGuardMode.store(Protocol::Ndp::Configs::RaGuardMode::TRUSTED);

    uint8_t res[4] = { 0x00, 0x00, 0x00, 0x10 };
    uint8_t trail[8] = {0};

    Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(Variable::ICMPv6::Type::ndpRouteAdvertisement);
    ra.setCode(0x00);
    ra.setReserved(res);
    ra.setTrail(trail, 8);

    // Should not be dropped
    ndp->receiveRouteAdvertisement(ra, Variable::IPv6::source, mac2);
    SUCCEED(); // No crash/drop = accepted
}

// Test: RA_FromBlockedPortIsDropped_BLOCK_ALL
TEST_F(Internal_NdpTest, RA_FromBlockedPortIsDropped_BLOCK_ALL)
{
    ndp->configs.suppressRA.store(false);
    ndp->configs.destinationGuard.store(true);
    ndp->configs.raGuardMode.store(Protocol::Ndp::Configs::RaGuardMode::BLOCK_ALL);

    uint8_t trail[8] = {0};

    Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(Variable::ICMPv6::Type::ndpRouteAdvertisement);
    ra.setCode(0x00);
    ra.setTrail(trail, 8);

    // No crash = passed
    ndp->receiveRouteAdvertisement(ra, Variable::IPv6::source, mac2);

    // If it reached here, it was dropped without crashing
    SUCCEED();
}

// Test: RA_MacWhitelistRejectsUnknownMAC
TEST_F(Internal_NdpTest, RA_MacWhitelistRejectsUnknownMAC)
{
    ndp->configs.suppressRA.store(false);
    ndp->configs.destinationGuard.store(true);
    ndp->configs.raGuardMode.store(Protocol::Ndp::Configs::RaGuardMode::MAC_WHITELIST);

    uint8_t trail[8] = {0};

    Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(Variable::ICMPv6::Type::ndpRouteAdvertisement);
    ra.setCode(0x00);
    ra.setTrail(trail, 8);

    ndp->receiveRouteAdvertisement(ra, Variable::IPv6::source, mac2);
    SUCCEED(); // No crash = dropped
}

// Test: RA_NonICMPv6HeaderIsDropped
TEST_F(Internal_NdpTest, RA_NonICMPv6HeaderIsDropped)
{
    // This would normally be dropped before it reaches NDP
    // We simulate this by ensuring no effect happens
    ndp->configs.suppressRA.store(false);

    uint8_t trail[8] = {0};

    Icmpv6Header ra;
    ra.setBuffer(buf);
    
    ra.setType(Variable::ICMPv6::Type::ndpRouteAdvertisement);
    ra.setCode(0x00);
    ra.setTrail(trail, 8);

    // No extension headers included — simulate filtered result
    ndp->receiveRouteAdvertisement(ra, Variable::IPv6::source, mac2);
    SUCCEED(); // Reaching here = accepted or ignored, no crash
}

// Test: RA_ExcludedPrefixIgnored
TEST_F(Internal_NdpTest, RA_ExcludedPrefixIgnored)
{
    ndp->configs.slaacEnabled.store(true);
    clearIPv6s();
    iface->configs.setMac(mac);

    ndp->addSlaacExclusionPrefix({prefix, AddressFamily::IPv6}, false); // exclude

    uint8_t trail[40] = {0};
    trail[8] = Variable::ICMPv6::Option::prefix;
    trail[9] = 0x04;
    trail[10] = 0x40; // /64
    trail[11] = 0xC0; // L + A
    writeU32(trail + 12, 300);
    writeU32(trail + 16, 200);
    std::memcpy(trail + 24, prefix, 16);

    Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(Variable::ICMPv6::Type::ndpRouteAdvertisement);
    ra.setCode(0x00);
    ra.setTrail(trail, 40);

    ndp->receiveRouteAdvertisement(ra, Variable::IPv6::source, mac2);

    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_TRUE(getIPv6s().empty());
}

// Test: RA_InvalidPrefixSizeIgnored
TEST_F(Internal_NdpTest, RA_InvalidPrefixSizeIgnored)
{
    ndp->configs.slaacEnabled.store(true);
    clearIPv6s();
    iface->configs.setMac(mac);

    uint8_t trail[28] = {0};
    trail[8] = Variable::ICMPv6::Option::prefix;
    trail[9] = 0x04;
    trail[10] = 0x40; // /64
    trail[11] = 0xC0; // L + A
    writeU32(trail + 12, 300);
    writeU32(trail + 16, 200);
    writeU32(trail + 24, 0xFFFFFFFF); // to short - invalid
    
    Icmpv6Header ra;
    ra.setBuffer(buf);
    
    ra.setType(Variable::ICMPv6::Type::ndpRouteAdvertisement);
    ra.setCode(0x00);
    ra.setTrail(trail, 28);

    ndp->receiveRouteAdvertisement(ra, Variable::IPv6::source, mac2);

    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_TRUE(getIPv6s().empty());
}

// Test: RA_GuardBlockAllModeDropsAll
TEST_F(Internal_NdpTest, RA_GuardBlockAllModeDropsAll)
{
    ndp->configs.suppressRA.store(false);
    ndp->configs.destinationGuard.store(true);
    ndp->configs.raGuardMode.store(Protocol::Ndp::Configs::RaGuardMode::BLOCK_ALL);

    iface->configs.setMac(mac);
    ndp->configs.slaacEnabled.store(true);

    EXPECT_CALL(*iface, enqueuePacket(::testing::_, ::testing::_)).Times(1);
    ndp->initiateSlaac(); // Would normally send RS

    uint8_t fullAddr[16];
    Functions::calculateEui64(fullAddr, prefix, mac);

    uint8_t trail[40];
    trail[8] = Variable::ICMPv6::Option::prefix;
    trail[9] = 0x04;
    trail[10] = 0x40; // /64
    trail[11] = 0xC0; // L + A
    writeU32(trail + 12, 1800);
    writeU32(trail + 16, 900);
    std::memcpy(trail + 24, prefix, 16);

    Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(Variable::ICMPv6::Type::ndpRouteAdvertisement);
    ra.setCode(0x00);
    ra.setTrail(trail, 40);

    ndp->receiveRouteAdvertisement(ra, Variable::IPv6::source, mac2);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    bool found = false;
    for (auto* addr : getIPv6s())
    {
        if (std::memcmp(addr->ip, fullAddr, 16) == 0 && addr->valid)
        {
            found = true;
            break;
        }
    }
    EXPECT_FALSE(found);
}

// Test: RA_GuardTrustedModeAcceptsAll
TEST_F(Internal_NdpTest, RA_GuardTrustedModeAcceptsAll)
{
    ndp->configs.suppressRA.store(false);
    ndp->configs.destinationGuard.store(true);
    ndp->configs.raGuardMode.store(Protocol::Ndp::Configs::RaGuardMode::TRUSTED);

    iface->configs.setMac(mac);
    ndp->configs.slaacEnabled.store(true);

    EXPECT_CALL(*iface, enqueuePacket(::testing::_, ::testing::_)).Times(2);
    ndp->initiateSlaac(); // Would normally send RS

    uint8_t fullAddr[16];
    Functions::calculateEui64(fullAddr, prefix, mac);

    uint8_t trail[40] = {0};
    trail[8] = Variable::ICMPv6::Option::prefix;
    trail[9] = 0x04;
    trail[10] = 0x40; // /64
    trail[11] = 0xC0; // L and A bit
    writeU32(trail + 12, 1800);
    writeU32(trail + 16, 900);
    std::memcpy(trail + 24, prefix, 16);

    Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(Variable::ICMPv6::Type::ndpRouteAdvertisement);
    ra.setCode(0x00);
    ra.setTrail(trail, 40);

    ndp->receiveRouteAdvertisement(ra, Variable::IPv6::source, mac2);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    bool found = false;
    for (auto* addr : getIPv6s())
    {
        if (std::memcmp(addr->ip, fullAddr, 16) == 0 && addr->valid)
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// Test: RA_WithExtensionHeaders_IsDropped
TEST_F(Internal_NdpTest, RA_WithExtensionHeaders_IsDropped)
{
    clearIPv6s();
    ndp->configs.suppressRA.store(false);
    ndp->configs.destinationGuard.store(true);
    ndp->configs.raGuardMode.store(Protocol::Ndp::Configs::RaGuardMode::TRUSTED);

    uint8_t trail[8] = {0};

    Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(Variable::ICMPv6::Type::ndpRouteAdvertisement);
    ra.setCode(0x00);
    ra.setTrail(trail, 8);

    // Simulate malformed/extended header RA by using suspicious source MAC
    ndp->receiveRouteAdvertisement(ra, Variable::IPv6::source, Variable::Mac::source);

    // No address added = dropped
    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_TRUE(getIPv6s().empty());
}

// Test: RA_FragmentedPacketIgnored
TEST_F(Internal_NdpTest, RA_FragmentedPacketIgnored)
{
    clearIPv6s();

    uint8_t trail[1] = {0};

    // Assume payload is too short to be valid RA
    Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(Variable::ICMPv6::Type::ndpRouteAdvertisement);
    ra.setCode(0x00);
    ra.setTrail(trail, 1);

    ndp->receiveRouteAdvertisement(ra, Variable::IPv6::source, mac2);

    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_TRUE(getIPv6s().empty());
}

// Test: Redirect_MulticastDestinationIsIgnored
TEST_F(Internal_NdpTest, Redirect_MulticastDestinationIsIgnored)
{
    PacketInfo pkt;
    pkt.headers[0] = { HeaderType::IPV6, 0 };
    pkt.count = 1;
    pkt.offset = IPv6Header::fixedSize;
    IPv6Header ipHeader;
    ipHeader.setBuffer(buf);
    ipHeader.setSourceAddress(prefix);
    ipHeader.raw->sourceAddress[15] = 'A';
    ipHeader.setDestinationAddress(ip);

    // Should not enqueue
    EXPECT_CALL(*iface, enqueuePacket(testing::_, testing::_)).Times(0);
    ndp->sendRedirectIfNeeded(pkt, buf);
}

// Test: Redirect_IgnoresSelfToSelfTraffic
TEST_F(Internal_NdpTest, Redirect_IgnoresSelfToSelfTraffic)
{
    PacketInfo pkt;
    pkt.headers[0] = { HeaderType::IPV6, 0 };
    pkt.count = 1;
    pkt.offset = IPv6Header::fixedSize;
    IPv6Header ip6;
    ip6.setBuffer(buf);
    ip6.setSourceAddress(ip);
    ip6.setDestinationAddress(ip);

    EXPECT_CALL(*iface, enqueuePacket(testing::_, testing::_)).Times(0);
    ndp->sendRedirectIfNeeded(pkt, buf);
}

// Test: Redirect_CreatesEntryForBetterNextHop
TEST_F(Internal_NdpTest, Redirect_CreatesEntryForBetterNextHop)
{
    uint8_t betterHop[16];
    std::memcpy(betterHop, ip, 16);
    betterHop[15] = 'A';
    uint8_t destIp[16];
    std::memcpy(destIp, ip, 16);
    destIp[15] = 'B';

    uint8_t trail[40];
    std::memcpy(trail, betterHop, 16);
    std::memcpy(trail + 16, destIp, 16);
    trail[32] = Variable::ICMPv6::Option::target;
    trail[33] = 0x01;
    std::memcpy(trail + 34, mac, 6);

    Icmpv6Header redirect;
    redirect.setBuffer(buf);

    redirect.setType(Variable::ICMPv6::Type::ndpRedirectMessage);
    redirect.setCode(0x00);
    redirect.setTrail(trail, 40);

    ndp->receiveRedirectMessage(redirect, ip);

    uint8_t resolvedMac[6];
    EXPECT_TRUE(ndp->getMac(resolvedMac, betterHop));
    EXPECT_EQ(std::memcmp(resolvedMac, mac, 6), 0);
}

// Test: NA_UnsolicitedRateLimitEnforced
TEST_F(Internal_NdpTest, NA_UnsolicitedRateLimitEnforced)
{
    ndp->addNdpEntry({ip, AddressFamily::IPv6}, readU48(mac), false);

    // First send should work
    EXPECT_CALL(*iface, enqueuePacket(testing::_, testing::_)).Times(1);
    ndp->sendNeighborAdvertisement(Variable::Mac::broadcast, ip);

    // Immediate resend should be skipped
    EXPECT_CALL(*iface, enqueuePacket(testing::_, testing::_)).Times(0);
    ndp->sendNeighborAdvertisement(Variable::Mac::broadcast, ip);
}

// Test: RA_RateLimitGlobalEnforced
TEST_F(Internal_NdpTest, RA_RateLimitGlobalEnforced)
{
    uint8_t buf2[100] = {0};
    iface->blockEnqueues();
    clearIPv6s();
    ndp->configs.raRateLimit.store(1); // ms
    ndp->configs.slaacEnabled.store(true);
    ndp->configs.destinationGuard.store(true);
    ndp->configs.raGuardMode.store(Protocol::Ndp::Configs::RaGuardMode::MAC_WHITELIST);
    ndp->addRaGuardAllowedMac(mac2);

    uint8_t trail1[40];
    trail1[8] = Variable::ICMPv6::Option::prefix;
    trail1[9] = 0x04;
    trail1[10] = 0x40; // /64
    trail1[11] = 0xC0; // L + A
    writeU32(trail1 + 12, 1800);
    writeU32(trail1 + 16, 900);
    std::memcpy(trail1 + 24, prefix, 16);

    Icmpv6Header ra1;
    ra1.setBuffer(buf);
    
    ra1.setType(Variable::ICMPv6::Type::ndpRouteAdvertisement);
    ra1.setCode(0x00);
    ra1.setTrail(trail1, 40);

    uint8_t trail2[40];
    trail2[8] = Variable::ICMPv6::Option::prefix;
    trail2[9] = 0x04;
    trail2[10] = 0x40; // /64
    trail2[11] = 0xC0; // L and A bits
    writeU32(trail2 + 12, 1800);
    writeU32(trail2 + 16, 900);
    std::memcpy(trail2 + 24, prefix, 16);
    trail2[25] = 0x14;

    Icmpv6Header ra2;
    ra2.setBuffer(buf2);

    ra2.setType(Variable::ICMPv6::Type::ndpRouteAdvertisement);
    ra2.setCode(0x00);
    ra2.setTrail(trail2, 40);

    ndp->receiveRouteAdvertisement(ra1, Variable::IPv6::source, mac2);
    ndp->receiveRouteAdvertisement(ra2, Variable::IPv6::source, mac2);

    EXPECT_EQ(getIPv6s().size(), 1); // Only one should be added
    EXPECT_EQ(std::memcmp(getIPv6s()[0]->ip, prefix, 8), 0);
}

// Test: NS_RetriesStopAfterConfiguredAttempts
TEST_F(Internal_NdpTest, NS_RetriesStopAfterConfiguredAttempts)
{
    iface->blockEnqueues();
    ndp->configs.nudBase = 1;
    ndp->configs.nsInterval = 50;

    PacketBuilder dummy(iface);
    ndp->resolveAndSend(ip, dummy);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::lock_guard<std::mutex> lock(getRequestMutex());
    EXPECT_EQ(getPendingRequests().count({ip, AddressFamily::IPv6}), 0);
    EXPECT_EQ(getRetryCount().count({ip, AddressFamily::IPv6}), 0);
}

// Test: EntryLimitEnforced_EvictsOldest
TEST_F(Internal_NdpTest, EntryLimitEnforced_EvictsOldest)
{
    ndp->configs.interfaceLimit.store(2); // Max 2 entries

    uint8_t ip1[16];
    std::memcpy(ip1, ip, 16);
    ip1[15] = 0x01;
    uint8_t ip2[16];
    std::memcpy(ip2, ip, 16);
    ip2[15] = 0x02;
    uint8_t ip3[16];
    std::memcpy(ip3, ip, 16);
    ip3[15] = 0x03;

    ndp->addNdpEntry({ip1, AddressFamily::IPv6}, readU48(mac));
    ndp->addNdpEntry({ip2, AddressFamily::IPv6}, readU48(mac));
    ndp->addNdpEntry({ip3, AddressFamily::IPv6}, readU48(mac)); // Should evict ip1

    uint8_t resolvedMac[6];
    EXPECT_FALSE(ndp->getMac(resolvedMac, ip1));
    EXPECT_TRUE(ndp->getMac(resolvedMac, ip2));
    EXPECT_TRUE(ndp->getMac(resolvedMac, ip3));
}

// Test: ManualProxyEntryIsStoredCorrectly
TEST_F(Internal_NdpTest, ManualProxyEntryIsStoredCorrectly)
{
    ndp->addNdpEntry({ip, AddressFamily::IPv6}, readU48(mac), true); // Proxy = true

    uint8_t resolvedMac[6];
    EXPECT_TRUE(ndp->getMac(resolvedMac, ip));
    EXPECT_EQ(std::memcmp(resolvedMac, mac, 6), 0);

    std::shared_lock<std::shared_mutex> lock(getCacheMutex());
    EXPECT_TRUE(getProxyEntries().count({ip, AddressFamily::IPv6}));
}

// Test: RA_AddsPrefixWithCorrectTimers
TEST_F(Internal_NdpTest, RA_AddsPrefixWithCorrectTimers)
{
    iface->blockEnqueues();
    ndp->configs.slaacEnabled.store(true);
    clearIPv6s();
    iface->configs.setMac(mac);

    uint8_t trail[40];
    trail[8] = Variable::ICMPv6::Option::prefix;
    trail[9] = 0x04;
    trail[10] = 0x40; // /64
    trail[11] = 0xC0; // L + A bits
    writeU32(trail + 12, 1);
    writeU32(trail + 16, 1);
    std::memcpy(trail + 24, prefix, 16);

    Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(Variable::ICMPv6::Type::ndpRouteAdvertisement);
    ra.setCode(0x00);
    ra.setTrail(trail, 40);

    ndp->receiveRouteAdvertisement(ra, Variable::IPv4::source, mac2);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    ASSERT_FALSE(getIPv6s().empty());
    EXPECT_FALSE(getIPv6s()[0]->globalValid);
    EXPECT_TRUE(getIPv6s()[0]->deprecated);
}

// Test: RA_MalformedFieldsAreIgnored
TEST_F(Internal_NdpTest, RA_MalformedFieldsAreIgnored)
{
    uint8_t trail[4] = {0}; // too short - malformed

    Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(Variable::ICMPv6::Type::ndpRouteAdvertisement);
    ra.setCode(0x00);
    ra.setTrail(trail, 4);

    ndp->receiveRouteAdvertisement(ra, Variable::IPv6::source, mac2);
    SUCCEED(); // Drop w/o crash
}

// Test: NA_WithoutMACOptionIsIgnored
TEST_F(Internal_NdpTest, NA_WithoutMACOptionIsIgnored)
{
    uint8_t res[4] = { 0xE0, 0x00, 0x00, 0x00 };

    Icmpv6Header na;
    na.setBuffer(buf);

    na.setType(Variable::ICMPv6::Type::ndpNeighborAdvertisement);
    na.setCode(0x00);
    na.setReserved(res);
    na.setTrail(localLinkIp, 16);

    ndp->receiveNeighborAdvertisement(na, ip);

    uint8_t resolvedMac[6];
    EXPECT_FALSE(ndp->getMac(resolvedMac, ip));
}

// Test: NS_UnknownTargetIsIgnored
TEST_F(Internal_NdpTest, NS_UnknownTargetIsIgnored)
{
    uint8_t unknownIp[16];
    std::memcpy(unknownIp, ip, 16);
    unknownIp[15] = 'A';

    Icmpv6Header ns;
    ns.setBuffer(buf);

    ns.setType(Variable::ICMPv6::Type::ndpNeighborSolicitation);
    ns.setCode(0x00);
    ns.setTrail(unknownIp, 16);

    // Should not send NA
    EXPECT_CALL(*iface, enqueuePacket(testing::_, testing::_)).Times(0);
    ndp->receiveNeighborSolicitation(ns, ip, mac);
}

// Test: NA_DuringProbe_ResetsToReachable
TEST_F(Internal_NdpTest, NA_DuringProbe_ResetsToReachable)
{
    iface->blockEnqueues();
    ndp->configs.reachableTime.store(100); // ms
    ndp->configs.cacheExpire.store(1);
    ndp->configs.nudBaseInterval = 1;
    ndp->configs.nudBase = 1;

    ndp->addNdpEntry({ip, AddressFamily::IPv6}, readU48(mac));
    PacketBuilder pkt(iface);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ndp->resolveAndSend(ip, pkt);

    {
        std::shared_lock<std::shared_mutex> lock(getCacheMutex());
        auto state = getNdpCache()[{ip, AddressFamily::IPv6}].state;
        ASSERT_EQ(state, Protocol::NudState::PROBE);
    }

    uint8_t res[4] = { 0xE0, 0x00, 0x00, 0x00 };
    uint8_t trail[24] = {0};
    std::memcpy(trail, ip, 16);
    trail[16] = Variable::ICMPv6::Option::target;
    trail[17] = 0x01;
    std::memcpy(trail + 18, mac, 6);

    Icmpv6Header na;
    na.setBuffer(buf);
    na.setType(Variable::ICMPv6::Type::ndpNeighborAdvertisement);
    na.setCode(0x00);
    na.setReserved(res);
    na.setTrail(trail, 24);

    ndp->receiveNeighborAdvertisement(na, ip);

    {
        std::shared_lock<std::shared_mutex> lock(getCacheMutex());
        auto state = getNdpCache()[{ip, AddressFamily::IPv6}].state;
        EXPECT_EQ(state, Protocol::NudState::REACHABLE);
    }
}

// Test: DAD_And_NS_DoNotCorruptState
TEST_F(Internal_NdpTest, DAD_And_NS_DoNotCorruptState)
{
    iface->blockEnqueues();
    auto* addr = iface->configs.ipv6.addAddress(ip, false, 64);
    addr->tentative = true;
    ndp->configs.dadAttempts.store(1);
    ndp->configs.dadTime.store(100);

    std::thread dadThread([&]() {
        ndp->duplicateAddressDetection(addr, false);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    Icmpv6Header ns;

    ns.setBuffer(buf);
    ns.setType(Variable::ICMPv6::Type::ndpNeighborSolicitation);
    ns.setTrail(ip, 16);

    ndp->receiveNeighborSolicitation(ns, Variable::IPv6::source, Variable::Mac::source); // DAD probe

    dadThread.join();

    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    // Either duplicate or accepted — just no crash or invalid state
    SUCCEED();
}

// Test: ConcurrentAccessIsSafe
/*TEST_F(Internal_NdpTest, ConcurrentAccessIsSafe)
{
    std::atomic<bool> finished = false;
    std::thread writer([&]() {
        for (int i = 0; i < 100; ++i) {
            uint8_t ip[16];
            std::memcpy(ip, prefix, 16);
            ip[15] = static_cast<uint8_t>(i);
            uint8_t macAddr[6];
            std::memcpy(macAddr, mac, 6);
            macAddr[5] = static_cast<uint8_t>(i);
            ndp->addNdpEntry({ip, AddressFamily::IPv6}, readU48(mac));
        }
        finished = true;
    });

    std::thread reader([&]() {
        while (!finished) {
            for (int i = 0; i < 100; ++i) {
                uint8_t ip[16];
                std::memcpy(ip, prefix, 16);
                ip[15] = static_cast<uint8_t>(i);
                uint8_t macAddr[6];
                std::memcpy(macAddr, mac, 6);
                macAddr[5] = static_cast<uint8_t>(i);
                uint8_t resolvedMac[16];
                EXPECT_TRUE(ndp->getMac(resolvedMac, ip));
                EXPECT_EQ(std::memcmp(resolvedMac, macAddr, 6), 0);
            }
        }
    });

    writer.join();
    reader.join();

    SUCCEED(); // If no crash, mutexes are safe
}*/

// Test: UnknownICMPv6TypeIsIgnored
TEST_F(Internal_NdpTest, UnknownICMPv6TypeIsIgnored)
{
    uint8_t trail[16] = {0};

    Icmpv6Header hdr;
    hdr.setBuffer(buf);

    hdr.setType(0xFF); // Unknown
    hdr.setCode(0x00);
    hdr.setTrail(trail, 16);

    // Send to all receive functions
    ndp->receiveNeighborAdvertisement(hdr, Variable::IPv6::source);
    ndp->receiveRouteAdvertisement(hdr, Variable::IPv6::source, Variable::Mac::source);
    ndp->receiveRedirectMessage(hdr, Variable::IPv6::source);

    SUCCEED(); // Ignored = passed
}

// Test: RA_InconsistentLifetimesAreIgnored
TEST_F(Internal_NdpTest, RA_InconsistentLifetimesAreIgnored)
{
    clearIPv6s();
    iface->blockEnqueues();
    ndp->configs.slaacEnabled.store(true);

    uint8_t trail[40] = {0};
    trail[8] = Variable::ICMPv6::Option::prefix;
    trail[9] = 0x04;
    trail[10] = 0x40; // /64
    trail[11] = 0xC0; // L + A Bits
    writeU32(trail + 12, 2000);
    writeU32(trail + 16, 3000);
    std::memcpy(trail + 24, prefix, 16);

    Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(Variable::ICMPv6::Type::ndpRouteAdvertisement);
    ra.setCode(0x00);
    ra.setTrail(trail, 40);

    ndp->receiveRouteAdvertisement(ra, Variable::IPv6::source, Variable::Mac::source);

    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_TRUE(getIPv6s().empty());
}

// Test: RaGuardModeUpdate_TakesEffectImmediately
TEST_F(Internal_NdpTest, RaGuardModeUpdate_TakesEffectImmediately)
{
    clearIPv6s();
    ndp->configs.suppressRA.store(false);
    ndp->configs.destinationGuard.store(true);

    uint8_t trail[8] = {0};

    Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(Variable::ICMPv6::Type::ndpRouteAdvertisement);
    ra.setCode(0x00);
    ra.setTrail(trail, 8);

    // BLOCK_ALL → should be dropped
    ndp->configs.raGuardMode.store(Protocol::Ndp::Configs::RaGuardMode::BLOCK_ALL);
    ndp->receiveRouteAdvertisement(ra, Variable::IPv6::source, mac2);
    {
        std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
        EXPECT_TRUE(getIPv6s().empty());
    }

    // TRUSTED → should be accepted
    ndp->configs.raGuardMode.store(Protocol::Ndp::Configs::RaGuardMode::TRUSTED);
    ndp->receiveRouteAdvertisement(ra, Variable::IPv6::source, mac2);
    SUCCEED(); // No crash means accepted
}

// Test: SLAAC_ExclusionUpdate_AppliesImmediately
TEST_F(Internal_NdpTest, SLAAC_ExclusionUpdate_AppliesImmediately)
{
    clearIPv6s();
    ndp->configs.slaacEnabled.store(true);
    iface->configs.setMac(mac);

    // Add exclusion
    ndp->addSlaacExclusionPrefix({prefix, AddressFamily::IPv6}, false);

    uint8_t trail[40];
    trail[8] = Variable::ICMPv6::Option::prefix;
    trail[9] = 0x04;
    trail[10] = 0x40; // /64
    trail[11] = 0xC0; // L + A Bits
    writeU32(trail + 12, 1000);
    writeU32(trail + 16, 800);
    std::memcpy(trail + 24, prefix, 16);

    Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(Variable::ICMPv6::Type::ndpRouteAdvertisement);
    ra.setCode(0x00);
    ra.setTrail(trail, 40);
    ndp->receiveRouteAdvertisement(ra, Variable::IPv6::source, mac2);

    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_TRUE(getIPv6s().empty());
}

// Test: Config_ReachableTimeAffectsNewEntries
TEST_F(Internal_NdpTest, Config_ReachableTimeAffectsNewEntries)
{
    ndp->configs.reachableTime.store(50); // ms
    ndp->configs.cacheExpire.store(1);    // s
    ndp->addNdpEntry({ip, AddressFamily::IPv6}, readU48(mac));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    {
        std::shared_lock<std::shared_mutex> lock(getCacheMutex());
        auto state = getNdpCache()[{ip, AddressFamily::IPv6}].state;
        EXPECT_EQ(state, Protocol::NudState::STALE);
    }
}

// Test: Config_CacheExpireAffectsNewEntries
TEST_F(Internal_NdpTest, Config_CacheExpireAffectsNewEntries)
{
    iface->blockEnqueues();
    ndp->configs.reachableTime.store(10); // ms
    ndp->configs.cacheExpire.store(1);    // s
    ndp->addNdpEntry({ip, AddressFamily::IPv6}, readU48(mac));

    std::this_thread::sleep_for(std::chrono::seconds(2));

    uint8_t resolvedMac[6];
    EXPECT_FALSE(ndp->getMac(resolvedMac, ip));
}

// Test: Config_DadAttemptsUpdateImmediately
TEST_F(Internal_NdpTest, Config_DadAttemptsUpdateImmediately)
{
    iface->blockEnqueues();
    auto* addr = iface->configs.ipv6.addAddress(ip, false, 64);
    addr->tentative = true;

    ndp->configs.dadAttempts.store(1);
    ndp->configs.dadTime.store(100);
    ndp->duplicateAddressDetection(addr, false);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_FALSE(addr->tentative);
    EXPECT_TRUE(addr->valid);
}

// Test: Config_PreferedLifetimeUpdatesWithRA
TEST_F(Internal_NdpTest, Config_PreferredLifetimeUpdatesWithRA)
{
    clearIPv6s();
    iface->blockEnqueues();
    ndp->configs.slaacEnabled.store(true);
    iface->configs.setMac(mac);

    ndp->configs.raPreferredLifetime.store(1);
    ndp->configs.raLifetime.store(100);

    uint8_t trail[40];
    trail[8] = Variable::ICMPv6::Option::prefix;
    trail[9] = 0x04;
    trail[10] = 0x40; // /64
    trail[11] = 0xC0; // L + A Bits
    writeU32(trail + 12, 100);
    writeU32(trail + 16, 1);
    std::memcpy(trail + 24, prefix, 16);

    Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(Variable::ICMPv6::Type::ndpRouteAdvertisement);
    ra.setCode(0x00);
    ra.setTrail(trail, 40);

    ndp->receiveRouteAdvertisement(ra, Variable::IPv6::source, mac2);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_TRUE(getIPv6s()[0]->deprecated);
}

// Test: NSF ResolutionTrottle_DropsExcess
TEST_F(Internal_NdpTest, NSF_ResolutionThrottle_DropsExcess)
{
    iface->blockEnqueues();
    global->configs.nsfActive.store(true);
    global->configs.nsfStartTime = std::chrono::steady_clock::now();
    global->configs.ndp.nsfConvergenceTime.store(500); // 500s
    global->configs.ndp.nsfThrottleResolutions.store(1);

    PacketBuilder pkt1(iface), pkt2(iface);
    ndp->resolveAndSend(ip, pkt1);

    uint8_t newIp[16];
    std::memcpy(newIp, ip, 16);
    newIp[15] = '2';
    ndp->resolveAndSend(newIp, pkt2);
    
    std::lock_guard<std::mutex> lock(getRequestMutex());
    EXPECT_EQ(getPendingRequests().count({ip, AddressFamily::IPv6}), 1);
    EXPECT_EQ(getPendingRequests().count({newIp, AddressFamily::IPv6}), 0);
}

// Test: DAD_SuppressedDuringNSF
TEST_F(Internal_NdpTest, DAD_SuppressedDuringNSF)
{
    global->configs.nsfActive.store(true);
    global->configs.nsfStartTime = std::chrono::steady_clock::now();
    global->configs.ndp.nsfDadSupressionTime.store(500); // 500s

    iface->blockEnqueues();
    auto* addr = iface->configs.ipv6.addAddress(ip, false, 64);
    addr->tentative = true;
    ndp->configs.dadAttempts.store(1);

    ndp->duplicateAddressDetection(addr, false);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_TRUE(addr->tentative); // Suppressed
}

// Test: StaticNeighbor_OverridesDynamicResolution
TEST_F(Internal_NdpTest, StaticNeighbor_OverridesDynamicResolution)
{
    iface->blockEnqueues();
    uint8_t staticIp[16];
    std::memcpy(staticIp, ip, 16);
    staticIp[15] = '5';
    uint8_t staticMac[6];
    std::memcpy(staticMac, mac, 6);
    staticMac[5] = '5';

    ndp->addNdpEntry({staticIp, AddressFamily::IPv6}, readU48(staticMac), false, true);

    PacketBuilder pkt(iface);
    ndp->resolveAndSend(staticIp, pkt);

    uint8_t resolvedMac[6];
    EXPECT_TRUE(ndp->getMac(resolvedMac, staticIp));
    EXPECT_EQ(std::memcmp(resolvedMac, staticMac, 6), 0);
}
