// EigrpTest.cpp

#include <gtest/gtest.h>
#include <Eigrp.h>
#include <MockInterface.hpp>
#include <MockRoutingTable.hpp>
#include <MockEigrpInterface.hpp>
#include <condition_variable>
#include <mutex>

using namespace Protocol;

// Test fixture for global EIGRP
class EigrpTest : public ::testing::Test
{
protected:
    uint32_t asNumber = 1;
    AddressFamily addressFamily = AddressFamily::IPv4;
    Eigrp* eigrpInstance;
    MockInterface* mockInterface;
    MockRoutingTable* mockRoutingTable;
    MockEigrpInterface* mockEigrpInterface;

    // Synchronization helpers for threads
    std::condition_variable cv;
    std::mutex cvMutex;
    bool packetEnqueued = false;

    // Setup and teardown for each test
    void SetUp() override
    {
        eigrpInstance = new Eigrp(asNumber, addressFamily);
        mockInterface = new MockInterface();
        mockInterface->enableIPs();
        mockInterface->enableShutdown();
        setIPv4(ByteString("\xc0\xa8\x01\x01", 4), 24);
        setIPv6(ByteString("\xc0\xa8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x01", 16), 64);
        interfaceList[InterfaceType::GIGABIT_ETHERNET][0] = mockInterface;

        EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(::testing::AtLeast(1));

        mockEigrpInterface = new MockEigrpInterface(*eigrpInstance, mockInterface);
        eigrpInstance->eigrpInterfaceList[0] = mockEigrpInterface;
    }

    void TearDown() override
    {
        interfaceList[InterfaceType::GIGABIT_ETHERNET].erase(0);
        EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(::testing::AnyNumber());
        delete eigrpInstance;
        delete mockInterface;

        interfaceList.clear();
        RoutingTable::getInstance().clear();
    }

    void notifyPacketEnqueued()
    {
        std::lock_guard<std::mutex> lock(cvMutex);
        packetEnqueued = true;
        cv.notify_one();
    }

    void waitForPacketEnqueued()
    {
        std::unique_lock<std::mutex> lock(cvMutex);
        cv.wait(lock, [this] { return packetEnqueued; });
    }

    // Helper functions in order to access private information
    TopologyTable* getTopologyTable() { return eigrpInstance->topologyTable; }
    std::unordered_map<uint8_t, EigrpInterface*>& getInterfaceList(Eigrp* eigrp = nullptr) { if (eigrp) { return eigrp->eigrpInterfaceList; } else { return eigrpInstance->eigrpInterfaceList; } }
    uint32_t getAsNumber() { return eigrpInstance->getAsNumber(); }
    void setIPv4(const ByteString& ip, uint8_t mask, MockInterface* interface = nullptr) { if (interface) { EXPECT_CALL(*interface, enqueuePacket).Times(::testing::AnyNumber()); interface->enableIPs(); interface->enableShutdown(); interface->setIPv4(ip, mask); } else { mockInterface->setIPv4(ip, mask); }}
    void setIPv6(const ByteString& ip, uint8_t mask, Interface* interface = nullptr) { if (interface) { interface->setIPv6(ip, mask, false); } else { mockInterface->setIPv6(ip, mask, false); }}
    IpInfo& getIpInfo(Interface* interface = nullptr) { if (interface) {return interface->configs;} else {return mockInterface->configs;}}
    void clearNetworks(Eigrp* eigrp = nullptr) { if (eigrp) eigrp->configs.networks.clear(); else eigrpInstance->configs.networks.clear(); }
    void addNeighbor(const ByteString& ipAddress, EigrpConfigs::CommunicationMode mode, EigrpInterface* interface = nullptr) { if (interface) interface->addNeighbor(ipAddress, ByteString("\x11\x22\x33\x44\x55\x66", 6), mode); else mockEigrpInterface->addNeighbor(ipAddress, ByteString("\x11\x22\x33\x44\x55\x66", 6), mode);}
    
    std::optional<EigrpConfigs::NeighborInfo*>  getNeighbors(ByteString& ip, EigrpInterface* eigrp = nullptr) { if (eigrp) return eigrp->getNeighborInfo(ip); else return mockEigrpInterface->getNeighborInfo(ip); }
};

#pragma region Initialization

// Test: Initialize EIGRP with valid AS and address family
TEST_F(EigrpTest, InitializeEigrp_ValidParameters) 
{
    // Assert
    ASSERT_NE(getTopologyTable(), nullptr);
    ASSERT_TRUE(getInterfaceList().size() == 1);
    ASSERT_EQ(getAsNumber(), asNumber);
}

// Test: Validate default metric values after initialization
TEST_F(EigrpTest, InitializeEigrp_DefaultMetrics) 
{
    // Assert
    ASSERT_EQ(eigrpInstance->getConfigs()->kvalue.k1_Bandwidth, 1);
    ASSERT_EQ(eigrpInstance->getConfigs()->kvalue.k2_Load, 0);
    ASSERT_EQ(eigrpInstance->getConfigs()->kvalue.k3_Delay, 1);
    ASSERT_EQ(eigrpInstance->getConfigs()->kvalue.k4_Reliability, 0);
    ASSERT_EQ(eigrpInstance->getConfigs()->kvalue.k5_MTU, 0);
    ASSERT_EQ(eigrpInstance->getConfigs()->wideMetric, 10000000);
}

// Test: Ensure topology table is created during initialization
TEST_F(EigrpTest, InitializeEigrp_TopologyTableCreated) 
{
    // Assert
    ASSERT_NE(getTopologyTable(), nullptr);
}

// Test: Shutdown EIGRP gracefully
TEST_F(EigrpTest, ShutdownEigrp_CleanResources) 
{
    // Act
    eigrpInstance->shutdown();

    // Assert
    ASSERT_TRUE(getInterfaceList().empty());
    ASSERT_EQ(getTopologyTable(), nullptr);
}

#pragma endregion
#pragma region HelloPacket

// Test: Create a valid hello packet
TEST_F(EigrpTest, ConstructHelloPacket) 
{
    // Arrange
    EigrpHeader helloPacket;
    ByteString neighborIp("\xC0\xA8\x01\x01", 4); // 192.168.1.1
    uint32_t sequenceNumber = 1234;

    // Act
    eigrpInstance->eigrpHello(helloPacket, mockEigrpInterface, neighborIp, sequenceNumber, false, false);

    // Assert
    ASSERT_EQ(helloPacket.opcode, Variable::Eigrp::Type::hello);
    ASSERT_EQ(helloPacket.sequence, ByteString("\x00\x00\x00\x00", 4));
}

// Test: Create a hello packet with acknowledgment
TEST_F(EigrpTest, ConstructHelloPacket_WithAck) 
{
    // Arrange
    EigrpHeader helloPacket;
    ByteString neighborIp("\xC0\xA8\x01\x01", 4); // 192.168.1.1
    uint32_t sequenceNumber = 1234;

    // Act
    eigrpInstance->eigrpHello(helloPacket, mockEigrpInterface, neighborIp, sequenceNumber, true, false);

    // Assert
    ASSERT_EQ(helloPacket.ack, Functions::numToByte(sequenceNumber, 4));
}

// Test: Validate hello packet K-values
TEST_F(EigrpTest, ConstructHelloPacket_KValues) 
{
    // Arrange
    EigrpHeader helloPacket;
    ByteString neighborIp("\xC0\xA8\x01\x01", 4); // 192.168.1.1

    // Act
    eigrpInstance->eigrpHello(helloPacket, mockEigrpInterface, neighborIp, 0, false, false);

    // Assert
    ASSERT_TRUE(std::any_of(helloPacket.options.begin(), helloPacket.options.end(), [](const EigrpHeader::Option& option){ return option.option == Variable::Eigrp::Option::parameter; }));
}

// Test: Test hello packet construction for IPv6
TEST_F(EigrpTest, ConstructHelloPacket_IPv6) 
{
    // Arrange
    EigrpHeader helloPacket;
    ByteString neighborIp("\x20\x01\x0D\xB8\x00\x00\x00\x01", 8); // 2001:db8::1

    // Act
    eigrpInstance->eigrpHello(helloPacket, mockEigrpInterface, neighborIp, 0, false, false);

    // Assert
    ASSERT_EQ(helloPacket.version, ByteString("\x02", 1));
}

#pragma endregion
#pragma region UpdatePacket

// Test: Create an update packet for adding a new route
TEST_F(EigrpTest, ConstructUpdatePacket_AddRoute) 
{
    // Arrange
    EigrpHeader updatePacket;
    uint32_t sequenceNumber = 4321;

    // Act
    eigrpInstance->eigrpUpdate(updatePacket, sequenceNumber, false, false, false, false, false, false);

    // Assert
    ASSERT_EQ(updatePacket.opcode, Variable::Eigrp::Type::update);
    ASSERT_EQ(updatePacket.sequence, Functions::numToByte(sequenceNumber, 4));
}

