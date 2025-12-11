// AprTest.cpptest.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory.h>

// Include the ARP implementation and mock classes
#include <Arp.h>
#include <MockInterface.hpp>
#include <Interface.h>
#include <Global.h>
#include <PacketBuilder.hpp>

using namespace Protocol;

class Internal_ArpTest : public ::testing::Test
{
protected:

    Global* global = nullptr;

    uint32_t ip = 0xc0a80110;
    uint32_t ip2 = 0xc0a80210;
    uint8_t mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t mac2[6] = {0x00, 0x22, 0x22, 0x22, 0x22, 0x22};
    alignas(64) uint8_t buf[128];

    uint32_t ifaceIp = 0xc0a80001;

    void SetUp() override
    {
        global = new Global({}, true, true);
        std::memset(buf, 0, 128);

        mockInterface = new MockInterface(*global);
        mockInterface->enableShutdown();
        mockInterface->enableIPs();
        mockInterface->setIPv4(ifaceIp, 24);
        arp = new Protocol::Arp(*mockInterface);
        mockInterface->arp = arp;
    }

    void TearDown() override
    {
        arp->shutdown();
        delete mockInterface;
        delete global;
    }

    // Member variables
    MockInterface* mockInterface;
    Protocol::Arp* arp;

    // Helper functions
    std::unordered_map<uint32_t, ArpCacheEntry>& getArpCache() {return arp->arpCache;}
    std::shared_mutex& getArpCacheMutex() {return arp->arpCacheMutex;}
    std::unordered_map<uint32_t, std::queue<PacketBuilder>>& getPacketQueuePerIp() {return arp->packetQueuePerIp;}
    std::mutex& getPacketQueueMutex() {return arp->packetQueueMutex;}
    std::unordered_set<uint32_t>& getPendingRequests() {return arp->pendingRequests;}
    std::mutex& getPendingRequestsMutex() {return arp->requestMutex;}
    std::mutex& getPendingReplyMutex() {return arp->replyStatusMutex;}
    std::unordered_map<uint32_t, std::atomic<bool>>& getReplyStatus() {return arp->replyStatus;}
    std::unordered_set<uint32_t> getPendingIncompletes() {return arp->pendingIncompletes;}
    uint32_t getIncompletes() {return arp->incompletes.load(std::memory_order_relaxed);}
};

// Helper to simulate a reply
void simulateArpReply(uint8_t* buffer, Arp& arp, const uint32_t& ip, const uint8_t* mac)
{
    ArpHeader arpReply;
    arpReply.setBuffer(buffer);
    arpReply.setSenderIpAddr(ip);
    arpReply.setSenderHwAddr(mac);
    arp.receiveReply(arpReply);
}

// Test: StaticEntry_ImmediateResolution
TEST_F(Internal_ArpTest, StaticEntry_ImmediateResolution)
{
    arp->addArpEntry(ip, readU48(mac), false, true); // isStatic = true

    uint8_t resMac[6];
    uint8_t addr[4];
    writeU32(addr, ip);
    ASSERT_TRUE(arp->getMac(resMac, addr));
    EXPECT_EQ(std::memcmp(resMac, mac, 6), 0);
}

// Test: StaticEntry_Overwrite
TEST_F(Internal_ArpTest, StaticEntry_Overwrite)
{
    arp->addArpEntry(ip, readU48(mac), false, true);
    uint8_t resolvedMac[6];
    uint8_t addr[4];
    writeU32(addr, ip);
    ASSERT_TRUE(arp->getMac(resolvedMac, addr));
    EXPECT_EQ(std::memcmp(resolvedMac, mac, 6), 0);

    arp->addArpEntry(ip, readU48(mac2), false, true);
    ASSERT_TRUE(arp->getMac(resolvedMac, addr));
    EXPECT_EQ(std::memcmp(resolvedMac, mac2, 6), 0);
}

// Test: StaticEntry_Removal
TEST_F(Internal_ArpTest, StaticEntry_Removal)
{
    arp->addArpEntry(ip, readU48(mac), false, true);

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    writeU32(addr, ip);
    ASSERT_TRUE(arp->getMac(resolvedMac, addr));

    arp->removeArpEntry(ip);

    ASSERT_FALSE(arp->getMac(resolvedMac, addr));
}

// Test: StaticProxyEntry_RepliesToArpRequest
TEST_F(Internal_ArpTest, StaticProxyEntry_RepliesToArpRequest)
{
    mockInterface->blockEnqueues();
    ArpHeader request;
    request.setBuffer(buf);

    request.setTargetIpAddr(ip);
    request.setSenderIpAddr(ifaceIp);

    arp->addArpEntry(ip, readU48(mac), true, true); // Proxy=true, static=true

    arp->receiveRequest(request, request.getSenderHwAddr());
}

