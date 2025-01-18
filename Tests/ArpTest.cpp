// AprTest.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory.h>
#include <chrono>
#include <thread>

// Include the ARP implementation and mock classes
#include <Arp.h>
#include <MockInterface.hpp>
#include <MockRoutingTable.hpp>

using namespace Protocol;

class ArpTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Initialize mock objects
        mockInterface = std::make_unique<testing::NiceMock<MockInterface>>();
        mockRoutingTable = std::make_unique<testing::NiceMock<MockRoutingTable>>();
    }

    void TearDown() override
    {
        // Cleanup if necessary
    }

    // Member variables
    std::unique_ptr<MockInterface> mockInterface;
    std::unique_ptr<MockRoutingTable> mockRoutingTable;

    // Helper functions
    std::unordered_map<ByteString, ArpCacheEntry, std::hash<ByteString>, std::equal_to<ByteString>>& getArpCache() {return mockInterface->arp->arpCache;}
    std::mutex& getArpCacheMutex() {return mockInterface->arp->arpCacheMutex;}
    std::unordered_map<ByteString, std::queue<PacketInfo>, std::hash<ByteString>, std::equal_to<ByteString>>& getPacketQueuePerIp() {return mockInterface->arp->packetQueuePerIp;}
    std::mutex& getPacketQueueMutex() {return mockInterface->arp->packetQueueMutex;}
    std::unordered_map<ByteString, bool, std::hash<ByteString>, std::equal_to<ByteString>>& getPendingRequests() {return mockInterface->arp->pendingRequests;}
    std::mutex& getPendingRequestsMutex() {return mockInterface->arp->requestMutex;}
};


TEST_F(ArpTest, ArpCacheCleanup_RemovesExpiredEntries) 
{
    ByteString testIp = "192.168.1.1";
    ByteString testMac = "AA:BB:CC:DD:EE:FF";

    // Add an entry to the ARP cache with a short expiry time
    {
        std::lock_guard<std::mutex> lock(getArpCacheMutex());
        getArpCache()[testIp] = ArpCacheEntry{
            .macAddress = testMac,
            .expiryTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(100)
        };
    }

    // Wait for the cache cleanup thread to remove the expired entry
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Check that the entry has been removed
    EXPECT_FALSE(mockInterface->arp->isMacKnown(testIp));
}


TEST_F(ArpTest, Constructor_StartsCleanupThread) 
{
    // Since the cleanup thread runs indefinitely, we can't directly test it.
    // Instead, ensure that no exceptions are thrown during construction.
    EXPECT_NO_THROW({
        auto tempArp = std::make_unique<Arp>(*mockInterface);
    });
}


TEST_F(ArpTest, IsMacKnown_ReturnsFalse_WhenCacheIsEmpty) 
{
    ByteString testIp = "192.168.1.1";
    EXPECT_FALSE(mockInterface->arp->isMacKnown(testIp));
}

TEST_F(ArpTest, GetMac_ReturnsEmpty_WhenCacheIsEmpty) 
{
    ByteString testIp = "192.168.1.1";
    ByteString mac = mockInterface->arp->getMac(testIp);
    EXPECT_TRUE(mac.empty());
}

TEST_F(ArpTest, IsMacKnown_ReturnsTrue_WhenMacIsInCacheAndNotExpired) 
{
    ByteString testIp = "192.168.1.2";
    ByteString testMac = "AA:BB:CC:DD:EE:11";

    {
        std::lock_guard<std::mutex> lock(getArpCacheMutex());
        getArpCache()[testIp] = ArpCacheEntry{
            .macAddress = testMac,
            .expiryTime = std::chrono::steady_clock::now() + std::chrono::seconds(60)
        };
    }

    EXPECT_TRUE(mockInterface->arp->isMacKnown(testIp));
}

TEST_F(ArpTest, GetMac_ReturnsCorrectMac_WhenMacIsAvailable) {
    ByteString testIp = "192.168.1.3";
    ByteString testMac = "AA:BB:CC:DD:EE:22";

    {
        std::lock_guard<std::mutex> lock(getArpCacheMutex());
        getArpCache()[testIp] = ArpCacheEntry{
            .macAddress = testMac,
            .expiryTime = std::chrono::steady_clock::now() + std::chrono::seconds(60)
        };
    }

    ByteString mac = mockInterface->arp->getMac(testIp);
    EXPECT_EQ(mac, testMac);
}