// Test: Create an update packet for withdrawing a route
TEST_F(EigrpTest, ConstructUpdatePacket_WithdrawRoute) 
{
    // Arrange
    EigrpHeader updatePacket;
    uint32_t sequenceNumber = 1234;

    // Act
    eigrpInstance->eigrpUpdate(updatePacket, sequenceNumber, false, false, false, true, false, false);

    // Assert
    ASSERT_EQ(updatePacket.flags.endOfTable, "1");
}

#pragma endregion
#pragma region testAddress

// Test: Match IP address within network scope (IPv4)
TEST_F(EigrpTest, TestAddress_ValidIPv4) 
{
    // Arrange
    EigrpConfigs::Network network{ByteString("\xC0\xA8\x01\x00", 4), ByteString("\x00\x00\x00\xFF", 4)};
    eigrpInstance->addNetwork(network);
    ByteString testIp("\xC0\xA8\x01\x64", 4);

    // Act
    bool result = eigrpInstance->testAddress(testIp);

    // Assert
    ASSERT_TRUE(result);
}

// Test: Non-matching IP address
TEST_F(EigrpTest, TestAddress_NonMatchingIP) 
{
    // Arrange
    EigrpConfigs::Network network{ByteString("\xC0\xA8\x01\x00", 4), ByteString("\x00\x00\x00\xFF", 4)};
    eigrpInstance->addNetwork(network);
    ByteString testIp("\x0A\x00\x00\x01", 4);

    // Act
    bool result = eigrpInstance->testAddress(testIp);

    // Assert
    ASSERT_FALSE(result);
}

// Test: Overlapping network scopes
TEST_F(EigrpTest, TestAddress_OverlappingNetworks) 
{
    // Arrange
    EigrpConfigs::Network network1{ByteString("\xC0\xA8\x01\x00", 4), ByteString("\x00\x00\x00\xFF", 4)};
    EigrpConfigs::Network network2{ByteString("\xC0\xA8\x01\x00", 4), ByteString("\x00\x00\xFF\xFF", 4)};
    eigrpInstance->addNetwork(network1);
    eigrpInstance->addNetwork(network2);
    ByteString testIp("\xC0\xA8\x01\x32", 4);

    // Act
    bool result = eigrpInstance->testAddress(testIp);

    // Assert
    ASSERT_TRUE(result);
}

// Test: Empty IP input
TEST_F(EigrpTest, TestAddress_EmptyIP) 
{
    // Arrange
    ByteString emptyIp;

    // Act
    bool result = eigrpInstance->testAddress(emptyIp);

    // Assert
    ASSERT_FALSE(result);
}

#pragma endregion
#pragma region Metric

// Test: Calculate metric with valid inputs (IPv4)
// Ensures that a valid metric is calculated for IPv4 parameters.
TEST_F(EigrpTest, CalculateMetric_ValidInputIPv4) 
{
    // Arrange
    uint32_t bandwidth = 100000; // 100 Mbps
    uint8_t load = 10;           // 10% load
    uint32_t delay = 10000;      // 1 ms delay (scaled by 10 for microseconds)
    uint8_t reliability = 255;   // Maximum reliability
    uint8_t hopCount = 1;

    // Act
    uint32_t metric = eigrpInstance->calculateMetric(bandwidth, load, delay, reliability, hopCount);

    // Assert
    ASSERT_GT(metric, 0);
    ASSERT_LT(metric, std::numeric_limits<uint32_t>::max());
}

// Test: Calculate metric with zero bandwidth
// Ensures that the metric calculation returns a maximum value when bandwidth is zero.
TEST_F(EigrpTest, CalculateMetric_ZeroBandwidth) 
{
    // Arrange
    uint32_t bandwidth = 0;       // Invalid bandwidth
    uint8_t load = 50;            // 50% load
    uint32_t delay = 50000;       // 5 ms delay
    uint8_t reliability = 255;    // Maximum reliability
    uint8_t hopCount = 1;

    // Act
    uint32_t metric = eigrpInstance->calculateMetric(bandwidth, load, delay, reliability, hopCount);

    // Assert
    ASSERT_EQ(metric, std::numeric_limits<uint32_t>::max());
}

// Test: Calculate metric with infinite delay
// Ensures that the metric calculation handles an infinite delay scenario.
TEST_F(EigrpTest, CalculateMetric_InfiniteDelay) 
{
    // Arrange
    uint32_t bandwidth = 100000;  // 100 Mbps
    uint8_t load = 20;            // 20% load
    uint32_t delay = std::numeric_limits<uint32_t>::max(); // Infinite delay
    uint8_t reliability = 200;    // Reduced reliability
    uint8_t hopCount = 2;

    // Act
    uint32_t metric = eigrpInstance->calculateMetric(bandwidth, load, delay, reliability, hopCount);

    // Assert
    ASSERT_EQ(metric, std::numeric_limits<uint32_t>::max());
}

// Test: Validate metric with non-default K-values
// Ensures that the metric calculation respects custom K-values.
TEST_F(EigrpTest, CalculateMetric_CustomKValues) 
{
    // Arrange
    eigrpInstance->getConfigs()->kvalue.k1_Bandwidth = 2;
    eigrpInstance->getConfigs()->kvalue.k3_Delay = 3;

    uint32_t bandwidth = 50000; // 50 Mbps
    uint8_t load = 30;          // 30% load
    uint32_t delay = 20000;     // 2 ms delay
    uint8_t reliability = 220;  // Moderate reliability
    uint8_t hopCount = 2;

    // Act
    uint32_t metric = eigrpInstance->calculateMetric(bandwidth, load, delay, reliability, hopCount);

    // Assert
    ASSERT_GT(metric, 0);
    ASSERT_LT(metric, std::numeric_limits<uint32_t>::max());
}

#pragma endregion
#pragma region UpdateInterface

// Test: Add a new interface to the list (IPv4)
// Ensures that an active interface is correctly added to the interface list.
TEST_F(EigrpTest, UpdateInterfaceList_AddInterfaceIPv4) 
{
    // Arrange
    EigrpConfigs::Network network{ByteString("\xC0\xA8\x01\x00", 4), ByteString("\x00\x00\x00\xFF", 4)};
    eigrpInstance->addNetwork(network);

    // Act
    eigrpInstance->updateInterfaceList();

    // Assert
    ASSERT_EQ(getInterfaceList().size(), 1);
}

// Test: Remove an interface out of network scope
// Ensures that an interface is removed from the list if it no longer matches the network scope.
TEST_F(EigrpTest, UpdateInterfaceList_RemoveInterfaceOutOfScope) 
{
    // Arrange
    EigrpConfigs::Network network{ByteString("\xC0\xA8\x01\x00", 4), ByteString("\x00\x00\x00\xFF", 4)};
    eigrpInstance->addNetwork(network);
    eigrpInstance->updateInterfaceList();

    // Act
    clearNetworks();
    eigrpInstance->updateInterfaceList();

    // Assert
    ASSERT_TRUE(getInterfaceList().empty());
}

// Test: Handle no active interfaces
// Ensures that no interfaces are added when none are active.
TEST_F(EigrpTest, UpdateInterfaceList_NoActiveInterfaces) 
{
    // Arrange
    getInterfaceList().erase(0);
    getIpInfo().ipv4.ipAddress.clear();
    EigrpConfigs::Network network{ByteString("\xC0\xA8\x01\x00", 4), ByteString("\x00\x00\x00\xFF", 4)};
    eigrpInstance->addNetwork(network);

    // Act
    eigrpInstance->updateInterfaceList();

    // Assert
    ASSERT_TRUE(getInterfaceList().empty());
}

// Test: Handle overlapping networks for interfaces
// Ensures that overlapping networks are handled correctly for interfaces.
TEST_F(EigrpTest, UpdateInterfaceList_OverlappingNetworks) 
{
    // Arrange
    EigrpConfigs::Network network1{ByteString("\xC0\xA8\x01\x00", 4), ByteString("\x00\x00\x00\xFF", 4)};
    EigrpConfigs::Network network2{ByteString("\xC0\xA8\x00\x00", 4), ByteString("\x00\x00\xFF\xFF", 4)};
    eigrpInstance->addNetwork(network1);
    eigrpInstance->addNetwork(network2);

    // Act
    eigrpInstance->updateInterfaceList();

    // Assert
    ASSERT_EQ(getInterfaceList().size(), 1);
}

// Test: Make sure interface will delete itself from the routing table (IPv4)
// Ensures that an active interface is correctly added to the interface list.
TEST_F(EigrpTest, UpdateInterfaceList_DeleteInterfaceProperly) 
{
    // Arrange
    EigrpConfigs::Network network{ByteString("\xC0\xA8\x01\x00", 4), ByteString("\x00\x00\x00\xFF", 4)};
    eigrpInstance->addNetwork(network); // Add a network that matches the current interface

    // Assert
    ASSERT_EQ(getInterfaceList().size(), 1);
    ASSERT_EQ(RoutingTable::getInstance().getAllConnectedEigrpRoutes(AddressFamily::IPv4, asNumber).size(), 1);

    clearNetworks();
    eigrpInstance->updateInterfaceList();

    ASSERT_TRUE(RoutingTable::getInstance().getAllConnectedEigrpRoutes(AddressFamily::IPv4, asNumber).empty());
}