// Test: StaticNonProxyEntry_DoesNotReplyToUnownedRequest
TEST_F(Internal_ArpTest, StaticNonProxyEntry_DoesNotReplyToUnownedRequest)
{
    ArpHeader request;
    request.setBuffer(buf);

    request.setTargetIpAddr(ip);
    request.setSenderIpAddr(ifaceIp);

    arp->addArpEntry(ip, readU48(mac), false, true); // proxy=false

    EXPECT_CALL(*mockInterface, enqueuePacket).Times(0); // No reply expected

    arp->receiveRequest(request, request.getSenderHwAddr());
}

// Test: StaticEntry_NeverExpiresOrProbes
TEST_F(Internal_ArpTest, StaticEntry_NeverExpiresOrProbes)
{
    arp->addArpEntry(ip, readU48(mac), false, true);

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    writeU32(addr, ip);
    ASSERT_TRUE(arp->getMac(resolvedMac, addr));

    std::this_thread::sleep_for(std::chrono::seconds(2));

    ASSERT_TRUE(arp->getMac(resolvedMac, addr));
    EXPECT_EQ(std::memcmp(resolvedMac, mac, 6), 0);
}

// Test: StaticEntries_MultipleUniqueEntries
TEST_F(Internal_ArpTest, StaticEntries_MultipleUniqueEntries)
{
    for (int i = 1; i <= 5; ++i)
    {
        uint8_t ip[4] = {(uint8_t)192, (uint8_t)168, (uint8_t)1, (uint8_t)(20 + i)};
        uint8_t mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, (uint8_t)i};
        arp->addArpEntry(readU32(ip), readU48(mac), false, true);

        uint8_t resolvedMac[6];
        ASSERT_TRUE(arp->getMac(resolvedMac, ip));
        EXPECT_EQ(std::memcmp(resolvedMac, mac, 6), 0);
    }
}

// Test: DynamicEntry_ExpiresAfterTimeout
TEST_F(Internal_ArpTest, DynamicEntry_ExpiresAfterTimeout)
{
    mockInterface->blockEnqueues();
    arp->configs.timeout.store(1);

    arp->addArpEntry(ip, readU48(mac), false, false); // isStatic = false

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    writeU32(addr, ip);
    ASSERT_TRUE(arp->getMac(mac, addr));

    std::this_thread::sleep_for(std::chrono::seconds(arp->configs.timeout + 1));

    ASSERT_FALSE(arp->getMac(resolvedMac, addr));
}

// Test: DynamicEntry_ReprobesWhenStale_IfIncompleteEnabled
TEST_F(Internal_ArpTest, DynamicEntry_ReprobesWhenStale_IfIncompleteEnabled)
{
    arp->configs.timeout.store(1);

    global->configs.arp.incompleteEnabled.store(true);
    arp->addArpEntry(ip, readU48(mac), false, false);

    std::condition_variable cv;
    std::mutex cvMutex;
    bool sent = false;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .WillOnce([&](const PacketBuilder&, const uint8_t*) {
            std::lock_guard<std::mutex> lock(cvMutex);
            sent = true;
            cv.notify_one();
        });

    std::this_thread::sleep_for(std::chrono::seconds(arp->configs.timeout + 1));

    std::unique_lock<std::mutex> lock(cvMutex);
    EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return sent; }));
}

// Test: DynamicEntry_RemovedWhenStale_IfIncompleteDisabled
TEST_F(Internal_ArpTest, DynamicEntry_RemovedWhenStale_IfIncompleteDisabled)
{
    arp->configs.timeout.store(1);
    global->configs.arp.incompleteEnabled.store(false);
    arp->addArpEntry(ip, readU48(mac), false, false);

    std::this_thread::sleep_for(std::chrono::seconds(arp->configs.timeout + 1));

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    ASSERT_FALSE(arp->getMac(resolvedMac, addr));
}

// Test: DynamicEntry_UsesUpdatedTimeout
TEST_F(Internal_ArpTest, DynamicEntry_UsesUpdatedTimeout)
{
    mockInterface->blockEnqueues();
    arp->configs.timeout.store(1); // 1 second

    arp->addArpEntry(ip, readU48(mac), false, false);

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    writeU32(addr, ip);
    ASSERT_TRUE(arp->getMac(resolvedMac, addr));

    std::this_thread::sleep_for(std::chrono::seconds(2));

    ASSERT_FALSE(arp->getMac(resolvedMac, addr));
}

