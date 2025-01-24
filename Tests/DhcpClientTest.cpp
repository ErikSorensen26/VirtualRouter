// DhcpClientTest.cpp

#include <gtest/gtest.h>
#include <Dhcp.h>
#include <Interface.h>
#include <MockInterface.hpp>
#include <ByteString.hpp>
#include <Functions.h>

using namespace Protocol;

// Test Fixture for DHCP
class DhcpClientTest : public ::testing::Test
{
protected:
    std::mutex cvMutex;
    std::condition_variable cv;
    bool eventOccurred = false;

    void SetUp() override
    {
        // Initialize mock interface
        mockInterface = new ::testing::NiceMock<MockInterface>();
        dhcpClient = new DhcpClient(*mockInterface);

        // Initialize DHCP with hardware address
        hardwareAddress = "\x01\x02\x03\x04\x05\x06";
        //dhcpClient->InitializeDhcp(hardwareAddress);
    }
    
    void TearDown() override
    {
        ::testing::Mock::VerifyAndClearExpectations(&dhcpClient);
        delete dhcpClient;
        delete mockInterface;
    }

    // Mock Interface and DhcpClient instance
    MockInterface* mockInterface;
    DhcpClient* dhcpClient;
    ByteString hardwareAddress;
    std::string hostname = "router";

    bool stopFlag() { return dhcpClient->stopFlag.load(); }
    void sendDhcpDiscover(std::string& name, ByteString& mac) { dhcpClient->sendDhcpDiscover(name, mac); }
    void setDhcpOffer(PacketInfo& packet) {dhcpClient->dhcpOffer = packet;}
    void setDhcpAck(PacketInfo& packet) {dhcpClient->dhcpAck = packet;}
    void setDhcpNak(PacketInfo& packet) {dhcpClient->dhcpNak = packet;}
    void setDhcpDecline(PacketInfo& packet) {dhcpClient->dhcpDecline = packet;}
    void setDhcpInform(PacketInfo& packet) {dhcpClient->dhcpInformPacket = packet;}
    void processDhcpOffer(std::string& name, ByteString& mac) { dhcpClient->processDhcpOffer(name, mac);}
    void processDhcpResponses(std::string& name, ByteString& mac) { dhcpClient->processDhcpResponses(name, mac); }
    double getLeaseStart() { return dhcpClient->leaseStart; }
    void setLeaseTime(double leaseTime) { dhcpClient->leaseStart = leaseTime; }
    bool getAcked() { return dhcpClient->acked; }
    bool getNaked() { return dhcpClient->naked; }
    bool getOffered() { return dhcpClient->offered; }
    void handleLeaseRenewal(ByteString& mac, std::string name) { dhcpClient->handleLeaseRenewal(mac, name); }
    void setAck(bool ack) { dhcpClient->acked = ack;}
    void setOffer(bool offer) { dhcpClient->offered = offer; }
    void setStop(bool stop) {dhcpClient->stopFlag.store(stop);}
    void notifycv() {dhcpClient->cv.notify_one();}
};

// Test generating DHCP transaction ID
TEST_F(DhcpClientTest, GenerateDhcpTransid_UniqueAndCorrectSize)
{
    ByteString transId1 = dhcpClient->generateDhcpTransid();
    ByteString transId2 = dhcpClient->generateDhcpTransid();

    // Ensure that transaction IDs are 4 bytes long
    EXPECT_EQ(transId1.size(), 4);
    EXPECT_EQ(transId2.size(), 4);

    // Ensure that two consecutive transaction IDs are different (high probability)
    EXPECT_NE(transId1, transId2);
}

// Test sending DHCP Discover
TEST_F(DhcpClientTest, SendDhcpDiscover_EnqueuesCorrectPacket)
{
    // Expect enqueuedPacket to be called once with anyPacketInfo and the correct hardware address.
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](PacketInfo&, ByteString) {
            // Notify the test once the packet is queued
            std::lock_guard<std::mutex> lock(cvMutex);
            eventOccurred = true;
            cv.notify_one();
        }));
    
    // Trigger sendDhcpDiscover
    dhcpClient->InitializeDhcp(hardwareAddress);

    // Wait for the packet to be enqueued
    {
        std::unique_lock<std::mutex> lock(cvMutex);
        EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&]() {return eventOccurred; }));
    }

    eventOccurred = false;
}

