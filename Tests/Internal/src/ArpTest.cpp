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
    void SetUp() override
    {
        // Initialize mock objects
        mockInterface = std::make_unique<testing::NiceMock<MockInterface>>();

        // Initializeee ARP instance from the interface
        arp = mockInterface->arp;
    }

    void TearDown() override
    {
        // Cleanup if necessary
        mockInterface.reset();
    }

    // Member variables
    std::unique_ptr<MockInterface> mockInterface;
    Protocol::Arp* arp;

    // Helper functions
    std::unordered_map<ByteString, ArpCacheEntry>& getArpCache() {return arp->arpCache;}
    std::shared_mutex& getArpCacheMutex() {return arp->arpCacheMutex;}
    std::unordered_map<ByteString, std::queue<PacketInfo>>& getPacketQueuePerIp() {return arp->packetQueuePerIp;}
    std::mutex& getPacketQueueMutex() {return arp->packetQueueMutex;}
    std::unordered_set<ByteString>& getPendingRequests() {return arp->pendingRequests;}
    std::mutex& getPendingRequestsMutex() {return arp->requestMutex;}
};

// Helper to simulate a reply
void simulateArpReply(Arp& arp, const ByteString& ip, const ByteString& mac)
{
    ArpHeader arpReply;
    arpReply.senderIpAddress = ip;
    arpReply.senderHardwareAddress = mac;
    arp.receiveReply(arpReply);
}

// Test Verify MAC is know after receiving a reply
TEST_F(Internal_ArpTest, ReceiveReply_UpdatesArpCache)
{
    ByteString testIp = "\xC0\xA8\x01\x01";
    ByteString testMac = "\xAA\xBB\xCC\xDD\xEE\xFF";

    ArpHeader reply;
    reply.senderIpAddress = testIp;
    reply.senderHardwareAddress = testMac;

    arp->receiveReply(reply);

    {
        std::shared_lock<std::shared_mutex> lock(getArpCacheMutex());
        auto it = getArpCache().find(testIp);
        ASSERT_NE(it, getArpCache().end());
        EXPECT_EQ(it->second.macAddress, testMac);
    }
}

// Test Verifies resolveAndSend triggers an ARP request and enqueues packet
TEST_F(Internal_ArpTest, ResolveAndSend_EnqueuesPacketWhenResolved)
{
    ByteString testIp = "\xC0\xA8\x01\x02";
    PacketInfo testPacket;

    // Conditional varibale to synchronize with the thread
    std::condition_variable cv;
    std::mutex cvMutex;
    bool packetEnqueued = false;

    // Mock behavior for enqueuePacket
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce([&](PacketInfo&, ByteString)
        {
            // Notify the test once the packet is enqueued
            std::lock_guard<std::mutex> lock(cvMutex);
            packetEnqueued = true;
            cv.notify_one();
        });

    arp->resolveAndSend(testIp, testPacket);

    // Wait for the packet to be enqueued
    {
        std::unique_lock<std::mutex> lock(cvMutex);
        EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&]() { return packetEnqueued; }));
    }
    
    // Ensure that the IP os added to the pending requests
    {
        std::lock_guard<std::mutex> lock(getPendingRequestsMutex());
        EXPECT_TRUE(getPendingRequests().count(testIp) > 0);
    }
}

// Test Verifies timeout behabior when no ARP reply is recieved
TEST_F(Internal_ArpTest, ResolveAndSend_TimesOutWhenNoReply)
{
    ByteString testIp = "\xC0\xA8\x01\x03";
    ByteString testMac = Variable::Mac::broadcast;
    PacketInfo testPacket;
    arp->retryTime = 0;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::Eq(testMac))).Times(3);

    arp->resolveAndSend(testIp, testPacket);

    std::this_thread::sleep_for(std::chrono::seconds(1)); // Wait for all retries

    {
        std::shared_lock<std::shared_mutex> lock(getArpCacheMutex());
        EXPECT_TRUE(getArpCache().find(testIp) == getArpCache().end());
    }
}