// Test: DynamicEntry_TimerIsCancelledWhenOverwritten
TEST_F(Internal_ArpTest, DynamicEntry_TimerIsCancelledWhenOverwritten)
{
    arp->addArpEntry(ip, readU48(mac), false, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    arp->addArpEntry(ip, readU48(mac2), false, false); // Overwrite

    // Wait just under 1 second (timeout default is longer)
    std::this_thread::sleep_for(std::chrono::seconds(2));

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    writeU32(addr, ip);
    ASSERT_TRUE(arp->getMac(resolvedMac, addr));
    EXPECT_EQ(std::memcmp(resolvedMac, mac2, 6), 0);
}

// Test: ResolveAndSend_CreatesIncompleteEntry
TEST_F(Internal_ArpTest, ResolveAndSend_CreatesIncompleteEntry)
{
    mockInterface->blockEnqueues();
    PacketBuilder pkt(mockInterface);

    uint8_t addr[4];
    arp->resolveAndSend(writeU32(addr, ip), pkt);

    std::shared_lock<std::shared_mutex> lock(getArpCacheMutex());
    auto& cache = getArpCache();
    ASSERT_TRUE(cache.contains(ip));
    EXPECT_EQ(cache.at(ip).status, ArpCacheStatus::INCOMPLETE);
}

// Test: IncompleteEntry_SendsRetriesUpToLimit
TEST_F(Internal_ArpTest, IncompleteEntry_SendsRetriesUpToLimit)
{
    PacketBuilder pkt(mockInterface);
    global->configs.arp.incompleteRetries.store(3);
    global->configs.arp.incompleteInterval.store(0); // immediate retries

    std::atomic<int> count = 0;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .WillRepeatedly([&](const PacketBuilder&, const uint8_t*) {
            count++;
        });

    uint8_t addr[4];
    arp->resolveAndSend(writeU32(addr, ip), pkt);

    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_GE(count.load(), 3);
}

// Test: IncompleteEntry_RemovedAfterMaxRetries
TEST_F(Internal_ArpTest, IncompleteEntry_RemovedAfterMaxRetries)
{
    mockInterface->blockEnqueues();
    PacketBuilder pkt(mockInterface);
    global->configs.arp.incompleteRetries.store(2);
    global->configs.arp.incompleteInterval.store(0);

    uint8_t addr[4];
    arp->resolveAndSend(writeU32(addr, ip), pkt);
    std::this_thread::sleep_for(std::chrono::seconds(1));

    {
        std::shared_lock<std::shared_mutex> lock(getArpCacheMutex());
        EXPECT_FALSE(getArpCache().contains(ip));
    }

    {
        std::lock_guard<std::mutex> lock(getPendingRequestsMutex());
        EXPECT_FALSE(getPendingRequests().contains(ip));
    }
}

// Test: IncompleteEntry_LateReplyRestoresIfNotCleared
TEST_F(Internal_ArpTest, IncompleteEntry_LateReplyRestoresIfNotCleared)
{
    mockInterface->blockEnqueues();
    PacketBuilder pkt(mockInterface);

    global->configs.arp.incompleteRetries.store(10);
    global->configs.arp.incompleteInterval.store(1);

    uint8_t addr[4];
    arp->resolveAndSend(writeU32(addr, ip), pkt);

    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // before max retries

    simulateArpReply(buf, *arp, ip, mac);

    uint8_t resolvedMac[6];
    ASSERT_TRUE(arp->getMac(resolvedMac, addr));
    EXPECT_EQ(std::memcmp(resolvedMac, mac, 6), 0);
}

// Test: ReplyStatus_ResetBetweenAttempts
TEST_F(Internal_ArpTest, ReplyStatus_ResetBetweenAttempts)
{
    mockInterface->blockEnqueues();
    PacketBuilder pkt(mockInterface);

    uint8_t addr[4];
    arp->resolveAndSend(writeU32(addr, ip), pkt);

    {
        std::lock_guard<std::mutex> lock(getPendingReplyMutex());
        EXPECT_TRUE(getReplyStatus()[ip].load(std::memory_order_relaxed) == false);
    }

    // Simulate reply to mark it true
    simulateArpReply(buf, *arp, ip, mac);

    {
        std::lock_guard<std::mutex> lock(getPendingReplyMutex());
        EXPECT_TRUE(getReplyStatus().empty() || getReplyStatus()[ip].load(std::memory_order_relaxed) == true);
    }
}

// Test: IncompleteDisabled_SkipsRequestAndEntry
TEST_F(Internal_ArpTest, IncompleteDisabled_SkipsRequestAndEntry)
{
    PacketBuilder pkt(mockInterface);

    global->configs.arp.incompleteEnabled.store(false);

    EXPECT_CALL(*mockInterface, enqueuePacket).Times(0); // no requests sent

    uint8_t addr[4];
    arp->resolveAndSend(writeU32(addr, ip), pkt);

    {
        std::shared_lock<std::shared_mutex> lock(getArpCacheMutex());
        EXPECT_FALSE(getArpCache().contains(ip));
    }
}

// Test: PacketQueued_ForUnresolvedIP
TEST_F(Internal_ArpTest, PacketQueued_ForUnresolvedIP)
{
    mockInterface->blockEnqueues();
    PacketBuilder pkt(mockInterface);

    uint8_t addr[4];
    arp->resolveAndSend(writeU32(addr, ip), pkt);

    std::lock_guard<std::mutex> lock(getPacketQueueMutex());
    auto& queueMap = getPacketQueuePerIp();
    ASSERT_TRUE(queueMap.contains(ip));
    EXPECT_EQ(queueMap[ip].size(), 1);
}

// Test: PacketQueue_FlushesOnResolution
TEST_F(Internal_ArpTest, PacketQueue_FlushesOnResolution)
{
    PacketBuilder pkt(mockInterface);

    std::condition_variable cv;
    std::mutex cvMutex;
    bool flushed = false;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(::testing::AtLeast(1))
        .WillRepeatedly([&](const PacketBuilder& pkt, const uint8_t*) {
            for (int i = 0; i < pkt.getHeaderCount(); ++i)
            {
                if (pkt.getHeaders()[i].type == HeaderType::ARP)
                {
                    std::lock_guard<std::mutex> lock(cvMutex);
                    flushed = true;
                    cv.notify_one();
                    break;
                }
            }
        });

    uint8_t addr[4];
    arp->resolveAndSend(writeU32(addr, ip), pkt);
    simulateArpReply(buf, *arp, ip, mac);

    std::unique_lock<std::mutex> lock(cvMutex);
    EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return flushed; }));
}