// Test processing DHCP Offer
TEST_F(DhcpClientTest, ProcessDhcpOffer_SendsDhcpRequest)
{
    // Prepare a mock DHCP Offer packet
    PacketInfo offerPacket;

    // Layer2: Ethernet Header
    EthernetHeader eth;
    eth.destinationMac = "\xFF\xFF\xFF\xFF\xFF\xFF"; // Broadcast
    eth.sourceMac = "\x0a\x0b\x0c\x0d\x0e\x0f"; // Server MAC
    eth.type = "\x08\x00"; // IPv4
    offerPacket.Layer2.emplace_back(std::move(eth));

    // Layer5: DHCP Header
    DhcpHeader dhcpOffer;
    dhcpOffer.boot = Variable::Dhcp::Type::offer;
    dhcpOffer.transID = dhcpClient->generateDhcpTransid();
    dhcpOffer.yourClientIP = "\xc0\xa8\x00\x64"; // 192.168.0.100
    dhcpOffer.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::serverIdentifier,
        "\x04",
        "\xc0\xa8\x00\x01" // 192.168.0.1
    });
    dhcpOffer.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::leaseTime,
        "\x04",
        "\x00\x00\x0e\x10" // 3600 seconds
    });
    dhcpOffer.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::mask,
        "\x04",
        "\xFF\xFF\xFF\x00" // 255.255.255.0
    });
    dhcpOffer.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::router,
        "\x04",
        "\xc0\xa8\x00\x01" // 192.168.0.1
    });
    dhcpOffer.end = Variable::Dhcp::end;

    offerPacket.Layer5.emplace_back(std::move(dhcpOffer));

    // Assign the offerPacket to dhcpClient's dhcpOffer
    setDhcpOffer(offerPacket);

    // Expect enqueuePacket to be called with DHCP Request
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](PacketInfo&, ByteString) {
            // Notify the test once the packet is queued
            std::lock_guard<std::mutex> lock(cvMutex);
            eventOccurred = true;
            setAck(true);
            notifycv();
            cv.notify_one();
        }));
    
    // Set the dhcp server field in order to skip discovery
    dhcpClient->configs.dhcpServer = "\xc0\xa8\x00\x01";

    // Trigger processing of DHCP Offer
    processDhcpOffer(hostname, hardwareAddress);

    // Wait for enqueuePacket to be called
    {
        std::unique_lock<std::mutex> lock(cvMutex);
        EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(1), [this]() { return eventOccurred; }));
    }

    eventOccurred = false;

    setAck(false);
}

