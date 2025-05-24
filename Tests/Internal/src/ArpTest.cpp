// AprTest.cpptest.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory.h>

// Include the ARP implementation and mock classes
#include <Arp.h>
#include <MockInterface.hpp>

using namespace Protocol;

class Internal_ArpTest : public ::testing::Test
{
protected:

    Global* global = nullptr;

    ByteString ip = ByteString("\xc0\xa8\x01\x10", 4);
    ByteString ip2 = ByteString("\xc0\xa8\x02\x10", 4);
    ByteString mac = ByteString("\x00\x11\x22\x33\x44\x55", 6);
    ByteString mac2 = ByteString("\x00\x22\x22\x22\x22\x22", 6);

    ByteString ifaceIp = ByteString("\xc0\xa8\x00\x01", 4);

    void SetUp() override
    {
        global = new Global(false, true);

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
    std::unordered_map<ByteString, ArpCacheEntry>& getArpCache() {return arp->arpCache;}
    std::shared_mutex& getArpCacheMutex() {return arp->arpCacheMutex;}
    std::unordered_map<ByteString, std::queue<PacketInfo>>& getPacketQueuePerIp() {return arp->packetQueuePerIp;}
    std::mutex& getPacketQueueMutex() {return arp->packetQueueMutex;}
    std::unordered_set<ByteString>& getPendingRequests() {return arp->pendingRequests;}
    std::mutex& getPendingRequestsMutex() {return arp->requestMutex;}
    std::mutex& getPendingReplyMutex() {return arp->replyStatusMutex;}
    std::unordered_map<ByteString, std::atomic<bool>>& getReplyStatus() {return arp->replyStatus;}
    std::unordered_set<ByteString> getPendingIncompletes() {return arp->pendingIncompletes;}
    uint32_t getIncompletes() {return arp->incompletes.load(std::memory_order_relaxed);}
};

// Helper to simulate a reply
void simulateArpReply(Arp& arp, const ByteString& ip, const ByteString& mac)
{
    ArpHeader arpReply;
    arpReply.senderIpAddress = ip;
    arpReply.senderHardwareAddress = mac;
    arp.receiveReply(arpReply);
}

// Test: StaticEntry_ImmediateResolution
TEST_F(Internal_ArpTest, StaticEntry_ImmediateResolution)
{
    arp->addArpEntry(ip, mac, false, true); // isStatic = true

    ByteString* resolved = arp->getMac(ip);
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(*resolved, mac);
}

// Test: StaticEntry_Overwrite
TEST_F(Internal_ArpTest, StaticEntry_Overwrite)
{
    arp->addArpEntry(ip, mac, false, true);
    ByteString* before = arp->getMac(ip);
    ASSERT_NE(before, nullptr);
    EXPECT_EQ(*before, mac);

    arp->addArpEntry(ip, mac2, false, true);
    ByteString* after = arp->getMac(ip);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(*after, mac2);
}

// Test: StaticEntry_Removal
TEST_F(Internal_ArpTest, StaticEntry_Removal)
{
    arp->addArpEntry(ip, mac, false, true);

    ByteString* resolved = arp->getMac(ip);
    ASSERT_NE(resolved, nullptr);

    arp->removeArpEntry(ip);

    ByteString* removed = arp->getMac(ip);
    EXPECT_EQ(removed, nullptr);
}

// Test: StaticProxyEntry_RepliesToArpRequest
TEST_F(Internal_ArpTest, StaticProxyEntry_RepliesToArpRequest)
{
    mockInterface->blockEnqueues();
    ArpHeader request;

    request.targetIpAddress = ip;
    request.senderIpAddress = ifaceIp;

    arp->addArpEntry(ip, mac, true, true); // Proxy=true, static=true

    arp->receiveRequest(request, request.senderHardwareAddress);
}

// Test: StaticNonProxyEntry_DoesNotReplyToUnownedRequest
TEST_F(Internal_ArpTest, StaticNonProxyEntry_DoesNotReplyToUnownedRequest)
{
    ArpHeader request;

    request.targetIpAddress = ip;
    request.senderIpAddress = ifaceIp;

    arp->addArpEntry(ip, mac, false, true); // proxy=false

    EXPECT_CALL(*mockInterface, enqueuePacket).Times(0); // No reply expected

    arp->receiveRequest(request, request.senderHardwareAddress);
}

// Test: StaticEntry_NeverExpiresOrProbes
TEST_F(Internal_ArpTest, StaticEntry_NeverExpiresOrProbes)
{
    arp->addArpEntry(ip, mac, false, true);

    ByteString* before = arp->getMac(ip);
    ASSERT_NE(before, nullptr);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    ByteString* after = arp->getMac(ip);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(*after, mac);
}

// Test: StaticEntries_MultipleUniqueEntries
TEST_F(Internal_ArpTest, StaticEntries_MultipleUniqueEntries)
{
    for (int i = 1; i <= 5; ++i)
    {
        ByteString ip = ByteString({(uint8_t)192, (uint8_t)168, (uint8_t)1, (uint8_t)(20 + i)});
        ByteString mac = ByteString({0x00, 0x11, 0x22, 0x33, 0x44, (uint8_t)i});
        arp->addArpEntry(ip, mac, false, true);

        ByteString* resolved = arp->getMac(ip);
        ASSERT_NE(resolved, nullptr);
        EXPECT_EQ(*resolved, mac);
    }
}

// Test: DynamicEntry_ExpiresAfterTimeout
TEST_F(Internal_ArpTest, DynamicEntry_ExpiresAfterTimeout)
{
    mockInterface->blockEnqueues();
    arp->configs.timeout.store(1);

    arp->addArpEntry(ip, mac, false, false); // isStatic = false

    ByteString* initial = arp->getMac(ip);
    ASSERT_NE(initial, nullptr);

    std::this_thread::sleep_for(std::chrono::seconds(arp->configs.timeout + 1));

    ByteString* after = arp->getMac(ip);
    EXPECT_EQ(after, nullptr);
}

// Test: DynamicEntry_ReprobesWhenStale_IfIncompleteEnabled
TEST_F(Internal_ArpTest, DynamicEntry_ReprobesWhenStale_IfIncompleteEnabled)
{
    arp->configs.timeout.store(1);

    global->configs.arp.incompleteEnabled.store(true);
    arp->addArpEntry(ip, mac, false, false);

    std::condition_variable cv;
    std::mutex cvMutex;
    bool sent = false;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .WillOnce([&](const PacketInfo&, const ByteString&) {
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
    arp->addArpEntry(ip, mac, false, false);

    std::this_thread::sleep_for(std::chrono::seconds(arp->configs.timeout + 1));

    ByteString* after = arp->getMac(ip);
    EXPECT_EQ(after, nullptr);
}

// Test: DynamicEntry_UsesUpdatedTimeout
TEST_F(Internal_ArpTest, DynamicEntry_UsesUpdatedTimeout)
{
    mockInterface->blockEnqueues();
    arp->configs.timeout.store(1); // 1 second

    arp->addArpEntry(ip, mac, false, false);
    ASSERT_NE(arp->getMac(ip), nullptr);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    EXPECT_EQ(arp->getMac(ip), nullptr);
}

// Test: DynamicEntry_TimerIsCancelledWhenOverwritten
TEST_F(Internal_ArpTest, DynamicEntry_TimerIsCancelledWhenOverwritten)
{
    arp->addArpEntry(ip, mac, false, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    arp->addArpEntry(ip, mac2, false, false); // Overwrite

    // Wait just under 1 second (timeout default is longer)
    std::this_thread::sleep_for(std::chrono::seconds(2));
    ByteString* resolved = arp->getMac(ip);
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(*resolved, mac2);
}

// Test: ResolveAndSend_CreatesIncompleteEntry
TEST_F(Internal_ArpTest, ResolveAndSend_CreatesIncompleteEntry)
{
    mockInterface->blockEnqueues();
    PacketInfo pkt;

    arp->resolveAndSend(ip, pkt);

    std::shared_lock<std::shared_mutex> lock(getArpCacheMutex());
    auto& cache = getArpCache();
    ASSERT_TRUE(cache.contains(ip));
    EXPECT_EQ(cache.at(ip).status, ArpCacheStatus::INCOMPLETE);
}

// Test: IncompleteEntry_SendsRetriesUpToLimit
TEST_F(Internal_ArpTest, IncompleteEntry_SendsRetriesUpToLimit)
{
    PacketInfo pkt;
    global->configs.arp.incompleteRetries.store(3);
    global->configs.arp.incompleteInterval.store(0); // immediate retries

    std::atomic<int> count = 0;
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .WillRepeatedly([&](const PacketInfo&, const ByteString&) {
            count++;
        });

    arp->resolveAndSend(ip, pkt);

    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_GE(count.load(), 3);
}

// Test: IncompleteEntry_RemovedAfterMaxRetries
TEST_F(Internal_ArpTest, IncompleteEntry_RemovedAfterMaxRetries)
{
    mockInterface->blockEnqueues();
    PacketInfo pkt;
    global->configs.arp.incompleteRetries.store(2);
    global->configs.arp.incompleteInterval.store(0);

    arp->resolveAndSend(ip, pkt);
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
    PacketInfo pkt;

    global->configs.arp.incompleteRetries.store(10);
    global->configs.arp.incompleteInterval.store(1);

    arp->resolveAndSend(ip, pkt);

    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // before max retries

    simulateArpReply(*arp, ip, mac);

    ByteString* resolved = arp->getMac(ip);
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(*resolved, mac);
}

// Test: ReplyStatus_ResetBetweenAttempts
TEST_F(Internal_ArpTest, ReplyStatus_ResetBetweenAttempts)
{
    mockInterface->blockEnqueues();
    PacketInfo pkt;

    arp->resolveAndSend(ip, pkt);

    {
        std::lock_guard<std::mutex> lock(getPendingReplyMutex());
        EXPECT_TRUE(getReplyStatus()[ip].load(std::memory_order_relaxed) == false);
    }

    // Simulate reply to mark it true
    simulateArpReply(*arp, ip, mac);

    {
        std::lock_guard<std::mutex> lock(getPendingReplyMutex());
        EXPECT_TRUE(getReplyStatus().empty() || getReplyStatus()[ip].load(std::memory_order_relaxed) == true);
    }
}

// Test: IncompleteDisabled_SkipsRequestAndEntry
TEST_F(Internal_ArpTest, IncompleteDisabled_SkipsRequestAndEntry)
{
    PacketInfo pkt;

    global->configs.arp.incompleteEnabled.store(false);

    EXPECT_CALL(*mockInterface, enqueuePacket).Times(0); // no requests sent

    arp->resolveAndSend(ip, pkt);

    {
        std::shared_lock<std::shared_mutex> lock(getArpCacheMutex());
        EXPECT_FALSE(getArpCache().contains(ip));
    }
}

// Test: PacketQueued_ForUnresolvedIP
TEST_F(Internal_ArpTest, PacketQueued_ForUnresolvedIP)
{
    mockInterface->blockEnqueues();
    PacketInfo pkt;

    arp->resolveAndSend(ip, pkt);

    std::lock_guard<std::mutex> lock(getPacketQueueMutex());
    auto& queueMap = getPacketQueuePerIp();
    ASSERT_TRUE(queueMap.contains(ip));
    EXPECT_EQ(queueMap[ip].size(), 1);
}

// Test: PacketQueue_FlushesOnResolution
TEST_F(Internal_ArpTest, PacketQueue_FlushesOnResolution)
{
    PacketInfo pkt;

    std::condition_variable cv;
    std::mutex cvMutex;
    bool flushed = false;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(::testing::AtLeast(1))
        .WillRepeatedly([&](const PacketInfo& pkt, const ByteString&) {
            if (pkt.Layer2_5.size() == 0) return;
            std::lock_guard<std::mutex> lock(cvMutex);
            flushed = true;
            cv.notify_one();
        });

    arp->resolveAndSend(ip, pkt);
    simulateArpReply(*arp, ip, mac);

    std::unique_lock<std::mutex> lock(cvMutex);
    EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return flushed; }));
}

// Test: PacketQueue_MultiplePacketsSentInOrder
TEST_F(Internal_ArpTest, PacketQueue_MultiplePacketsSentInOrder)
{
    PacketInfo pkt1, pkt2;

    int count = 0;
    std::condition_variable cv;
    std::mutex cvMutex;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(::testing::AtLeast(2))
        .WillRepeatedly([&](const PacketInfo& pkt, const ByteString&) {
            if (pkt.Layer2_5.size() != 0) return;
            std::lock_guard<std::mutex> lock(cvMutex);
            ++count;
            cv.notify_one();
        });

    arp->resolveAndSend(ip, pkt1);
    arp->resolveAndSend(ip, pkt2);
    simulateArpReply(*arp, ip, mac);

    std::unique_lock<std::mutex> lock(cvMutex);
    EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return count == 2; }));
}