// Test: PacketQueue_MultiplePacketsSentInOrder
TEST_F(Internal_ArpTest, PacketQueue_MultiplePacketsSentInOrder)
{
    PacketBuilder pkt1(mockInterface), pkt2(mockInterface);

    int count = 0;
    std::condition_variable cv;
    std::mutex cvMutex;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(::testing::AtLeast(1))
        .WillRepeatedly([&](const PacketBuilder& pkt, const uint8_t*) {
            for (int i = 0; i < pkt.getHeaderCount(); ++i)
            {
                if (pkt.getHeaders()[i].type == HeaderType::ARP)
                {
                    std::lock_guard<std::mutex> lock(cvMutex);
                    ++count;
                    break;
                }
            }
        });

    uint8_t addr[4];
    arp->resolveAndSend(writeU32(addr, ip), pkt1);
    arp->resolveAndSend(addr, pkt2);
    simulateArpReply(buf, *arp, ip, mac);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::unique_lock<std::mutex> lock(cvMutex);
    EXPECT_EQ(count, 1);
}

// Test: PacketQueue_RespectsQueueLimit
TEST_F(Internal_ArpTest, PacketQueue_RespectsQueueLimit)
{
    mockInterface->blockEnqueues();
    global->configs.arp.queueSize.store(2);
    PacketBuilder pkt(mockInterface);

    uint8_t addr[4];
    arp->resolveAndSend(writeU32(addr, ip), pkt);
    arp->resolveAndSend(addr, pkt);
    arp->resolveAndSend(addr, pkt); // should be dropped

    std::lock_guard<std::mutex> lock(getPacketQueueMutex());
    auto& queueMap = getPacketQueuePerIp();
    ASSERT_TRUE(queueMap.contains(ip));
    EXPECT_EQ(queueMap[ip].size(), 2);
}

// Test: PacketQueue_ClearedAfterFlush
TEST_F(Internal_ArpTest, PacketQueue_ClearedAfterFlush)
{
    PacketBuilder pkt(mockInterface);

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(::testing::AtLeast(1));

    uint8_t addr[4];
    arp->resolveAndSend(writeU32(addr, ip), pkt);
    simulateArpReply(buf, *arp, ip, mac);

    std::lock_guard<std::mutex> lock(getPacketQueueMutex());
    EXPECT_FALSE(getPacketQueuePerIp().contains(ip));
}

// Test: GarpAccepted_CreatesEntry
TEST_F(Internal_ArpTest, GarpAccepted_CreatesEntry)
{
    global->configs.arp.acceptGratiutous.store(true);

    ArpHeader garp;
    garp.setBuffer(buf);
    garp.setSenderIpAddr(ip);
    garp.setTargetIpAddr(ip);
    garp.setSenderHwAddr(mac);

    arp->receiveReply(garp);

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    ASSERT_TRUE(arp->getMac(resolvedMac, writeU32(addr, ip)));
    EXPECT_EQ(std::memcmp(resolvedMac, mac, 6), 0);
}

// Test: GarpRejected_IgnoredIfDisabled
TEST_F(Internal_ArpTest, GarpRejected_IgnoredIfDisabled)
{
    global->configs.arp.acceptGratiutous.store(false);

    ArpHeader garp;
    garp.setBuffer(buf);
    garp.setSenderIpAddr(ip);
    garp.setTargetIpAddr(ip);
    garp.setSenderHwAddr(mac);

    arp->receiveReply(garp);

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    ASSERT_FALSE(arp->getMac(resolvedMac, writeU32(addr, ip)));
}

