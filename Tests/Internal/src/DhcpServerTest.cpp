// DhcpServerTest.cpp

#include <gtest/gtest.h>
#include <Dhcp.h>
#include <Interface.h>
#include <MockInterface.hpp>
#include <ByteString.hpp>
#include <Functions.h>
#include <thread>

using namespace Protocol;

// Helper function to create DHCP Header
DhcpHeader createDhcpHeader(
    const ByteString& messageType,
    const ByteString& relayAgentIP,
    const ByteString& clientMacAddress = ByteString(),
    const ByteString& transID = ByteString(),
    const ByteString& yourClientIP = ByteString(),
    const ByteString& requestedIP = ByteString(),
    const ByteString& serverIdentifier = ByteString())
{
    DhcpHeader dhcp;
    dhcp.boot = messageType;
    dhcp.hardwareType = ByteString("\x01", 1);
    dhcp.hardwareAddressLength = ByteString("\x06", 1);
    dhcp.hops = ByteString("\x00", 1);
    dhcp.transID = transID.empty() ? ByteString(4, '\x00') : transID;
    dhcp.secondsElapsed = ByteString(2, '\x00');
    dhcp.bootpFlags.broadcast = "0";
    dhcp.bootpFlags.reserved = "000000000000000";
    dhcp.clientIP = ByteString(4, '\x00');
    dhcp.yourClientIP = yourClientIP;
    dhcp.nextServerIP = ByteString(4, '\x00');
    dhcp.relayAgentIP = relayAgentIP;
    dhcp.clientMacAddress = clientMacAddress;
    dhcp.clientHardwareAddressPadding = Variable::Dhcp::clientHardwareAddressPadding;
    dhcp.serverHostName = Variable::Dhcp::serverHostName;
    dhcp.bootFile = Variable::Dhcp::bootfile;
    dhcp.magicCookie = Variable::Dhcp::magicCookie;

    // Add Options
    if (messageType == Variable::Dhcp::Type::discover)
    {
        dhcp.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::type,
            ByteString("\x01", 1),
            Variable::Dhcp::Type::discover
        });
    }
    else if (messageType == Variable::Dhcp::Type::request)
    {
        dhcp.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::type,
            ByteString("\x01", 1),
            Variable::Dhcp::Type::request
        });
        if (!requestedIP.empty())
        {
            dhcp.options.emplace_back(DhcpHeader::Option{
                Variable::Dhcp::Option::requestIP,
                ByteString("\x04", 1),
                requestedIP
            });
        }
        if (!serverIdentifier.empty())
        {
            dhcp.options.emplace_back(DhcpHeader::Option{
                Variable::Dhcp::Option::serverIdentifier,
                ByteString("\x04", 1),
                serverIdentifier
            });
        }
    }
    else if (messageType == Variable::Dhcp::Type::release)
    {
        dhcp.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::type,
            ByteString("\x01", 1),
            Variable::Dhcp::Type::release
        });
    }

    dhcp.end = Variable::Dhcp::end;
    return dhcp;
}

// Test Ficture for DHCP server
class DhcpServerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        mockInterface = new ::testing::NiceMock<MockInterface>();
        dhcpServer = new DhcpServer();
        networkConfig = new Dhcp::DhcpNetworkConfig;

        // Define a test network configuration
        ByteString network = ByteString("\xc0\xa8\x00\x00", 4); // 192.168.0.0
        uint8_t subnetPrefix = 24; // 255.255.255.0
        ByteString defaultGateway = ByteString("\xc0\xa8\x00\x01", 4); // 192.168.0.1
        networkConfig->updateNetwork(&network, &subnetPrefix, &defaultGateway, dhcpServer);
        networkConfig->dnsServer = { ByteString("\x08\x08\x08\x08", 4), ByteString("\x08\x08\x04\x04", 4) };
        networkConfig->leaseTime = 3600;
        networkConfig->renewalTime = ByteString("\x00\x00\x07\x08", 4);
        networkConfig->rebindingTime = ByteString("\x00\x00\x0b\xb8");
        networkConfig->interface = mockInterface;

        // Add the test network to the DHCP server
        dhcpServer->addNetwork(networkConfig);
    }

    void TearDown() override
    {
        delete dhcpServer;
        delete mockInterface;
    }

    // Helper method to add a network
    void addTestNetwork(Dhcp::DhcpNetworkConfig& config)
    {
        dhcpServer->addNetwork(&config);
    }

    // Member variables
    MockInterface* mockInterface;
    DhcpServer* dhcpServer;
    Dhcp::DhcpNetworkConfig* networkConfig = nullptr;

    // Helper functions
    std::unordered_map<ByteString, Dhcp::DhcpNetwork*> getDhcpNetworks() { return dhcpServer->dhcpNetworks; }
    ByteString allocateIPAddress(const ByteString& network, uint32_t subnet, ByteString& mac) {return dhcpServer->dhcpNetworks[network + "/" + std::to_string(subnet)]->lease->allocateIP(networkConfig->leaseTime, networkConfig->t1Percentage, networkConfig->t2Percentage, &mac); }
    void handleDhcpPacket(const PacketInfo& packet) {dhcpServer->handleDhcpPacket(packet, mockInterface);}
    std::unordered_map<ByteString, LeaseManager::Lease>& getLeases(Dhcp::DhcpNetworkConfig* network) { return dhcpServer->dhcpNetworks[network->getNetworkID()]->lease->leases; }
    void cleanupExpiredLeases() {for (auto& config : dhcpServer->dhcpNetworks) {config.second->lease->cleanupExpiredLeases();}}
    std::unordered_map<ByteString, ByteString>& getAllocatedIPs(ByteString networkID) {return dhcpServer->dhcpNetworks[networkID]->pool->allocatedIPs;}
    bool isAllocated(const ByteString& network, const ByteString ip) {return dhcpServer->dhcpNetworks[network]->pool->isAllocated(ip);}
    bool isTemporary(const ByteString& network, const ByteString ip) {return dhcpServer->dhcpNetworks[network]->pool->isTemporarilyOffered(ip);}
    std::map<Protocol::Dhcp::TimerType, std::vector<Protocol::Dhcp::TrackedTimer>>& getTimeouts() {return dhcpServer->activeTimers; }
    ByteString generateTransactionID() {return dhcpServer->generateTransactionID();}
};

// Test adding a network successfully
TEST_F(DhcpServerTest, AddNetwork_Success)
{
    Dhcp::DhcpNetworkConfig* newConfig = new Dhcp::DhcpNetworkConfig;
    ByteString network = ByteString("\xc0\xa8\x01\x00", 4); // 192.168.1.0
    uint8_t subnetPrefix = 24; // 255.255.255.0
    ByteString defaultGateway = ByteString("\xc0\xa8\x01\x01", 4); // 192.168.1.1
    newConfig->updateNetwork(&network, &subnetPrefix, &defaultGateway, dhcpServer);
    newConfig->dnsServer = { ByteString("\x08\x08\x08\x08", 4) };
    newConfig->leaseTime = 7200;
    newConfig->renewalTime = ByteString("\x00\x00\x0e\x10", 4); // 3600 seconds
    newConfig->rebindingTime = ByteString("\x00\x00\x1c\x20", 4); // 7200 seconds
    newConfig->interface = mockInterface;

    // Add the new network
    dhcpServer->addNetwork(newConfig);

    // Verify that the network was added by allocating an IP
    ByteString mac = ByteString("\x00\x11\x22\x33\x44\x55", 6);
    ByteString allocatedIP = allocateIPAddress(newConfig->getNetwork(), 24, mac);
    EXPECT_EQ(allocatedIP, ByteString("\xc0\xa8\x01\x02", 4));
}

// Test DHCP Server Handling DHCP Discover and Sending DHCP Offer
TEST_F(DhcpServerTest, HandlingDhcpDiscover_SendsDhcpOffer)
{
    // Prepare a DHCP Discover packet
    PacketInfo discoverPacket;
    // Layer2: Ethernet Header
    EthernetHeader eth;
    eth.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6);
    eth.sourceMac = ByteString("\x00\x11\x22\x33\x44\x55", 6);
    eth.type = ByteString("\x08\x00", 2);
    discoverPacket.Layer2.emplace_back(std::move(eth));
    
    // Layer5: DHCP Header
    ByteString transID = generateTransactionID();
    DhcpHeader dhcpDiscover = createDhcpHeader(
        Variable::Dhcp::Type::discover,
        ByteString("\xc0\xa8\x00\x01", 4),
        ByteString("\x00\x11\x22\x33\x44\x55", 6),
        transID
    );
    discoverPacket.Layer5.emplace_back(std::move(dhcpDiscover));

    // Expect enqueuePacket to be called once with DHCP Offer
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](const PacketInfo& pkt, const ByteString&) {
            // Verify that the packet is a DHCP Offer
            ASSERT_FALSE(pkt.Layer5.empty());
            const DhcpHeader& offerHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
            EXPECT_EQ(offerHeader.boot, Variable::Dhcp::Type::offer);
            EXPECT_EQ(offerHeader.transID, transID);
            EXPECT_EQ(offerHeader.yourClientIP, ByteString("\xc0\xa8\x00\x02", 4));
        }));

    // Handle the DHCP Discover packet
    handleDhcpPacket(discoverPacket);
}