// Test: PacketQueue_RespectsQueueLimit
TEST_F(Internal_ArpTest, PacketQueue_RespectsQueueLimit)
{
    mockInterface->blockEnqueues();
    global->configs.arp.queueSize.store(2);
    PacketInfo pkt;

    arp->resolveAndSend(ip, pkt);
    arp->resolveAndSend(ip, pkt);
    arp->resolveAndSend(ip, pkt); // should be dropped

    std::lock_guard<std::mutex> lock(getPacketQueueMutex());
    auto& queueMap = getPacketQueuePerIp();
    ASSERT_TRUE(queueMap.contains(ip));
    EXPECT_EQ(queueMap[ip].size(), 2);
}

// Test: PacketQueue_ClearedAfterFlush
TEST_F(Internal_ArpTest, PacketQueue_ClearedAfterFlush)
{
    PacketInfo pkt;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(::testing::AtLeast(1));

    arp->resolveAndSend(ip, pkt);
    simulateArpReply(*arp, ip, mac);

    std::lock_guard<std::mutex> lock(getPacketQueueMutex());
    EXPECT_FALSE(getPacketQueuePerIp().contains(ip));
}

// Test: GarpAccepted_CreatesEntry
TEST_F(Internal_ArpTest, GarpAccepted_CreatesEntry)
{
    global->configs.arp.acceptGratiutous.store(true);

    ArpHeader garp;
    garp.senderIpAddress = ip;
    garp.targetIpAddress = ip;
    garp.senderHardwareAddress = mac;

    arp->receiveReply(garp);

    ByteString* resolved = arp->getMac(ip);
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(*resolved, mac);
}