// Test: GarpRefreshes_ExistingEntry
TEST_F(Internal_ArpTest, GarpRefreshes_ExistingEntry)
{
    global->configs.arp.acceptGratiutous.store(true);
    arp->addArpEntry(ip, readU48(mac), false, false); // dynamic entry

    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // let timer begin

    ArpHeader garp;
    garp.setBuffer(buf);
    garp.setSenderIpAddr(ip);
    garp.setTargetIpAddr(ip);
    garp.setSenderHwAddr(mac2);

    arp->receiveReply(garp);

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    ASSERT_TRUE(arp->getMac(resolvedMac, writeU32(addr, ip)));
    EXPECT_EQ(std::memcmp(resolvedMac, mac2, 6), 0);
}

// Test: GarpBlockedByStickyArp
TEST_F(Internal_ArpTest, GarpBlockedByStickyArp)
{
    global->configs.arp.acceptGratiutous.store(true);
    global->configs.arp.stickyArp.store(true);

    arp->addArpEntry(ip, readU48(mac), false, false); // dynamic entry

    ArpHeader garp;
    garp.setBuffer(buf);
    garp.setSenderIpAddr(ip);
    garp.setTargetIpAddr(ip);
    garp.setSenderHwAddr(mac2);

    arp->receiveReply(garp);

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    ASSERT_TRUE(arp->getMac(resolvedMac, writeU32(addr, ip)));
    ASSERT_EQ(std::memcmp(resolvedMac, mac, 6), 0);
}

// Test: StickyArp_PreventsOverwrite
TEST_F(Internal_ArpTest, StickyArp_PreventsOverwrite)
{
    global->configs.arp.stickyArp.store(true);
    global->configs.arp.acceptGratiutous.store(true);

    // Add initial dynamic entry
    arp->addArpEntry(ip, readU48(mac), false, false);

    // Simulate GARP with different MAC
    ArpHeader garp;
    garp.setBuffer(buf);
    garp.setSenderIpAddr(ip);
    garp.setTargetIpAddr(ip);
    garp.setSenderHwAddr(mac2);

    arp->receiveReply(garp);

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    ASSERT_TRUE(arp->getMac(resolvedMac, writeU32(addr, ip)));
    ASSERT_EQ(std::memcmp(resolvedMac, mac, 6), 0); // Should  not overwrite
}

// Test: StickyArp_Off_AllowsOverwrite
TEST_F(Internal_ArpTest, StickyArp_Off_AllowsOverwrite)
{
    global->configs.arp.stickyArp.store(false);
    global->configs.arp.acceptGratiutous.store(true);

    // Add initial dynamic entry
    arp->addArpEntry(ip, readU48(mac), false, false);

    // Simulate GARP with different MAC
    ArpHeader garp;
    garp.setBuffer(buf);
    garp.setSenderIpAddr(ip);
    garp.setTargetIpAddr(ip);
    garp.setSenderHwAddr(mac2);

    arp->receiveReply(garp);

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    ASSERT_TRUE(arp->getMac(resolvedMac, writeU32(addr, ip)));
    EXPECT_EQ(std::memcmp(resolvedMac, mac2, 6), 0); // Should overwrite
}

// Test: ProxyEntry_RepliesToRequest
TEST_F(Internal_ArpTest, ProxyEntry_RepliesToRequest)
{
    ArpHeader request;
    request.setBuffer(buf);
    request.setTargetIpAddr(ip);
    request.setTargetHwAddr(mac);
    request.setSenderIpAddr(ifaceIp);
    uint8_t macAddr[6];
    request.setSenderHwAddr(mockInterface->configs.getMac(macAddr));

    global->configs.arp.disableProxy.store(false);

    arp->addArpEntry(ip, readU48(mac), true, true); // proxy=true

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](PacketBuilder pkt, const uint8_t*) {
            for (int i = 0; i < pkt.getHeaderCount(); ++i)
            {
                auto header = pkt.getHeaders()[i];
                if (header.type == HeaderType::ETHERNET)
                {
                    EXPECT_EQ(std::memcmp(reinterpret_cast<EthernetHeaderRaw*>(header.buffer)->destinationMac, request.raw->senderHardwareAddress, 6), 0);
                    return;
                }
            }
            FAIL();
        }));

    arp->receiveRequest(request, request.raw->senderHardwareAddress);
}

// Test: NonProxyEntry_DoesNotReplyToRequest
TEST_F(Internal_ArpTest, NonProxyEntry_DoesNotReplyToRequest)
{
    ArpHeader request;
    request.setBuffer(buf);
    request.setTargetIpAddr(ip);
    request.setSenderHwAddr(mac);
    request.setSenderIpAddr(ifaceIp);
    uint8_t macAddr[6];
    request.setSenderHwAddr(mockInterface->configs.getMac(macAddr));

    global->configs.arp.disableProxy.store(false);

    arp->addArpEntry(ip, readU48(mac), false, true); // proxy=false

    EXPECT_CALL(*mockInterface, enqueuePacket).Times(0); // no reply expected

    arp->receiveRequest(request, request.raw->senderHardwareAddress);
}