#pragma endregion
#pragma region CalculateParameters


// Test: Validate default K-values in parameter calculation
// Ensures that default K-values and hold time are correctly calculated.
TEST_F(EigrpTest, CalculateParameters_DefaultKValues) 
{
    // Act
    ByteString parameters = eigrpInstance->calculateParameters(15); // Default hold time: 15 seconds

    // Assert
    ASSERT_EQ(parameters.size(), 8);
    ASSERT_EQ(parameters, ByteString("\x01\x00\x01\x00\x00\x00\x00\x0F", 8));
}

// Test: Validate custom K-values in parameter calculation
// Ensures that custom K-values are correctly calculated.
TEST_F(EigrpTest, CalculateParameters_CustomKValues) 
{
    // Arrange
    eigrpInstance->getConfigs()->kvalue.k1_Bandwidth = 2;
    eigrpInstance->getConfigs()->kvalue.k2_Load = 3;
    eigrpInstance->getConfigs()->kvalue.k3_Delay = 4;
    eigrpInstance->getConfigs()->kvalue.k4_Reliability = 5;
    eigrpInstance->getConfigs()->kvalue.k5_MTU = 6;

    // Act
    ByteString parameters = eigrpInstance->calculateParameters(30); // Custom hold time: 30 seconds

    // Assert
    ASSERT_EQ(parameters.size(), 8);
    ASSERT_EQ(parameters, ByteString("\x02\x03\x04\x05\x06\x00\x00\x1E", 8));
}

#pragma endregion
#pragma region UpdateRoutingTable

// Test: Add a connected route to the routing table (IPv4)
TEST_F(EigrpTest, UpdateRoutingTableForConnected_AddRouteIPv4) 
{
    // Arrange
    eigrpInstance->addNetwork({ByteString("\xC0\xA8\x01\x00", 4), ByteString("\x00\x00\x00\xFF", 4)});

    // Act
    eigrpInstance->updateRoutingTableForConnected(mockEigrpInterface);

    // Assert
    ASSERT_TRUE(RoutingTable::getInstance().getEigrpRoute(ByteString("\xC0\xA8\x01\x00", 4), 24, AddressFamily::IPv4, asNumber).has_value());
}

// Test: Remove a connected route when interface goes inactive
TEST_F(EigrpTest, UpdateRoutingTableForConnected_RemoveRoute) 
{
    // Arrange
    eigrpInstance->addNetwork({ByteString("\xC0\xA8\x01\x00", 4), ByteString("\xFF\xFF\xFF\x00", 4)});
    eigrpInstance->updateRoutingTableForConnected(mockEigrpInterface);
    mockInterface->Shutdown(true);

    // Act
    eigrpInstance->updateRoutingTableForConnected(mockEigrpInterface);

    // Assert
    ASSERT_FALSE(RoutingTable::getInstance().getEigrpRoute(ByteString("\xC0\xA8\x01\x00", 4), 24, AddressFamily::IPv4, asNumber).has_value());
}

// Test: Handle multiple connected routes in the routing table
TEST_F(EigrpTest, UpdateRoutingTableForConnected_MultipleRoutes) 
{
    MockInterface* interface = new MockInterface();
    interface->blockEnqueues();

    interface->configs.id = 1;
    setIPv4(ByteString("\x0A\x00\x00\x02", 4), 24, interface);
    interfaceList[InterfaceType::GIGABIT_ETHERNET][1] = interface;

    // Arrange
    eigrpInstance->addNetwork({ByteString("\xC0\xA8\x01\x00", 4), ByteString("\x00\x00\x00\xFF", 4)});
    eigrpInstance->addNetwork({ByteString("\x0A\x00\x00\x00", 4), ByteString("\x00\xFF\xFF\xFF", 4)});

    // Assert
    ASSERT_EQ(RoutingTable::getInstance().getAllEigrpRoutes(AddressFamily::IPv4, asNumber).size(), 2);

    eigrpInstance->shutdown();
    delete interface;
}

#pragma endregion
#pragma region NotifyRoutingChange