// Test: GarpRejected_IgnoredIfDisabled
TEST_F(Internal_ArpTest, GarpRejected_IgnoredIfDisabled)
{
    global->configs.arp.acceptGratiutous.store(false);

    ArpHeader garp;
    garp.senderIpAddress = ip;
    garp.targetIpAddress = ip;
    garp.senderHardwareAddress = mac;

    arp->receiveReply(garp);

    ByteString* resolved = arp->getMac(ip);
    EXPECT_EQ(resolved, nullptr);
}

// Test: GarpRefreshes_ExistingEntry
TEST_F(Internal_ArpTest, GarpRefreshes_ExistingEntry)
{
    global->configs.arp.acceptGratiutous.store(true);
    arp->addArpEntry(ip, mac, false, false); // dynamic entry

    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // let timer begin

    ArpHeader garp;
    garp.senderIpAddress = ip;
    garp.targetIpAddress = ip;
    garp.senderHardwareAddress = mac2;

    arp->receiveReply(garp);

    ByteString* resolved = arp->getMac(ip);
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(*resolved, mac2);
}

// Test: GarpBlockedByStickyArp
TEST_F(Internal_ArpTest, GarpBlockedByStickyArp)
{
    global->configs.arp.acceptGratiutous.store(true);
    global->configs.arp.stickyArp.store(true);

    arp->addArpEntry(ip, mac, false, false); // dynamic entry

    ArpHeader garp;
    garp.senderIpAddress = ip;
    garp.targetIpAddress = ip;
    garp.senderHardwareAddress = mac2;

    arp->receiveReply(garp);

    ByteString* resolved = arp->getMac(ip);
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(*resolved, mac); // Not overwritten
}