// Test Ensures multiple packets for the same IP are queued and sent
TEST_F(Internal_ArpTest, ResolveAndSend_HandlesMultiplePackets)
{
    ByteString testIp = "\xC0\xA8\x01\x04";
    ByteString testMac = "\xAA\xBB\xCC\xDD\xEE\xFF";
    PacketInfo packet1, packet2;

    // Conditional varibale to track enqueuedPacket calls
    std::condition_variable cv;
    std::mutex cvMutex;
    int enqueueCallCount = 0;

    // Expectation for enqueuePacket with broadcast MAC (ARP request)
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::Eq(Variable::Mac::broadcast)))
        .Times(1)
        .WillRepeatedly([&](const PacketInfo&, const ByteString&) {
            std::lock_guard<std::mutex> lock(cvMutex);
            enqueueCallCount++;
            cv.notify_one();
        });

    // Expectation for enqueuePacket with resolved MAC (actual packet sends)
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::Eq(testMac)))
        .Times(2)
        .WillRepeatedly([&](const PacketInfo&, const ByteString&) {
            std::lock_guard<std::mutex> lock(cvMutex);
            enqueueCallCount++;
            cv.notify_one();
        });

    // Enqueue two packets
    arp->resolveAndSend(testIp, packet1);
    arp->resolveAndSend(testIp, packet2);

    // Wait for the ARP request to be sent
    {
        std::unique_lock<std::mutex> lock(cvMutex);
        EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&]() { return enqueueCallCount >= 1; }));
    }

    // Simulate ARP reply
    simulateArpReply(*arp, testIp, testMac);

    // Wait for the actual packets to be sent
    {
        std::unique_lock<std::mutex> lock(cvMutex);
        EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&]() { return enqueueCallCount >= 3; }));
    }

    // Final verification
    EXPECT_EQ(enqueueCallCount, 3);
}

// Test Ensures ARP cache is cleaned up after expiracy
TEST_F(Internal_ArpTest, ArpCacheCleanup_RemovesExpiresEntries)
{
    ByteString testIp = "\xC0\xA8\x01\x05";
    ByteString testMac = "\xFF\xEE\xDD\xCC\xBB\xAA";
    arp->replyTimeout = 100;

    simulateArpReply(*arp, testIp, testMac);

    {
        std::shared_lock<std::shared_mutex> lock(getArpCacheMutex());
        EXPECT_TRUE(arp->getMac(testIp));
    }

    // Wait for expiracy
    std::this_thread::sleep_for(std::chrono::seconds(1));

    {
        std::shared_lock<std::shared_mutex> lock(getArpCacheMutex());
        EXPECT_FALSE(arp->getMac(testIp));
    }
}

// Test Simultaneous requests for multiple IPs
TEST_F(Internal_ArpTest, ResolveAndSend_HandlesSumultaneousRequests)
{
    ByteString ip1 = "\xC0\xA8\x01\x06";
    ByteString ip2 = "\xC0\xA8\x01\x07";
    ByteString mac1 = "\xAA\xBB\xCC\xDD\xEE\xFF";
    ByteString mac2 = "\x11\x22\x33\x44\x55\x66";
    PacketInfo packet1, packet2;

    // Conditional variables to track enqueuePacket calls
    std::condition_variable cv;
    std::mutex cvMutex;
    int enqueueCallCount = 0;

    // Expectation for enqueuePacket with broadcast MAC (ARP requests)
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::Eq(Variable::Mac::broadcast)))
        .Times(2)
        .WillRepeatedly([&](const PacketInfo&, const ByteString&) {
            std::lock_guard<std::mutex> lock(cvMutex);
            enqueueCallCount++;
            cv.notify_one();
        });

    // Expectation for enqueuePacket with resolved MAC (actual packet sends)
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::AnyOf(::testing::Eq(mac1), ::testing::Eq(mac2))))
        .Times(2)
        .WillRepeatedly([&](const PacketInfo&, const ByteString&) {
            std::lock_guard<std::mutex> lock(cvMutex);
            enqueueCallCount++;
            cv.notify_one();
        });

    // Start sumultaneous resolveAndSend calls
    std::thread t1([&]() { arp->resolveAndSend(ip1, packet1); });
    std::thread t2([&]() { arp->resolveAndSend(ip2, packet2); });

    t1.join();
    t2.join();

    // Wait for ARP requests to be sent
    {
        std::unique_lock<std::mutex> lock(cvMutex);
        EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&]() { return enqueueCallCount >= 2; }));
    }

    // Simulate replies
    simulateArpReply(*arp, ip1, mac1);
    simulateArpReply(*arp, ip2, mac2);

    // Wait for actual packets to be sent
    {
        std::unique_lock<std::mutex> lock(cvMutex);
        EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&]() { return enqueueCallCount >= 4; }));
    }

    // Final verification
    EXPECT_EQ(enqueueCallCount, 4);
}