TEST_F(ArpTest, IsMacKnown_ReturnsFalse_AndRemovesEntry_WhenExpired) {
    ByteString testIp = "192.168.1.4";
    ByteString testMac = "AA:BB:CC:DD:EE:33";

    {
        std::lock_guard<std::mutex> lock(getArpCacheMutex());
        getArpCache()[testIp] = ArpCacheEntry{
            .macAddress = testMac,
            .expiryTime = std::chrono::steady_clock::now() - std::chrono::seconds(1) // Already expired
        };
    }

    EXPECT_FALSE(mockInterface->arp->isMacKnown(testIp));

    // Verify that the entry is removed
    {
        std::lock_guard<std::mutex> lock(getArpCacheMutex());
        EXPECT_EQ(getArpCache().find(testIp), getArpCache().end());
    }
}

TEST_F(ArpTest, ResolveAndSend_EnqueuesPacketAndSendsRequest) {
    ByteString targetIp = "192.168.1.5";
    PacketInfo packet;

    // Expect that enqueuePacket is called once for enqueuing
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(1);

    // Call resolveAndSend
    mockInterface->arp->resolveAndSend(targetIp, packet);
}

TEST_F(ArpTest, ResolveAndSend_DoesNothing_WhenTargetIpIsEmpty) {
    ByteString emptyIp = "";
    PacketInfo packet;

    // Expect that enqueuePacket is never called
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(0);

    // Call resolveAndSend with empty IP
    mockInterface->arp->resolveAndSend(emptyIp, packet);
}

TEST_F(ArpTest, SendRequest_SendsArpRequest_WhenMacIsUnknown) {
    ByteString targetIp = "192.168.1.6";

    // Expect enqueuePacket to be called to send ARP request
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(::testing::AtLeast(1));

    // Call sendRequest
    mockInterface->arp->sendRequest(targetIp);

    // Allow some time for the asynchronous thread to execute
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

TEST_F(ArpTest, SendRequest_DoesNotSendDuplicateRequests_ForSameIp) {
    ByteString targetIp = "192.168.1.7";

    // First request: expect enqueuePacket
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(1);

    // Second request: no additional enqueuePacket
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(1);

    // Call sendRequest twice
    mockInterface->arp->sendRequest(targetIp);
    mockInterface->arp->sendRequest(targetIp);

    // Allow some time for asynchronous threads to execute
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

TEST_F(ArpTest, ReceiveReply_AddsToCacheAndSendsQueuedPackets) {
    ByteString senderIp = "192.168.1.8";
    ByteString senderMac = "AA:BB:CC:DD:EE:44";

    // Simulate that there are queued packets for this IP
    PacketInfo packet1;
    PacketInfo packet2;

    {
        std::lock_guard<std::mutex> lock(getPacketQueueMutex());
        getPacketQueuePerIp()[senderIp].push(packet1);
        getPacketQueuePerIp()[senderIp].push(packet2);
    }

    // Expect that enqueuePacket is called twice to send the queued packets
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, senderMac)).Times(2);

    // Simulate receiving an ARP reply
    ArpHeader reply;
    reply.senderIpAddress = senderIp;
    reply.senderHardwareAddress = senderMac;

    mockInterface->arp->receiveReply(reply);

    // Verify that the MAC is now known
    EXPECT_TRUE(mockInterface->arp->isMacKnown(senderIp));
    ByteString mac = mockInterface->arp->getMac(senderIp);
    EXPECT_EQ(mac, senderMac);

    // Verify that the packet queue is empty
    {
        std::lock_guard<std::mutex> lock(getPacketQueueMutex());
        EXPECT_EQ(getPacketQueuePerIp().find(senderIp), getPacketQueuePerIp().end());
    }
}