// Test DHCP Server Handling DHCP Request and Sending DHCP Ack
TEST_F(DhcpServerTest, HandleDhcpRequest_SendsDhcpAck) 
{
    // Simulate a DHCP Discover to allocate an IP first
    PacketInfo discoverPacket;
    EthernetHeader ethDiscover;
    ethDiscover.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    ethDiscover.sourceMac = ByteString("\x00\x11\x22\x33\x44\x55", 6); // Client MAC
    ethDiscover.type = "\x08\x00"; // IPv4
    discoverPacket.Layer2.emplace_back(std::move(ethDiscover));

    ByteString transID = generateTransactionID();
    DhcpHeader dhcpDiscover = createDhcpHeader(
        Variable::Dhcp::Type::discover,
        ByteString("\xc0\xa8\x00\x01", 4),
        ByteString("\x00\x11\x22\x33\x44\x55", 6),
        transID
    );
    discoverPacket.Layer5.emplace_back(std::move(dhcpDiscover));

    // Handle the DHCP Discover packet
    dhcpServer->handleDhcpPacket(discoverPacket, mockInterface);

    // Prepare a DHCP Request packet
    PacketInfo requestPacket;
    EthernetHeader ethRequest;
    ethRequest.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    ethRequest.sourceMac = ByteString("\x00\x11\x22\x33\x44\x55", 6); // Client MAC
    ethRequest.type = "\x08\x00"; // IPv4
    requestPacket.Layer2.emplace_back(std::move(ethRequest));

    DhcpHeader dhcpRequest = createDhcpHeader(
        Variable::Dhcp::Type::request,
        ByteString("\xc0\xa8\x00\x01", 4),
        ByteString("\x00\x11\x22\x33\x44\x55", 6),
        transID,
        ByteString("\x00\x00\x00\x00", 4), // Your IP
        ByteString("\xc0\xa8\x00\x02", 4), // Requested IP
        ByteString("\xc0\xa8\x00\x00", 4)  // Server Identifier
    );
    dhcpRequest.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::serverIdentifier,
        ByteString("\x04", 1),
        ByteString("\xc0\xa8\x00\x00", 4) // Server ID
    });
    requestPacket.Layer5.emplace_back(std::move(dhcpRequest));

    // Expect enqueuePacket to be called once with DHCP Ack
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](const PacketInfo& pkt, const ByteString&) {
            // Verify that the packet is a DHCP Ack
            ASSERT_FALSE(pkt.Layer5.empty());
            const DhcpHeader& ackHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
            EXPECT_EQ(ackHeader.boot, Variable::Dhcp::Type::ack);
            EXPECT_EQ(ackHeader.transID, transID);
            EXPECT_EQ(ackHeader.yourClientIP, ByteString("\xc0\xa8\x00\x02", 4)); // Acknowledged IP
            // Additional verification of DHCP options can be added here
        }));

    // Handle the DHCP Request packet
    dhcpServer->handleDhcpPacket(requestPacket, mockInterface);
}

// Test DHCP Server Handling DHCP Release
TEST_F(DhcpServerTest, HandleDhcpRelease_ReleaseIP) 
{
    // Simulate a DHCP Discover and Request to allocate an IP first
    PacketInfo discoverPacket;
    EthernetHeader ethDiscover;
    ethDiscover.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    ethDiscover.sourceMac = ByteString("\x00\x11\x22\x33\x44\x55", 6); // Client MAC
    ethDiscover.type = "\x08\x00"; // IPv4
    discoverPacket.Layer2.emplace_back(std::move(ethDiscover));

    ByteString transID = generateTransactionID();
    DhcpHeader dhcpDiscover = createDhcpHeader(
        Variable::Dhcp::Type::discover,
        ByteString("\xc0\xa8\x00\x01", 4),
        ByteString("\x00\x11\x22\x33\x44\x55", 6),
        transID
    );
    discoverPacket.Layer5.emplace_back(std::move(dhcpDiscover));

    // Handle the DHCP Discover packet
    dhcpServer->handleDhcpPacket(discoverPacket, mockInterface);

    // Prepare a DHCP Request packet
    PacketInfo requestPacket;
    EthernetHeader ethRequest;
    ethRequest.destinationMac = "\xff\xff\xff\xff\xff\xff"; // Broadcast
    ethRequest.sourceMac = "\x00\x11\x22\x33\x44\x55"; // Client MAC
    ethRequest.type = "\x08\x00"; // IPv4
    requestPacket.Layer2.emplace_back(std::move(ethRequest));

    DhcpHeader dhcpRequest = createDhcpHeader(
        Variable::Dhcp::Type::request,
        ByteString("\xc0\xa8\x00\x01", 4),
        ByteString("\x00\x11\x22\x33\x44\x55", 6),
        transID,
        ByteString("\x00\x00\x00\x00", 4), // Your IP
        ByteString("\xc0\xa8\x00\x02", 4), // Requested IP
        ByteString("\xc0\xa8\x00\x00", 4)  // Server Identifier
    );
    dhcpRequest.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::serverIdentifier,
        ByteString("\x04", 1),
        ByteString("\xc0\xa8\x00\x00", 4) // Server ID
    });
    requestPacket.Layer5.emplace_back(std::move(dhcpRequest));

    // Handle the DHCP Request packet
    dhcpServer->handleDhcpPacket(requestPacket, mockInterface);

    // Verify that the IP is allocated
    EXPECT_TRUE(getLeases(networkConfig).find(ByteString("\xc0\xa8\x00\x02", 4)) != getLeases(networkConfig).end());

    // Prepare a DHCP Release packet
    PacketInfo releasePacket;
    EthernetHeader ethRelease;
    ethRelease.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    ethRelease.sourceMac = ByteString("\x00\x11\x22\x33\x44\x55", 6); // Client MAC
    ethRelease.type = ByteString("\x08\x00", 2); // IPv4
    releasePacket.Layer2.emplace_back(std::move(ethRelease));

    DhcpHeader dhcpRelease;
    dhcpRelease.boot = Variable::Dhcp::Type::release;
    dhcpRelease.transID = transID;
    dhcpRelease.clientIP = ByteString("\xc0\xa8\x00\x02", 4); // Released IP
    dhcpRelease.clientMacAddress = ByteString("\x00\x11\x22\x33\x44\x55", 6);
    dhcpRelease.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::type,
        ByteString("\x01", 1),
        Variable::Dhcp::Type::release
    });
    dhcpRelease.end = Variable::Dhcp::end;
    releasePacket.Layer5.emplace_back(std::move(dhcpRelease));

    // Expect no DHCP packet to be enqueued since Release does not require a response
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(0);

    // Handle the DHCP Release packet
    dhcpServer->handleDhcpPacket(releasePacket, mockInterface);

    // Verify that the lease has been removed
    EXPECT_TRUE(getLeases(networkConfig).find(ByteString("\xc0\xa8\x00\x02", 4)) == getLeases(networkConfig).end());
}

// Test DHCP Server Handling DHCP NAK
TEST_F(DhcpServerTest, HandleDhcpNak_SendsDhcpNak) 
{
    // Prepare a DHCP Request packet that will trigger a NAK
    PacketInfo requestPacket;
    EthernetHeader ethRequest;
    ethRequest.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    ethRequest.sourceMac = ByteString("\x00\x11\x22\x33\x44\x55", 6); // Client MAC
    ethRequest.type = ByteString("\x08\x00", 2); // IPv4
    requestPacket.Layer2.emplace_back(std::move(ethRequest));

    ByteString transID = generateTransactionID();
    DhcpHeader dhcpRequest = createDhcpHeader(
        Variable::Dhcp::Type::request,
        ByteString("\xc0\xa8\x00\x01", 4),
        ByteString("\x00\x11\x22\x33\x44\x55", 6),
        transID,
        ByteString("\x00\x00\x00\x00", 4), // Requested IP that is not allocated
        ByteString("\xc0\xa8\x00\x05", 4), // Requested IP
        ByteString("\xc0\xa8\x00\x00", 4)  // Server Identifier
    );
    dhcpRequest.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::serverIdentifier,
        ByteString("\x04", 1),
        ByteString("\xc0\xa8\x00\x00", 4) // Server ID
    });
    requestPacket.Layer5.emplace_back(std::move(dhcpRequest));

    // Expect enqueuePacket to be called once with DHCP Nak
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](const PacketInfo& pkt, const ByteString&) {
            // Verify that the packet is a DHCP Nak
            ASSERT_FALSE(pkt.Layer5.empty());
            const DhcpHeader& nakHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
            EXPECT_EQ(nakHeader.boot, Variable::Dhcp::Type::nak);
            EXPECT_EQ(nakHeader.transID, transID);
            EXPECT_EQ(nakHeader.yourClientIP, ByteString("\x00\x00\x00\x00", 4)); // No IP assigned
            // Additional verification of DHCP options can be added here
        }));

    // Handle the DHCP Request packet
    dhcpServer->handleDhcpPacket(requestPacket, mockInterface);
}

// Test DHCP Server Lease Expiration and Cleanup
TEST_F(DhcpServerTest, LeaseExpiration_CleanupExpiredLeases) 
{
    // Simulate a lease that has expired
    ByteString ip = ByteString("\xc0\xa8\x00\x02", 4); // 192.168.0.2
    ByteString mac = ByteString("\x00\x11\x22\x33\x44\x66", 6);
    double pastTime = secondsSinceEpoch() - 4000; // Lease time was 3600 seconds
    getLeases(networkConfig)[ip] = { ip, mac, pastTime, 3600, 1800, 3400 };

    // Expect that the lease is cleaned up by releasing the IP
    // Depending on implementation, the server may enqueue a DHCP Release or simply remove the lease
    // For this test, we'll assume it simply removes the lease
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(0); // No packet is sent during cleanup

    // Trigger lease cleanup
    cleanupExpiredLeases();

    // Verify that the lease has been removed
    EXPECT_TRUE(getLeases(networkConfig).find(ip) == getLeases(networkConfig).end());
}