// Test: Notify neighbors of added routes
// Ensures that neighbors are notified of new routes being added.
TEST_F(EigrpTest, NotifyRoutingChange_AddedRoutes) 
{
    addNeighbor(ByteString("\xc0\xa8\x01\x02", 4), EigrpConfigs::CommunicationMode::MULTICAST);
    // Arrange
    RoutingTable::Eigrp route;
    route.network = ByteString("\xc0\xa8\x02\x00", 4);
    route.mask = 24;
    std::vector<RoutingTable::Eigrp*> routesToAdd{&route};
    EXPECT_CALL(*mockEigrpInterface, sendUpdateToNeighbor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .Times(1);

    // Act
    eigrpInstance->notifyRoutingChange(routesToAdd, false);
}

// Test: Notify neighbors of removed routes
// Ensures that neighbors are notified when routes are withdrawn.
TEST_F(EigrpTest, NotifyRoutingChange_RemovedRoutes) 
{
    addNeighbor(ByteString("\xc0\xa8\x01\x02", 4), EigrpConfigs::CommunicationMode::MULTICAST);
    // Arrange
    RoutingTable::Eigrp route;
    route.network = ByteString("\xc0\xa8\x01\x00", 4);
    route.mask = 24;
    std::vector<RoutingTable::Eigrp*> routesToRemove{&route};
    EXPECT_CALL(*mockEigrpInterface, sendUpdateToNeighbor(::testing::_, ::testing::_, EigrpConfigs::UpdateType::WITHDRAW, ::testing::_, ::testing::_, ::testing::_))
        .Times(1);

    // Act
    eigrpInstance->notifyRoutingChange(routesToRemove, true);
}

// Test: Notify neighbors of multiple route changes
// Ensures that multiple route changes are notified correctly.
TEST_F(EigrpTest, NotifyRoutingChange_MultipleRoutes) 
{
    // Arrange
    addNeighbor(ByteString("\xc0\xa8\x01\x02", 4), EigrpConfigs::CommunicationMode::MULTICAST);
    RoutingTable::Eigrp route1;
    route1.network = ByteString("\xc0\xa8\x01\x00", 4);
    route1.mask = 24;
    RoutingTable::Eigrp route2;
    route2.network = ByteString("\x0a\x00\x00\x00", 4);
    route2.mask = 8;
    std::vector<RoutingTable::Eigrp*> routes{&route1, &route2};

    EXPECT_CALL(*mockEigrpInterface, sendUpdateToNeighbor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .Times(1);

    // Act
    eigrpInstance->notifyRoutingChange(routes, false);
}

#pragma endregion
#pragma region Shutdown

// Test: Shutdown clears all interfaces and resources
// Ensures that shutdown cleans up all interfaces and the topology table.
TEST_F(EigrpTest, Shutdown_CleanInterfacesAndResources) 
{
    // Arrange
    eigrpInstance->addNetwork({ByteString("\xC0\xA8\x01\x00", 4), ByteString("\xFF\xFF\xFF\x00", 4)});
    eigrpInstance->updateInterfaceList();

    // Act
    eigrpInstance->shutdown();

    // Assert
    ASSERT_TRUE(getInterfaceList().empty());
    ASSERT_EQ(getTopologyTable(), nullptr);
}

#pragma endregion
#pragma region AddNetwork

// Test: Add a new network scope
// Ensures that a network is correctly added to the configuration.
TEST_F(EigrpTest, AddNetwork_ValidNetwork) 
{
    // Arrange
    EigrpConfigs::Network network{ByteString("\xC0\xA8\x01\x00", 4), ByteString("\xFF\xFF\xFF\x00", 4)};

    // Act
    eigrpInstance->addNetwork(network);

    // Assert
    ASSERT_EQ(eigrpInstance->getConfigs()->networks.size(), 1);
}

// Test: Add a duplicate network
// Ensures that adding the same network multiple times does not create duplicates.
TEST_F(EigrpTest, AddNetwork_DuplicateNetwork) 
{
    // Arrange
    EigrpConfigs::Network network{ByteString("\xC0\xA8\x01\x00", 4), ByteString("\x00\x00\x00\xFF", 4)};
    eigrpInstance->addNetwork(network);

    // Act
    eigrpInstance->addNetwork(network);

    // Assert
    ASSERT_EQ(eigrpInstance->getConfigs()->networks.size(), 1);
}

#pragma endregion
#pragma region SummaryRoute

// Test: Add a summary route
// Ensures that a summary route is correctly added to the routing table.
TEST_F(EigrpTest, AddSummaryRoute_ValidSummary) 
{
    // Act
    eigrpInstance->addSummaryRoute(ByteString("\xC0\xA8\x00\x00", 4), 16, 0);

    // Assert
    ASSERT_TRUE(RoutingTable::getInstance().getEigrpRoute(ByteString("\xC0\xA8\x00\x00", 4), 16, AddressFamily::IPv4, asNumber).has_value());
}

// Test: Add a summary route with invalid mask
// Ensures that adding a summary route with an invalid mask does nothing.
TEST_F(EigrpTest, AddSummaryRoute_InvalidMask) 
{
    // Act
    eigrpInstance->addSummaryRoute(ByteString("\xC0\xA8\x00\x00", 4), 33, 0); // Invalid mask

    // Assert
    ASSERT_FALSE(RoutingTable::getInstance().getEigrpRoute(ByteString("\xC0\xA8\x00\x00", 4), 33, AddressFamily::IPv4, asNumber).has_value());
}

#pragma endregion
#pragma region RemoveSummaryRoute

// Test: Remove a valid summary route
// Ensures that a summary route is correctly removed from the routing table.
TEST_F(EigrpTest, RemoveSummaryRoute_ValidSummary) 
{
    // Arrange
    eigrpInstance->addSummaryRoute(ByteString("\xC0\xA8\x00\x00", 4), 16, 0);

    // Act
    eigrpInstance->removeSummaryRoute(ByteString("\xC0\xA8\x00\x00", 4), 16);

    // Assert
    ASSERT_FALSE(RoutingTable::getInstance().getEigrpRoute(ByteString("\xC0\xA8\x00\x00", 4), 16, AddressFamily::IPv4, asNumber).has_value());
}

// Test: Remove a non-existent summary route
// Ensures that removing a non-existent summary route does not cause issues.
TEST_F(EigrpTest, RemoveSummaryRoute_NonExistent) 
{
    // Act
    eigrpInstance->removeSummaryRoute(ByteString("\xC0\xA8\x00\x00", 4), 16);
}

#pragma endregion
#pragma region EnableAutoSummary

// FIXME
// Test: Enable auto-summary
// Ensures that enabling auto-summary creates appropriate summary routes.
TEST_F(EigrpTest, EnableAutoSummary_Enable) 
{
    // Arrange
    EigrpConfigs::Network network{ByteString("\xC0\xA8\x01\x00", 4), ByteString("\xFF\xFF\xFF\x00", 4)};
    eigrpInstance->addNetwork(network);

    // Act
    eigrpInstance->enableAutoSummary(true);

    // Assert
    ASSERT_FALSE(eigrpInstance->getConfigs()->summaryRoutes.empty());
}

// Test: Disable auto-summary
// Ensures that disabling auto-summary removes all auto-summary routes.
TEST_F(EigrpTest, EnableAutoSummary_Disable) 
{
    // Arrange
    eigrpInstance->enableAutoSummary(true);

    // Act
    eigrpInstance->enableAutoSummary(false);

    // Assert
    ASSERT_TRUE(eigrpInstance->getConfigs()->summaryRoutes.empty());
}

#pragma endregion
#pragma region Stub

// Test: Set EIGRP to stub mode
// Ensures that EIGRP is correctly configured as a stub.
TEST_F(EigrpTest, SetStub_EnableStub) 
{
    // Act
    eigrpInstance->setStub(true, true, true, true, true);

    // Assert
    ASSERT_TRUE(eigrpInstance->getConfigs()->stubConfig.isStub);
    ASSERT_TRUE(eigrpInstance->getConfigs()->stubConfig.advertiseConnected);
}

// Test: Disable stub mode
// Ensures that disabling stub mode updates the configuration correctly.
TEST_F(EigrpTest, SetStub_DisableStub) 
{
    // Arrange
    eigrpInstance->setStub(true, true, true, true, true);

    // Act
    eigrpInstance->setStub(false, false, false, false, false);

    // Assert
    ASSERT_FALSE(eigrpInstance->getConfigs()->stubConfig.isStub);
}

#pragma endregion
#pragma region AddDefautlRoute

// FIXME
// Test: Add a default route
// Ensures that a default route is correctly added to the routing table.
TEST_F(EigrpTest, AddDefaultRoute) 
{
    // Act
    eigrpInstance->addDefaultRoute();

    // Assert
    auto defaultRoute = RoutingTable::getInstance().getEigrpRoute(eigrpInstance->getConfigs()->defaultNetwork,
                                                        eigrpInstance->getConfigs()->defaultMask,
                                                        AddressFamily::IPv4, asNumber);
    ASSERT_TRUE(defaultRoute.has_value());
}

// Test: Remove a default route
// Ensures that a default route is correctly removed from the routing table.
TEST_F(EigrpTest, RemoveDefaultRoute) 
{
    // Arrange
    eigrpInstance->addDefaultRoute();

    // Act
    eigrpInstance->removeDefaultRoute();

    // Assert
    auto defaultRoute = RoutingTable::getInstance().getEigrpRoute(eigrpInstance->getConfigs()->defaultNetwork,
                                                        eigrpInstance->getConfigs()->defaultMask,
                                                        AddressFamily::IPv4, asNumber);
    ASSERT_FALSE(defaultRoute.has_value());
}

#pragma endregion
#pragma region UpdatingRoutes

// Test: Recalculate routes with new variance
// Ensures that routes are updated in the routing table based on new variance.
TEST_F(EigrpTest, UpdatingRoutes_UpdateWithVariance) 
{
    // Arrange
    ByteString neighborIp = ByteString("\xc0\xa8\x01\x02", 4);
    addNeighbor(neighborIp, EigrpConfigs::CommunicationMode::MULTICAST);
    RoutingTable::Eigrp route1;
    route1.network = ByteString("\xc0\xa8\x01\x00", 4);
    route1.mask = 24;
    route1.nextHop = neighborIp;
    RoutingTable::Eigrp route2;
    route2.network = ByteString("\x0a\x00\x00\x00", 4);
    route2.mask = 8;
    route1.nextHop = neighborIp;
    mockEigrpInterface->updateRoutingTable(getNeighbors(neighborIp).value(), neighborIp, {&route1, &route2}, false);

    // Act
    eigrpInstance->setVariance(2);

    // Assert
    ASSERT_EQ(getTopologyTable()->getTopologyEntries().size(), 2);
}

// Test: Recalculate routes with invalid variance
// Ensures that routes are unaffected by invalid variance settings.
TEST_F(EigrpTest, UpdatingRoutes_InvalidVariance)
{
    ByteString neighborIp = ByteString("\xc0\xa8\x01\x02", 4);
    addNeighbor(neighborIp, EigrpConfigs::CommunicationMode::MULTICAST);
    // Arrange
    RoutingTable::Eigrp route;
    route.network = ByteString("\xc0\xa8\x01\x00", 4);
    route.mask = 24;
    route.nextHop = neighborIp;
    mockEigrpInterface->updateRoutingTable(getNeighbors(neighborIp).value(), neighborIp, {&route}, false);

    // Act
    eigrpInstance->setVariance(0); // Invalid variance

    // Assert
    ASSERT_EQ(eigrpInstance->getConfigs()->variance, 1);
    ASSERT_EQ(getTopologyTable()->getTopologyEntries().size(), 1);
}

// Test: Recalculate routes with feasible successors
// Ensures that routes with feasible successors are updated correctly.
TEST_F(EigrpTest, RecalculateRoutes_WithFeasibleSuccessor) 
{
    ByteString neighborIp1 = ByteString("\xc0\xa8\x01\x02", 4);
    ByteString neighborIp2 = ByteString("\xc0\xa8\x01\x03", 4);
    addNeighbor(neighborIp1, EigrpConfigs::CommunicationMode::MULTICAST);
    addNeighbor(neighborIp2, EigrpConfigs::CommunicationMode::MULTICAST);
    // Arrange
    RoutingTable::Eigrp route1;
    route1.network = ByteString("\x0a\x00\x00\x00", 4);
    route1.mask = 8;
    route1.feasibleDistance = 100;
    route1.reportedDistance = 80;
    route1.nextHop = ByteString("\xc0\xa8\x01\x02", 4);
    RoutingTable::Eigrp route2;
    route2.network = ByteString("\x0a\x00\x00\x00", 4);
    route2.mask = 8;
    route2.feasibleDistance = 150;
    route2.reportedDistance = 70;
    route2.nextHop = ByteString("\xc0\xa8\x01\x03", 4);
    mockEigrpInterface->updateRoutingTable(getNeighbors(neighborIp1).value(), neighborIp1, {&route1}, false);
    mockEigrpInterface->updateRoutingTable(getNeighbors(neighborIp2).value(), neighborIp2, {&route2}, false);

    // Act
    eigrpInstance->setVariance(2);

    // Assert
    ASSERT_EQ(getTopologyTable()->getTopologyEntries().size(), 1);

    auto& routeInfo = getTopologyTable()->getTopologyEntries()[ByteString("\x0a\x00\x00\x00", 4)];

    ASSERT_EQ(routeInfo->routesByNeighbor.size(), 2);
    ASSERT_EQ(routeInfo->feasibleSuccessors.size(), 2);
    ASSERT_EQ(routeInfo->successors.size(), 1);
    ASSERT_EQ(routeInfo->successors[0], ByteString("\xc0\xa8\x01\x02", 4));
}

// Test: Recalculate routes with invalid feasible successors
// Ensures that routes with feasible successors are updated correctly.
TEST_F(EigrpTest, RecalculateRoutes_WithoutFeasibleSuccessor) 
{
    ByteString neighborIp1 = ByteString("\xc0\xa8\x01\x02", 4);
    ByteString neighborIp2 = ByteString("\xc0\xa8\x01\x03", 4);
    addNeighbor(neighborIp1, EigrpConfigs::CommunicationMode::MULTICAST);
    addNeighbor(neighborIp2, EigrpConfigs::CommunicationMode::MULTICAST);
    // Arrange
    RoutingTable::Eigrp route1;
    route1.network = ByteString("\x0a\x00\x00\x00", 4);
    route1.mask = 8;
    route1.feasibleDistance = 100;
    route1.reportedDistance = 80;
    route1.nextHop = ByteString("\xc0\xa8\x01\x02", 4);
    RoutingTable::Eigrp route2;
    route2.network = ByteString("\x0a\x00\x00\x00", 4);
    route2.mask = 8;
    route2.feasibleDistance = 150;
    route2.reportedDistance = 120;
    route2.nextHop = ByteString("\xc0\xa8\x01\x03", 4);
    mockEigrpInterface->updateRoutingTable(getNeighbors(neighborIp1).value(), neighborIp1, {&route1}, false);
    mockEigrpInterface->updateRoutingTable(getNeighbors(neighborIp2).value(), neighborIp2, {&route2}, false);

    // Act
    eigrpInstance->setVariance(2);

    // Assert
    ASSERT_EQ(getTopologyTable()->getTopologyEntries().size(), 1);

    auto& routeInfo = getTopologyTable()->getTopologyEntries()[ByteString("\x0a\x00\x00\x00", 4)];

    ASSERT_EQ(routeInfo->routesByNeighbor.size(), 2);
    ASSERT_EQ(routeInfo->feasibleSuccessors.size(), 1);
    ASSERT_EQ(routeInfo->successors.size(), 1);
    ASSERT_EQ(routeInfo->successors[0], ByteString("\xc0\xa8\x01\x02", 4));
}

#pragma endregion
#pragma region GracefulRestart

// Test: Perform graceful restart
// Ensures that EIGRP preserves state and notifies neighbors during a graceful restart.
TEST_F(EigrpTest, GracefulRestart_PreserveState) 
{
    // Expect neighbors to be notified during the restart
    addNeighbor(ByteString("\x0a\x00\x00\x01", 1), EigrpConfigs::CommunicationMode::MULTICAST);
    EXPECT_CALL(*mockEigrpInterface, sendHelloPacket(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .Times(::testing::AtLeast(1));

    // Act
    eigrpInstance->gracefulRestart();

    // Assert
}

// Test: Restart EIGRP process
// Ensures that restarting the EIGRP process clears state and reinitializes it.
TEST_F(EigrpTest, Restart_ClearAndReinitialize) 
{
    // Act
    eigrpInstance->restart();

    // Assert
    ASSERT_TRUE(getInterfaceList().empty());
    ASSERT_NE(getTopologyTable(), nullptr);
}

// Test: Restart preserves neighbor backup
// Ensures that neighbor information is backed up.
TEST_F(EigrpTest, Restart_PreserveNeighborBackup) 
{
    addNeighbor(ByteString("\xc0\xa8\x00\x01"), EigrpConfigs::CommunicationMode::MULTICAST);
    addNeighbor(ByteString("\xc0\xa8\x00\x02"), EigrpConfigs::CommunicationMode::MULTICAST);
    // Act
    eigrpInstance->restart();

    // Assert
    ASSERT_TRUE(!eigrpInstance->neighborBackup.empty());
}

#pragma endregion
#pragma region Cleanup

// Test: Cleanup resources
// Ensures that cleanup removes all configuration and resource allocations.
TEST_F(EigrpTest, Cleanup_RemoveAllResources) 
{
    // Arrange
    eigrpInstance->initializeEigrp();

    // Act
    eigrpInstance->cleanup();

    // Assert
    ASSERT_TRUE(getInterfaceList().empty());
    ASSERT_TRUE(eigrpInstance->getConfigs()->networks.empty());
}

// Test: Periodic maintenance prunes stale routes
// Ensures that periodic maintenance removes stale routes from the topology table.
TEST_F(EigrpTest, PeriodicMaintenance_PruneStaleRoutes) 
{
    // Arrange
    eigrpInstance->initializeEigrp();

    // Simulate stale routes in the topology table
    auto topologyTable = getTopologyTable();
    eigrpInstance->updateInterfaceList();
    eigrpInstance->updateRoutingTableForConnected();
    TopologyTable::RouteInfo routeInfo;
    routeInfo.lastUpdate = std::chrono::steady_clock::now() - std::chrono::seconds(100);
    getTopologyTable()->addOrUpdateRoute(ByteString("\xc0\xa8\x01\x02", 4), ByteString("\x0a\x00\x00\x00"), 8, routeInfo);
    mockEigrpInterface->addNeighbor(ByteString("\xc0\xa8\x01\x02", 4), ByteString("\x11\xBB\x33\xDD\x55\xFF", 6), EigrpConfigs::CommunicationMode::MULTICAST);

    // Act
    eigrpInstance->periodicMaintenance();

    // Assert
    ASSERT_TRUE(topologyTable->getTopologyEntries().empty());
}

#pragma endregion
#pragma region RouterIdHandling

// Test: Calculate router ID using highest loopback IP
// Ensures that the router ID is elected based on the highest loopback IP.
TEST_F(EigrpTest, CalculateRouterID_HighestLoopback) 
{
    // Arrange
    setIPv4(ByteString("\xC0\xA8\xFF\xFE", 4), 24);
    interfaceList[InterfaceType::GIGABIT_ETHERNET].clear();
    interfaceList[InterfaceType::LOOPBACK][0] = mockInterface;

    // Act
    eigrpInstance->calculateRouterID();

    // Assert
    ASSERT_EQ(eigrpInstance->getRouterID(), ByteString("\xC0\xA8\xFF\xFE", 4));
}

// Test: Calculate router ID using highest non-loopback IP
// Ensures that the router ID falls back to the highest non-loopback IP if no loopback exists.
TEST_F(EigrpTest, CalculateRouterID_HighestNonLoopback) 
{
    // Act
    eigrpInstance->calculateRouterID();

    // Assert
    ASSERT_EQ(eigrpInstance->getRouterID(), ByteString("\xC0\xA8\x01\x01", 4));
}

// Test: Handle empty router ID calculation
// Ensures that an empty router ID configuration does not crash the process.
TEST_F(EigrpTest, CalculateRouterID_EmptyConfiguration) 
{
    // Arrange
    mockInterface->Shutdown(true);

    // Act
    eigrpInstance->calculateRouterID();

    // Assert
    ASSERT_EQ(eigrpInstance->getRouterID(), ByteString());
}

// Test: Prevent duplicate router IDs
// Ensures that duplicate router IDs are detected and handled.
TEST_F(EigrpTest, CalculateRouterID_PreventDuplicate) 
{
    // Arrange
    MockInterface duplicateMockInterface;
    setIPv4(ByteString("\xc0\xa8\x01\x01", 4), 24, &duplicateMockInterface);

    interfaceList[InterfaceType::LOOPBACK][0] = &duplicateMockInterface;
    interfaceList[InterfaceType::LOOPBACK][1] = &duplicateMockInterface;

    // Act
    eigrpInstance->calculateRouterID();

    // Assert
    ASSERT_NE(eigrpInstance->getRouterID(), ByteString("\xC0\xA8\x00\x01", 4)) << "Duplicate router IDs should not be allowed.";
    interfaceList[InterfaceType::LOOPBACK].erase(0);
    interfaceList[InterfaceType::LOOPBACK].erase(1);
}

// Test: Handle flapping router ID elections
// Ensures that frequent changes in router IDs due to interface flaps are stabilized.
TEST_F(EigrpTest, CalculateRouterID_FlappingInterfaces) 
{
    // Arrange
    MockInterface flappInterface;
    setIPv4(ByteString("\xc0\xa8\x01\x02", 4), 24, &flappInterface);
    
    interfaceList[InterfaceType::LOOPBACK][0] = mockInterface;
    interfaceList[InterfaceType::LOOPBACK][1] = &flappInterface;

    // Act
    eigrpInstance->calculateRouterID();

    // Assert
    ASSERT_NE(eigrpInstance->getRouterID(), mockInterface->Get()->ipv4.ipAddress);
    interfaceList[InterfaceType::LOOPBACK].erase(0);
    interfaceList[InterfaceType::LOOPBACK].erase(1);
}

// Test: Handle neighbors with overlapping IP ranges
// Ensures that overlapping IP ranges between neighbors are handled correctly.
TEST_F(EigrpTest, HandleOverlappingIPRanges) 
{
    // Arrange
    EigrpConfigs::Network network1{ByteString("\xC0\xA8\x01\x00", 4), ByteString("\xFF\xFF\xFF\x00", 4)};
    EigrpConfigs::Network network2{ByteString("\xC0\xA8\x00\x00", 4), ByteString("\xFF\xFF\x00\x00", 4)};
    eigrpInstance->addNetwork(network1);
    eigrpInstance->addNetwork(network2);

    // Act
    eigrpInstance->updateInterfaceList();

    // Assert
    ASSERT_GE(getInterfaceList().size(), 1);
}

// Test: Detect duplicate router IDs across neighbors
// Ensures that duplicate router IDs across neighbors are flagged.
// FIXME
/*
TEST_F(EigrpTest, DetectDuplicateRouterIDs) {
    // Arrange
    eigrpInstance->routerID.ID = ByteString("\xC0\xA8\x01\x01", 4);

    // Act
    auto duplicateDetected = eigrpInstance->detectDuplicateRouterID(ByteString("\xC0\xA8\x01\x01", 4));

    // Assert
    ASSERT_TRUE(duplicateDetected) << "Duplicate router IDs should be detected.";
}
*/

// Test: Handle flapping interfaces gracefully
// Ensures that frequent interface state changes do not disrupt routing behavior.
TEST_F(EigrpTest, HandleFlappingInterfaces) 
{
    // Set the id
    mockInterface->configs.id = 0;
    // Arrange
    eigrpInstance->addNetwork({ByteString("\xC0\xA8\x01\x00", 4), ByteString("\x00\x00\x00\xff", 4)});

    // Act
    eigrpInstance->updateInterfaceList();
    mockInterface->Shutdown(true);
    eigrpInstance->updateInterfaceList(); // Simulate second flap

    // Assert
    ASSERT_TRUE(getInterfaceList().empty()) << "Interface should be removed after flapping.";
}

// Test: Handle large-scale routing table
// Ensures that EIGRP processes a routing table with a large number of entries efficiently.
TEST_F(EigrpTest, LargeScaleRoutingTable) 
{
    // Arrange
    for (int i = 0; i < 250; i++) {
        RoutingTable::Eigrp* route = new RoutingTable::Eigrp();
        route->network = ByteString("\xC0\xA8" + std::string(1, static_cast<char>(i)) + "\x00", 4);
        route->mask = 24;
        route->nextHop = ByteString("\x00\x00\x00\x00", 4);
        RoutingTable::getInstance().addEigrp(route, AddressFamily::IPv4, asNumber);
    }

    // Act
    auto routes = RoutingTable::getInstance().getAllEigrpRoutes(AddressFamily::IPv4, asNumber);

    // Assert
    ASSERT_EQ(routes.size(), 250) << "Routing table should have 500 entries.";

    for (auto* route : RoutingTable::getInstance().getAllEigrpRoutes(AddressFamily::IPv4, 1))
    {
        delete route;
        route = nullptr;
    }
}

// Test: Validate convergence with high route churn
// Ensures EIGRP converges correctly under frequent route additions and removals.
TEST_F(EigrpTest, ValidateConvergenceWithRouteChurn) 
{
    // Arrange
    for (int i = 0; i < 100; i++) {
        RoutingTable::Eigrp* route = new RoutingTable::Eigrp();
        route->network = ByteString("\xC0\xA8" + std::string(1, static_cast<char>(i)) + "\x00", 4);
        route->mask = 24;
        route->nextHop = ByteString("\x00\x00\x00\x00", 4);
        RoutingTable::getInstance().addEigrp(route, AddressFamily::IPv4, asNumber);
    }

    // Act
    for (int i = 0; i < 50; i++) {
        ByteString network("\xC0\xA8" + std::string(1, static_cast<char>(i)) + "\x00", 4);
        RoutingTable::getInstance().removeEigrp(network, 24, AddressFamily::IPv4, asNumber);
    }

    // Assert
    ASSERT_EQ(RoutingTable::getInstance().getAllEigrpRoutes(AddressFamily::IPv4, asNumber).size(), 50) << "50 routes should remain after churn.";

    for (auto* route : RoutingTable::getInstance().getAllEigrpRoutes(AddressFamily::IPv4, 1))
    {
        delete route;
        route = nullptr;
    }
}

#pragma endregion
#pragma region AdvancedPacketProcessing

// Test: Validate proper construction of EIGRP update packets
// Ensures that update packets are constructed with the correct fields and values.
TEST_F(EigrpTest, ConstructUpdatePacket_ValidFields) 
{
    // Arrange
    EigrpHeader updatePacket;
    uint32_t sequenceNumber = 1234;

    // Act
    eigrpInstance->eigrpUpdate(updatePacket, sequenceNumber, true, false, false, false, false, false);

    // Assert
    ASSERT_EQ(updatePacket.opcode, Variable::Eigrp::Type::update);
    ASSERT_EQ(updatePacket.sequence, Functions::numToByte(sequenceNumber, 4));
}

// Test: Validate EIGRP query packets
// Ensures that query packets are constructed correctly.
TEST_F(EigrpTest, ConstructQueryPacket) 
{
    // Arrange
    EigrpHeader queryPacket;
    uint32_t sequenceNumber = 5678;

    // Act
    eigrpInstance->eigrpUpdate(queryPacket, sequenceNumber, false, true, false, false, true, false);

    // Assert
    ASSERT_EQ(queryPacket.opcode, Variable::Eigrp::Type::query);
    ASSERT_EQ(queryPacket.sequence, Functions::numToByte(sequenceNumber, 4));
}

// Test: Validate EIGRP reply packets
// Ensures that reply packets are constructed correctly.
TEST_F(EigrpTest, ConstructReplyPacket) 
{
    // Arrange
    EigrpHeader replyPacket;
    uint32_t sequenceNumber = 91011;

    // Act
    eigrpInstance->eigrpUpdate(replyPacket, sequenceNumber, false, false, false, false, false, true);

    // Assert
    ASSERT_EQ(replyPacket.opcode, Variable::Eigrp::Type::reply) << "Opcode should indicate a reply packet.";
    ASSERT_EQ(replyPacket.sequence, Functions::numToByte(sequenceNumber, 4)) << "Sequence number should match.";
}

#pragma endregion
#pragma region DuelStack

// Test: Form adjacencies on IPv4 and IPv6 simultaneously
// Ensures that adjacencies can form correctly in both IPv4 and IPv6 environments.
TEST_F(EigrpTest, FormAdjacencies_DualStack) 
{
    // Arrange
    auto ipv6Instance = new Eigrp(asNumber, AddressFamily::IPv6);

    // Act
    eigrpInstance->addNetwork({ByteString("\xC0\xA8\x01\x00", 4), ByteString("\xFF\xFF\xFF\x00", 4)});
    ipv6Instance->addNetwork({ByteString("\x20\x01\x0D\xB8\x00\x00\x00\x00", 8), ByteString("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 8)});

    // Simulate adjacency formation
    EXPECT_CALL(*mockEigrpInterface, sendHelloPacket(::testing::_, ::testing::_, ::testing::_, ::testing::_)).Times(::testing::AtLeast(2));

    // Assert
    delete ipv6Instance;
}

// Test: Validate IPv6 route summarization
// Ensures that IPv6 summary routes are created correctly.
TEST_F(EigrpTest, ValidateIPv6RouteSummarization) 
{
    // Arrange
    delete eigrpInstance;
    eigrpInstance = new Eigrp(asNumber, AddressFamily::IPv6);
    eigrpInstance->initializeEigrp();

    // Act
    eigrpInstance->addSummaryRoute(ByteString("\x20\x01\x0D\xB8\x00\x00\x00\x00", 8), 64, 0);

    // Assert
    ASSERT_TRUE(RoutingTable::getInstance().getEigrpRoute(ByteString("\x20\x01\x0D\xB8\x00\x00\x00\x00", 8), 64, AddressFamily::IPv6, asNumber).has_value());
}

#pragma endregion
#pragma region MiscellaneousTesting

// Test: Handle empty network configurations
// Ensures that an empty network configuration does not crash the system.
TEST_F(EigrpTest, HandleEmptyNetworkConfigurations) 
{
    // Act
    eigrpInstance->updateInterfaceList();

    // Assert
    ASSERT_TRUE(getInterfaceList().empty());
}

// Test: Handle excessive K-value configurations
// Ensures that invalid or excessive K-value configurations are handled gracefully.
TEST_F(EigrpTest, HandleExcessiveKValueConfigurations) 
{
    // Arrange
    eigrpInstance->getConfigs()->kvalue.k1_Bandwidth = 100;
    eigrpInstance->getConfigs()->kvalue.k3_Delay = 200;

    // Act
    ByteString parameters = eigrpInstance->calculateParameters(15);

    // Assert
    ASSERT_EQ(parameters.size(), 8);
}

// Test: Validate default settings upon initialization
// Ensures that EIGRP initializes with correct default configurations.
TEST_F(EigrpTest, ValidateDefaultSettingsOnInitialization) 
{
    // Act
    eigrpInstance->initializeEigrp();

    // Assert
    ASSERT_EQ(eigrpInstance->getConfigs()->kvalue.k1_Bandwidth, 1);
    ASSERT_EQ(eigrpInstance->getConfigs()->wideMetric, 10000000);
    ASSERT_FALSE(eigrpInstance->getConfigs()->autoSummarizationEnabled);
}

#pragma endregion
#pragma region MultiInterface

// Test: Propagate a route across multiple interfaces
// Ensures that a route is sent through all active interfaces to notify neighbors.
TEST_F(EigrpTest, PropagateRouteAcrossMultipleInterfaces) 
{
    // Arrange
    MockInterface* interface1 = new MockInterface();
    MockInterface* interface2 = new MockInterface();
    interface1->blockEnqueues();
    interface2->blockEnqueues();

    // Add mock interfaces
    auto eigrpInterface1 = new MockEigrpInterface(*eigrpInstance, interface1);
    auto eigrpInterface2 = new MockEigrpInterface(*eigrpInstance, interface2);
    getInterfaceList()[1] = eigrpInterface1;
    getInterfaceList()[2] = eigrpInterface2;
    addNeighbor(ByteString("\x0a\x00\x00\x01", 4), EigrpConfigs::CommunicationMode::MULTICAST, eigrpInterface1);
    addNeighbor(ByteString("\x0a\x00\x00\x02", 4), EigrpConfigs::CommunicationMode::MULTICAST, eigrpInterface2);

    EXPECT_CALL(*eigrpInterface1, sendUpdateToNeighbor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_)).Times(1);
    EXPECT_CALL(*eigrpInterface2, sendUpdateToNeighbor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_)).Times(1);

    RoutingTable::Eigrp route;
    route.network = ByteString("\xC0\xA8\x01\x00", 4);
    route.mask = 24;
    route.nextHop = ByteString("\xc0\xa8\x01\x02", 4);

    // Act
    eigrpInstance->notifyRoutingChange({&route}, false);

    eigrpInstance->shutdown();
    delete interface1;
    delete interface2;
}

// Test: Validate route advertisements for multiple interfaces
// Ensures that route advertisements are consistent across all interfaces.
TEST_F(EigrpTest, ValidateRouteAdvertisementsMultipleInterfaces) 
{
    // Arrange
    MockInterface* interface1 = new MockInterface();
    MockInterface* interface2 = new MockInterface();
    interface1->blockEnqueues();
    interface2->blockEnqueues();

    // Add mock interfaces
    auto eigrpInterface1 = new MockEigrpInterface(*eigrpInstance, interface1);
    setIPv4(ByteString("\xc0\xa8\x02\x02", 4), 24, interface1);
    addNeighbor(ByteString("\x0a\x00\x00\x01", 4), EigrpConfigs::CommunicationMode::MULTICAST, eigrpInterface1);
    auto eigrpInterface2 = new MockEigrpInterface(*eigrpInstance, interface2);
    setIPv4(ByteString("\xc0\xa8\x03\x03", 4), 24, interface2);
    addNeighbor(ByteString("\x0a\x00\x00\x02", 4), EigrpConfigs::CommunicationMode::MULTICAST, eigrpInterface2);
    getInterfaceList()[1] = eigrpInterface1;
    getInterfaceList()[2] = eigrpInterface2;

    EXPECT_CALL(*eigrpInterface1, sendUpdateToNeighbor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_)).Times(1);
    EXPECT_CALL(*eigrpInterface2, sendUpdateToNeighbor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_)).Times(1);

    // Simulate updates from both interfaces
    eigrpInstance->updateRoutingTableForConnected();

    // Assert
    ASSERT_EQ(RoutingTable::getInstance().getAllEigrpRoutes(AddressFamily::IPv4, asNumber).size(), 3);

    eigrpInstance->shutdown();
    delete interface1;
    delete interface2;
}

// Test: Simulate neighbor failure on one interface
// Ensures that neighbor failure on one interface does not affect others.
TEST_F(EigrpTest, SimulateNeighborFailure) 
{
    // Arrange
    MockInterface* interface1 = new MockInterface();
    MockInterface* interface2 = new MockInterface();
    interface1->blockEnqueues();
    interface2->blockEnqueues();

    auto eigrpInterface1 = new MockEigrpInterface(*eigrpInstance, interface1);
    auto eigrpInterface2 = new MockEigrpInterface(*eigrpInstance, interface2);
    getInterfaceList()[1] = eigrpInterface1;
    getInterfaceList()[2] = eigrpInterface2;

    // Act
    eigrpInstance->shutdown(); // Simulate failure on one interface

    // Assert
    ASSERT_TRUE(getInterfaceList().empty());

    delete interface1;
    delete interface2;
}

#pragma endregion
#pragma region EdgeCasesWithMultipleInterfaces

// Test: Handle route flapping across interfaces
// Ensures that flapping routes are handled gracefully.
TEST_F(EigrpTest, HandleRouteFlapping) 
{
    // Arrange
    auto interface1 = new MockInterface();
    auto interface2 = new MockInterface();
    interface1->blockEnqueues();
    interface2->blockEnqueues();

    auto eigrpInterface1 = new MockEigrpInterface(*eigrpInstance, interface1);
    auto eigrpInterface2 = new MockEigrpInterface(*eigrpInstance, interface2);
    getInterfaceList()[1] = eigrpInterface1;
    getInterfaceList()[2] = eigrpInterface2;
    addNeighbor(ByteString("\x0a\x00\x00\x01", 4), EigrpConfigs::CommunicationMode::MULTICAST, eigrpInterface1);
    addNeighbor(ByteString("\x0a\x00\x00\x02", 4), EigrpConfigs::CommunicationMode::MULTICAST, eigrpInterface2);

    RoutingTable::Eigrp route;
    route.network = ByteString("\xC0\xA8\x05\x00", 4);
    route.mask = 24;
    route.nextHop = ByteString("\x00\x00\x00\x00", 4);
    RoutingTable::getInstance().addEigrp(&route, AddressFamily::IPv4, asNumber);

    EXPECT_CALL(*eigrpInterface1, sendUpdateToNeighbor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_)).Times(2);
    EXPECT_CALL(*eigrpInterface2, sendUpdateToNeighbor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_)).Times(2);

    // Act
    eigrpInstance->notifyRoutingChange({&route}, false);
    eigrpInstance->notifyRoutingChange({&route}, true);

    eigrpInstance->shutdown();
    delete interface1;
    delete interface2;
}

#pragma endregion
#pragma region PreformanceWithMultipleInstances

// Test: Notify route changes on multiple interfaces simultaneously
// Ensures that simultaneous route changes are correctly notified across interfaces.
TEST_F(EigrpTest, NotifyRouteChangesSimultaneously) 
{
    // Arrange
    auto interface1 = new MockInterface();
    auto interface2 = new MockInterface();
    interface1->blockEnqueues();
    interface2->blockEnqueues();

    auto eigrpInterface1 = new MockEigrpInterface(*eigrpInstance, interface1);
    auto eigrpInterface2 = new MockEigrpInterface(*eigrpInstance, interface2);
    getInterfaceList()[1] = eigrpInterface1;
    getInterfaceList()[2] = eigrpInterface2;
    addNeighbor(ByteString("\x0a\x00\x00\x01", 4), EigrpConfigs::CommunicationMode::MULTICAST, eigrpInterface1);
    addNeighbor(ByteString("\x0a\x00\x00\x02", 4), EigrpConfigs::CommunicationMode::MULTICAST, eigrpInterface2);

    RoutingTable::Eigrp route1;
    route1.network = ByteString("\xC0\xA8\x07\x00", 4);
    route1.mask = 24;
    route1.nextHop = ByteString("\x00\x00\x00\x01", 4);
    RoutingTable::Eigrp route2;
    route2.network = ByteString("\xC0\xA8\x08\x00", 4);
    route2.mask = 24;
    route2.nextHop = ByteString("\x00\x00\x00\x02", 4);

    EXPECT_CALL(*eigrpInterface1, sendUpdateToNeighbor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_)).Times(1);
    EXPECT_CALL(*eigrpInterface2, sendUpdateToNeighbor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_)).Times(1);

    // Act
    eigrpInstance->notifyRoutingChange({&route1, &route2}, false);

    eigrpInstance->shutdown();
    delete interface1;
    delete interface2;
}