// Test: ProxyAllowed_WhenNotDisabled
TEST_F(Internal_ArpTest, ProxyAllowed_WhenNotDisabled)
{
    global->configs.arp.disableProxy.store(false);

    ArpHeader request;
    request.setBuffer(buf);
    request.setTargetIpAddr(ip);
    request.setSenderIpAddr(ifaceIp);
    uint8_t macAddr[6];
    request.setSenderHwAddr(mockInterface->configs.getMac(macAddr));

    arp->addArpEntry(ip, readU48(mac), true, true); // proxy = true

    EXPECT_CALL(*mockInterface, enqueuePacket).Times(1);
    arp->receiveRequest(request, request.raw->senderHardwareAddress);
}

// Test: ProxyBlocked_WhenDisabled
TEST_F(Internal_ArpTest, ProxyBlocked_WhenDisabled)
{
    global->configs.arp.disableProxy.store(true);

    ArpHeader request;
    request.setBuffer(buf);
    request.setTargetIpAddr(ip);
    request.setSenderIpAddr(ifaceIp);
    uint8_t macAddr[6];
    request.setSenderHwAddr(mockInterface->configs.getMac(macAddr));

    arp->addArpEntry(ip, readU48(mac), true, true); // proxy = true

    EXPECT_CALL(*mockInterface, enqueuePacket).Times(0); // blocked by config
    arp->receiveRequest(request, request.raw->senderHardwareAddress);
}

// Test: UnknownIp_NoReply
TEST_F(Internal_ArpTest, UnknownIp_NoReply)
{
    ArpHeader request;
    request.setBuffer(buf);
    request.setTargetIpAddr(ip);
    request.setSenderIpAddr(ifaceIp);
    uint8_t macAddr[6];
    request.setSenderHwAddr(mockInterface->configs.getMac(macAddr));

    global->configs.arp.disableProxy.store(false);

    EXPECT_CALL(*mockInterface, enqueuePacket).Times(0); // no reply
    arp->receiveRequest(request, request.raw->senderHardwareAddress);
}

// Test: LocalIp_RepliesToRequest
TEST_F(Internal_ArpTest, LocalIp_RepliesToRequest)
{
    ArpHeader request;
    request.setBuffer(buf);
    request.setTargetIpAddr(ifaceIp);
    uint8_t macAddr[6];
    request.setTargetHwAddr(mockInterface->configs.getMac(macAddr));
    request.setSenderIpAddr(ip);
    request.setSenderHwAddr(mac);

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](PacketBuilder pkt, const uint8_t*) {
            for (int i = 0; i < pkt.getHeaderCount(); ++i)
            {
                auto header = pkt.getHeaders()[i];
                if (header.type == HeaderType::ETHERNET)
                {
                    EXPECT_EQ(std::memcmp(reinterpret_cast<EthernetHeaderRaw*>(header.buffer)->destinationMac, request.raw->senderHardwareAddress, 6), 0);
                    return;
                }
            }
            FAIL();
        }));

    arp->receiveRequest(request, request.raw->senderHardwareAddress);
}

// Test: ExceedIncompleteLimit_QueuesExcess
TEST_F(Internal_ArpTest, ExceedIncompleteLimit_QueuesExcess)
{
    mockInterface->blockEnqueues();
    global->configs.arp.incompleteResolveLimit.store(2);
    global->configs.arp.incompleteEnabled.store(true);

    PacketBuilder pkt(mockInterface);

    // Fill up the limit
    uint8_t ip1[4] = { 0xC0, 0xA8, 0x08, 0x01 };
    uint8_t ip2[4] = { 0xC0, 0xA8, 0x08, 0x02 };
    uint8_t ip3[4] = { 0xC0, 0xA8, 0x08, 0x03 };
    arp->resolveAndSend(ip1, pkt);
    arp->resolveAndSend(ip2, pkt);

    // This one should go into pendingIncompletes
    arp->resolveAndSend(ip3, pkt);

    std::shared_lock<std::shared_mutex> lock(getArpCacheMutex());
    EXPECT_TRUE(getArpCache().contains(readU32(ip1)));
    EXPECT_TRUE(getArpCache().contains(readU32(ip2)));
    EXPECT_FALSE(getArpCache().contains(readU32(ip3)));
    EXPECT_TRUE(getPendingIncompletes().contains(readU32(ip3)));
}