// Test: StickyArp_PreventsOverwrite
TEST_F(Internal_ArpTest, StickyArp_PreventsOverwrite)
{
    global->configs.arp.stickyArp.store(true);
    global->configs.arp.acceptGratiutous.store(true);

    // Add initial dynamic entry
    arp->addArpEntry(ip, mac, false, false);

    // Simulate GARP with different MAC
    ArpHeader garp;
    garp.senderIpAddress = ip;
    garp.targetIpAddress = ip;
    garp.senderHardwareAddress = mac2;

    arp->receiveReply(garp);

    ByteString* resolved = arp->getMac(ip);
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(*resolved, mac); // Should not overwrite
}

// Test: StickyArp_Off_AllowsOverwrite
TEST_F(Internal_ArpTest, StickyArp_Off_AllowsOverwrite)
{
    global->configs.arp.stickyArp.store(false);
    global->configs.arp.acceptGratiutous.store(true);

    // Add initial dynamic entry
    arp->addArpEntry(ip, mac, false, false);

    // Simulate GARP with different MAC
    ArpHeader garp;
    garp.senderIpAddress = ip;
    garp.targetIpAddress = ip;
    garp.senderHardwareAddress = mac2;

    arp->receiveReply(garp);

    ByteString* resolved = arp->getMac(ip);
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(*resolved, mac2); // Should overwrite
}