// Test: Add interfaces during operation and validate routes
// Ensures that routes are correctly updated as interfaces are added dynamically.
TEST_F(EigrpTest, AddInterfacesDuringOperation) 
{
    // Arrange
    auto interface1 = new MockInterface();
    auto interface2 = new MockInterface();
    interface1->blockEnqueues();
    interface2->blockEnqueues();

    setIPv4(ByteString("\xc0\xa8\x02\x02", 4), 24, interface1);
    setIPv4(ByteString("\xc0\xa8\x03\x02", 4), 24, interface2);

    auto eigrpInterface1 = new MockEigrpInterface(*eigrpInstance, interface1);
    auto eigrpInterface2 = new MockEigrpInterface(*eigrpInstance, interface2);
    getInterfaceList()[1] = eigrpInterface1;
    getInterfaceList()[2] = eigrpInterface2;

    RoutingTable::Eigrp route;
    route.network = ByteString("\xC0\xA8\x09\x00", 4);
    route.mask = 24;
    route.nextHop = ByteString("\x00\x00\x00\x00", 4);
    RoutingTable::getInstance().addEigrp(&route, AddressFamily::IPv4, asNumber);
    EigrpConfigs::Network network{ByteString("\xC0\xA8\x00\x00", 4), ByteString("\x00\x00\xFF\xFF", 4)};
    eigrpInstance->addNetwork(network);

    // Act
    getInterfaceList()[1] = eigrpInterface1;
    getInterfaceList()[2] = eigrpInterface2;

    eigrpInstance->updateRoutingTableForConnected();

    // Assert
    auto idk = RoutingTable::getInstance().getAllEigrpRoutes(AddressFamily::IPv4, asNumber);
    ASSERT_EQ(RoutingTable::getInstance().getAllEigrpRoutes(AddressFamily::IPv4, asNumber).size(), 4);

    eigrpInstance->shutdown();
    delete interface1;
    delete interface2;
}