// Test processing DHCP ACK
TEST_F(DhcpClientTest, ProcessDhcpAck_SetsIPv4AndUpdatesLease)
{
    // Prepare a mock DHCP ACK packet
    PacketInfo ackPacket;

    // Layer2: Ethernet Header
    EthernetHeader eth;
    eth.destinationMac = hardwareAddress; // Client MAC
    eth.sourceMac = "\xc0\xa8\x00\x01\x0a\x0b";
    eth.type = "\x08\x00"; // IPv4
    ackPacket.Layer2.emplace_back(std::move(eth));

    // Layer5: DHCP Header
    DhcpHeader dhcpAck;
    dhcpAck.boot = Variable::Dhcp::Type::ack;
    dhcpAck.transID = dhcpClient->generateDhcpTransid();
    dhcpAck.yourClientIP = "\xc0\xa8\x00\x64"; // 192.168.0.100
    dhcpAck.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::leaseTime,
        "\x04",
        "\x00\x00\x0e\x10" // 3600 seconds
    });
    dhcpAck.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::mask,
        "\x04",
        "\xFF\xFF\xFF\x00" // 255.255.255.0
    });
    dhcpAck.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::router,
        "\x04",
        "\xc0\xa8\x00\x01" // 192.168.0.1
    });
    dhcpAck.end = Variable::Dhcp::end;

    ackPacket.Layer5.emplace_back(std::move(dhcpAck));

    // Assign the ackPacket to dhcpClient's dhcpAck
    setDhcpAck(ackPacket);

    // Set expectation: setIPv4 should be called once
    EXPECT_CALL(*mockInterface, setIPv4(ByteString("\xc0\xa8\x00\x64"), ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](ByteString, uint8_t) {
            // Notify the test once setIPv4 is called
            std::lock_guard<std::mutex> lock(cvMutex);
            eventOccurred = true;
            cv.notify_one();
        }));

    // Simulate leave start time
    setLeaseTime(secondsSinceEpoch() - 1000); // Some past time
    
    // Trigger processing of DHCP ACK
    processDhcpResponses(hostname, hardwareAddress);

    // Wait for setIPv4 to be called
    {
        std::unique_lock<std::mutex> lock(cvMutex);
        EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&]() {return eventOccurred; }));
    }

    // Verify that leaseStart has been updated
    EXPECT_TRUE(getAcked());
    EXPECT_GT(getLeaseStart(), 0.0);

    eventOccurred = false;
}

// Test handling DHCP NAK
TEST_F(DhcpClientTest, HandleDhcpNak_ResetState)
{
    // Prepare a mock DHCP NAK packet
    PacketInfo nakPacket;

    // Layer2: Ethernet Header
    EthernetHeader eth;
    eth.destinationMac = hardwareAddress; // Client MAC
    eth.sourceMac = "\xc0\xa8\x00\x01\x0a\x0b"; // Server MAC
    eth.type = "\x08\x00"; // IPv4
    nakPacket.Layer2.emplace_back(std::move(eth));

    // Layer5: DHCP Header
    DhcpHeader dhcpNak;
    dhcpNak.boot = Variable::Dhcp::Type::nak;
    dhcpNak.transID = dhcpClient->generateDhcpTransid();
    dhcpNak.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::type,
        "\x01",
        "\x06" // NAK type
    });
    dhcpNak.end = Variable::Dhcp::end;

    nakPacket.Layer5.emplace_back(std::move(dhcpNak));

    // Assign the nakPacket to the dhcpClient's dhcpNak
    setDhcpNak(nakPacket);
    
    // Trigger processing of DHCP NAK
    processDhcpResponses(hostname, hardwareAddress);

    // Check parameters
    EXPECT_FALSE(getAcked());
    EXPECT_FALSE(getNaked()); // Will reset before this is called
}

// Tets handling DHCP DECLINE
TEST_F(DhcpClientTest, HandleDhcpDecline_ResetState)
{
    // Prepare a mock DHCP DECLINE packet
    PacketInfo declinePacket;

    // Layer2: Ethernet Header
    EthernetHeader eth;
    eth.destinationMac = hardwareAddress; // Client MAC
    eth.sourceMac = "\xc0\xa8\x00\x01\x0a\x0b"; // Server MAC
    eth.type = "\x08\x00"; // IPv4
    declinePacket.Layer2.emplace_back(std::move(eth));

    // Layer5: DHCP Header
    DhcpHeader dhcpDecline;
    dhcpDecline.boot = Variable::Dhcp::Type::decline;
    dhcpDecline.transID = dhcpClient->generateDhcpTransid();
    dhcpDecline.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::type,
        "\x01",
        "\x07" // DECLINE type
    });
    dhcpDecline.end = Variable::Dhcp::end;

    declinePacket.Layer5.emplace_back(std::move(dhcpDecline));

    // Assign the declinePacket to dhcpClient's dhcpDecline
    setDhcpDecline(declinePacket);

    // Trigger processing of DHCP DECLINE
    processDhcpResponses(hostname, hardwareAddress);

    // Verify that the state has been reset
    EXPECT_FALSE(getAcked());
    EXPECT_FALSE(getNaked());
}