// Test: ResolveOne_TriggersPending
TEST_F(Internal_ArpTest, ResolveOne_TriggersPending)
{
    mockInterface->blockEnqueues();
    global->configs.arp.incompleteResolveLimit.store(1);
    global->configs.arp.incompleteEnabled.store(true);

    PacketBuilder pkt(mockInterface);

    uint8_t ip1[4] = { 0xC0, 0xA8, 0x08, 0x10 };
    uint8_t ip2[4] = { 0xC0, 0xA8, 0x08, 0x11 };
    uint8_t mac1[6] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };

    // First fills the limit
    arp->resolveAndSend(ip1, pkt);
    arp->resolveAndSend(ip2, pkt); // Should go pending

    // Confirm ip2 is pending
    EXPECT_TRUE(getPendingIncompletes().contains(readU32(ip2)));
    uint8_t resolvedMac[6];
    EXPECT_FALSE(arp->getMac(resolvedMac, ip2)); // not yet resolved

    // Resolve ip1
    simulateArpReply(buf, *arp, readU32(ip1), mac1);

    // Allow processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::shared_lock<std::shared_mutex> lock(getArpCacheMutex());
    EXPECT_TRUE(getArpCache().contains(readU32(ip2))); // should now be in cache
    EXPECT_FALSE(arp->getMac(resolvedMac, ip2)); // still incomplete
}

// Test: PendingQueueIgnoredWhenIncompleteDisabled
TEST_F(Internal_ArpTest, PendingQueueIgnoredWhenIncompleteDisabled)
{
    global->configs.arp.incompleteResolveLimit.store(1);
    global->configs.arp.incompleteEnabled.store(false);

    PacketBuilder pkt(mockInterface);

    uint8_t ip1[4] = { 0xC0, 0xA8, 0x08, 0x20 };
    uint8_t ip2[4] = { 0xC0, 0xA8, 0x08, 0x21 };
    uint8_t ip3[4] = { 0xC0, 0xA8, 0x08, 0x22 };

    arp->resolveAndSend(ip1, pkt);
    arp->resolveAndSend(ip2, pkt);
    arp->resolveAndSend(ip3, pkt);

    EXPECT_TRUE(getPendingIncompletes().empty());
    std::shared_lock<std::shared_mutex> lock(getArpCacheMutex());
    EXPECT_FALSE(getArpCache().contains(readU32(ip2)));
    EXPECT_FALSE(getArpCache().contains(readU32(ip3)));
}

// Test: EntryDropBeyondLimitWhenIncompleteDisabled
TEST_F(Internal_ArpTest, EntryDropBeyondLimitWhenIncompleteDisabled)
{
    global->configs.arp.incompleteEnabled.store(false);
    global->configs.arp.incompleteResolveLimit.store(1);

    PacketBuilder pkt(mockInterface);

    uint8_t addr[4];

    arp->resolveAndSend(writeU32(addr, ip), pkt);
    arp->resolveAndSend(writeU32(addr, ip2), pkt);

    std::shared_lock<std::shared_mutex> lock(getArpCacheMutex());
    EXPECT_FALSE(getArpCache().contains(ip)); // nothing should be created
    EXPECT_FALSE(getArpCache().contains(ip2));
}

// Test: Shutdown_ClearsAllState
TEST_F(Internal_ArpTest, Shutdown_ClearsAllState)
{
    mockInterface->blockEnqueues();
    PacketBuilder pkt(mockInterface);
    uint8_t addr[4] = { 0xC0, 0xA8, 0x09, 0x02 };
    arp->addArpEntry(ip, readU48(mac), false, false);
    arp->resolveAndSend(addr, pkt); // creates incomplete
    getPendingIncompletes().insert(0xC0A80903);

    arp->shutdown();

    {
        std::shared_lock<std::shared_mutex> lock(getArpCacheMutex());
        EXPECT_TRUE(getArpCache().empty());
    }

    {
        std::lock_guard<std::mutex> lock(getPendingRequestsMutex());
        EXPECT_TRUE(getPendingRequests().empty());
    }

    {
        std::lock_guard<std::mutex> lock(getPacketQueueMutex());
        EXPECT_TRUE(getPacketQueuePerIp().empty());
    }

    EXPECT_TRUE(getPendingIncompletes().empty());
    EXPECT_EQ(getIncompletes(), 0u);
}

// Test: Reinitiation_DoesNotRestorePreviousEntries
TEST_F(Internal_ArpTest, Reinitiation_DoesNotRestorePreviousEntries)
{
    arp->addArpEntry(ip, readU48(mac), false, false);

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    ASSERT_TRUE(arp->getMac(resolvedMac, writeU32(addr, ip)));

    arp->shutdown();

    // Simulate "restart"
    arp->initiateArp();

    EXPECT_FALSE(arp->getMac(resolvedMac, addr));
}