// Test DHCP Server Lease Renewal
TEST_F(DhcpServerTest, LeaseRenewal_HandlesRenewalProperly) 
{
    // Simulate a valid lease that is up for renewal
    ByteString ip = ByteString("\xc0\xa8\x00\x03", 4); // 192.168.0.3
    ByteString mac = ByteString("\x00\x11\x22\x33\x44\x77", 6);
    double leaseStart = secondsSinceEpoch() - 1800; // Half of leaseTime (3600 seconds)
    getLeases(networkConfig)[ip] = { ip, mac, leaseStart, 3600, 1800, 3400 };
    getAllocatedIPs(networkConfig->getNetworkID())[ByteString("\xc0\xa8\x00\x03", 4)] = mac;

    // Prepare a DHCP Request for renewal
    PacketInfo requestPacket;
    EthernetHeader ethRequest;
    ethRequest.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    ethRequest.sourceMac = mac; // Client MAC
    ethRequest.type = "\x08\x00"; // IPv4
    requestPacket.Layer2.emplace_back(std::move(ethRequest));

    ByteString transID = generateTransactionID();
    DhcpHeader dhcpRequest = createDhcpHeader(
        Variable::Dhcp::Type::request,
        ByteString("\xc0\xa8\x00\x01", 4),
        mac,
        transID,
        ByteString("\x00\x00\x00\x00", 4),     // Your IP
        ip,                                    // Requested IP
        ByteString("\xc0\xa8\x00\x00", 4)      // Server Identifier
    );
    dhcpRequest.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::serverIdentifier,
        ByteString("\x04", 1),
        ByteString("\xc0\xa8\x00\x00", 4) // Server ID
    });
    requestPacket.Layer5.emplace_back(std::move(dhcpRequest));

    // Expect enqueuePacket to be called once with DHCP Ack for renewal
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](const PacketInfo& pkt, const ByteString&) {
            // Verify that the packet is a DHCP Ack
            ASSERT_FALSE(pkt.Layer5.empty());
            const DhcpHeader& ackHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
            EXPECT_EQ(ackHeader.boot, Variable::Dhcp::Type::ack);
            EXPECT_EQ(ackHeader.transID, transID);
            EXPECT_EQ(ackHeader.yourClientIP, ip); // Acknowledged IP
            // Additional verification of DHCP options can be added here
        }));

    // Handle the DHCP Request packet
    dhcpServer->handleDhcpPacket(requestPacket, mockInterface);

    // Verify that the lease start time has been updated
    EXPECT_NE(getLeases(networkConfig)[ip].leaseStart, leaseStart);
}

// Test DHCP Server Handling Multiple Clients
TEST_F(DhcpServerTest, HandleMultipleClients_AllReceiveUniqueIPs) 
{
    // Define multiple client MAC addresses
    std::vector<ByteString> clientMACs = {
        ByteString("\x00\x11\x22\x33\x44\x55", 6),
        ByteString("\x00\x11\x22\x33\x44\x66", 6),
        ByteString("\x00\x11\x22\x33\x44\x77)")
    };

    // Define expected allocated IPs
    std::vector<ByteString> expectedIPs = {
        ByteString("\xc0\xa8\x00\x02", 4),
        ByteString("\xc0\xa8\x00\x03", 4),
        ByteString("\xc0\xa8\x00\x04", 4)
    };

    // Iterate over each client to send DHCP Discover and handle Offer
    for (size_t i = 0; i < clientMACs.size(); ++i) {
        PacketInfo discoverPacket;
        EthernetHeader ethDiscover;
        ethDiscover.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
        ethDiscover.sourceMac = clientMACs[i];
        ethDiscover.type = "\x08\x00"; // IPv4
        discoverPacket.Layer2.emplace_back(std::move(ethDiscover));

        ByteString transID = generateTransactionID();
        DhcpHeader dhcpDiscover = createDhcpHeader(
            Variable::Dhcp::Type::discover,
            ByteString("\xc0\xa8\x00\x01", 4),
            clientMACs[i],
            transID
        );
        discoverPacket.Layer5.emplace_back(std::move(dhcpDiscover));

        // Expect enqueuePacket to be called once with DHCP Offer
        EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
            .Times(1)
            .WillOnce(::testing::Invoke([&, i](const PacketInfo& pkt, const ByteString&) {
                // Verify that the packet is a DHCP Offer
                ASSERT_FALSE(pkt.Layer5.empty());
                const DhcpHeader& offerHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
                EXPECT_EQ(offerHeader.boot, Variable::Dhcp::Type::offer);
                EXPECT_EQ(offerHeader.yourClientIP, expectedIPs[i]);
                // Additional verification of DHCP options can be added here
            }));

        // Handle the DHCP Discover packet
        dhcpServer->handleDhcpPacket(discoverPacket, mockInterface);
    }

    // Now, simulate DHCP Requests from all clients
    for (size_t i = 0; i < clientMACs.size(); ++i) {
        PacketInfo requestPacket;
        EthernetHeader ethRequest;
        ethRequest.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
        ethRequest.sourceMac = clientMACs[i];
        ethRequest.type = "\x08\x00"; // IPv4
        requestPacket.Layer2.emplace_back(std::move(ethRequest));

        ByteString transID = generateTransactionID();
        DhcpHeader dhcpRequest = createDhcpHeader(
            Variable::Dhcp::Type::request,
            ByteString("\xc0\xa8\x00\x01", 4),
            clientMACs[i],
            transID,
            ByteString("\x00\x00\x00\x00"),   // Your IP
            expectedIPs[i],                   // Requested IP
            ByteString("\xc0\xa8\x00\x00", 4) // Server Identifier
        );
        dhcpRequest.options.emplace_back(DhcpHeader::Option{
            Variable::Dhcp::Option::serverIdentifier,
            ByteString("\x04", 1),
            ByteString("\xc0\xa8\x00\x00", 4) // Server ID
        });
        requestPacket.Layer5.emplace_back(std::move(dhcpRequest));

        // Expect enqueuePacket to be called once with DHCP Ack
        EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
            .Times(1)
            .WillOnce(::testing::Invoke([&, i](const PacketInfo& pkt, const ByteString&) {
                // Verify that the packet is a DHCP Ack
                ASSERT_FALSE(pkt.Layer5.empty());
                const DhcpHeader& ackHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
                EXPECT_EQ(ackHeader.boot, Variable::Dhcp::Type::ack);
                EXPECT_EQ(ackHeader.transID, transID);
                EXPECT_EQ(ackHeader.yourClientIP, expectedIPs[i]);
                // Additional verification of DHCP options can be added here
            }));

        // Handle the DHCP Request packet
        dhcpServer->handleDhcpPacket(requestPacket, mockInterface);
    }

    // Verify that each client has a unique lease
    for (size_t i = 0; i < clientMACs.size(); ++i) {
        EXPECT_TRUE(getLeases(networkConfig).find(expectedIPs[i]) != getLeases(networkConfig).end());
        EXPECT_EQ(getLeases(networkConfig)[expectedIPs[i]].clientID, clientMACs[i]);
    }
}
// Test DHCP Server Handling DHCP Inform
TEST_F(DhcpServerTest, HandleDhcpInform_SendsDhcpAckWithoutIPAssignment) 
{
    // Prepare a DHCP Inform packet
    PacketInfo informPacket;
    // Layer2: Ethernet Header
    EthernetHeader eth;
    eth.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    eth.sourceMac = ByteString("\x00\x11\x22\x33\x44\x88", 6); // Client MAC
    eth.type = ByteString("\x08\x00", 2); // IPv4
    informPacket.Layer2.emplace_back(std::move(eth));

    ByteString transID = generateTransactionID();
    DhcpHeader dhcpInform = createDhcpHeader(
        Variable::Dhcp::Type::inform,
        ByteString("\xc0\xa8\x00\x01", 4),
        ByteString("\x00\x11\x22\x33\x44\x88", 6),
        transID
    );
    dhcpInform.clientIP = ByteString("\xc0\xa8\x00\x04", 4); // Client's current IP
    dhcpInform.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::type,
        ByteString("\x01", 1),
        Variable::Dhcp::Type::inform
    });
    dhcpInform.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::clientID,
        ByteString("\x06", 6),
        ByteString("\x00\x11\x22\x33\x44\x88", 6)
    });
    dhcpInform.end = Variable::Dhcp::end;
    informPacket.Layer5.emplace_back(std::move(dhcpInform));

    // Expect enqueuePacket to be called once with DHCP Ack containing no IP assignment
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](const PacketInfo& pkt, const ByteString&) {
            // Verify that the packet is a DHCP Ack for Inform
            ASSERT_FALSE(pkt.Layer5.empty());
            const DhcpHeader& ackHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
            EXPECT_EQ(ackHeader.boot, Variable::Dhcp::Type::ack);
            EXPECT_EQ(ackHeader.transID, transID);
            EXPECT_EQ(ackHeader.clientIP, ByteString("\xc0\xa8\x00\x04", 4)); // Acknowledged IP remains the same
            // Additional verification of DHCP options can be added here
        }));

    // Handle the DHCP Inform packet
    dhcpServer->handleDhcpPacket(informPacket, mockInterface);
}

// Test DHCP Server Handling Invalid DHCP Packets
TEST_F(DhcpServerTest, HandleInvalidDhcpPacket_IgnoresPacket) 
{
    // Prepare an invalid DHCP packet (missing DHCP Header)
    PacketInfo invalidPacket;
    // Layer2: Ethernet Header
    EthernetHeader eth;
    eth.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    eth.sourceMac = ByteString("\x00\x11\x22\x33\x44\x99", 6); // Client MAC
    eth.type = "\x08\x00"; // IPv4
    invalidPacket.Layer2.emplace_back(std::move(eth));

    // No Layer5: DHCP Header

    // Expect enqueuePacket to never be called
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(0);

    // Handle the invalid DHCP packet
    dhcpServer->handleDhcpPacket(invalidPacket, mockInterface);
}