// Test DHCP Lease Renewal
TEST_F(DhcpClientTest, LeaseRenewal_SendsDhcpRequestAfterRenewalTime) {
    // Simulate an initial lease
    setLeaseTime(secondsSinceEpoch() - 4000); // Assume lease time is 3600S

    // Set renewal time to 3600 seconds
    dhcpClient->configs.renewalTime = ByteString("\x00\x00\x0e\x10", 4); // 3600 seconds

    // Prepare a mock DHCP ACK packet for renewal
    PacketInfo ackPacket;

    // Layer2: Ethernet Header
    EthernetHeader eth;
    eth.destinationMac = hardwareAddress; // Client MAC
    eth.sourceMac = "\xc0\xa8\x00\x01\x0a\x0b"; // Server MAC
    eth.type = "\x08\x00"; // IPv4
    ackPacket.Layer2.emplace_back(std::move(eth));

    // Layer5: DHCP Header
    DhcpHeader dhcpAck;
    dhcpAck.boot = Variable::Dhcp::Type::ack;
    dhcpAck.transID = dhcpClient->generateDhcpTransid();
    dhcpAck.yourClientIP = "\xc0\xa8\x00\x64"; // 192.168.0.100
    dhcpAck.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::leaseTime,
        "\x04",
        "\x00\x00\x0e\x10" // 3600 seconds
    });
    dhcpAck.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::mask,
        "\x04",
        "\xff\xff\xff\x00" // 255.255.255.0
    });
    dhcpAck.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::router,
        "\x04",
        "\xc0\xa8\x00\x01" // 192.168.0.1
    });
    dhcpAck.end = Variable::Dhcp::end;

    ackPacket.Layer5.emplace_back(std::move(dhcpAck));

    // Assign the ackPacket to dhcpClient's dhcpAck
    setDhcpAck(ackPacket);

    // Set expectation: enqueuePacket should be called once with DHCP Request
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](PacketInfo&, ByteString) {
            // Notify the test once the DHCP Request is enqueued
            std::lock_guard<std::mutex> lock(cvMutex);
            setAck(true);
            notifycv();
            eventOccurred = true;
            cv.notify_one();
        }));

    // Set expectation: setIPv4 should be called once
    EXPECT_CALL(*mockInterface, setIPv4(ByteString("\xc0\xa8\x00\x64"), ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](ByteString, uint8_t) {
            // Notify the test once setIPv4 is called
            std::lock_guard<std::mutex> lock(cvMutex);
            eventOccurred = true;
            cv.notify_one();
        }));

    // Trigger lease renewal
    handleLeaseRenewal(hardwareAddress, hostname);

    // Wait for the DHCP Request to be enqueued
    {
        std::unique_lock<std::mutex> lock(cvMutex);
        EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(10), [&]() { return eventOccurred; }));
    }

    // Reset the flag
    eventOccurred = false;

    // Assign the ackPacket to dhcpClient's dhcpAck again for renewal
    setDhcpAck(ackPacket);

    // Trigger processing of DHCP ACK for renewal
    processDhcpResponses(hostname, hardwareAddress);

    // Wait for setIPv4 to be called
    {
        std::unique_lock<std::mutex> lock(cvMutex);
        EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(10), [&]() { return eventOccurred; }));
    }

    // Verify that leaseStart has been updated
    EXPECT_TRUE(getAcked());
    EXPECT_GT(getLeaseStart(), 0.0);
    
    setAck(false);
}

// Test DHCP Release
TEST_F(DhcpClientTest, DhcpRelease_SendsCorrectPacket) {
    // Prepare a mock DHCP Release packet
    setAck(true);

    // Set expectation: enqueuePacket should be called once with DHCP Release
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1);

    // Send DHCP Release
    dhcpClient->sendDhcpRelease();

    setAck(false);
}