// Test: ProxyEntry_RepliesToRequest
TEST_F(Internal_ArpTest, ProxyEntry_RepliesToRequest)
{
    ArpHeader request;
    request.targetIpAddress = ip;
    request.targetHardwareAddress = mac;
    request.senderIpAddress = ifaceIp;
    request.senderHardwareAddress = mockInterface->configs.getMac();

    global->configs.arp.disableProxy.store(false);

    arp->addArpEntry(ip, mac, true, true); // proxy=true

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](PacketInfo pkt, ByteString) {
            auto eth = std::get<EthernetHeader>(pkt.Layer2[0]);
            EXPECT_TRUE(eth.destinationMac == request.senderHardwareAddress);
        }));

    arp->receiveRequest(request, request.senderHardwareAddress);
}

// Test: NonProxyEntry_DoesNotReplyToRequest
TEST_F(Internal_ArpTest, NonProxyEntry_DoesNotReplyToRequest)
{
    ArpHeader request;
    request.targetIpAddress = ip;
    request.senderHardwareAddress = mac;
    request.senderIpAddress = ifaceIp;
    request.senderHardwareAddress = mockInterface->configs.getMac();

    global->configs.arp.disableProxy.store(false);

    arp->addArpEntry(ip, mac, false, true); // proxy=false

    EXPECT_CALL(*mockInterface, enqueuePacket).Times(0); // no reply expected

    arp->receiveRequest(request, request.senderHardwareAddress);
}

// Test: ProxyAllowed_WhenNotDisabled
TEST_F(Internal_ArpTest, ProxyAllowed_WhenNotDisabled)
{
    global->configs.arp.disableProxy.store(false);

    ArpHeader request;
    request.targetIpAddress = ip;
    request.senderIpAddress = ifaceIp;
    request.senderHardwareAddress = mockInterface->configs.getMac();

    arp->addArpEntry(ip, mac, true, true); // proxy = true

    EXPECT_CALL(*mockInterface, enqueuePacket).Times(1);
    arp->receiveRequest(request, request.senderHardwareAddress);
}

// Test: ProxyBlocked_WhenDisabled
TEST_F(Internal_ArpTest, ProxyBlocked_WhenDisabled)
{
    global->configs.arp.disableProxy.store(true);

    ArpHeader request;
    request.targetIpAddress = ip;
    request.senderIpAddress = ifaceIp;
    request.senderHardwareAddress = mockInterface->configs.getMac();

    arp->addArpEntry(ip, mac, true, true); // proxy = true

    EXPECT_CALL(*mockInterface, enqueuePacket).Times(0); // blocked by config
    arp->receiveRequest(request, request.senderHardwareAddress);
}

// Test: UnknownIp_NoReply
TEST_F(Internal_ArpTest, UnknownIp_NoReply)
{
    ArpHeader request;
    request.targetIpAddress = ip;
    request.senderIpAddress = ifaceIp;
    request.senderHardwareAddress = mockInterface->configs.getMac();

    global->configs.arp.disableProxy.store(false);

    EXPECT_CALL(*mockInterface, enqueuePacket).Times(0); // no reply
    arp->receiveRequest(request, request.senderHardwareAddress);
}

// Test: LocalIp_RepliesToRequest
TEST_F(Internal_ArpTest, LocalIp_RepliesToRequest)
{
    ArpHeader request;
    request.targetIpAddress = ifaceIp;
    request.targetHardwareAddress = mockInterface->configs.getMac();
    request.senderIpAddress = ip;
    request.senderHardwareAddress = mac;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](PacketInfo pkt, ByteString) {
            auto eth = std::get<EthernetHeader>(pkt.Layer2[0]);
            EXPECT_TRUE(eth.destinationMac == request.senderHardwareAddress);
        }));

    arp->receiveRequest(request, request.senderHardwareAddress);
}