// Test DHCP Server Handling Duplicate MAC Addresses
TEST_F(DhcpServerTest, HandleDuplicateMacAddress_AssignsSameIP) 
{
    // Define a client MAC address
    ByteString mac = ByteString("\x00\x11\x22\x33\x44\xAA", 6);

    // Simulate first DHCP Discover and Request
    PacketInfo discoverPacket1;
    EthernetHeader ethDiscover1;
    ethDiscover1.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    ethDiscover1.sourceMac = mac;
    ethDiscover1.type = ByteString("\x08\x00", 2); // IPv4
    discoverPacket1.Layer2.emplace_back(std::move(ethDiscover1));

    ByteString transID1 = generateTransactionID();
    DhcpHeader dhcpDiscover1 = createDhcpHeader(
        Variable::Dhcp::Type::discover,
        ByteString("\xc0\xa8\x00\x01", 4),
        mac,
        transID1
    );
    discoverPacket1.Layer5.emplace_back(std::move(dhcpDiscover1));

    // Expect enqueuePacket to be called once with DHCP Offer
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](const PacketInfo& pkt, const ByteString&) {
            // Verify that the packet is a DHCP Offer
            ASSERT_FALSE(pkt.Layer5.empty());
            const DhcpHeader& offerHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
            EXPECT_EQ(offerHeader.boot, Variable::Dhcp::Type::offer);
            EXPECT_EQ(offerHeader.yourClientIP, ByteString("\xc0\xa8\x00\x02", 4)); // First usable IP
        }));

    // Handle the first DHCP Discover packet
    dhcpServer->handleDhcpPacket(discoverPacket1, mockInterface);

    // Simulate first DHCP Request
    PacketInfo requestPacket1;
    EthernetHeader ethRequest1;
    ethRequest1.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    ethRequest1.sourceMac = mac;
    ethRequest1.type = ByteString("\x08\x00", 2); // IPv4
    requestPacket1.Layer2.emplace_back(std::move(ethRequest1));

    DhcpHeader dhcpRequest1 = createDhcpHeader(
        Variable::Dhcp::Type::request,
        ByteString("\xc0\xa8\x00\x01", 4),
        mac,
        transID1,
        ByteString("\x00\x00\x00\x00", 4), // Your IP
        ByteString("\xc0\xa8\x00\x02", 4), // Requested IP
        ByteString("\xc0\xa8\x00\x00", 4)  // Server Identifier
    );
    dhcpRequest1.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::serverIdentifier,
        ByteString("\x04", 1),
        ByteString("\xc0\xa8\x00\x00", 4) // Server ID
    });
    requestPacket1.Layer5.emplace_back(std::move(dhcpRequest1));

    // Expect enqueuePacket to be called once with DHCP Ack
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](const PacketInfo& pkt, const ByteString&) {
            // Verify that the packet is a DHCP Ack
            ASSERT_FALSE(pkt.Layer5.empty());
            const DhcpHeader& ackHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
            EXPECT_EQ(ackHeader.boot, Variable::Dhcp::Type::ack);
            EXPECT_EQ(ackHeader.transID, transID1);
            EXPECT_EQ(ackHeader.yourClientIP, ByteString("\xc0\xa8\x00\x02", 4)); // Acknowledged IP
        }));

    // Handle the first DHCP Request packet
    dhcpServer->handleDhcpPacket(requestPacket1, mockInterface);

    // Simulate second DHCP Discover and Request with the same MAC
    PacketInfo discoverPacket2;
    EthernetHeader ethDiscover2;
    ethDiscover2.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    ethDiscover2.sourceMac = mac;
    ethDiscover2.type = ByteString("\x08\x00", 2); // IPv4
    discoverPacket2.Layer2.emplace_back(std::move(ethDiscover2));

    ByteString transID2 = generateTransactionID();
    DhcpHeader dhcpDiscover2 = createDhcpHeader(
        Variable::Dhcp::Type::discover,
        ByteString("\xc0\xa8\x00\x01", 4),
        mac,
        transID2
    );
    discoverPacket2.Layer5.emplace_back(std::move(dhcpDiscover2));

    // Expect enqueuePacket to be called once with DHCP Offer (same IP)
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](const PacketInfo& pkt, const ByteString&) {
            // Verify that the packet is a DHCP Offer with the same IP
            ASSERT_FALSE(pkt.Layer5.empty());
            const DhcpHeader& offerHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
            EXPECT_EQ(offerHeader.boot, Variable::Dhcp::Type::offer);
            EXPECT_EQ(offerHeader.yourClientIP, ByteString("\xc0\xa8\x00\x02", 4)); // Same IP
        }));

    // Handle the second DHCP Discover packet
    dhcpServer->handleDhcpPacket(discoverPacket2, mockInterface);

    // Simulate second DHCP Request
    PacketInfo requestPacket2;
    EthernetHeader ethRequest2;
    ethRequest2.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    ethRequest2.sourceMac = mac;
    ethRequest2.type = ByteString("\x08\x00", 2); // IPv4
    requestPacket2.Layer2.emplace_back(std::move(ethRequest2));

    DhcpHeader dhcpRequest2 = createDhcpHeader(
        Variable::Dhcp::Type::request,
        ByteString("\xc0\xa8\x00\x01", 4),
        mac,
        transID2,
        ByteString("\x00\x00\x00\x00", 4), // Your IP
        ByteString("\xc0\xa8\x00\x02", 4), // Requested IP
        ByteString("\xc0\xa8\x00\x00", 4)  // Server Identifier
    );
    dhcpRequest2.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::serverIdentifier,
        ByteString("\x04", 1),
        ByteString("\xc0\xa8\x00\x00", 4) // Server ID
    });
    requestPacket2.Layer5.emplace_back(std::move(dhcpRequest2));

    // Expect enqueuePacket to be called once with DHCP Ack (same IP)
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](const PacketInfo& pkt, const ByteString&) {
            // Verify that the packet is a DHCP Ack
            ASSERT_FALSE(pkt.Layer5.empty());
            const DhcpHeader& ackHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
            EXPECT_EQ(ackHeader.boot, Variable::Dhcp::Type::ack);
            EXPECT_EQ(ackHeader.transID, transID2);
            EXPECT_EQ(ackHeader.clientIP, ByteString("\xc0\xa8\x00\x02", 4)); // Same IP
        }));

    // Handle the second DHCP Request packet
    dhcpServer->handleDhcpPacket(requestPacket2, mockInterface);

    // Verify that only one lease exists for the IP
    EXPECT_EQ(getLeases(networkConfig).size(), 1);
    EXPECT_TRUE(getLeases(networkConfig).find(ByteString("\xc0\xa8\x00\x02", 4)) != getLeases(networkConfig).end());
    EXPECT_EQ(getLeases(networkConfig)[ByteString("\xc0\xa8\x00\x02", 4)].clientID, mac);
}

// Test DHCP Server Automatic Lease Expiration and Cleanup
TEST_F(DhcpServerTest, AutomaticLeaseExpiration_CleansUpLeases) 
{
    // Simulate multiple leases with varying leaseStart times
    ByteString ip1 = ByteString("\xc0\xa8\x00\x04", 4); // 192.168.0.4
    ByteString mac1 = ByteString("\x00\x11\x22\x33\x44\x88", 6);
    double currentTime = secondsSinceEpoch();
    getLeases(networkConfig)[ip1] = { ip1, mac1, currentTime - 4000, 3600, 1800, 3400}; // Expired

    ByteString ip2 = ByteString("\xc0\xa8\x00\x05", 4); // 192.168.0.5
    ByteString mac2 = ByteString("\x00\x11\x22\x33\x44\x99", 6);
    getLeases(networkConfig)[ip2] = { ip2, mac2, currentTime - 2000, 3600, 1800, 3400 }; // Active

    ByteString ip3 = ByteString("\xc0\xa8\x00\x06", 4); // 192.168.0.6
    ByteString mac3 = ByteString("\x00\x11\x22\x33\x44\xAA", 6);
    getLeases(networkConfig)[ip3] = { ip3, mac3, currentTime - 5000, 3600, 1800, 3400 }; // Expired

    // Expect enqueuePacket to be called zero times during cleanup if no packet is sent
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(0);

    // Trigger lease cleanup
    cleanupExpiredLeases();

    // Verify that expired leases are removed and active leases remain
    EXPECT_TRUE(getLeases(networkConfig).find(ip1) == getLeases(networkConfig).end());
    EXPECT_TRUE(getLeases(networkConfig).find(ip3) == getLeases(networkConfig).end());
    EXPECT_TRUE(getLeases(networkConfig).find(ip2) != getLeases(networkConfig).end());
    EXPECT_EQ(getLeases(networkConfig)[ip2].clientID, mac2);
}