// Test handling DHCP Inform
TEST_F(DhcpClientTest, HandleDhcpInform_ProcessOptions)
{
    // Prepare a mock DHCP Inform packet
    PacketInfo informPacket;

    // Layer2: Ethernet Header
    EthernetHeader eth;
    eth.destinationMac = hardwareAddress; // Client MAC
    eth.sourceMac = std::string("\xc0\xa8\x00\x01\x0a\x0b"); // Server MAC
    eth.type = std::string("\x08\x00"); // IPv4
    informPacket.Layer2.emplace_back(std::move(eth));

    // Layer5: DHCP header
    DhcpHeader dhcpInform;
    dhcpInform.boot = Variable::Dhcp::Type::inform;
    dhcpInform.transID = dhcpClient->generateDhcpTransid();
    dhcpInform.yourClientIP = "\xc0\xa8\x00\x64"; // 192.168.0.100
    dhcpInform.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::leaseTime,
        "\x04",
        "\x00\x00\x0e\x10" // 3600 seconds
    });
    dhcpInform.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::mask,
        std::string("\x04", 1),
        std::string("\xFF\xFF\xFF\x00", 4)
    });
    dhcpInform.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::domainServer,
        std::string("\x04", 1),
        std::string("\x08\x08\x08\x08", 4) // DNS Server (e.g., 8.8.8.8)
    });
    dhcpInform.end = Variable::Dhcp::end;

    informPacket.Layer5.emplace_back(std::move(dhcpInform));

    // Assign the informPacket to dhcpClient's dhcpInformPacket
    setDhcpInform(informPacket);

    // Expect that processing DHCP Inform does not enqueue any packets
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(0);

    // Trigger processing of DHCP Inform
    processDhcpResponses(hostname, hardwareAddress);

    // Verify that DHCP state remains unchanged
    EXPECT_FALSE(getAcked());
    EXPECT_FALSE(getNaked());
}

// Test Lease Renewal when DHCP ACK is not received within expected time
TEST_F(DhcpClientTest, LeaseRenewal_NoAck_HandlesTimeout) 
{
    // Simulate an initial lease
    setLeaseTime(secondsSinceEpoch() - 4000); // Assume lease time is 3600S

    // Set renewal time to 3600 seconds
    dhcpClient->configs.renewalTime = ByteString("\x00\x00\x0e\x10", 4); // 3600 seconds

    // Bool indicating the first transmission
    bool firstTransmission = false;
    bool secondTransmission = false;

    // No DHCP ACK is received; expect the client to resend DHCP Request
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(4)
        .WillRepeatedly(::testing::Invoke([&](PacketInfo&, ByteString) {
            // Notify the test each time a packet is enqueued
            std::lock_guard<std::mutex> lock(cvMutex);
            eventOccurred = true;
            if (secondTransmission)
            {
                setAck(true);
                notifycv();
            }
            if (firstTransmission)
            {
                secondTransmission = true;
            }
            firstTransmission = true;
            cv.notify_one();
        }));

    // Trigger lease renewal
    handleLeaseRenewal(hardwareAddress, hostname);

    // Wait for at least one DHCP Request to be enqueued
    {
        std::unique_lock<std::mutex> lock(cvMutex);
        EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(10), [&]() { return eventOccurred; }));
    }

    // Reset the flag
    eventOccurred = false;


    // Optionally, verify that the client eventually stops trying after max retries
    // This depends on your implementation's retry logic
}