// Test ARP reply for unsolicited IP
TEST_F(Internal_ArpTest, ReceiveReply_Unsolicited)
{
    ByteString unsolicitedIp = "\xC0\xA8\x01\x08";
    ByteString unsolicitedMac = "\xAA\xBB\xCC\xDD\xEE\xFF";

    ArpHeader unsolicitedReply;
    unsolicitedReply.senderIpAddress = unsolicitedIp;
    unsolicitedReply.senderHardwareAddress = unsolicitedMac;

    EXPECT_NO_THROW(arp->receiveReply(unsolicitedReply));

    {
        std::shared_lock<std::shared_mutex> lock(getArpCacheMutex());
        EXPECT_EQ(getArpCache().at(unsolicitedIp).macAddress, unsolicitedMac);
    }
}

// Test Unterrupt waitForReply with shutdown
TEST_F(Internal_ArpTest, WaitForReply_InterruptsOnShutdown)
{
    ByteString targetIp = "\xC0\xA8\x01\x09";
    ByteString testMac = Variable::Mac::broadcast;
    PacketInfo testPacket;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::Eq(testMac))).Times(1);

    std::thread t([&]() {
        arp->resolveAndSend(targetIp, testPacket);
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));
    arp->shutdown();
    t.join();

    EXPECT_FALSE(arp->getMac(targetIp));
}

// Test SendReply sends correct ARP reply packet
TEST_F(Internal_ArpTest, SendReply_SendsCorrectPacket)
{
    ByteString senderMac = "\x11\x22\x33\x44\x55\x66";
    ByteString senderIp = "\xC0\xA8\x01\x10";
    ByteString targetMac = "\xAA\xBB\xCC\xDD\xEE\xFF";
    ByteString targetIp = "\xC0\xA8\x01\x11";

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::Eq(targetMac))).Times(1);

    arp->sendReply(targetMac, targetIp);
}

// Test Hight Volume of Requests
TEST_F(Internal_ArpTest, ResolveAndSend_StressTestWithHighVolumeRequests)
{
    const unsigned int numRequests = 200;
    std::vector<ByteString> ips;
    std::vector<ByteString> macs;
    std::vector<PacketInfo> packets(numRequests);

    // Prepare IPs and MACs
    for (unsigned int i = 0; i < numRequests; ++i)
    {
        ips.push_back(ByteString(i, 1));   // Generate IPs
        macs.push_back(ByteString(i, 1));               // Generate MACs
    }

    std::condition_variable cv;
    std::mutex cvMutex;
    int completedRequests = 0;

    // Mock behavior for enqueuePacket to track completed requests
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::Eq(Variable::Mac::broadcast)))
        .Times(numRequests)
        .WillRepeatedly([&](const PacketInfo&, const ByteString) {
            std::lock_guard<std::mutex> lock(cvMutex);
            completedRequests++;
            cv.notify_one();
        });

    // Mock behavior for ARP replies (resolved MACs)
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::Not(::testing::Eq(Variable::Mac::broadcast))))
        .Times(numRequests)
        .WillRepeatedly([&](const PacketInfo&, const ByteString&) {
            {
                std::lock_guard<std::mutex> lock(cvMutex);
                completedRequests++;
            }
            cv.notify_one();
        });

    // Simultaneous start ARP resolution for all requests
    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < numRequests; ++i)
    {
        threads.emplace_back([&, i]() { arp->resolveAndSend(ips[i], packets[i]); });
    }

    // Wait for all threads to finish
    for (auto& thread : threads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    // Wait for all ARP requests to be processed
    {
        std::unique_lock<std::mutex> lock(cvMutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(30), [&]() {
            return completedRequests == numRequests;
        }));
    }

    // Simulate replies for all IPs
    for (unsigned int i = 0; i < numRequests; ++i)
    {
        simulateArpReply(*arp, ips[i], macs[i]);
    }

    // Wait for actual packets to be sent
    {
        std::unique_lock<std::mutex> lock(cvMutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(30), [&]() {
            return completedRequests == 2 * numRequests;
        }));
    }

    EXPECT_EQ(completedRequests, 2 * numRequests);
}
