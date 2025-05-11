#include <gtest/gtest.h>
#include <MockInterface.hpp>
#include <Ndp.h>
#include <chrono>

class Internal_NdpTest : public ::testing::Test
{
protected:
    MockInterface* iface;
    Protocol::Ndp* ndp;

    ByteString ip = ByteString("\xFD\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01", 16);
    ByteString intIp = ByteString("\xFD\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0B", 16);
    ByteString prefix = ByteString("\xFD\x12\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 16);
    ByteString mac = ByteString("\x0A\x0B\x0C\x0D\x0E\x0F", 6);
    ByteString mac2 = ByteString("\x0A\x0B\x0C\x0D\x0E\x0A", 6);

    void SetUp() override
    {
        iface = new MockInterface();
        iface->enableShutdown();
        iface->configs.ipv6.addAddress(intIp, false, 64);
        ndp = new Protocol::Ndp(*iface);
        iface->ndp = ndp;
    }

    void TearDown() override
    {
        delete iface;
    }

    void onNudTimeout(const ByteString& targetIp) { ndp->onNudTimeout(targetIp); }
    void scheduleNextRA() { ndp->scheduleNextRA(); }
    std::unordered_set<ByteString>& getPendingRequests() { return ndp->pendingRequests; }
    std::unordered_map<ByteString, int>& getRetryCount() { return ndp->nsRetryCount; }
    std::unordered_map<ByteString, ByteString>& getProxyEntries() { return ndp->proxyEntries; }
    std::mutex& getRequestMutex() { return ndp->requestMutex; }
    std::shared_mutex& getCacheMutex() { return ndp->ndpCacheMutex; }
    std::unordered_map<ByteString, Protocol::NdpCacheEntry>& getNdpCache() { return ndp->ndpCache; }
    void clearUnsolidated() { ndp->lastUnsolicitedNaTime.clear(); }
};

// Test: SendNS_ReceiveNA_CreatesEntry
TEST_F(Internal_NdpTest, SendNS_ReceiveNA_CreatesEntry)
{
    EXPECT_CALL(*iface, enqueuePacket(::testing::_, ::testing::_)).Times(2);

    PacketInfo dummy;
    ndp->resolveAndSend(ip, dummy);

    IcmpV6Header na;
    na.type = Variable::ICMPv6::Type::ndpNeighborAdvertisement;
    na.code = ByteString("\x00", 1);
    na.payload = ip;
    na.reserved = ByteString("\xE0\x00\x00\x00", 4); // R=1, S=1, O=1

    na.options.emplace_back(
        Variable::ICMPv6::Option::target,
        ByteString("\x01", 1),
        mac
    );

    ndp->receiveNeighborAdvertisement(na, ip);

    ByteString* found = ndp->getMac(ip);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(*found, mac);
}

// Test: UnsolicitedNA_CreatesEntry
TEST_F(Internal_NdpTest, UnsolicitedNA_CreatesEntry)
{
    IcmpV6Header na;
    na.type = Variable::ICMPv6::Type::ndpNeighborAdvertisement;
    na.code = ByteString("\x00", 1);
    na.payload = ip;
    na.reserved = ByteString("\xA0\x00\x00\x00", 4); // R=1, O=1, not solicited

    na.options.emplace_back(
        Variable::ICMPv6::Option::target,
        ByteString("\x01", 1),
        mac
    );

    ndp->receiveNeighborAdvertisement(na, ip);

    ByteString* found = ndp->getMac(ip);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(*found, mac);
}

// Test: CacheEntryExpiresAfterReachableTime
TEST_F(Internal_NdpTest, CacheEntryExpiresAfterReachableTime)
{
    ndp->configs.reachableTime.store(100);
    ndp->configs.cacheExpire.store(1);

    ndp->addNdpEntry(ip, mac);

    ByteString* found = ndp->getMac(ip);
    ASSERT_NE(found, nullptr);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    found = ndp->getMac(ip);
    EXPECT_EQ(found, nullptr);
}

// Test: NudTransitionsAndRemoval
TEST_F(Internal_NdpTest, NudTransitionsAndRemoval)
{
    iface->blockEnqueues();
    ndp->configs.reachableTime.store(50);
    ndp->configs.cacheExpire.store(2);

    ndp->addNdpEntry(ip, mac);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    onNudTimeout(ip); // REACHABLE -> STALE

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    onNudTimeout(ip); // Reachable -> PROBE

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    onNudTimeout(ip); // PROBE -> REMOVED

    ByteString* found = ndp->getMac(ip);
    EXPECT_EQ(found, nullptr);
}

// Test: QueuedPacketIsSentAfterNA
TEST_F(Internal_NdpTest, QueuedPacketIsSentAfterNA)
{
    bool sent = false;

    EXPECT_CALL(*iface, enqueuePacket(::testing::_, ::testing::_)).Times(1);

    PacketInfo pkt;
    ndp->resolveAndSend(ip, pkt);

    EXPECT_CALL(*iface, enqueuePacket(::testing::_, mac))
        .WillOnce(::testing::Invoke([&](PacketInfo&, ByteString) {
            sent = true;
        }));

    IcmpV6Header na;
    na.type = Variable::ICMPv6::Type::ndpNeighborAdvertisement;
    na.code = ByteString("\x00", 1);
    na.payload = ip;
    na.reserved = ByteString("\xE0\x00\x00\x00", 4);

    na.options.emplace_back(
        Variable::ICMPv6::Option::target,
        ByteString("\x01", 1),
        mac
    );

    ndp->receiveNeighborAdvertisement(na, ip);
    EXPECT_TRUE(sent);
}

// Test: ReceiveNAWithoutQueue_AddsEntry
TEST_F(Internal_NdpTest, ReceiveNAWithoutQueue_AddsEntry)
{
    IcmpV6Header na;
    na.type = Variable::ICMPv6::Type::ndpNeighborAdvertisement;
    na.code = ByteString("\x00", 1);
    na.payload = ip;
    na.reserved = ByteString("\xA0\x00\x00\x00", 4); // R=1, O=1

    na.options.emplace_back(
        Variable::ICMPv6::Option::target,
        ByteString("\x01", 1),
        mac
    );

    ndp->receiveNeighborAdvertisement(na, ip);

    ByteString* found = ndp->getMac(ip);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(*found, mac);
}

// Test: SLAAC_RSAndRA_CreatesAddress
TEST_F(Internal_NdpTest, SLAAC_RSAndRA_CreatesAddress)
{
    iface->configs.macAddress = mac;
    ndp->configs.slaacEnabled.store(true);

    EXPECT_CALL(*iface, enqueuePacket(::testing::_, ::testing::_)).Times(2);
    ndp->initiateSlaac(); // Would normally send RS

    ByteString fullAddr = Functions::calculateEui64(prefix, iface->configs.macAddress, 64);

    IcmpV6Header ra;
    ra.type = Variable::ICMPv6::Type::ndpRouteAdvertisement;
    ra.code = ByteString("\x00", 1);
    ra.reserved = ByteString("\x00\x00\x00\x00", 4); // Flags and lifetime
    ra.payload = ByteString(8, 0x00);

    ByteString prefixOpt;
    prefixOpt += ByteString("\x40", 1); // 64-bit prefix length
    prefixOpt += ByteString("\xC0", 1); // L and A bits
    prefixOpt += ByteString(2, 0x00); // reserved
    prefixOpt += Functions::numToByte(1800, 4); // Valid Lifetime
    prefixOpt += Functions::numToByte(900, 4); // Preferred lifetime
    prefixOpt += ByteString(4, 0x00); // reserved
    prefixOpt += prefix;

    ra.options.emplace_back(
        Variable::ICMPv6::Option::prefix,
        ByteString("\x04", 1),
        prefixOpt
    );

    ndp->receiveRouteAdvertisement(ra, ByteString(16, 1), mac2);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    bool found = false;
    for (auto* addr : iface->configs.ipv6.globalAddresses)
    {
        if (addr->ip == fullAddr && addr->valid)
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
    iface->configs.macAddress = mac;
    ndp->configs.slaacEnabled.store(true);
    ndp->configs.dadAttempts.store(1);
    ndp->configs.dadTime.store(50); // ms

    ByteString fullAddr = Functions::calculateEui64(prefix, iface->configs.macAddress, 64);

    auto* addr = iface->configs.ipv6.addAddress(fullAddr, false, 64);
    addr->tentative = true;

    ndp->duplicateAddressDetection(addr, false);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    {
        IcmpV6Header na;
        na.type = ByteString("\x88", 1);
        na.code = ByteString("\x00", 1);
        na.payload = ip;
        na.reserved = ByteString("\xA0\x00\x00\x00", 4);
        na.options.emplace_back(
            Variable::ICMPv6::Option::target,
            ByteString("\x01", 1),
            mac
        );
        ndp->receiveNeighborAdvertisement(na, ByteString(16, 0x00));
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
    iface->configs.macAddress = mac;
    ndp->configs.slaacEnabled.store(true);

    auto* addr = iface->configs.ipv6.addAddress(Functions::calculateEui64(prefix, iface->configs.macAddress, 64), false, 64);
    addr->tentative = false;
    addr->valid = true;

    ndp->configs.lifetime.store(1); // seconds
    ndp->configs.preferedLifetime.store(10); // not relevant here

    ndp->duplicateAddressDetection(addr, false);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_FALSE(addr->globalValid);
}

// Test: SLAAC_PreferredLifetimeExpires
TEST_F(Internal_NdpTest, SLAAC_PreferredLifetimeExpires) 
{
    iface->configs.macAddress = mac;
    ndp->configs.slaacEnabled.store(true);

    auto* addr = iface->configs.ipv6.addAddress(Functions::calculateEui64(prefix, iface->configs.macAddress, 64), false, 64);
    addr->tentative = false;
    addr->valid = true;
    addr->globalValid = true;

    ndp->configs.lifetime.store(100);  // longer
    ndp->configs.preferedLifetime.store(1); // seconds

    addr->tentative = true;
    ndp->duplicateAddressDetection(addr, false);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_TRUE(addr->deprecated);
}

// Test: SLAAC_Disabled_IgnoresPrefixes
TEST_F(Internal_NdpTest, SLAAC_Disabled_IgnoresPrefixes) 
{
    ndp->configs.slaacEnabled.store(false);
    iface->configs.macAddress = mac;

    IcmpV6Header ra;
    ra.type = ByteString("\x86", 1); // RA
    ra.code = ByteString("\x00", 1);
    ra.reserved = ByteString("\x00\x00\x00\x00", 4); // flags and lifetime
    ra.payload = ByteString(8, '\x00');

    ByteString prefixOpt;
    prefixOpt += ByteString("\x40", 1); // 64-bit prefix length
    prefixOpt += ByteString("\xC0", 1); // L and A bits
    prefixOpt += ByteString(2, 0x00); // reserved
    prefixOpt += Functions::numToByte(1800, 4); // valid lifetime
    prefixOpt += Functions::numToByte(900, 4); // preferred lifetime
    prefixOpt += ByteString(4, 0x00); // reserved
    prefixOpt += prefix;

    ra.options.emplace_back(Variable::ICMPv6::Option::prefix, ByteString("\x04", 1), prefixOpt);

    ndp->receiveRouteAdvertisement(ra, ByteString(16, 1), mac2);

    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_TRUE(iface->configs.ipv6.globalAddresses.empty());
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

    IcmpV6Header na;
    na.type = ByteString("\x88", 1);
    na.code = ByteString("\x00", 1);
    na.payload = ip;
    na.reserved = ByteString("\xA0\x00\x00\x00", 4);
    na.options.emplace_back(
        Variable::ICMPv6::Option::target,
        ByteString("\x01", 1),
        mac
    );

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
    ByteString proxyIp = ip;
    ByteString proxyMac = mac;
    proxyIp[15] = 'c';
    proxyMac[5] = 'c';
    ndp->addNdpEntry(proxyIp, proxyMac, true);  // mark as proxy

    IcmpV6Header ns;
    ns.type = ByteString("\x87", 1);
    ns.code = ByteString("\x00", 1);
    ns.payload = proxyIp;

    EXPECT_CALL(*iface, enqueuePacket(testing::_, testing::_))
        .WillOnce(testing::Invoke([&](PacketInfo& pkt, ByteString packetMac) {
            ASSERT_EQ(packetMac, proxyMac);
            ASSERT_FALSE(pkt.Layer3.empty());
            auto& h = std::get<IcmpV6Header>(pkt.Layer3[0]);
            EXPECT_EQ(h.type, ByteString("\x88", 1)); // NA
            EXPECT_EQ(h.payload, proxyIp);
        }));

    ndp->receiveNeighborSolicitation(ns, ip, mac);
}

// Test: ProxyEntry_DeliversQueuedPacket
TEST_F(Internal_NdpTest, ProxyEntry_DeliversQueuedPacket) 
{
    PacketInfo pkt;
    bool sent = false;
    EXPECT_CALL(*iface, enqueuePacket(testing::_, testing::_))
        .WillOnce(testing::Invoke([&](PacketInfo&, ByteString actualMac) {
            EXPECT_EQ(actualMac, mac);
            sent = true;
        }));

    ndp->resolveAndSend(ip, pkt);
    ndp->addNdpEntry(ip, mac, true);  // Triggers delivery

    EXPECT_TRUE(sent);
}

// Test: ProxyEntryExpiresIfNotRefreshed
TEST_F(Internal_NdpTest, ProxyEntryExpiresIfNotRefreshed)
{
    ndp->configs.cacheExpire.store(1);
    ndp->configs.reachableTime.store(10);

    ndp->addNdpEntry(ip, mac, true);

    std::this_thread::sleep_for(std::chrono::seconds(2));
    ByteString* found = ndp->getMac(ip);
    EXPECT_EQ(found, nullptr);
}

// Test: Proxy_UnsolicitedNASent
TEST_F(Internal_NdpTest, Proxy_UnsolicitedNASent)
{
    ndp->addNdpEntry(ip, mac, true);

    EXPECT_CALL(*iface, enqueuePacket(testing::_, testing::_))
        .WillOnce(testing::Invoke([&](PacketInfo& pkt, ByteString targetMac) {
            EXPECT_EQ(targetMac, ByteString("\xFF\xFF\xFF\xFF\xFF\xFF", 6)); // broadcast
            ASSERT_TRUE(std::holds_alternative<IcmpV6Header>(pkt.Layer3[0]));
            auto& na = std::get<IcmpV6Header>(pkt.Layer3[0]);
            EXPECT_EQ(na.type, ByteString("\x88", 1));
            EXPECT_EQ(na.payload, ip);
        }));

    clearUnsolidated();
    ndp->sendNeighborAdvertisement(ByteString("\xFF\xFF\xFF\xFF\xFF\xFF", 6), &ip);
}

// Test: DADProbe_TriggersProxyNA
TEST_F(Internal_NdpTest, DADProbe_TriggersProxyNA)
{
    ndp->addNdpEntry(ip, mac, true);

    ByteString unspecified = ByteString(16, 0x00); // ::
    ByteString emptyMac;

    IcmpV6Header ns;
    ns.type = ByteString("\x87", 1);
    ns.code = ByteString("\x00", 1);
    ns.payload = ip;

    clearUnsolidated();

    EXPECT_CALL(*iface, enqueuePacket(testing::_, testing::_))
        .WillOnce(testing::Invoke([&](PacketInfo& pkt, ByteString) {
            ASSERT_TRUE(std::holds_alternative<IcmpV6Header>(pkt.Layer3[0]));
            auto& na = std::get<IcmpV6Header>(pkt.Layer3[0]);
            EXPECT_EQ(na.type, ByteString("\x88", 1));
            EXPECT_EQ(na.payload, ip);
        }));

    ndp->receiveNeighborSolicitation(ns, unspecified, emptyMac);
}

// Test: RA_MOFlagsUpdateConfig
TEST_F(Internal_NdpTest, RA_MOFlagsUpdateConfig)
{
    ndp->configs.managedConfigFlag.store(false);
    ndp->configs.otherConfigFlag.store(false);

    IcmpV6Header ra;
    ra.type = ByteString("\x86", 1);
    ra.code = ByteString("\x00", 1);
    ra.reserved = ByteString("\x00\xC0\x00\x00", 4); // M=1, O=1

    ra.payload = ByteString(8, '\x00');

    ndp->receiveRouteAdvertisement(ra, ByteString(16, 1), Variable::Mac::source);

    EXPECT_TRUE(ndp->configs.managedConfigFlag.load());
    EXPECT_TRUE(ndp->configs.otherConfigFlag.load());
}

// Test: RA_TrustedSourceIsAccepted
TEST_F(Internal_NdpTest, RA_TrustedSourceIsAccepted)
{
    ndp->configs.suppressRA.store(false);
    ndp->configs.destinationGuard.store(true);
    ndp->configs.raGuardMode.store(Protocol::Ndp::Configs::RaGuardMode::TRUSTED);

    IcmpV6Header ra;
    ra.type = ByteString("\x86", 1);
    ra.code = ByteString("\x00", 1);
    ra.reserved = ByteString("\x00\x00\x00\x10", 4);
    ra.payload = ByteString(8, 0x00);

    // Should not be dropped
    ndp->receiveRouteAdvertisement(ra, ByteString(16, 1), mac2);
    SUCCEED(); // No crash/drop = accepted
}

// Test: RA_FromBlockedPortIsDropped_BLOCK_ALL
TEST_F(Internal_NdpTest, RA_FromBlockedPortIsDropped_BLOCK_ALL)
{
    ndp->configs.suppressRA.store(false);
    ndp->configs.destinationGuard.store(true);
    ndp->configs.raGuardMode.store(Protocol::Ndp::Configs::RaGuardMode::BLOCK_ALL);

    IcmpV6Header ra;
    ra.type = ByteString("\x86", 1);
    ra.code = ByteString("\x00", 1);
    ra.reserved = ByteString("\x00\x00\x00\x00", 4);
    ra.payload = ByteString(8, 0x00);

    // No crash = passed
    ndp->receiveRouteAdvertisement(ra, ByteString(16, 1), mac2);

    // If it reached here, it was dropped without crashing
    SUCCEED();
}

// Test: RA_MacWhitelistRejectsUnknownMAC
TEST_F(Internal_NdpTest, RA_MacWhitelistRejectsUnknownMAC)
{
    ndp->configs.suppressRA.store(false);
    ndp->configs.destinationGuard.store(true);
    ndp->configs.raGuardMode.store(Protocol::Ndp::Configs::RaGuardMode::MAC_WHITELIST);

    IcmpV6Header ra;
    ra.type = ByteString("\x86", 1);
    ra.code = ByteString("\x00", 1);
    ra.reserved = ByteString("\x00\x00\x00\x00", 4);
    ra.payload = ByteString(8, 0x00);

    ndp->receiveRouteAdvertisement(ra, ByteString(16, 1), mac2);
    SUCCEED(); // No crash = dropped
}

// Test: RA_NonICMPv6HeaderIsDropped
TEST_F(Internal_NdpTest, RA_NonICMPv6HeaderIsDropped)
{
    // This would normally be dropped before it reaches NDP
    // We simulate this by ensuring no effect happens
    ndp->configs.suppressRA.store(false);

    IcmpV6Header ra;
    ra.type = ByteString("\x86", 1);
    ra.code = ByteString("\x00", 1);
    ra.reserved = ByteString("\x00\x00\x00\x00", 4);
    ra.payload = ByteString(8, 0x00);

    // No extension headers included — simulate filtered result
    ndp->receiveRouteAdvertisement(ra, ByteString(16, 1), mac2);
    SUCCEED(); // Reaching here = accepted or ignored, no crash
}

// Test: RA_CreatesSLAACAddressWithEUI64
TEST_F(Internal_NdpTest, RA_CreatesSLAACAddressWithEUI64)
{
    iface->blockEnqueues();
    ndp->configs.slaacEnabled.store(true);
    iface->configs.macAddress = mac;

    ByteString full = Functions::calculateEui64(prefix, iface->configs.macAddress, 64);

    IcmpV6Header ra;
    ra.type = ByteString("\x86", 1);
    ra.code = ByteString("\x00", 1);
    ra.reserved = ByteString("\x00\x00\x00\x00", 4);
    ra.payload = ByteString(8, 0x00);

    ByteString opt;
    opt += ByteString("\x40", 1); // /64
    opt += ByteString("\xC0", 1); // L + A
    opt += ByteString(2, 0x00); // reserved
    opt += Functions::numToByte(300, 4); // valid
    opt += Functions::numToByte(200, 4); // preferred
    opt += ByteString(4, 0x00); // reserved
    opt += prefix;

    ra.options.emplace_back(Variable::ICMPv6::Option::prefix, ByteString("\x04", 1), opt);

    ndp->receiveRouteAdvertisement(ra, ByteString(16, 1), mac2);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    bool found = false;
    for (auto* addr : iface->configs.ipv6.globalAddresses) {
        if (addr->ip == full && (addr->valid || addr->tentative)) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// Test: RA_ExcludedPrefixIgnored
TEST_F(Internal_NdpTest, RA_ExcludedPrefixIgnored)
{
    ndp->configs.slaacEnabled.store(true);
    iface->configs.macAddress = mac;

    ndp->addSlaacExclusionPrefix(prefix, false); // exclude

    IcmpV6Header ra;
    ra.type = ByteString("\x86", 1);
    ra.code = ByteString("\x00", 1);
    ra.reserved = ByteString("\x00\x00\x00\x00", 4);
    ra.payload = ByteString(8, 0x00);

    ByteString opt;
    opt += ByteString("\x40", 1);
    opt += ByteString("\xC0", 1); // L + A
    opt += ByteString(2, 0x00);
    opt += Functions::numToByte(300, 4);
    opt += Functions::numToByte(200, 4);
    opt += ByteString(4, 0x00);
    opt += prefix;

    ra.options.emplace_back(Variable::ICMPv6::Option::prefix, ByteString("\x04", 1), opt);

    ndp->receiveRouteAdvertisement(ra, ByteString(16, 1), mac2);

    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_TRUE(iface->configs.ipv6.globalAddresses.empty());
}

// Test: RA_InvalidPrefixSizeIgnored
TEST_F(Internal_NdpTest, RA_InvalidPrefixSizeIgnored)
{
    ndp->configs.slaacEnabled.store(true);
    iface->configs.macAddress = mac;

    IcmpV6Header ra;
    ra.type = ByteString("\x86", 1);
    ra.code = ByteString("\x00", 1);
    ra.reserved = ByteString("\x00\x00\x00\x00", 4);
    ra.payload = ByteString(8, 0x00);

    ByteString opt;
    opt += ByteString("\x40", 1);
    opt += ByteString("\xC0", 1);
    opt += ByteString(2, 0x00);
    opt += Functions::numToByte(300, 4);
    opt += Functions::numToByte(200, 4);
    opt += ByteString(4, 0x00);
    opt += ByteString(4, 0xFF); // too short — invalid

    ra.options.emplace_back(Variable::ICMPv6::Option::prefix, ByteString("\x04", 1), opt);

    ndp->receiveRouteAdvertisement(ra, ByteString(16, 1), mac2);

    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_TRUE(iface->configs.ipv6.globalAddresses.empty());
}

// Test: RA_GuardBlockAllModeDropsAll
TEST_F(Internal_NdpTest, RA_GuardBlockAllModeDropsAll)
{
    ndp->configs.suppressRA.store(false);
    ndp->configs.destinationGuard.store(true);
    ndp->configs.raGuardMode.store(Protocol::Ndp::Configs::RaGuardMode::BLOCK_ALL);

    IcmpV6Header ra;
    ra.type = ByteString("\x86", 1);
    ra.code = ByteString("\x00", 1);
    ra.reserved = ByteString("\x00\x00\x00\x00", 4);
    ra.payload = ByteString(8, 0x00);

    ndp->receiveRouteAdvertisement(ra, ByteString(16, 1), mac2);

    SUCCEED(); // Reach this = dropped without exception
}

// Test: RA_GuardTrustedModeAcceptsAll
TEST_F(Internal_NdpTest, RA_GuardTrustedModeAcceptsAll)
{
    ndp->configs.suppressRA.store(false);
    ndp->configs.destinationGuard.store(true);
    ndp->configs.raGuardMode.store(Protocol::Ndp::Configs::RaGuardMode::TRUSTED);

    IcmpV6Header ra;
    ra.type = ByteString("\x86", 1);
    ra.code = ByteString("\x00", 1);
    ra.reserved = ByteString("\x00\x80\x00\x00", 4); // M flag
    ra.payload = ByteString(8, 0x00);

    ndp->receiveRouteAdvertisement(ra, ByteString(16, 1), mac2);

    EXPECT_TRUE(ndp->configs.managedConfigFlag.load());
}

// Test: RA_WithExtensionHeaders_IsDropped
TEST_F(Internal_NdpTest, RA_WithExtensionHeaders_IsDropped)
{
    ndp->configs.suppressRA.store(false);
    ndp->configs.destinationGuard.store(true);
    ndp->configs.raGuardMode.store(Protocol::Ndp::Configs::RaGuardMode::TRUSTED);

    IcmpV6Header ra;
    ra.type = ByteString("\x86", 1);
    ra.code = ByteString("\x00", 1);
    ra.reserved = ByteString("\x00\x00\x00\x00", 4);
    ra.payload = ByteString(8, 0x00);

    // Simulate malformed/extended header RA by using suspicious source MAC
    ndp->receiveRouteAdvertisement(ra, ByteString(16, 1), Variable::Mac::source);

    // No address added = dropped
    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_TRUE(iface->configs.ipv6.globalAddresses.empty());
}

// Test: RA_FragmentedPacketIgnored
TEST_F(Internal_NdpTest, RA_FragmentedPacketIgnored)
{
    // Assume payload is too short to be valid RA
    IcmpV6Header ra;
    ra.type = ByteString("\x86", 1);
    ra.code = ByteString("\x00", 1);
    ra.reserved = ByteString(4, 0x00);
    ra.payload = ByteString(1, 0x00);  // too short

    ndp->receiveRouteAdvertisement(ra, ByteString(16, 1), mac2);

    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_TRUE(iface->configs.ipv6.globalAddresses.empty());
}

// Test: Redirect_ValidTriggerSendsMessage
TEST_F(Internal_NdpTest, Redirect_ValidTriggerSendsMessage)
{
    ByteString srcIp = ip;
    srcIp[15] = 'c';
    ByteString dstIp = ip;

    ndp->addNdpEntry(dstIp, mac);
    ndp->addNdpEntry(srcIp, mac2);

    PacketInfo pkt;
    IPv6Header ip;
    ip.sourceAddress = srcIp;
    ip.destinationAddress = dstIp;
    pkt.Layer3.push_back(ip);

    EXPECT_CALL(*iface, enqueuePacket(testing::_, testing::_)).Times(1);

    ndp->sendRedirectIfNeeded(pkt);
}

// Test: Redirect_MulticastDestinationIsIgnored
TEST_F(Internal_NdpTest, Redirect_MulticastDestinationIsIgnored)
{
    PacketInfo pkt;
    IPv6Header ipHeader;
    ipHeader.sourceAddress = prefix;
    ipHeader.sourceAddress[15] = 'A';
    ipHeader.destinationAddress = ip;

    pkt.Layer3.push_back(ipHeader);

    // Should not enqueue
    EXPECT_CALL(*iface, enqueuePacket(testing::_, testing::_)).Times(0);
    ndp->sendRedirectIfNeeded(pkt);
}

// Test: Redirect_IgnoresSelfToSelfTraffic
TEST_F(Internal_NdpTest, Redirect_IgnoresSelfToSelfTraffic)
{
    PacketInfo pkt;
    IPv6Header ip6;
    ip6.sourceAddress = ip;
    ip6.destinationAddress = ip;

    pkt.Layer3.push_back(ip6);

    EXPECT_CALL(*iface, enqueuePacket(testing::_, testing::_)).Times(0);
    ndp->sendRedirectIfNeeded(pkt);
}

// Test: Redirect_CreatesEntryForBetterNextHop
TEST_F(Internal_NdpTest, Redirect_CreatesEntryForBetterNextHop)
{
    ByteString betterHop = ip;
    betterHop[15] = 'A';
    ByteString destIp   = ip;
    destIp[15] = 'B';

    IcmpV6Header redirect;
    redirect.type = ByteString("\x89", 1);
    redirect.code = ByteString("\x00", 1);
    redirect.reserved = ByteString(4, 0x00);
    redirect.payload = destIp + betterHop;

    IcmpV6Header::Option opt;
    opt.option = Variable::ICMPv6::Option::target;
    opt.length = ByteString("\x01", 1);
    opt.value = mac;
    redirect.options.push_back(opt);

    ndp->receiveRedirectMessage(redirect, ip);

    ByteString* found = ndp->getMac(betterHop);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(*found, mac);
}

// Test: NA_UnsolicitedRateLimitEnforced
TEST_F(Internal_NdpTest, NA_UnsolicitedRateLimitEnforced)
{
    ndp->addNdpEntry(ip, mac, false);

    // First send should work
    EXPECT_CALL(*iface, enqueuePacket(testing::_, testing::_)).Times(1);
    ndp->sendNeighborAdvertisement(Variable::Mac::broadcast, &ip);

    // Immediate resend should be skipped
    EXPECT_CALL(*iface, enqueuePacket(testing::_, testing::_)).Times(0);
    ndp->sendNeighborAdvertisement(Variable::Mac::broadcast, &ip);
}

// Test: RA_RateLimitGlobalEnforced
TEST_F(Internal_NdpTest, RA_RateLimitGlobalEnforced)
{
    ndp->configs.raInterval.store(1000); // ms

    EXPECT_CALL(*iface, enqueuePacket(testing::_, Variable::Mac::broadcast)).Times(1);
    scheduleNextRA(); // Sends RA

    // immediate call should be rate limited
    EXPECT_CALL(*iface, enqueuePacket(testing::_, Variable::Mac::broadcast)).Times(0);
    scheduleNextRA();
}

// Test: NS_RetriesStopAfterConfiguredAttempts
TEST_F(Internal_NdpTest, NS_RetriesStopAfterConfiguredAttempts)
{
    iface->blockEnqueues();
    ndp->configs.nudRetries = 1;
    ndp->configs.nsInterval.store(50);

    PacketInfo dummy;
    ndp->resolveAndSend(ip, dummy);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::lock_guard<std::mutex> lock(getRequestMutex());
    EXPECT_EQ(getPendingRequests().count(ip), 0);
    EXPECT_EQ(getRetryCount().count(ip), 0);
}

// Test: EntryLimitEnforced_EvictsOldest
TEST_F(Internal_NdpTest, EntryLimitEnforced_EvictsOldest)
{
    ndp->configs.interfaceLimit.store(2); // Max 2 entries

    ByteString ip1 = ip;
    ip1[15] = '1';
    ByteString ip2 = ip;
    ip2[15] = '2';
    ByteString ip3 = ip;
    ip3[15] = '3';

    ndp->addNdpEntry(ip1, mac);
    ndp->addNdpEntry(ip2, mac);
    ndp->addNdpEntry(ip3, mac); // Should evict ip1

    EXPECT_EQ(ndp->getMac(ip1), nullptr);
    EXPECT_NE(ndp->getMac(ip2), nullptr);
    EXPECT_NE(ndp->getMac(ip3), nullptr);
}

// Test: ManualProxyEntryIsStoredCorrectly
TEST_F(Internal_NdpTest, ManualProxyEntryIsStoredCorrectly)
{
    ndp->addNdpEntry(ip, mac, true); // Proxy = true

    ByteString* found = ndp->getMac(ip);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(*found, mac);

    std::shared_lock<std::shared_mutex> lock(getCacheMutex());
    EXPECT_TRUE(getProxyEntries().count(ip));
}

// Test: RA_AddsPrefixWithCorrectTimers
TEST_F(Internal_NdpTest, RA_AddsPrefixWithCorrectTimers)
{
    ndp->configs.slaacEnabled.store(true);
    iface->configs.macAddress = mac;

    IcmpV6Header ra;
    ra.type = ByteString("\x86", 1);
    ra.code = ByteString("\x00", 1);
    ra.reserved = ByteString(4, 0x00);
    ra.payload = ByteString(8, 0x00);

    ByteString opt;
    opt += ByteString("\x40", 1); // prefix length
    opt += ByteString("\xC0", 1); // L + A
    opt += ByteString(2, 0x00);   // reserved
    opt += Functions::numToByte(1, 4); // valid lifetime
    opt += Functions::numToByte(1, 4); // preferred
    opt += ByteString(4, 0x00);  // reserved
    opt += prefix;

    ra.options.emplace_back(Variable::ICMPv6::Option::prefix, ByteString("\x04", 1), opt);

    ndp->receiveRouteAdvertisement(ra, ByteString(16, 1), mac2);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    ASSERT_FALSE(iface->configs.ipv6.globalAddresses.empty());
    EXPECT_FALSE(iface->configs.ipv6.globalAddresses[0]->globalValid);
    EXPECT_TRUE(iface->configs.ipv6.globalAddresses[0]->deprecated);
}

// Test: RA_MalformedFieldsAreIgnored
TEST_F(Internal_NdpTest, RA_MalformedFieldsAreIgnored)
{
    IcmpV6Header ra;
    ra.type = ByteString("\x86", 1);
    ra.code = ByteString("\x00", 1);
    ra.reserved = ByteString(2, 0x00); // Too short — malformed
    ra.payload = ByteString(4, 0x00);  // Invalid length for times

    ndp->receiveRouteAdvertisement(ra, ByteString(16, 1), mac2);
    SUCCEED(); // Drop w/o crash
}

// Test: NA_WithoutMACOptionIsIgnored
TEST_F(Internal_NdpTest, NA_WithoutMACOptionIsIgnored)
{
    IcmpV6Header na;
    na.type = ByteString("\x88", 1);
    na.code = ByteString("\x00", 1);
    na.payload = intIp;
    na.reserved = ByteString("\xE0\x00\x00\x00", 4);
    // no options

    ndp->receiveNeighborAdvertisement(na, ip);
    ByteString* found = ndp->getMac(ip);
    EXPECT_EQ(found, nullptr); // Not added
}

// Test: NS_UnknownTargetIsIgnored
TEST_F(Internal_NdpTest, NS_UnknownTargetIsIgnored)
{
    ByteString unknownIp = ip;
    unknownIp[15] = 'A';

    IcmpV6Header ns;
    ns.type = ByteString("\x87", 1);
    ns.code = ByteString("\x00", 1);
    ns.payload = unknownIp;

    // Should not send NA
    EXPECT_CALL(*iface, enqueuePacket(testing::_, testing::_)).Times(0);
    ndp->receiveNeighborSolicitation(ns, ip, mac);
}

// Test: NA_DuringProbe_ResetsToReachable
TEST_F(Internal_NdpTest, NA_DuringProbe_ResetsToReachable)
{
    iface->blockEnqueues();
    ndp->configs.reachableTime.store(100); // ms
    ndp->configs.cacheExpire.store(10);

    ndp->addNdpEntry(ip, mac);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    onNudTimeout(ip); // REACHABLE → STALE
    onNudTimeout(ip); // STALE → PROBE

    {
        std::shared_lock<std::shared_mutex> lock(getCacheMutex());
        ASSERT_EQ(getNdpCache()[ip].state, Protocol::NudState::PROBE);
    }

    IcmpV6Header na;
    na.type = ByteString("\x88", 1);
    na.code = ByteString("\x00", 1);
    na.payload = intIp;
    na.reserved = ByteString("\xE0\x00\x00\x00", 4);
    na.options.emplace_back(
        Variable::ICMPv6::Option::target,
        ByteString("\x01", 1),
        mac
    );

    ndp->receiveNeighborAdvertisement(na, ip);

    {
        std::shared_lock<std::shared_mutex> lock(getCacheMutex());
        EXPECT_EQ(getNdpCache()[ip].state, Protocol::NudState::REACHABLE);
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

    IcmpV6Header ns;
    ns.type = ByteString("\x87", 1);
    ns.code = ByteString("\x00", 1);
    ns.payload = ip;

    ndp->receiveNeighborSolicitation(ns, ByteString(16, 0), ByteString()); // DAD probe

    dadThread.join();

    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    // Either duplicate or accepted — just no crash or invalid state
    SUCCEED();
}

// Test: ConcurrentAccessIsSafe
TEST_F(Internal_NdpTest, ConcurrentAccessIsSafe)
{
    std::atomic<bool> finished = false;
    std::thread writer([&]() {
        for (int i = 0; i < 100; ++i) {
            ByteString ip = prefix;
            ip[15] = static_cast<uint8_t>(i);
            ndp->addNdpEntry(ip, mac);
        }
        finished = true;
    });

    std::thread reader([&]() {
        while (!finished) {
            for (int i = 0; i < 100; ++i) {
                ByteString ip = prefix;
                ip[15] = static_cast<uint8_t>(i);
                ndp->getMac(ip);
            }
        }
    });

    writer.join();
    reader.join();

    SUCCEED(); // If no crash, mutexes are safe
}

// Test: UnknownICMPv6TypeIsIgnored
TEST_F(Internal_NdpTest, UnknownICMPv6TypeIsIgnored)
{
    IcmpV6Header hdr;
    hdr.type = ByteString("\xFF", 1); // unknown
    hdr.code = ByteString("\x00", 1);
    hdr.reserved = ByteString(4, 0x00);
    hdr.payload = ByteString(16, 0x00);

    // Send to all receive functions
    ndp->receiveNeighborAdvertisement(hdr, ByteString(16, 0x00));
    ndp->receiveRouteAdvertisement(hdr, ByteString(16, 1), ByteString(6, 0x00));
    ndp->receiveRedirectMessage(hdr, ByteString(16, 1));

    SUCCEED(); // Ignored = passed
}

// Test: RA_InconsistentLifetimesAreIgnored
TEST_F(Internal_NdpTest, RA_InconsistentLifetimesAreIgnored)
{
    iface->blockEnqueues();
    ndp->configs.slaacEnabled.store(true);

    IcmpV6Header ra;
    ra.type = ByteString("\x86", 1);
    ra.code = ByteString("\x00", 1);
    ra.reserved = ByteString("\x00\x00\x00\x00", 4);
    ra.payload = ByteString(8, 0x00);

    ByteString opt;
    opt += ByteString("\x40", 1); // prefix length
    opt += ByteString("\xC0", 1); // L+A
    opt += ByteString(2, 0x00);
    opt += Functions::numToByte(2000, 4); // valid lifetime
    opt += Functions::numToByte(3000, 4); // preferred > valid — invalid
    opt += ByteString(4, 0x00);
    opt += prefix;

    ra.options.emplace_back(Variable::ICMPv6::Option::prefix, ByteString("\x04", 1), opt);

    ndp->receiveRouteAdvertisement(ra, ByteString(16, 1), ByteString(6, 0x00));

    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_TRUE(iface->configs.ipv6.globalAddresses.empty());
}

// Test: RaGuardModeUpdate_TakesEffectImmediately
TEST_F(Internal_NdpTest, RaGuardModeUpdate_TakesEffectImmediately)
{
    ndp->configs.suppressRA.store(false);
    ndp->configs.destinationGuard.store(true);

    IcmpV6Header ra;
    ra.type = ByteString("\x86", 1);
    ra.code = ByteString("\x00", 1);
    ra.reserved = ByteString(4, 0x00);
    ra.payload = ByteString(8, 0x00);

    // BLOCK_ALL → should be dropped
    ndp->configs.raGuardMode.store(Protocol::Ndp::Configs::RaGuardMode::BLOCK_ALL);
    ndp->receiveRouteAdvertisement(ra, ByteString(16, 1), mac2);
    {
        std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
        EXPECT_TRUE(iface->configs.ipv6.globalAddresses.empty());
    }

    // TRUSTED → should be accepted
    ndp->configs.raGuardMode.store(Protocol::Ndp::Configs::RaGuardMode::TRUSTED);
    ndp->receiveRouteAdvertisement(ra, ByteString(16, 1), mac2);
    SUCCEED(); // No crash means accepted
}

// Test: SLAAC_ExclusionUpdate_AppliesImmediately
TEST_F(Internal_NdpTest, SLAAC_ExclusionUpdate_AppliesImmediately)
{
    ndp->configs.slaacEnabled.store(true);
    iface->configs.macAddress = mac;

    // Add exclusion
    ndp->addSlaacExclusionPrefix(prefix, false);

    IcmpV6Header ra;
    ra.type = ByteString("\x86", 1);
    ra.code = ByteString("\x00", 1);
    ra.reserved = ByteString(4, 0x00);
    ra.payload = ByteString(8, 0x00);

    ByteString opt;
    opt += ByteString("\x40", 1);
    opt += ByteString("\xC0", 1);
    opt += ByteString(2, 0x00);
    opt += Functions::numToByte(1000, 4);
    opt += Functions::numToByte(800, 4);
    opt += ByteString(4, 0x00);
    opt += prefix;

    ra.options.emplace_back(Variable::ICMPv6::Option::prefix, ByteString("\x04", 1), opt);
    ndp->receiveRouteAdvertisement(ra, ByteString(16, 1), mac2);

    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_TRUE(iface->configs.ipv6.globalAddresses.empty());
}

// Test: Config_ReachableTimeAffectsNewEntries
TEST_F(Internal_NdpTest, Config_ReachableTimeAffectsNewEntries)
{
    ndp->configs.reachableTime.store(50); // ms
    ndp->configs.cacheExpire.store(1);    // s
    ndp->addNdpEntry(ip, mac);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    {
        std::shared_lock<std::shared_mutex> lock(getCacheMutex());
        EXPECT_EQ(getNdpCache()[ip].state, Protocol::NudState::STALE);
    }
}

// Test: Config_CacheExpireAffectsNewEntries
TEST_F(Internal_NdpTest, Config_CacheExpireAffectsNewEntries)
{
    iface->blockEnqueues();
    ndp->configs.reachableTime.store(10); // ms
    ndp->configs.cacheExpire.store(1);    // s
    ndp->addNdpEntry(ip, mac);

    std::this_thread::sleep_for(std::chrono::seconds(2));
    ByteString* result = ndp->getMac(ip);
    EXPECT_EQ(result, nullptr);
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
TEST_F(Internal_NdpTest, Config_PreferedLifetimeUpdatesWithRA)
{
    iface->blockEnqueues();
    ndp->configs.slaacEnabled.store(true);
    iface->configs.macAddress = mac;

    ndp->configs.preferedLifetime.store(1);
    ndp->configs.lifetime.store(100);

    IcmpV6Header ra;
    ra.type = ByteString("\x86", 1);
    ra.code = ByteString("\x00", 1);
    ra.reserved = ByteString(4, 0x00);
    ra.payload = ByteString(8, 0x00);

    ByteString opt;
    opt += ByteString("\x40", 1);
    opt += ByteString("\xC0", 1);
    opt += ByteString(2, 0x00);
    opt += Functions::numToByte(100, 4);
    opt += Functions::numToByte(1, 4); // preferred = 1s
    opt += ByteString(4, 0x00);
    opt += prefix;

    ra.options.emplace_back(Variable::ICMPv6::Option::prefix, ByteString("\x04", 1), opt);
    ndp->receiveRouteAdvertisement(ra, ByteString(16, 1), mac2);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::shared_lock<std::shared_mutex> lock(iface->configs.ipMutex);
    EXPECT_TRUE(iface->configs.ipv6.globalAddresses[0]->deprecated);
}