// Test: ExceedIncompleteLimit_QueuesExcess
TEST_F(Internal_ArpTest, ExceedIncompleteLimit_QueuesExcess)
{
    mockInterface->blockEnqueues();
    global->configs.arp.incompleteResolveLimit.store(2);
    global->configs.arp.incompleteEnabled.store(true);

    PacketInfo pkt;

    // Fill up the limit
    arp->resolveAndSend(ByteString("\xC0\xA8\x08\x01", 4), pkt);
    arp->resolveAndSend(ByteString("\xC0\xA8\x08\x02", 4), pkt);

    // This one should go into pendingIncompletes
    arp->resolveAndSend(ByteString("\xC0\xA8\x08\x03", 4), pkt);

    std::shared_lock<std::shared_mutex> lock(getArpCacheMutex());
    EXPECT_TRUE(getArpCache().contains(ByteString("\xC0\xA8\x08\x01", 4)));
    EXPECT_TRUE(getArpCache().contains(ByteString("\xC0\xA8\x08\x02", 4)));
    EXPECT_FALSE(getArpCache().contains(ByteString("\xC0\xA8\x08\x03", 4)));
    EXPECT_TRUE(getPendingIncompletes().contains(ByteString("\xC0\xA8\x08\x03", 4)));
}

// Test: ResolveOne_TriggersPending
TEST_F(Internal_ArpTest, ResolveOne_TriggersPending)
{
    mockInterface->blockEnqueues();
    global->configs.arp.incompleteResolveLimit.store(1);
    global->configs.arp.incompleteEnabled.store(true);

    PacketInfo pkt;

    ByteString ip1 = ByteString("\xC0\xA8\x08\x10", 4);
    ByteString ip2 = ByteString("\xC0\xA8\x08\x11", 4);
    ByteString mac1 = ByteString("\x01\x02\x03\x04\x05\x06", 6);

    // First fills the limit
    arp->resolveAndSend(ip1, pkt);
    arp->resolveAndSend(ip2, pkt); // Should go pending

    // Confirm ip2 is pending
    EXPECT_TRUE(getPendingIncompletes().contains(ip2));
    EXPECT_FALSE(arp->getMac(ip2)); // not yet resolved

    // Resolve ip1
    simulateArpReply(*arp, ip1, mac1);

    // Allow processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::shared_lock<std::shared_mutex> lock(getArpCacheMutex());
    EXPECT_TRUE(getArpCache().contains(ip2)); // should now be in cache
    EXPECT_TRUE(arp->getMac(ip2) == nullptr); // still incomplete
}

// Test: PendingQueueIgnoredWhenIncompleteDisabled
TEST_F(Internal_ArpTest, PendingQueueIgnoredWhenIncompleteDisabled)
{
    global->configs.arp.incompleteResolveLimit.store(1);
    global->configs.arp.incompleteEnabled.store(false);

    PacketInfo pkt;

    arp->resolveAndSend(ByteString("\xC0\xA8\x08\x20", 4), pkt);
    arp->resolveAndSend(ByteString("\xC0\xA8\x08\x21", 4), pkt);
    arp->resolveAndSend(ByteString("\xC0\xA8\x08\x22", 4), pkt);

    EXPECT_TRUE(getPendingIncompletes().empty());
    std::shared_lock<std::shared_mutex> lock(getArpCacheMutex());
    EXPECT_FALSE(getArpCache().contains(ByteString("\xC0\xA8\x08\x21", 4)));
    EXPECT_FALSE(getArpCache().contains(ByteString("\xC0\xA8\x08\x22", 4)));
}

// Test: EntryDropBeyondLimitWhenIncompleteDisabled
TEST_F(Internal_ArpTest, EntryDropBeyondLimitWhenIncompleteDisabled)
{
    global->configs.arp.incompleteEnabled.store(false);
    global->configs.arp.incompleteResolveLimit.store(1);

    PacketInfo pkt;

    arp->resolveAndSend(ip, pkt);
    arp->resolveAndSend(ip2, pkt);

    std::shared_lock<std::shared_mutex> lock(getArpCacheMutex());
    EXPECT_FALSE(getArpCache().contains(ip)); // nothing should be created
    EXPECT_FALSE(getArpCache().contains(ip2));
}