// Test: ThreadSafety_AddRemoveConcurrent
TEST_F(Internal_ArpTest, ThreadSafety_AddRemoveConcurrent)
{
    const int numThreads = 10;
    std::vector<std::thread> threads;

    for (int i = 0; i < numThreads; ++i)
    {
        threads.emplace_back([&, i] {
            uint8_t ip[4] = { 192, 168, 10, (uint8_t)i };
            uint8_t mac[6] = { 0x10, 0x20, 0x30, 0x40, 0x50, (uint8_t)i };
            arp->addArpEntry(readU32(ip), readU48(mac), false, false);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            arp->removeArpEntry(readU32(ip));
        });
    }

    for (auto& t : threads) t.join();

    std::shared_lock<std::shared_mutex> lock(getArpCacheMutex());
    EXPECT_TRUE(getArpCache().empty());
}

// Test: InvalidRequest_ZeroSenderIp_Ignored
TEST_F(Internal_ArpTest, InvalidRequest_ZeroSenderIp_Ignored)
{
    ArpHeader request;
    request.setBuffer(buf);
    request.setSenderIpAddr((uint32_t)0);
    request.setTargetIpAddr(ip);
    uint8_t macAddr[6];
    request.setSenderHwAddr(mockInterface->configs.getMac(macAddr));

    EXPECT_CALL(*mockInterface, enqueuePacket).Times(0);
    arp->receiveRequest(request, request.raw->senderHardwareAddress);
}

// Test: InvalidRequest_SenderEqualsTarget_Ignored
TEST_F(Internal_ArpTest, InvalidRequest_SenderEqualsTarget_Ignored)
{
    ArpHeader request;
    request.setBuffer(buf);
    request.setSenderIpAddr(ip);
    request.setTargetIpAddr(ip);
    uint8_t macAddr[6];
    request.setSenderHwAddr(mockInterface->configs.getMac(macAddr));

    EXPECT_CALL(*mockInterface, enqueuePacket).Times(0);
    arp->receiveRequest(request, request.raw->senderHardwareAddress);
}

// Test: ReplyWithInvalidMac_Ignored
TEST_F(Internal_ArpTest, ReplyWithInvalidMac_Ignored)
{
    uint8_t invalidMac[6];

    ArpHeader reply;
    reply.setBuffer(buf);
    reply.setSenderIpAddr(ip);
    reply.setTargetIpAddr(ip);
    reply.setSenderHwAddr(ETHERNET_MAC_BROADCAST);

    // MAC is broadcast — invalid in reply
    // In your implementation, this might not yet block it unless logic is added to reject non-unicast MACs
    // Assuming it is ignored:
    uint8_t addr[4];
    arp->receiveReply(reply);
    EXPECT_FALSE(arp->getMac(invalidMac, writeU32(addr, ip)));
}

// Test: ResolvedEntry_ReResolutionResetsTimer
TEST_F(Internal_ArpTest, ResolvedEntry_ReResolutionResetsTimer)
{
    arp->addArpEntry(ip, readU48(mac), false, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Save current expiry
    std::shared_lock<std::shared_mutex> lock(getArpCacheMutex());
    auto originalExpiry = getArpCache()[ip].expiryTime;
    lock.unlock();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Re-add same entry (should overwrite and reset expiry)
    arp->addArpEntry(ip, readU48(mac), false, false);

    std::shared_lock<std::shared_mutex> lock2(getArpCacheMutex());
    auto newExpiry = getArpCache()[ip].expiryTime;

    EXPECT_GT(newExpiry, originalExpiry);
}

// Test: UnsolicitedReply_CreatesOnlyIfGarp
TEST_F(Internal_ArpTest, UnsolicitedReply_CreatesOnlyIfGarp)
{
    global->configs.arp.acceptGratiutous.store(true);

    // Non-GARP reply
    ArpHeader reply1;
    reply1.setBuffer(buf);
    reply1.setSenderIpAddr(ip);
    reply1.setTargetIpAddr(ip2);
    reply1.setSenderHwAddr(mac);
    arp->receiveReply(reply1);

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    EXPECT_FALSE(arp->getMac(resolvedMac, writeU32(addr, ip)));

    // GARP reply
    ArpHeader reply2;
    std::memset(buf, 0, 128);
    reply2.setBuffer(buf);
    reply2.setSenderIpAddr(ip);
    reply2.setTargetIpAddr(ip); // GARP
    reply2.setSenderHwAddr(mac);
    arp->receiveReply(reply2);

    ASSERT_TRUE(arp->getMac(resolvedMac, addr));
    EXPECT_EQ(std::memcmp(resolvedMac, mac, 6), 0);
}

// Test: ReplyIgnored_IfArpNotRunning
TEST_F(Internal_ArpTest, ReplyIgnored_IfArpNotRunning)
{
    arp->shutdown();

    ArpHeader reply;
    reply.setBuffer(buf);
    reply.setSenderIpAddr(ip);
    reply.setTargetIpAddr(ip);
    reply.setSenderHwAddr(mac);

    EXPECT_NO_THROW(arp->receiveReply(reply));
    uint8_t resolvedMac[6];
    uint8_t addr[4];
    EXPECT_FALSE(arp->getMac(resolvedMac, writeU32(addr, ip)));
}