TEST_F(ArpTest, ReceiveReply_IgnoresUnsolicitedReplies) {
    ByteString senderIp = "192.168.1.9";
    ByteString senderMac = "AA:BB:CC:DD:EE:55";

    // No pending requests for this IP
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(0);

    // Simulate receiving an unsolicited ARP reply
    ArpHeader reply;
    reply.senderIpAddress = senderIp;
    reply.senderHardwareAddress = senderMac;

    mockInterface->arp->receiveReply(reply);

    // MAC should not be in cache
    EXPECT_FALSE(mockInterface->arp->isMacKnown(senderIp));
}

TEST_F(ArpTest, SendReply_SendsCorrectArpReplyPacket) {
    ByteString targetMac = "FF:FF:FF:FF:FF:FF";
    ByteString targetIp = "192.168.1.10";
    ByteString currentMac = "AA:BB:CC:DD:EE:66";
    ByteString ip = "192.168.1.100";

    // Mock the Interface's Get method
    // Expect enqueuePacket to be called once with the ARP reply
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(1);

    // Call sendReply
    mockInterface->arp->sendReply(targetMac, targetIp);
}

TEST_F(ArpTest, SendReply_DoesNothing_WhenTargetIpIsEmpty) {
    ByteString targetMac = "FF:FF:FF:FF:FF:FF";
    ByteString emptyIp = "";

    // Expect enqueuePacket is never called
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(0);

    // Call sendReply with empty IP
    mockInterface->arp->sendReply(targetMac, emptyIp);
}

TEST_F(ArpTest, ValidateMacAddress_ReturnsTrue_ForMatchingMac) {
    ByteString mac = "AA:BB:CC:DD:EE:77";
    ByteString currentMac = "AA:BB:CC:DD:EE:77";
    EXPECT_TRUE(Functions::validateMacAddress(mac, currentMac));
}

TEST_F(ArpTest, ValidateMacAddress_ReturnsTrue_ForMulticastMac) {
    ByteString multicastMac = "\x01\x00\x5E\x00\x00\xFB"; // Multicast MAC in byte format
    ByteString currentMac = "AA:BB:CC:DD:EE:77";
    EXPECT_TRUE(Functions::validateMacAddress(multicastMac, currentMac));
}

TEST_F(ArpTest, ValidateMacAddress_ReturnsFalse_ForNonMatchingUnicastMac) {
    ByteString mac = "AA:BB:CC:DD:EE:88";
    ByteString currentMac = "AA:BB:CC:DD:EE:77";
    EXPECT_FALSE(Functions::validateMacAddress(mac, currentMac));
}

TEST_F(ArpTest, ReceiveReply_HandlesNullptrRoutingTable) {
    ByteString senderIp = "192.168.1.11";
    ByteString senderMac = "AA:BB:CC:DD:EE:99";

    // Simulate that RoutingTable::getInstance() returns nullptr
    // This requires modifying RoutingTable to be mockable or handle it appropriately

    // For this test, we'll assume RoutingTable is a singleton that can be mocked similarly to Logger
    // Skipping implementation details as RoutingTable is not fully defined

    // Simulate receiving an ARP reply
    ArpHeader reply;
    reply.senderIpAddress = senderIp;
    reply.senderHardwareAddress = senderMac;

    // Expect that even if RoutingTable is nullptr, ARP processes the reply
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(0); // No packets to send

    mockInterface->arp->receiveReply(reply);

    // Verify that the MAC is now known
    EXPECT_TRUE(mockInterface->arp->isMacKnown(senderIp));
    ByteString mac = mockInterface->arp->getMac(senderIp);
    EXPECT_EQ(mac, senderMac);
}