// Test DHCP Server Handling DHCP Inform Messages
TEST_F(DhcpServerTest, HandleDhcpInform_SendsDhcpAckWithConfiguration) {
    // Prepare a DHCP Inform packet
    PacketInfo informPacket;
    // Layer2: Ethernet Header
    EthernetHeader eth;
    eth.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    eth.sourceMac = ByteString("\x00\x11\x22\x33\x44\xBB", 6); // Client MAC
    eth.type = "\x08\x00"; // IPv4
    informPacket.Layer2.emplace_back(std::move(eth));

    ByteString key = networkConfig->getNetwork() + "/" + std::to_string(networkConfig->getPrefixLen());

    auto net = getDhcpNetworks()[key]->config;

    uint8_t subnetPrefix = 24;
    ByteString defaultGateway = ByteString("\xc0\xa8\x00\x01", 4);
    net->updateNetwork(nullptr, &subnetPrefix, &defaultGateway, dhcpServer);
    net->dnsServer = { ByteString("\x08\x08\x08\x08", 4), ByteString("\x08\x08\x04\x04", 4) };
    net->leaseTime = 3600.0;

    ByteString transID = generateTransactionID();
    DhcpHeader dhcpInform = createDhcpHeader(
        Variable::Dhcp::Type::inform,
        ByteString("\xc0\xa8\x00\x01", 4),
        ByteString("\x00\x11\x22\x33\x44\xBB", 6),
        transID,
        ByteString("\x00\x00\x00\x00", 4), // Client's current IP
        ByteString(),          // Requested IP (none)
        ByteString()           // Server Identifier (none)
    );
    dhcpInform.clientIP = ByteString("\xc0\xa8\x00\x05", 4);
    dhcpInform.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::type,
        ByteString("\x01", 1),
        Variable::Dhcp::Type::inform
    });
    dhcpInform.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::clientID,
        ByteString("\x06", 1),
        ByteString("\x00\x11\x22\x33\x44\xBB", 6)
    });
    ByteString requestList;
    {
        using namespace Variable::Dhcp::Option;
        requestList = mask + router + domainServer + leaseTime;
    }
    dhcpInform.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::requestList,
        ByteString(1, static_cast<unsigned char>(requestList.size())),
        requestList
    });
    dhcpInform.end = Variable::Dhcp::end;
    informPacket.Layer5.emplace_back(std::move(dhcpInform));

    // Expect enqueuePacket to be called once with DHCP Ack containing configuration options
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](const PacketInfo& pkt, const ByteString&) {
            // Verify that the packet is a DHCP Ack
            ASSERT_FALSE(pkt.Layer5.empty());
            const DhcpHeader& ackHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
            EXPECT_EQ(ackHeader.boot, Variable::Dhcp::Type::ack);
            EXPECT_EQ(ackHeader.transID, transID);
            EXPECT_EQ(ackHeader.yourClientIP, ByteString("\xc0\xa8\x00\x05", 4)); // Same IP as Inform

            // Verify DHCP options (mask, router, DNS servers, lease time)
            bool maskFound = false, routerFound = false, dnsFound = false, leaseTimeFound = false;
            for (const auto& opt : ackHeader.options) {
                if (opt.option == Variable::Dhcp::Option::mask) {
                    EXPECT_EQ(opt.value, Functions::binToByte(Functions::numMaskToBin(networkConfig->getPrefixLen())));
                    maskFound = true;
                }
                else if (opt.option == Variable::Dhcp::Option::router) {
                    EXPECT_EQ(opt.value, networkConfig->getGateway());
                    routerFound = true;
                }
                else if (opt.option == Variable::Dhcp::Option::domainServer) {
                    // Expect both DNS servers to be sent
                    EXPECT_TRUE(opt.value == networkConfig->dnsServer[0] + networkConfig->dnsServer[1]);
                    dnsFound = true;
                }
                else if (opt.option == Variable::Dhcp::Option::leaseTime) {
                    EXPECT_EQ(opt.value, Functions::numToByte(static_cast<size_t>(networkConfig->leaseTime), 4));
                    leaseTimeFound = true;
                }
            }
            EXPECT_TRUE(maskFound);
            EXPECT_TRUE(routerFound);
            EXPECT_TRUE(dnsFound);
            EXPECT_TRUE(leaseTimeFound);
        }));

    // Handle the DHCP Inform packet
    dhcpServer->handleDhcpPacket(informPacket, mockInterface);
}

// Test DHCP Server Handling DHCP Packets with Invalid Options
TEST_F(DhcpServerTest, HandleDhcpPacket_InvalidOptions_IgnoresPacket) {
    // Prepare a DHCP Discover packet with missing mandatory options
    PacketInfo invalidRequestPacket;
    // Layer2: Ethernet Header
    EthernetHeader eth;
    eth.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    eth.sourceMac = ByteString("\x00\x11\x22\x33\x44\xCC", 6); // Client MAC
    eth.type = ByteString("\x08\x00", 2); // IPv4
    invalidRequestPacket.Layer2.emplace_back(std::move(eth));

    ByteString transID = generateTransactionID();
    DhcpHeader dhcpRequest = createDhcpHeader(
        Variable::Dhcp::Type::request,
        ByteString("\xc0\xa8\x00\x01", 4),
        ByteString("\x00\x11\x22\x33\x44\xCC", 6),
        transID
    );
    // Intentionally omit mandatory options like requestIP
    // Only include type option
    dhcpRequest.options.clear();
    dhcpRequest.options = { DhcpHeader::Option{
        Variable::Dhcp::Option::type,
        "\x01",
        Variable::Dhcp::Type::request
    } };
    dhcpRequest.end = Variable::Dhcp::end;
    invalidRequestPacket.Layer5.emplace_back(std::move(dhcpRequest));

    // Expect enqueuePacket to never be called since options are invalid
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(0);

    // Handle the invalid DHCP Discover packet
    dhcpServer->handleDhcpPacket(invalidRequestPacket, mockInterface);
}

// Test DHCP Server Handling DHCP Decline
TEST_F(DhcpServerTest, HandleDhcpDecline_SetIpConflicted) {
    // Simulate a DHCP Discover and Request to allocate an IP first
    PacketInfo discoverPacket;
    EthernetHeader ethDiscover;
    ethDiscover.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    ethDiscover.sourceMac = ByteString("\x00\x11\x22\x33\x44\xDD", 6); // Client MAC
    ethDiscover.type = ByteString("\x08\x00", 2); // IPv4
    discoverPacket.Layer2.emplace_back(std::move(ethDiscover));

    ByteString transID = generateTransactionID();
    DhcpHeader dhcpDiscover = createDhcpHeader(
        Variable::Dhcp::Type::discover,
        ByteString("\xc0\xa8\x00\x01", 4),
        ByteString("\x00\x11\x22\x33\x44\xDD", 6),
        transID
    );
    discoverPacket.Layer5.emplace_back(std::move(dhcpDiscover));

    // Expect enqueuePacket to be called once with DHCP Offer again for the declined IP
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](const PacketInfo& pkt, const ByteString&) {
            // Verify that the packet is a DHCP Offer with a different IP or marks the IP as unavailable
            ASSERT_FALSE(pkt.Layer5.empty());
            const DhcpHeader& offerHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
            EXPECT_EQ(offerHeader.boot, Variable::Dhcp::Type::offer);
            EXPECT_EQ(offerHeader.yourClientIP, ByteString("\xc0\xa8\x00\x02", 4));
            // Additional verification of DHCP options can be added here
        }));

    // Handle the DHCP Discover packet
    dhcpServer->handleDhcpPacket(discoverPacket, mockInterface);

    // Prepare a DHCP Decline packet to decline the offered IP
    PacketInfo declinePacket;
    EthernetHeader ethDecline;
    ethDecline.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    ethDecline.sourceMac = ByteString("\x00\x11\x22\x33\x44\xDD", 6); // Client MAC
    ethDecline.type = ByteString("\x08\x00", 2); // IPv4
    declinePacket.Layer2.emplace_back(std::move(ethDecline));

    DhcpHeader dhcpDecline;
    dhcpDecline.boot = Variable::Dhcp::Type::decline;
    dhcpDecline.transID = transID;
    dhcpDecline.clientIP = ByteString("\xc0\xa8\x00\x02", 4); // Declined IP
    dhcpDecline.clientMacAddress = ByteString("\x00\x11\x22\x33\x44\xDD", 6);
    dhcpDecline.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::type,
        ByteString("\x01", 1),
        Variable::Dhcp::Type::decline
    });
    dhcpDecline.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::requestIP,
        ByteString("\x04", 4),
        ByteString("\xc0\xa8\x00\x02", 4)
    });
    dhcpDecline.end = Variable::Dhcp::end;
    declinePacket.Layer5.emplace_back(std::move(dhcpDecline));

    // Handle the DHCP Decline packet
    dhcpServer->handleDhcpPacket(declinePacket, mockInterface);

    // Verify that the timeout is still active for declined IP
    ByteString clientMac("\x00\x11\x22\x33\x44\xDD", 6);
    auto test = getTimeouts();
    EXPECT_TRUE(getTimeouts()[Protocol::Dhcp::TimerType::DECLINE_HOLD][0].clientID == clientMac);
    EXPECT_TRUE(getAllocatedIPs(networkConfig->getNetworkID())[ByteString("\xc0\xa8\x00\x02")].empty());
}

// Test DHCP Server Lease Renewal Process
TEST_F(DhcpServerTest, LeaseRenewal_ProcessRenewalCorrectly) {
    // Simulate an active lease
    ByteString ip = ByteString("\xc0\xa8\x00\x07", 4); // 192.168.0.7
    ByteString mac = ByteString("\x00\x11\x22\x33\x44\xEE", 6);
    double leaseStart = secondsSinceEpoch() - 1800; // Half of leaseTime (3600 seconds)
    getLeases(networkConfig)[ip] = { ip, mac, leaseStart, 3600, 1800, 3400 };
    getAllocatedIPs(networkConfig->getNetworkID())[ByteString("\xc0\xa8\x00\x07", 4)] = mac;

    // Prepare a DHCP Request packet for renewal
    PacketInfo requestPacket;
    EthernetHeader ethRequest;
    ethRequest.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    ethRequest.sourceMac = mac;
    ethRequest.type = ByteString("\x08\x00", 2); // IPv4
    requestPacket.Layer2.emplace_back(std::move(ethRequest));

    ByteString transID = generateTransactionID();
    DhcpHeader dhcpRequest = createDhcpHeader(
        Variable::Dhcp::Type::request,
        ByteString("\xc0\xa8\x00\x01", 4),
        mac,
        transID,
        ByteString("\x00\x00\x00\x00", 4),     // Your IP
        ip,                                    // Requested IP
        ByteString("\xc0\xa8\x00\x00", 4)      // Server Identifier
    );
    dhcpRequest.clientIP = ip;
    dhcpRequest.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::serverIdentifier,
        ByteString("\x04", 1),
        ByteString("\xc0\xa8\x00\x00", 4) // Server ID
    });
    requestPacket.Layer5.emplace_back(std::move(dhcpRequest));

    // Expect enqueuePacket to be called once with DHCP Ack renewing the lease
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](const PacketInfo& pkt, const ByteString&) {
            // Verify that the packet is a DHCP Ack renewing the lease
            ASSERT_FALSE(pkt.Layer5.empty());
            const DhcpHeader& ackHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
            EXPECT_EQ(ackHeader.boot, Variable::Dhcp::Type::ack);
            EXPECT_EQ(ackHeader.transID, transID);
            EXPECT_EQ(ackHeader.clientIP, ip); // Same IP
            // Additional verification of DHCP options can be added here
        }));

    // Handle the DHCP Request packet
    dhcpServer->handleDhcpPacket(requestPacket, mockInterface);

    // Verify that the leaseStart has been updated to current time
    EXPECT_GE(getLeases(networkConfig)[ip].leaseStart, leaseStart + 1800); // At least renewalTime later
}