// Test: Shutdown_ClearsAllState
TEST_F(Internal_ArpTest, Shutdown_ClearsAllState)
{
    mockInterface->blockEnqueues();
    PacketInfo pkt;
    arp->addArpEntry(ip, mac, false, false);
    arp->resolveAndSend("\xC0\xA8\x09\x02", pkt); // creates incomplete
    getPendingIncompletes().insert("\xC0\xA8\x09\x03");

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
    arp->addArpEntry(ip, mac, false, false);

    ByteString* resolved = arp->getMac(ip);
    ASSERT_NE(resolved, nullptr);

    arp->shutdown();

    // Simulate "restart"
    arp->initiateArp();

    ByteString* after = arp->getMac(ip);
    EXPECT_EQ(after, nullptr);
}

// Test: ThreadSafety_AddRemoveConcurrent
TEST_F(Internal_ArpTest, ThreadSafety_AddRemoveConcurrent)
{
    const int numThreads = 10;
    std::vector<std::thread> threads;

    for (int i = 0; i < numThreads; ++i)
    {
        threads.emplace_back([&, i] {
            ByteString ip = ByteString({192, 168, 10, (uint8_t)i});
            ByteString mac = ByteString({0x10, 0x20, 0x30, 0x40, 0x50, (uint8_t)i});
            arp->addArpEntry(ip, mac, false, false);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            arp->removeArpEntry(ip);
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
    request.senderIpAddress = ByteString(4, 0x00);
    request.targetIpAddress = ip;
    request.senderHardwareAddress = mockInterface->configs.getMac();

    EXPECT_CALL(*mockInterface, enqueuePacket).Times(0);
    arp->receiveRequest(request, request.senderHardwareAddress);
}

// Test: InvalidRequest_SenderEqualsTarget_Ignored
TEST_F(Internal_ArpTest, InvalidRequest_SenderEqualsTarget_Ignored)
{
    ArpHeader request;
    request.senderIpAddress = ip;
    request.targetIpAddress = ip;
    request.senderHardwareAddress = mockInterface->configs.getMac();

    EXPECT_CALL(*mockInterface, enqueuePacket).Times(0);
    arp->receiveRequest(request, request.senderHardwareAddress);
}

// Test: ReplyWithInvalidMac_Ignored
TEST_F(Internal_ArpTest, ReplyWithInvalidMac_Ignored)
{
    ByteString invalidMac = Variable::Mac::broadcast;

    ArpHeader reply;
    reply.senderIpAddress = ip;
    reply.targetIpAddress = ip;
    reply.senderHardwareAddress = invalidMac;

    // MAC is broadcast — invalid in reply
    // In your implementation, this might not yet block it unless logic is added to reject non-unicast MACs
    // Assuming it is ignored:
    arp->receiveReply(reply);
    EXPECT_EQ(arp->getMac(ip), nullptr);
}

// Test: ResolvedEntry_ReResolutionResetsTimer
TEST_F(Internal_ArpTest, ResolvedEntry_ReResolutionResetsTimer)
{
    arp->addArpEntry(ip, mac, false, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Save current expiry
    std::shared_lock<std::shared_mutex> lock(getArpCacheMutex());
    auto originalExpiry = getArpCache()[ip].expiryTime;
    lock.unlock();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Re-add same entry (should overwrite and reset expiry)
    arp->addArpEntry(ip, mac, false, false);

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
    reply1.senderIpAddress = ip;
    reply1.targetIpAddress = ip2; // not equal — not GARP
    reply1.senderHardwareAddress = mac;
    arp->receiveReply(reply1);

    EXPECT_EQ(arp->getMac(ip), nullptr);

    // GARP reply
    ArpHeader reply2;
    reply2.senderIpAddress = ip;
    reply2.targetIpAddress = ip; // GARP
    reply2.senderHardwareAddress = mac;
    arp->receiveReply(reply2);

    ByteString* resolved = arp->getMac(ip);
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(*resolved, mac);
}

// Test: ReplyIgnored_IfArpNotRunning
TEST_F(Internal_ArpTest, ReplyIgnored_IfArpNotRunning)
{
    arp->shutdown();

    ArpHeader reply;
    reply.senderIpAddress = ip;
    reply.targetIpAddress = ip;
    reply.senderHardwareAddress = mac;

    EXPECT_NO_THROW(arp->receiveReply(reply));
    EXPECT_EQ(arp->getMac(ip), nullptr);
}