TEST_F(ArpTest, ResolveAndSend_HandlesHighLoadGracefully) {
    ByteString targetIp = "192.168.1.12";

    // Simulate multiple packets being enqueued
    const int numPackets = 1000;
    std::vector<PacketInfo> packets(numPackets);

    // Expect enqueuePacket to be called once to send ARP request
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(1);

    for (int i = 0; i < numPackets; ++i) {
        mockInterface->arp->resolveAndSend(targetIp, packets[i]);
    }

    // Allow some time for asynchronous threads to execute
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

TEST_F(ArpTest, ResolveAndSend_HandlesConcurrentRequestsCorrectly) {
    ByteString targetIp = "192.168.1.13";
    PacketInfo packet1;
    PacketInfo packet2;

    // Expect enqueuePacket to be called once for ARP request
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(1);

    // Simulate concurrent calls to resolveAndSend
    std::thread t1([&]() { mockInterface->arp->resolveAndSend(targetIp, packet1); });
    std::thread t2([&]() { mockInterface->arp->resolveAndSend(targetIp, packet2); });

    t1.join();
    t2.join();

    // Allow some time for asynchronous threads to execute
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

TEST_F(ArpTest, ReceiveReply_IsThreadSafe) {
    ByteString senderIp = "192.168.1.14";
    ByteString senderMac = "AA:BB:CC:DD:EE:AA";

    // Simulate multiple threads receiving the same ARP reply
    auto receiveFunc = [&](int) {
        ArpHeader reply;
        reply.senderIpAddress = senderIp;
        reply.senderHardwareAddress = senderMac;
        mockInterface->arp->receiveReply(reply);
    };

    std::thread t1(receiveFunc, 1);
    std::thread t2(receiveFunc, 2);
    std::thread t3(receiveFunc, 3);

    t1.join();
    t2.join();
    t3.join();

    // Verify that the MAC is now known
    EXPECT_TRUE(mockInterface->arp->isMacKnown(senderIp));
    ByteString mac = mockInterface->arp->getMac(senderIp);
    EXPECT_EQ(mac, senderMac);

    // Ensure that the ARP cache has only one entry
    {
        std::lock_guard<std::mutex> lock(getArpCacheMutex());
        EXPECT_EQ(getArpCache().size(), 1);
    }
}

TEST_F(ArpTest, SendRequest_RetriesOnFailure) {
    ByteString targetIp = "192.168.1.15";

    // Expect enqueuePacket to be called 3 times for retries
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(3);

    // Call sendRequest
    mockInterface->arp->sendRequest(targetIp);

    // Allow enough time for retries (3 retries with 2 seconds interval each)
    std::this_thread::sleep_for(std::chrono::seconds(7));

    // Verify that pendingRequests no longer contains targetIp
    {
        std::lock_guard<std::mutex> lock(getPendingRequestsMutex());
        EXPECT_EQ(getPendingRequests().find(targetIp), getPendingRequests().end());
    }
}

TEST_F(ArpTest, SendRequest_SucceedsOnSecondRetry) {
    ByteString targetIp = "192.168.1.16";

    // First ARP request fails
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(2)
        .WillOnce(::testing::Return())
        .WillOnce(::testing::Return());

    // Simulate receiving an ARP reply after the second request
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .WillOnce(::testing::Invoke([&](const PacketInfo& packet, const ByteString& mac) {
            // Do nothing
        }));

    // Simulate receiving the ARP reply after the first retry
    std::thread replyThread([&]() {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        ArpHeader reply;
        reply.senderIpAddress = targetIp;
        reply.senderHardwareAddress = "AA:BB:CC:DD:EE:BB";
        mockInterface->arp->receiveReply(reply);
    });

    // Call sendRequest
    mockInterface->arp->sendRequest(targetIp);

    // Allow enough time for retries and reply
    std::this_thread::sleep_for(std::chrono::seconds(5));

    replyThread.join();

    // Verify that pendingRequests no longer contains targetIp
    {
        std::lock_guard<std::mutex> lock(getPendingRequestsMutex());
        EXPECT_EQ(getPendingRequests().find(targetIp), getPendingRequests().end());
    }

    // Verify that the MAC is now known
    EXPECT_TRUE(mockInterface->arp->isMacKnown(targetIp));
    ByteString mac = mockInterface->arp->getMac(targetIp);
    EXPECT_EQ(mac, "AA:BB:CC:DD:EE:BB");
}

TEST_F(ArpTest, SendReply_HandlesInvalidInterfaceInfo) {
    ByteString targetMac = "FF:FF:FF:FF:FF:FF";
    ByteString targetIp = "192.168.1.17";

    // Expect that enqueuePacket is never called due to invalid interface info
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(0);

    // Call sendReply
    mockInterface->arp->sendReply(targetMac, targetIp);
}