// Test DHCP Server Handling Concurrent DHCP Discover Packets
TEST_F(DhcpServerTest, HandleConcurrentDhcpDiscover_PrioritizesThreadSafety) {
    // Define multiple client MAC addresses
    std::vector<ByteString> clientMACs = {
        ByteString("\x00\x11\x22\x33\x44\xFF", 6),
        ByteString("\x00\x11\x22\x33\x44\xAA", 6),
        ByteString("\x00\x11\x22\x33\x44\xBB", 6),
        ByteString("\x00\x11\x22\x33\x44\xCC", 6),
        ByteString("\x00\x11\x22\x33\x44\xDD", 6)
    };

    // Define expected allocated IPs
    std::vector<ByteString> expectedIPs;
    for (size_t i = 1; i <= clientMACs.size(); ++i) {
        expectedIPs.emplace_back(ByteString("\xc0\xa8\x00", 3) + ByteString(1, static_cast<char>(0x01 + i)));
    }

    // Mutex and condition variable to synchronize expectations
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<int> offerCount(0);

    // Set expectation: enqueuePacket should be called exactly clientMACs.size() times
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(static_cast<int>(clientMACs.size()))
        .WillRepeatedly(::testing::Invoke([&](const PacketInfo& pkt, const ByteString&) {
            // Verify that the packet is a DHCP Offer
            ASSERT_FALSE(pkt.Layer5.empty());
            const DhcpHeader& offerHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
            EXPECT_EQ(offerHeader.boot, Variable::Dhcp::Type::offer);
            // Assign expected IPs based on order
            EXPECT_EQ(offerHeader.yourClientIP, expectedIPs[offerCount]);
            offerCount++;
            // Notify the condition variable
            {
                std::lock_guard<std::mutex> lock(mtx);
                cv.notify_one();
            }
        }));

    // Function to simulate sending DHCP Discover from a client
    auto sendDiscover = [&](const ByteString& mac) {
        PacketInfo discoverPacket;
        EthernetHeader ethDiscover;
        ethDiscover.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
        ethDiscover.sourceMac = mac;
        ethDiscover.type = ByteString("\x08\x00", 2); // IPv4
        discoverPacket.Layer2.emplace_back(std::move(ethDiscover));

        ByteString transID = generateTransactionID();
        DhcpHeader dhcpDiscover = createDhcpHeader(
            Variable::Dhcp::Type::discover,
            ByteString("\xc0\xa8\x00\x01", 4),
            mac,
            transID
        );
        discoverPacket.Layer5.emplace_back(std::move(dhcpDiscover));

        // Handle the DHCP Discover packet
        dhcpServer->handleDhcpPacket(discoverPacket, mockInterface);
    };

    // Launch multiple threads to send DHCP Discover packets concurrently
    std::vector<std::thread> threads;
    for (const auto& mac : clientMACs) {
        threads.emplace_back(sendDiscover, mac);
    }

    // Wait for all threads to finish
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    // Wait until all DHCP Offers have been processed
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&]() { return offerCount == clientMACs.size(); });

    // Verify that all IPs have been correctly assigned
    for (size_t i = 0; i < clientMACs.size(); ++i) {
        EXPECT_TRUE(isTemporary(networkConfig->getNetworkID(), expectedIPs[i]));
    }
}

// Test DHCP Server Handling DHCP Requests for Already Allocated IPs
TEST_F(DhcpServerTest, HandleDhcpRequest_AlreadyAllocatedIP_SendsNak) {
    // Simulate a DHCP Discover and Request to allocate an IP first
    PacketInfo discoverPacket;
    EthernetHeader ethDiscover;
    ethDiscover.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    ethDiscover.sourceMac = ByteString("\x00\x11\x22\x33\x44\xEE", 6); // Client MAC
    ethDiscover.type = ByteString("\x08\x00", 2); // IPv4
    discoverPacket.Layer2.emplace_back(std::move(ethDiscover));

    ByteString transID1 = generateTransactionID();
    DhcpHeader dhcpDiscover = createDhcpHeader(
        Variable::Dhcp::Type::discover,
        ByteString("\xc0\xa8\x00\x01", 4),
        ByteString("\x00\x11\x22\x33\x44\xEE", 6),
        transID1
    );
    discoverPacket.Layer5.emplace_back(std::move(dhcpDiscover));

    // Handle the DHCP Discover packet
    dhcpServer->handleDhcpPacket(discoverPacket, mockInterface);

    // Prepare a DHCP Request packet to allocate the IP
    PacketInfo requestPacket1;
    EthernetHeader ethRequest1;
    ethRequest1.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    ethRequest1.sourceMac = ByteString("\x00\x11\x22\x33\x44\xEE", 6); // Client MAC
    ethRequest1.type = ByteString("\x08\x00", 2); // IPv4
    requestPacket1.Layer2.emplace_back(std::move(ethRequest1));

    DhcpHeader dhcpRequest1 = createDhcpHeader(
        Variable::Dhcp::Type::request,
        ByteString("\xc0\xa8\x00\x01", 4),
        ByteString("\x00\x11\x22\x33\x44\xEE", 6),
        transID1,
        ByteString("\x00\x00\x00\x00", 4), // Your IP
        ByteString("\xc0\xa8\x00\x02", 4), // Requested IP
        ByteString("\xc0\xa8\x00\x00", 4)  // Server Identifier
    );
    dhcpRequest1.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::serverIdentifier,
        ByteString("\x04", 1),
        ByteString("\xc0\xa8\x00\x00", 4) // Server ID
    });
    requestPacket1.Layer5.emplace_back(std::move(dhcpRequest1));

    // Expect enqueuePacket to be called once with DHCP Ack
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](const PacketInfo& pkt, const ByteString&) {
            // Verify that the packet is a DHCP Ack
            ASSERT_FALSE(pkt.Layer5.empty());
            const DhcpHeader& ackHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
            EXPECT_EQ(ackHeader.boot, Variable::Dhcp::Type::ack);
            EXPECT_EQ(ackHeader.transID, transID1);
            EXPECT_EQ(ackHeader.yourClientIP, ByteString("\xc0\xa8\x00\x02", 4)); // Acknowledged IP
        }));

    // Handle the first DHCP Request packet
    dhcpServer->handleDhcpPacket(requestPacket1, mockInterface);

    // Prepare another DHCP Request packet from a different client trying to request the same IP
    PacketInfo requestPacket2;
    EthernetHeader ethRequest2;
    ethRequest2.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    ethRequest2.sourceMac = ByteString("\x00\x11\x22\x33\x44\xFF", 6); // Different Client MAC
    ethRequest2.type = ByteString("\x08\x00", 2); // IPv4
    requestPacket2.Layer2.emplace_back(std::move(ethRequest2));

    ByteString transID2 = generateTransactionID();
    DhcpHeader dhcpRequest2 = createDhcpHeader(
        Variable::Dhcp::Type::request,
        ByteString("\xc0\xa8\x00\x01", 4),
        ByteString("\x00\x11\x22\x33\x44\xFF", 6),
        transID2,
        ByteString("\x00\x00\x00\x00", 4), // Requested IP
        ByteString("\xc0\xa8\x00\x01", 4), // Requested IP
        ByteString("\xc0\xa8\x00\x00", 4)  // Server Identifier
    );
    dhcpRequest2.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::serverIdentifier,
        ByteString("\x04", 1),
        ByteString("\xc0\xa8\x00\x00", 4) // Server ID
    });
    requestPacket2.Layer5.emplace_back(std::move(dhcpRequest2));

    // Expect enqueuePacket to be called once with DHCP Nak
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](const PacketInfo& pkt, const ByteString&) {
            // Verify that the packet is a DHCP Nak
            ASSERT_FALSE(pkt.Layer5.empty());
            const DhcpHeader& nakHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
            EXPECT_EQ(nakHeader.boot, Variable::Dhcp::Type::nak);
            EXPECT_EQ(nakHeader.transID, transID2);
            EXPECT_TRUE(nakHeader.yourClientIP == Variable::IPv4::source); // No IP assigned
        }));

    // Handle the second DHCP Request packet
    dhcpServer->handleDhcpPacket(requestPacket2, mockInterface);
}

// Test DHCP Server Start and Stop
TEST_F(DhcpServerTest, StartAndStop_ServerLifecycle) {
    // Expect that startServer starts the DHCP handler thread
    // Since the dhcpHandler runs indefinitely, we'll use a shorter lease cleanup interval for testing
    // Modify the DhcpServer class if necessary to allow configurable intervals

    // Start the DHCP server
    //dhcpServer->startServer();

    // Allow some time for the server to run (simulate lease cleanup)
    //std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Expect that stopServer stops the DHCP handler thread
    // This is implicitly tested by calling stopServer without crashes
    //EXPECT_NO_THROW(dhcpServer->stopServer());
}