// Test lease expiration triggers renewal
TEST_F(DhcpClientTest, LeaseExpiration_TriggersRenewal) 
{
    // Simulate an initial lease
    setLeaseTime(secondsSinceEpoch() - 4000); // Half of lease time (3600S)

    // Set renewal time to 1800 seconds
    dhcpClient->configs.renewalTime = ByteString("\x00\x00\x07\x08", 4); // 1800 seconds

    // Verify that leaseStart has been updated after receiving ACK
    // Simulate receiving DHCP ACK
    PacketInfo ackPacket;
    EthernetHeader eth;
    eth.destinationMac = hardwareAddress; // Client MAC
    eth.sourceMac = "\xc0\xa8\x00\x01\x0a\x0b"; // Server MAC
    eth.type = "\x08\x00"; // IPv4
    ackPacket.Layer2.emplace_back(std::move(eth));

    DhcpHeader dhcpAck;
    dhcpAck.boot = Variable::Dhcp::Type::ack;
    dhcpAck.transID = dhcpClient->generateDhcpTransid();
    dhcpAck.yourClientIP = "\xc0\xa8\x00\x64"; // 192.168.0.100
    dhcpAck.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::leaseTime,
        "\x04",
        "\x00\x00\x0e\x10" // 3600 seconds
    });
    dhcpAck.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::mask,
        "\x04",
        "\xFF\xFF\xFF\x00" // 255.255.255.0
    });
    dhcpAck.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::router,
        "\x04",
        "\xc0\xa8\x00\x01" // 192.168.0.1
    });
    dhcpAck.end = Variable::Dhcp::end;

    ackPacket.Layer5.emplace_back(std::move(dhcpAck));

    // Expect enqueuePacket to be called once for renewal
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](PacketInfo&, ByteString) {
            // Notify the test once the DHCP Request is enqueued
            std::lock_guard<std::mutex> lock(cvMutex);
            eventOccurred = true;
            // Assign the ackPacket to dhcpClient's dhcpAck
            setDhcpAck(ackPacket);
            // Trigger processing of DHCP ACK
            processDhcpResponses(hostname, hardwareAddress);
            cv.notify_one();
        }));

    // Trigger lease renewal
    handleLeaseRenewal(hardwareAddress, hostname);

    // Wait for the DHCP Request to be enqueued
    {
        std::unique_lock<std::mutex> lock(cvMutex);
        EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&]() { return eventOccurred; }));
    }

    // Wait for setIPv4 to be called
    {
        std::unique_lock<std::mutex> lock(cvMutex);
        EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&]() { return eventOccurred; }));
    }

    // Verify that leaseStart has been updated
    EXPECT_TRUE(getAcked());
    EXPECT_GT(getLeaseStart(), 0.0);

    setAck(false);
}

// Test DHCP Client Initialization and Cleanup
TEST_F(DhcpClientTest, InitializeAndCleanup_DhcpClientLifecycle) 
{
    // Expect that enqueuePacket is called at least once during initialization
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(::testing::AtLeast(1))
        .WillRepeatedly(::testing::Invoke([&](PacketInfo&, ByteString) {
            // Notify the test each time a packet is enqueued
            std::lock_guard<std::mutex> lock(cvMutex);
            eventOccurred = true;
            cv.notify_one();
        }));

    // Initialize DHCP
    dhcpClient->InitializeDhcp(hardwareAddress);

    // Wait for the DHCP Discover to be enqueued
    {
        std::unique_lock<std::mutex> lock(cvMutex);
        EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&]() { return eventOccurred; }));
    }

    // Reset the flag
    eventOccurred = false;

    // Cleanup DHCP Client (Destructor will be called in TearDown)
    dhcpClient->~DhcpClient();

    // Verify that stopFlag is set
    EXPECT_TRUE(stopFlag());

    // Optionally, verify that no more packets are enqueued after cleanup
    // This depends on your implementation's thread handling
}