// Test: Interface without IP address
// Ensures that EIGRP does not include an interface without an IP address in its operations.
TEST_F(EigrpTest, InterfaceWithoutIPAddress) 
{
    // Arrange
    setIPv4(ByteString(), 24);

    // Act
    EigrpConfigs::Network network{ByteString("\xC0\xA8\x00\x00", 4), ByteString("\x00\x00\xFF\xFF", 4)};
    eigrpInstance->addNetwork(network);

    // Assert
    ASSERT_TRUE(getInterfaceList().empty());
}

// Test: Handle dropped Hello packets
// Ensures that adjacency is re-established after Hello packets are dropped.
TEST_F(EigrpTest, HandleDroppedHelloPackets) 
{

    addNeighbor(ByteString("\xc0\xa8\x02\x01"), EigrpConfigs::CommunicationMode::MULTICAST);
    EigrpConfigs::Network network{ByteString("\xC0\xA8\x00\x00", 4), ByteString("\x00\x00\xFF\xFF", 4)};
    eigrpInstance->addNetwork(network);

    EXPECT_CALL(*mockEigrpInterface, sendHelloPacket(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .Times(2);

    // Act
    eigrpInstance->initializeEigrp();
    eigrpInstance->shutdown(); // Simulate Hello packet drop
}

// Test: Handle Interface Flapping
TEST_F(EigrpTest, HandleInterfaceFlapping)
{
    auto interface1 = new MockInterface();
    auto interface2 = new MockInterface();
    interface1->blockEnqueues();
    interface2->enableShutdown();

    setIPv4(ByteString("\xc0\xa8\x02\x01", 4), 24, interface1);
    setIPv4(ByteString("\xc0\xa8\x03\x01", 4), 24, interface2);

    interfaceList[InterfaceType::GIGABIT_ETHERNET][1] = interface1;
    interfaceList[InterfaceType::GIGABIT_ETHERNET][2] = interface2;
    interface1->configs.id = 1;
    interface2->configs.id = 2;
    EigrpConfigs::Network network{ByteString("\xC0\xA8\x00\x00", 4), ByteString("\x00\x00\xFF\xFF", 4)};
    eigrpInstance->addNetwork(network);

    // Simulate interface going down
    interface1->Shutdown(true);
    interface2->Shutdown(true);
    eigrpInstance->updateInterfaceList();
    ASSERT_EQ(eigrpInstance->eigrpInterfaceList.size(), 1);

    // Bring interface back up
    EXPECT_CALL(*interface1, startThreads).Times(1);
    interface1->Shutdown(false);
    eigrpInstance->updateInterfaceList();
    ASSERT_EQ(eigrpInstance->eigrpInterfaceList.size(), 2);

    eigrpInstance->shutdown();
    delete interface1;
    delete interface2;
}

// Test: Handles the neighbors timeout from being inactive
TEST_F(EigrpTest, HandleNeighborTimeout)
{
    ByteString neighborIp("\xC0\xA8\x02\x01", 4);
    addNeighbor(neighborIp, EigrpConfigs::CommunicationMode::MULTICAST);

    // Simulate neighbor failing to respond
    mockEigrpInterface->handleHoldTimeExpire(getNeighbors(neighborIp).value(), neighborIp);
    ASSERT_FALSE(getNeighbors(neighborIp).has_value());
}

// Test: Handles StuckInActive when router times out during a query
TEST_F(EigrpTest, HandleStuckInActive)
{
    RoutingTable::Eigrp route1;
    route1.network = ByteString("\xc0\xa8\x02\x00", 4);
    route1.mask = 24;
    route1.nextHop = ByteString("\xc0\xa8\x01\x02", 4);
    RoutingTable::Eigrp route2;
    route2.network = ByteString("\xc0\xa8\x03\x00", 4);
    route2.mask = 24;
    route2.nextHop = ByteString("\xc0\xa8\x01\x02", 4);
    addNeighbor(ByteString("\xc0\xa8\x01\x02", 4), EigrpConfigs::CommunicationMode::MULTICAST);
    RoutingTable::getInstance().addEigrp(&route1, AddressFamily::IPv4, 1);

    mockEigrpInterface->handleStuckInActive();

    // Simulate lack of response
    //handleStuckInActive(routeNetwork);

    // Ensure that the route is removed
    //ASSERT_FALSE(eigrpInstance->getTopologyTable()->getRoute(routeNetwork).has_value());
}




// Test: creates an outstanding query when the SIA timer expires
TEST_F(EigrpTest, SIATimer_ReSendsQuery)
{
    // Arrange: Create a dummy route to compute its query key.
    auto* testRoute = new RoutingTable::Eigrp();
    testRoute->network = ByteString("\xc0\xa8\x02\x00", 4); // 192.168.2.0
    testRoute->mask = 24;
    // Build a query key as "<network>/<mask>"
    ByteString queryKey = testRoute->network + "/" + std::to_string(testRoute->mask);
    
    // Set up an outstanding active query with one pending neighbor.
    EigrpConfigs::ActiveRoute activeRoute;
    ByteString pendingNeighbor("\xc0\xa8\x01\x02", 4);
    activeRoute.pendingReplies.insert(pendingNeighbor);
    activeRoute.originNeighbor = pendingNeighbor;
    eigrpInstance->outstandingReplies[queryKey] = activeRoute;

    // Also, add a neighbor with IP 192.168.1.2 on the mock EIGRP interface.
    addNeighbor(pendingNeighbor, EigrpConfigs::CommunicationMode::MULTICAST);

    // Set lower SIA time
    eigrpInstance->getConfigs()->stuckInActiveTime = 5;

    // Expect that sendQueryToNeighbor is called with the given number and route.
    EXPECT_CALL(*mockEigrpInterface, sendQueryToNeighbor(::testing::_, pendingNeighbor, testRoute))
        .Times(1);

    // Act: Invoke the SIATimer callback directly.
    mockEigrpInterface->handleSIATimeout(testRoute, queryKey);

    // Clean up the outstanding query and allocated route.
    eigrpInstance->outstandingReplies.erase(queryKey);
    delete testRoute;
}

// Test: creates a route whose query key is not present in the outstandingReplies.
TEST_F(EigrpTest, SIATimer_NoOutstandingQuery)
{
    // Arrange: Create a dummy route.
    auto* testRoute = new RoutingTable::Eigrp();
    testRoute->network = ByteString("\xc0\xa8\x03\x03", 4);
    testRoute->mask = 24;
    ByteString queryKey = testRoute->network + "/" + std::to_string(testRoute->mask);

    // Set lower SIA time
    eigrpInstance->getConfigs()->stuckInActiveTime = 2;

    // Ensure that the outstandingReplies does not contain this key
    eigrpInstance->outstandingReplies.erase(queryKey);

    // Expect that sendQueryToNeighbor does not contain this key.
    EXPECT_CALL(*mockEigrpInterface, sendQueryToNeighbor(::testing::_, ::testing::_, ::testing::_)).Times(0);
    
    // Clean up
    delete testRoute;
}