// Test DHCP Server Updating Network Configuration
TEST_F(DhcpServerTest, UpdateDhcpNetworkConfig_Success) {
    // Define a new subnet mask and DNS servers
    uint8_t newSubnetPrefix = 25; // 255.255.255.128
    ByteString newGateway = ByteString("\xc0\xa8\x00\x01", 4);    // Remains the same
    std::vector<ByteString> newDNSServers = { ByteString("\x08\x08\x08\x08", 4), ByteString("\x08\x08\x08\x09", 4) };
    uint32_t newLeaseTime = 7200;                   // 2 hours

    // Define new DhcpNetworkConfig
    Dhcp::DhcpNetworkConfig updatedConfig;
    ByteString network = networkConfig->getNetwork();
    uint8_t subnetPrefix = newSubnetPrefix;
    ByteString defaultGateway = newGateway;
    networkConfig->updateNetwork(&network, &subnetPrefix, &defaultGateway, dhcpServer);
    networkConfig->dnsServer = newDNSServers;
    networkConfig->leaseTime = newLeaseTime;
    networkConfig->renewalTime = ByteString("\x00\x00\x0e\x10", 4);     // 3600 seconds
    networkConfig->rebindingTime = ByteString("\x00\x00\x1c\x20", 4);   // 7200 seconds

    // Simulate a DHCP Discover packet after the update
    PacketInfo discoverPacket;
    EthernetHeader ethDiscover;
    ethDiscover.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    ethDiscover.sourceMac = ByteString("\x00\x11\x22\x33\x44\x99", 6); // Client MAC
    ethDiscover.type = ByteString("\x08\x00", 2); // IPv4
    discoverPacket.Layer2.emplace_back(std::move(ethDiscover));

    ByteString transID = generateTransactionID();
    DhcpHeader dhcpDiscover = createDhcpHeader(
        Variable::Dhcp::Type::discover,
        ByteString("\xc0\xa8\x00\x01", 4),
        ByteString("\x00\x11\x22\x33\x44\x99", 6),
        transID
    );
    discoverPacket.Layer5.emplace_back(std::move(dhcpDiscover));

    // Expect enqueuePacket to be called once with DHCP Offer containing updated subnet mask and DNS servers
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](const PacketInfo& pkt, const ByteString&) {
            // Verify that the packet is a DHCP Offer with updated configuration
            ASSERT_FALSE(pkt.Layer5.empty());
            const DhcpHeader& offerHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
            EXPECT_EQ(offerHeader.boot, Variable::Dhcp::Type::offer);
            EXPECT_EQ(offerHeader.yourClientIP, ByteString("\xc0\xa8\x00\x02", 4)); // First usable IP

            // Verify DHCP options
            bool maskFound = false, routerFound = false, dnsFound = false, leaseTimeFound = false;
            for (const auto& opt : offerHeader.options) {
                if (opt.option == Variable::Dhcp::Option::mask) {
                    EXPECT_EQ(opt.value, Functions::binToByte(Functions::numMaskToBin(newSubnetPrefix)));
                    maskFound = true;
                }
                else if (opt.option == Variable::Dhcp::Option::router) {
                    EXPECT_EQ(opt.value, newGateway);
                    routerFound = true;
                }
                else if (opt.option == Variable::Dhcp::Option::domainServer) {
                    // Expect both new DNS servers to be sent
                    if (!dnsFound)
                    {
                        EXPECT_TRUE(((opt.value == newDNSServers[0] + newDNSServers[1])));
                        dnsFound = true;
                    }
                }
                else if (opt.option == Variable::Dhcp::Option::leaseTime) {
                    EXPECT_EQ(opt.value, Functions::numToByte(newLeaseTime, 4));
                    leaseTimeFound = true;
                }
            }
            EXPECT_TRUE(maskFound);
            EXPECT_TRUE(routerFound);
            EXPECT_TRUE(dnsFound);
            EXPECT_TRUE(leaseTimeFound);
        }));

    // Handle the DHCP Discover packet
    dhcpServer->handleDhcpPacket(discoverPacket, mockInterface);
}

// Test DHCP Server Handling Exhausted IP Pool
TEST_F(DhcpServerTest, HandleExhaustedIpPool_SendsDhcpNak) {
    // Simulate allocation of all available IPs in the pool
    // Assuming subnet mask is 255.255.255.0, usable IPs are 192.168.0.1 to 192.168.0.254
    for (int i = 1; i <= 253; ++i) {
        ByteString network = ByteString("\xc0\xa8\x00\x00", 4);
        ByteString mac = ByteString("\x00\x11\x22\x33\x44", 5) + ByteString(1, static_cast<char>(0xAA + i));
        ByteString ip = allocateIPAddress(network, networkConfig->getPrefixLen(), mac);
        getLeases(networkConfig)[ip] = { ip, mac, secondsSinceEpoch(), 3600, 1800, 3400 };
    }

    // Prepare a DHCP Discover packet
    PacketInfo discoverPacket;
    EthernetHeader ethDiscover;
    ethDiscover.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    ethDiscover.sourceMac = ByteString("\x00\x11\x22\x33\xFF\xFF", 6); // New Client MAC
    ethDiscover.type = ByteString("\x08\x00", 2); // IPv4
    discoverPacket.Layer2.emplace_back(std::move(ethDiscover));

    ByteString transID = generateTransactionID();
    DhcpHeader dhcpDiscover = createDhcpHeader(
        Variable::Dhcp::Type::discover,
        ByteString("\xc0\xa8\x00\x01", 4),
        ByteString("\x00\x11\x22\x33\xFF\xFF", 6),
        transID
    );
    discoverPacket.Layer5.emplace_back(std::move(dhcpDiscover));

    // Expect enqueuePacket to be called once with DHCP Nak since pool is exhausted
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](const PacketInfo& pkt, const ByteString&) {
            // Verify that the packet is a DHCP Nak
            ASSERT_FALSE(pkt.Layer5.empty());
            const DhcpHeader& nakHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
            EXPECT_EQ(nakHeader.boot, Variable::Dhcp::Type::nak);
            EXPECT_EQ(nakHeader.transID, transID);
            EXPECT_EQ(nakHeader.yourClientIP, Variable::IPv4::source); // No IP assigned
        }));

    // Handle the DHCP Discover packet
    dhcpServer->handleDhcpPacket(discoverPacket, mockInterface);
}

// Test DHCP Server Handling Invalid DHCP Message Types
TEST_F(DhcpServerTest, HandleInvalidDhcpMessageType_IgnoresPacket) {
    // Prepare a DHCP packet with an invalid message type
    PacketInfo invalidPacket;
    // Layer2: Ethernet Header
    EthernetHeader eth;
    eth.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    eth.sourceMac = ByteString("\x00\x11\x22\x33\x44\xEE", 6); // Client MAC
    eth.type = ByteString("\x08\x00", 2); // IPv4
    invalidPacket.Layer2.emplace_back(std::move(eth));

    ByteString transID = generateTransactionID();
    DhcpHeader invalidDhcp = createDhcpHeader(
        "invalid_type", // Invalid message type
        ByteString("\xc0\xa8\x00\x01", 4),
        ByteString("\x00\x11\x22\x33\x44\xEE", 6),
        transID
    );
    invalidDhcp.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::type,
        ByteString("\x01", 1),
        "invalid" // Invalid type value
    });
    invalidDhcp.end = Variable::Dhcp::end;
    invalidPacket.Layer5.emplace_back(std::move(invalidDhcp));

    // Expect enqueuePacket to never be called
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(0);

    // Handle the invalid DHCP packet
    dhcpServer->handleDhcpPacket(invalidPacket, mockInterface);
}

// Test DHCP Server Handling Lease Renewal Requests Without Existing Lease
TEST_F(DhcpServerTest, HandleDhcpRequest_NoExistingLease_SendsDhcpNak) {
    // Prepare a DHCP Request packet for an IP that was never allocated
    PacketInfo requestPacket;
    EthernetHeader ethRequest;
    ethRequest.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    ethRequest.sourceMac = ByteString("\x00\x11\x22\x33\x44\xFF", 6); // Client MAC
    ethRequest.type = "\x08\x00"; // IPv4
    requestPacket.Layer2.emplace_back(std::move(ethRequest));

    ByteString transID = generateTransactionID();
    DhcpHeader dhcpRequest = createDhcpHeader(
        Variable::Dhcp::Type::request,
        ByteString("\xc0\xa8\x00\x01", 4),
        ByteString("\x00\x11\x22\x33\x44\xFF", 6),
        transID,
        ByteString("\x00\x00\x00\x00", 4), // Requested IP that was never allocated
        ByteString("\xc0\xa8\x00\x10", 4), // Requested IP
        ByteString("\xc0\xa8\x00\x00", 4)  // Server Identifier
    );
    dhcpRequest.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::serverIdentifier,
        ByteString("\x04", 1),
        ByteString("\xc0\xa8\x00\x00", 4) // Server ID
    });
    requestPacket.Layer5.emplace_back(std::move(dhcpRequest));

    // Expect enqueuePacket to be called once with DHCP Nak
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](const PacketInfo& pkt, const ByteString&) {
            // Verify that the packet is a DHCP Nak
            ASSERT_FALSE(pkt.Layer5.empty());
            const DhcpHeader& nakHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
            EXPECT_EQ(nakHeader.boot, Variable::Dhcp::Type::nak);
            EXPECT_EQ(nakHeader.transID, transID);
            EXPECT_EQ(nakHeader.yourClientIP, Variable::IPv4::source); // No IP assigned
        }));

    // Handle the DHCP Request packet
    dhcpServer->handleDhcpPacket(requestPacket, mockInterface);
}