// Test full DHCP lease acquisition process: Discover -> Offer -> Request -> ACK
TEST_F(DhcpClientTest, FullDhcpLeaseProcess_AcquireLease) 
{
    // Build offer packet
    PacketInfo offerPacket;
    EthernetHeader ethOffer;
    ethOffer.destinationMac = hardwareAddress; // Client MAC
    ethOffer.sourceMac = "\x0a\x0b\x0c\x0d\x0e\x0f"; // Server MAC
    ethOffer.type = "\x08\x00"; // IPv4
    offerPacket.Layer2.emplace_back(std::move(ethOffer));

    DhcpHeader dhcpOffer;
    dhcpOffer.boot = Variable::Dhcp::Type::offer;
    dhcpOffer.transID = dhcpClient->generateDhcpTransid();
    dhcpOffer.yourClientIP = "\xc0\xa8\x00\x64"; // 192.168.0.100
    dhcpOffer.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::serverIdentifier,
        "\x04",
        "\xc0\xa8\x00\x01" // 192.168.0.1
    });
    dhcpOffer.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::leaseTime,
        "\x04",
        "\x00\x00\x0e\x10" // 3600 seconds
    });
    dhcpOffer.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::mask,
        "\x04",
        "\xFF\xFF\xFF\x00" // 255.255.255.0
    });
    dhcpOffer.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::router,
        "\x04",
        "\xc0\xa8\x00\x01" // 192.168.0.1
    });
    dhcpOffer.end = Variable::Dhcp::end;

    offerPacket.Layer5.emplace_back(std::move(dhcpOffer));

    // Build ack packet
    PacketInfo ackPacket;
    EthernetHeader ethAck;
    ethAck.destinationMac = hardwareAddress; // Client MAC
    ethAck.sourceMac = "\xc0\xa8\x00\x01\x0a\x0b"; // Server MAC
    ethAck.type = "\x08\x00"; // IPv4
    ackPacket.Layer2.emplace_back(std::move(ethAck));

    DhcpHeader dhcpAck;
    dhcpAck.boot = Variable::Dhcp::Type::ack;
    dhcpAck.transID = dhcpOffer.transID; // Same transaction ID
    dhcpAck.yourClientIP = "\xc0\xa8\x00\x64"; // 192.168.0.100
    dhcpAck.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::leaseTime,
        "\x04",
        "\x00\x00\x0e\x10" // 3600 seconds
    });
    dhcpAck.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::mask,
        "\x04",
        "\xFF\xFF\xFF\x00" // 255.255.255.0
    });
    dhcpAck.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::router,
        "\x04",
        "\xc0\xa8\x00\x01" // 192.168.0.1
    });
    dhcpAck.end = Variable::Dhcp::end;

    ackPacket.Layer5.emplace_back(std::move(dhcpAck));

    // Step 1: DHCP Discover is sent during InitializeDhcp
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](PacketInfo&, ByteString) {
            // Notify the test once the DHCP Discover is enqueued
            std::lock_guard<std::mutex> lock(cvMutex);
            eventOccurred = true;
            cv.notify_one();
        }));

    // Initialize DHCP
    dhcpClient->InitializeDhcp(hardwareAddress);

    // Wait for DHCP Discover to be enqueued
    {
        std::unique_lock<std::mutex> lock(cvMutex);
        EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&]() { return eventOccurred; }));
    }

    // Reset the flag
    eventOccurred = false;

    // Expect DHCP Request to be enqueued after processing offer
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](PacketInfo&, ByteString) {
            // Notify the test once the DHCP Request is enqueued
            std::lock_guard<std::mutex> lock(cvMutex);
            eventOccurred = true;
            cv.notify_one();
        }));

    // Process DHCP Offer
    setDhcpOffer(offerPacket);
    processDhcpResponses(hostname, hardwareAddress);

    // Wait for DHCP Request to be enqueued
    {
        std::unique_lock<std::mutex> lock(cvMutex);
        EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(10), [&]() { return eventOccurred; }));
    }

    // Reset the flag
    eventOccurred = false;

    // Expect setIPv4 to be called once
    EXPECT_CALL(*mockInterface, setIPv4(ByteString("\xc0\xa8\x00\x64"), ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](ByteString, uint8_t) {
            // Notify the test once setIPv4 is called
            std::lock_guard<std::mutex> lock(cvMutex);
            eventOccurred = true;
            cv.notify_one();
        }));

    // Assign the ackPacket to dhcpClient's dhcpAck
    setDhcpAck(ackPacket);
    processDhcpResponses(hostname, hardwareAddress);

    // Wait for setIPv4 to be called
    {
        std::unique_lock<std::mutex> lock(cvMutex);
        EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&]() { return eventOccurred; }));
    }

    // Verify that leaseStart has been updated and acked is true
    EXPECT_TRUE(getAcked());
    EXPECT_GT(getLeaseStart(), 0.0);

    setAck(false);
}