// Test DHCP Server Update Network Config and Remove Leases Outside New Subnet
TEST_F(DhcpServerTest, UpdateDhcpNetworkConfig_RemoveLeasesOutsideNewSubnet) {
    // Simulate leases within the original subnet
    ByteString ip1 = ByteString("\xc0\xa8\x00\x02", 4); // 192.168.0.2
    ByteString mac1 = ByteString("\x00\x11\x22\x33\x44\xBB", 6);
    getLeases(networkConfig)[ip1] = { ip1, mac1, secondsSinceEpoch() - 1000, 3600, 1800, 3400 };
    getAllocatedIPs(networkConfig->getNetworkID())[ip1] = mac1;

    // Simulate leases outside the new subnet after update
    ByteString ip2 = ByteString("\xc0\xa8\x01\x02", 4); // 192.168.1.2 (outside 192.168.0.0/24)
    ByteString mac2 = ByteString("\x00\x11\x22\x33\x44\xCC", 6);
    getLeases(networkConfig)[ip2] = { ip2, mac2, secondsSinceEpoch() - 1000, 3600, 1800, 3400 };
    getAllocatedIPs(networkConfig->getNetworkID())[ip2] = mac2;

    // Define new subnet mask that excludes the second IP
    uint8_t newSubnetPrefix = 25; // 255.255.255.128

    // Define updated DhcpNetworkConfig
    ByteString network = ByteString("\xc0\xa8\x00\x00", 4); // 192.168.0.0
    uint8_t subnetPrefix = newSubnetPrefix;
    ByteString defaultGateway = ByteString("\xc0\xa8\x00\x01", 4); // 192.168.0.1
    networkConfig->updateNetwork(&network, &subnetPrefix, &defaultGateway, dhcpServer);
    networkConfig->dnsServer = { ByteString("\x08\x08\x08\x08", 4) }; // Remove one DNS server
    networkConfig->leaseTime = 7200; // 2 hours
    networkConfig->renewalTime = ByteString("\x00\x00\x0e\x10", 4);     // 3600 seconds
    networkConfig->rebindingTime = ByteString("\x00\x00\x1c\x20", 4);   // 7200 seconds
    networkConfig->interface = mockInterface;

    // Expect that leases outside the new subnet are removed
    EXPECT_TRUE(getLeases(networkConfig).find(ip1) != getLeases(networkConfig).end()); // Within new subnet
    EXPECT_TRUE(getLeases(networkConfig).find(ip2) == getLeases(networkConfig).end()); // Outside new subnet
}

// Test DHCP Server Prevents Duplicate IP Allocation to Different MACs
TEST_F(DhcpServerTest, PreventsDuplicateIpAllocation_ToDifferentMACs) {
    // Simulate first client DHCP Discover and Request
    ByteString mac1 = ByteString("\x00\x11\x22\x33\x44\x11", 4);
    PacketInfo discoverPacket1;
    EthernetHeader ethDiscover1;
    ethDiscover1.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    ethDiscover1.sourceMac = mac1;
    ethDiscover1.type = ByteString("\x08\x00", 2); // IPv4
    discoverPacket1.Layer2.emplace_back(std::move(ethDiscover1));

    ByteString transID1 = generateTransactionID();
    DhcpHeader dhcpDiscover1 = createDhcpHeader(
        Variable::Dhcp::Type::discover,
        ByteString("\xc0\xa8\x00\x01", 4),
        mac1,
        transID1
    );
    discoverPacket1.Layer5.emplace_back(std::move(dhcpDiscover1));

    // Expect enqueuePacket to be called once with DHCP Offer
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](const PacketInfo& pkt, const ByteString&) {
            // Verify that the packet is a DHCP Offer
            ASSERT_FALSE(pkt.Layer5.empty());
            const DhcpHeader& offerHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
            EXPECT_EQ(offerHeader.boot, Variable::Dhcp::Type::offer);
            EXPECT_EQ(offerHeader.yourClientIP, ByteString("\xc0\xa8\x00\x02", 4)); // First usable IP
        }));

    // Handle the first DHCP Discover packet
    dhcpServer->handleDhcpPacket(discoverPacket1, mockInterface);

    // Simulate first DHCP Request
    PacketInfo requestPacket1;
    EthernetHeader ethRequest1;
    ethRequest1.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    ethRequest1.sourceMac = mac1;
    ethRequest1.type = ByteString("\x08\x00", 2); // IPv4
    requestPacket1.Layer2.emplace_back(std::move(ethRequest1));

    DhcpHeader dhcpRequest1 = createDhcpHeader(
        Variable::Dhcp::Type::request,
        ByteString("\xc0\xa8\x00\x01", 4),
        mac1,
        transID1,
        ByteString("\x00\x00\x00\x00", 4), // Your IP
        ByteString("\xc0\xa8\x00\x02", 4), // Requested IP
        ByteString("\xc0\xa8\x00\x00", 4)  // Server Identifier
    );
    dhcpRequest1.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::serverIdentifier,
        ByteString("\x04", 1),
        ByteString("\xc0\xa8\x00\x00", 4) // Server ID
    });
    requestPacket1.Layer5.emplace_back(std::move(dhcpRequest1));

    // Expect enqueuePacket to be called once with DHCP Ack
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](const PacketInfo& pkt, const ByteString&) {
            // Verify that the packet is a DHCP Ack
            ASSERT_FALSE(pkt.Layer5.empty());
            const DhcpHeader& ackHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
            EXPECT_EQ(ackHeader.boot, Variable::Dhcp::Type::ack);
            EXPECT_EQ(ackHeader.transID, transID1);
            EXPECT_EQ(ackHeader.yourClientIP, ByteString("\xc0\xa8\x00\x02", 4)); // Acknowledged IP
        }));

    // Handle the first DHCP Request packet
    dhcpServer->handleDhcpPacket(requestPacket1, mockInterface);

    // Simulate second client DHCP Discover and Request for the same IP
    ByteString mac2 = ByteString("\x00\x11\x22\x33\x44\x22", 6);
    PacketInfo discoverPacket2;
    EthernetHeader ethDiscover2;
    ethDiscover2.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    ethDiscover2.sourceMac = mac2;
    ethDiscover2.type = ByteString("\x08\x00", 2); // IPv4
    discoverPacket2.Layer2.emplace_back(std::move(ethDiscover2));

    ByteString transID2 = generateTransactionID();
    DhcpHeader dhcpDiscover2 = createDhcpHeader(
        Variable::Dhcp::Type::discover,
        ByteString("\xc0\xa8\x00\x01", 4),
        mac2,
        transID2
    );
    discoverPacket2.Layer5.emplace_back(std::move(dhcpDiscover2));

    // Expect enqueuePacket to be called once with DHCP Offer (different IP)
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](const PacketInfo& pkt, const ByteString&) {
            // Verify that the packet is a DHCP Offer with a different IP
            ASSERT_FALSE(pkt.Layer5.empty());
            const DhcpHeader& offerHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
            EXPECT_EQ(offerHeader.boot, Variable::Dhcp::Type::offer);
            EXPECT_EQ(offerHeader.yourClientIP, ByteString("\xc0\xa8\x00\x03", 4)); // Next usable IP
        }));

    // Handle the second DHCP Discover packet
    dhcpServer->handleDhcpPacket(discoverPacket2, mockInterface);

    // Simulate second DHCP Request
    PacketInfo requestPacket2;
    EthernetHeader ethRequest2;
    ethRequest2.destinationMac = ByteString("\xff\xff\xff\xff\xff\xff", 6); // Broadcast
    ethRequest2.sourceMac = mac2;
    ethRequest2.type = "\x08\x00"; // IPv4
    requestPacket2.Layer2.emplace_back(std::move(ethRequest2));

    DhcpHeader dhcpRequest2 = createDhcpHeader(
        Variable::Dhcp::Type::request,
        ByteString("\xc0\xa8\x00\x01", 4),
        mac2,
        transID2,
        ByteString("\x00\x00\x00\x00", 4), // Your IP
        ByteString("\xc0\xa8\x00\x03", 4), // Requested IP
        ByteString("\xc0\xa8\x00\x00", 4)  // Server Identifier
    );
    dhcpRequest2.options.emplace_back(DhcpHeader::Option{
        Variable::Dhcp::Option::serverIdentifier,
        ByteString("\x04", 1),
        ByteString("\xc0\xa8\x00\x00", 4) // Server ID
    });
    requestPacket2.Layer5.emplace_back(std::move(dhcpRequest2));

    // Expect enqueuePacket to be called once with DHCP Ack
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](const PacketInfo& pkt, const ByteString&) {
            // Verify that the packet is a DHCP Ack
            ASSERT_FALSE(pkt.Layer5.empty());
            const DhcpHeader& ackHeader = std::get<DhcpHeader>(pkt.Layer5[0]);
            EXPECT_EQ(ackHeader.boot, Variable::Dhcp::Type::ack);
            EXPECT_EQ(ackHeader.transID, transID2);
            EXPECT_EQ(ackHeader.yourClientIP, ByteString("\xc0\xa8\x00\x03", 4)); // Acknowledged IP
        }));

    // Handle the second DHCP Request packet
    dhcpServer->handleDhcpPacket(requestPacket2, mockInterface);

    // Verify that both leases exist
    EXPECT_EQ(getLeases(networkConfig).size(), 2);
    EXPECT_TRUE(getLeases(networkConfig).find(ByteString("\xc0\xa8\x00\x02", 4)) != getLeases(networkConfig).end());
    EXPECT_TRUE(getLeases(networkConfig).find(ByteString("\xc0\xa8\x00\x03", 4)) != getLeases(networkConfig).end());

    // Verify that both IPs are associated with correct MACs
    EXPECT_EQ(getLeases(networkConfig)[ByteString("\xc0\xa8\x00\x02", 4)].clientID, mac1);
    EXPECT_EQ(getLeases(networkConfig)[ByteString("\xc0\xa8\x00\x03", 4)].clientID, mac2);
}
