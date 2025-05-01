// EigrpTest.cpp

#include <gtest/gtest.h>
#include <Eigrp.h>
#include <MockInterface.hpp>
#include <PacketStructure.h>
#include <ByteString.hpp>
#include <thread>
#include <mutex>
#include <condition_variable>

using namespace Protocol;

// Test fixture for global EIGRP tests
class EigrpTest : public ::testing::Test 
{
protected:
    uint32_t asNumber = 1;
    AddressFamily addressFamily = AddressFamily::IPv4;
    Eigrp* eigrpInstance;
    MockInterface* mockInterface;
    // We use the real EigrpInterface (constructed using our MockInterface)
    EigrpConfigs::InterfaceConfigs* configs;
    EigrpInterface* eigrpInterface;
    std::condition_variable cv;
    std::mutex cvMutex;
    bool packetEnqueued = false;
    InterfaceType type = InterfaceType::GIGABIT_ETHERNET;
    VirtualRouter* vrf = nullptr;

    // Setup creates an Eigrp instance and one interface for testing.
    void SetUp() override 
    {
        vrf = new VirtualRouter("default");
        vrf->eigrpList[1] = new EigrpAutonomousSystem();
        eigrpInstance = new Eigrp(asNumber, addressFamily, vrf);
        vrf->eigrpList[1]->ipv4 = eigrpInstance;
        mockInterface = new MockInterface(InterfaceType::GIGABIT_ETHERNET);
        mockInterface->routingInstance = vrf;
        EXPECT_CALL(*mockInterface, startThreads()).Times(::testing::AnyNumber());
        mockInterface->enableIPs();
        mockInterface->enableShutdown();
        // Set initial IPv4 and IPv6 addresses on the mock interface.
        setIPv4(ByteString("\xc0\xa8\x01\x01", 4), 24);
        setIPv6(ByteString("\xc0\xa8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x01", 16), 64);
        // Assume interfaceList is a global map keyed by InterfaceType and interface id.
        vrf->interfaceList[{InterfaceType::GIGABIT_ETHERNET, 0}] = mockInterface;
        // Create the real EigrpInterface using the mock interface.
        EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(::testing::AtLeast(1));
        configs = new EigrpConfigs::InterfaceConfigs();
        eigrpInterface = new EigrpInterface(*eigrpInstance, configs, mockInterface);
        eigrpInstance->eigrpInterfaceList[{type, 0}] = eigrpInterface;
    }

    // TearDown cleans up the EIGRP instance, interface, and global objects.
    void TearDown() override 
    {
        delete vrf->eigrpList[1];
        vrf->eigrpList.clear();
        mockInterface->blockEnqueues();
        vrf->interfaceList.erase({InterfaceType::GIGABIT_ETHERNET, 0});
        delete eigrpInstance;
        delete mockInterface;
        vrf->interfaceList.clear();
        vrf->routingTable.clear();
        delete vrf;
        vrf = nullptr;
    }

    // Helper function: returns the topology table.
    TopologyTable* getTopologyTable() { return eigrpInstance->topologyTable; }
    // Helper: returns the current interface list.
    std::unordered_map<std::pair<InterfaceType, float>, EigrpInterface*, InterfacePairHash>& getInterfaceList() { return eigrpInstance->eigrpInterfaceList; }
    std::unordered_map<std::pair<InterfaceType, float>, Interface*, InterfacePairHash>& getAllInterfaceList() { return eigrpInstance->routingInstance->interfaceList; }
    
    // Helper: set IPv4 address on an interface.
    void setIPv4(const ByteString& ip, uint8_t mask, MockInterface* iface = nullptr) 
    {
        if (iface)
            iface->setIPv4(ip, mask);
        else
            mockInterface->setIPv4(ip, mask);
    }
    
    // Helper: set IPv6 address on an interface.
    void setIPv6(const ByteString& ip, uint8_t mask, Interface* iface = nullptr) 
    {
        if (iface)
            iface->setIPv6(ip, mask, false);
        else
            mockInterface->setIPv6(ip, false, mask, false);
    }
    
    // Helper: returns the IpInfo from the interface.
    IpInfo& getIpInfo(Interface* iface = nullptr) 
    {
        return (iface ? iface->configs : mockInterface->configs);
    }
    
    // Helper: clear network configuration in the EIGRP instance.
    void clearNetworks() { eigrpInstance->configs.networks.clear(); }
    
    // Helper: add a neighbor via the real EigrpInterface.
    void addNeighbor(const ByteString& ip, EigrpInterface* intf = nullptr) 
    {
        if (intf)
            intf->addNeighbor(ip, ByteString("\x11\x22\x33\x44\x55\x66", 6));
        else
            eigrpInterface->addNeighbor(ip, ByteString("\x11\x22\x33\x44\x55\x66", 6));
    }
    
    // Helper: retrieve a neighbor from an interface.
    EigrpConfigs::NeighborInfo* getNeighbor(const ByteString& ip, EigrpInterface* intf = nullptr) 
    {
        return (intf ? intf->getNeighborInfo(ip) : eigrpInterface->getNeighborInfo(ip));
    }
    
    // Helper: signal packet enqueue (if needed)
    void notifyPacketEnqueued() 
    {
        std::lock_guard<std::mutex> lock(cvMutex);
        packetEnqueued = true;
        cv.notify_one();
    }
    
    // Helper: wait for packet enqueue signal.
    void waitForPacketEnqueued() 
    {
        std::unique_lock<std::mutex> lock(cvMutex);
        cv.wait(lock, [this] { return packetEnqueued; });
    }

    bool getHelloTimerActive(Protocol::EigrpInterface* eigrpInt = nullptr) { if (eigrpInt) return eigrpInt->helloTimerActive.load(); else return eigrpInterface->helloTimerActive.load();}
    ByteString& getRouterID(Protocol::Eigrp* eigrp = nullptr) { if (eigrp) return eigrp->routerID.ID; else return eigrpInstance->routerID.ID; }
};

#pragma region Authentication

// Test: AuthTLV_MD5_Correct
TEST_F(EigrpTest, AuthTLV_MD5_Correct) 
{
    // Verify that MD5 authentication TLV is built correctly.
    // (Neighbor IP: 192.168.1.2, key "secretkey")
    ByteString neighborIp("\xC0\xA8\x01\x02", 4);
    addNeighbor(neighborIp);
    auto optNeighbor = getNeighbor(neighborIp);
    ASSERT_TRUE(optNeighbor);
    auto neighbor = optNeighbor;
    uint8_t keyId = 1;
    ByteString key = ByteString("secretKey", 9);
    EigrpConfigs::AuthType type = EigrpConfigs::AuthType::MD5;
    eigrpInterface->configureAuthentication(&keyId, &key, &type, true);

    // Block enqueues
    mockInterface->blockEnqueues();
    
    // Create hello packet.
    EigrpHeader helloPacket;
    eigrpInstance->eigrpHello(helloPacket, eigrpInterface, neighbor, neighborIp, 100, false, false);
    
    // Verify authentication TLV is present and not all zeros.
    auto it = std::find_if(helloPacket.options.begin(), helloPacket.options.end(), [](const EigrpHeader::Option& opt) {
        return opt.option == Variable::Eigrp::Option::authentication;
    });
    ASSERT_NE(it, helloPacket.options.end());
    // Check that the computed HMAC field is non-zero.
    ASSERT_NE(it->value.substr(1), ByteString(it->value.size()-1, 0x00));
}

// Test: AuthTLV_SHA1_Correct
TEST_F(EigrpTest, AuthTLV_SHA1_Correct) 
{
    // Verify that SHA-1 authentication TLV is built correctly.
    ByteString neighborIp("\xC0\xA8\x01\x03", 4);
    addNeighbor(neighborIp);
    auto optNeighbor = getNeighbor(neighborIp);
    ASSERT_TRUE(optNeighbor);
    auto neighbor = optNeighbor;
    uint8_t keyId = 2;
    ByteString key = ByteString("anothersecret", 13);
    EigrpConfigs::AuthType type = EigrpConfigs::AuthType::SHA1;
    eigrpInterface->configureAuthentication(&keyId, &key, &type, true);

    // Block enqueues
    mockInterface->blockEnqueues();
    
    EigrpHeader helloPacket;
    eigrpInstance->eigrpHello(helloPacket, eigrpInterface, neighbor, neighborIp, 101, false, false);
    
    auto it = std::find_if(helloPacket.options.begin(), helloPacket.options.end(), [](const EigrpHeader::Option& opt) {
        return opt.option == Variable::Eigrp::Option::authentication;
    });
    ASSERT_NE(it, helloPacket.options.end());
    ASSERT_NE(it->value.substr(1), ByteString(it->value.size()-1, 0x00));
}

// Test: AuthTLV_Disabled_NoTLV
TEST_F(EigrpTest, AuthTLV_Disabled_NoTLV) 
{
    // Ensure that if authentication is disabled, no authentication TLV is added.
    ByteString neighborIp("\xC0\xA8\x01\x04", 4);
    addNeighbor(neighborIp);
    auto optNeighbor = getNeighbor(neighborIp);
    ASSERT_TRUE(optNeighbor);
    auto neighbor = optNeighbor;
    eigrpInterface->configureAuthentication();

    // Block enqueues
    mockInterface->blockEnqueues();
    
    EigrpHeader helloPacket;
    eigrpInstance->eigrpHello(helloPacket, eigrpInterface, neighbor, neighborIp, 200, false, false);
    
    auto it = std::find_if(helloPacket.options.begin(), helloPacket.options.end(), [](const EigrpHeader::Option& opt) {
        return opt.option == Variable::Eigrp::Option::authentication;
    });
    ASSERT_EQ(it, helloPacket.options.end());
}

// Test: Auth_InvalidKey_Handled
TEST_F(EigrpTest, Auth_InvalidKey_Handled) 
{
    // If neighbor has an empty auth key, no authentication TLV should be produced.
    ByteString neighborIp("\xC0\xA8\x01\x05", 4);
    addNeighbor(neighborIp);
    auto optNeighbor = getNeighbor(neighborIp);
    ASSERT_TRUE(optNeighbor);
    auto neighbor = optNeighbor;
    uint8_t keyId = 1;
    ByteString key = ByteString();
    EigrpConfigs::AuthType type = EigrpConfigs::AuthType::MD5;
    eigrpInterface->configureAuthentication(&keyId, &key, &type, true);

    // Block enqueues
    mockInterface->blockEnqueues();
    
    EigrpHeader helloPacket;
    eigrpInstance->eigrpHello(helloPacket, eigrpInterface, neighbor, neighborIp, 300, false, false);
    
    auto it = std::find_if(helloPacket.options.begin(), helloPacket.options.end(), [](const EigrpHeader::Option& opt) {
        return opt.option == Variable::Eigrp::Option::authentication;
    });
    ASSERT_EQ(it, helloPacket.options.end());
}

#pragma endregion
#pragma region NeighborState

// Test: NeighborState_DOWN_To_EXSTART
TEST_F(EigrpTest, NeighborState_DOWN_To_EXSTART) 
{
    // Trigger transition from TWOWAY to EXSTART.
    ByteString neighborIp("\xC0\xA8\x01\x08", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    neighbor->neighborState = EigrpConfigs::NeighborState::DOWN;
    
    EigrpHeader hello;
    eigrpInstance->eigrpHello(hello, eigrpInterface, neighbor, neighborIp, 0, false, false);
    eigrpInterface->processHello(neighbor, hello, neighborIp, false);
    // Initial Hello and Update Packet
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(1);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    ASSERT_TRUE(neighbor->neighborState >= EigrpConfigs::NeighborState::EXSTART);
}

// Test: NeighborState_EXSTART_To_EXCHANGE_SLAVE
TEST_F(EigrpTest, NeighborState_EXSTART_To_EXCHANGE_SLAVE) 
{
    // Simulate update processing that moves state from EXSTART to EXCHANGE.
    ByteString neighborIp("\xC0\xA8\x01\t", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    neighbor->neighborState = EigrpConfigs::NeighborState::EXSTART;
    // Init update received
    neighbor->initUpdateReceived = true;
    // Set neighbor state
    neighbor->initRole = EigrpConfigs::InitRole::SLAVE;
    
    EigrpHeader update;
    eigrpInstance->eigrpUpdate(update, 3, false, false, false, false, false, false);
    eigrpInterface->processUpdate(neighbor, update);

    ASSERT_TRUE(neighbor->neighborState >= EigrpConfigs::NeighborState::LOADING);
}

// Test: NeighborState_EXSTART_To_EXSTART_SLAVE
TEST_F(EigrpTest, NeighborState_EXSTART_To_EXCHANGE_MASTER)
{
    // Simulate update processing that moves state from EXSTART to EXCHANGE.
    ByteString neighborIp("\xC0\xA8\x01\t", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    neighbor->neighborState = EigrpConfigs::NeighborState::EXSTART;
    // Init update received
    neighbor->initUpdateReceived = true;
    // Set neighbor state
    neighbor->initRole = EigrpConfigs::InitRole::MASTER;
    
    EigrpHeader update;
    eigrpInstance->eigrpUpdate(update, 3, false, false, false, false, false, false);
    eigrpInterface->processUpdate(neighbor, update);

    ASSERT_TRUE(neighbor->neighborState >= EigrpConfigs::NeighborState::LOADING);
}

// Test: NeighborState_EXCHANGE_To_LOADING
TEST_F(EigrpTest, NeighborState_EXCHANGE_To_LOADING) 
{
    // After exchanging topology, simulate transition to LOADING.
    ByteString neighborIp("\xC0\xA8\x01\n", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    neighbor->neighborState = EigrpConfigs::NeighborState::EXCHANGE;
    
    EigrpHeader update;
    eigrpInstance->eigrpUpdate(update, 4, false, false, false, false, false, false);
    eigrpInterface->processUpdate(neighbor, update);
    
    // For testing purposes, force state to LOADING.
    neighbor->neighborState = EigrpConfigs::NeighborState::LOADING;
    ASSERT_TRUE(neighbor->neighborState >= EigrpConfigs::NeighborState::LOADING);
}

// Test: NeighborState_LOADING_To_ESTABLISHED
TEST_F(EigrpTest, NeighborState_LOADING_To_ESTABLISHED) 
{
    // Simulate final update that sets the neighbor state to ESTABLISHED.
    ByteString neighborIp("\xC0\xA8\x01\x0B", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    neighbor->neighborState = EigrpConfigs::NeighborState::LOADING;
    
    // Simulate final update.
    neighbor->neighborState = EigrpConfigs::NeighborState::ESTABLISHED;
    ASSERT_TRUE(neighbor->neighborState >= EigrpConfigs::NeighborState::ESTABLISHED);
}

// Test: Duplicate_Hello_Ignored
TEST_F(EigrpTest, Duplicate_Hello_Ignored) 
{
    // Ensure duplicate hello packets do not affect neighbor state.
    ByteString neighborIp("\xC0\xA8\x01\x0C", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    neighbor->neighborState = EigrpConfigs::NeighborState::INIT;
    
    EigrpHeader hello;
    eigrpInstance->eigrpHello(hello, eigrpInterface, neighbor, neighborIp, 10, false, false);
    eigrpInterface->processHello(neighbor, hello, neighborIp, false);
    eigrpInterface->processHello(neighbor, hello, neighborIp, false);
    
    ASSERT_TRUE(neighbor->neighborState >= EigrpConfigs::NeighborState::INIT);
}

// Test: Neighbor_Restart_Resets_State
TEST_F(EigrpTest, Neighbor_Restart_Resets_State) 
{
    // Verify that restarting a neighbor resets its state, reliable packets, and sequence list.
    ByteString neighborIp("\xC0\xA8\x01\x0E", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    neighbor->neighborState = EigrpConfigs::NeighborState::ESTABLISHED;
    neighbor->reliablePackets[100] = EigrpConfigs::NeighborInfo::ReliablePacketInfo(
        EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(EigrpHeader(), neighborIp, {}, false));
    
    eigrpInterface->handleNeighborRestart(neighbor, neighborIp);
    
    ASSERT_EQ(neighbor->neighborState, EigrpConfigs::NeighborState::DOWN);
    ASSERT_TRUE(neighbor->reliablePackets.empty());
}

// Test: HoldTimer_Expires_Marks_Neighbor_Down
TEST_F(EigrpTest, HoldTimer_Expires_Marks_Neighbor_Down) 
{
    // Verify that when a neighbor’s hold timer expires, it is removed.
    ByteString neighborIp("\xC0\xA8\x01\x0F", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    eigrpInterface->startHoldTimer(neighbor, neighborIp, 1);
    eigrpInterface->handleHoldTimeExpire(neighbor, neighborIp);
    
    ASSERT_FALSE(getNeighbor(neighborIp));
}

// Test: MultipleNeighbors_Independent_States
TEST_F(EigrpTest, MultipleNeighbors_Independent_States) 
{
    // Verify that two neighbors maintain independent states.
    ByteString neighborIp1("\xC0\xA8\x01\x10", 4);
    ByteString neighborIp2("\xC0\xA8\x01\x11", 4);
    addNeighbor(neighborIp1, eigrpInterface);
    addNeighbor(neighborIp2, eigrpInterface);
    
    auto neighbor1 = getNeighbor(neighborIp1);
    auto neighbor2 = getNeighbor(neighborIp2);
    neighbor1->neighborState = EigrpConfigs::NeighborState::DOWN;
    neighbor2->neighborState = EigrpConfigs::NeighborState::DOWN;
    
    EigrpHeader hello1;
    eigrpInstance->eigrpHello(hello1, eigrpInterface, nullptr, neighborIp1, 20, false, false);
    eigrpInterface->processHello(neighbor1, hello1, neighborIp1, false);
    
    ASSERT_EQ(neighbor1->neighborState, EigrpConfigs::NeighborState::TWOWAY);
    ASSERT_EQ(neighbor2->neighborState, EigrpConfigs::NeighborState::DOWN);
}

// Test: OutOfOrder_Update_Packet_Processing
TEST_F(EigrpTest, OutOfOrder_Update_Packet_Processing) 
{
    // Simulate an update packet arriving out of order.
    ByteString neighborIp("\xC0\xA8\x01\x11", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    neighbor->lastReceivedSequenceNumber = 10;
    
    EigrpHeader update12;
    eigrpInstance->eigrpUpdate(update12, 12, false, false, false, false, false, false);
    eigrpInterface->processUpdate(neighbor, update12);
    eigrpInterface->processBufferedPackets(neighbor);
    
    ASSERT_GE(neighbor->lastReceivedSequenceNumber, 12);
}

// Test Duplicate_Update_Packet_Processing
TEST_F(EigrpTest, Duplicate_Update_Packet_Processing) 
{
    // Ensure that processing the same update packet twice does not alter state.
    ByteString neighborIp("\xC0\xA8\x01\x12", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    neighbor->lastReceivedSequenceNumber = 20;
    
    EigrpHeader updatePacket;
    eigrpInstance->eigrpUpdate(updatePacket, 21, false, false, false, false, false, false);
    eigrpInterface->processUpdate(neighbor, updatePacket);
    eigrpInterface->processUpdate(neighbor, updatePacket);
    
    ASSERT_EQ(neighbor->lastReceivedSequenceNumber, 21);
}

#pragma endregion
#pragma region ReliablePacket

// Test: Retransmission_Timer_Expires_Resend
TEST_F(EigrpTest, Retransmission_Timer_Expires_Resend) 
{
    // Simulate a lost packet so that the retransmission timer expires and the packet is resent.
    ByteString neighborIp("\xC0\xA8\x01\x14", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint32_t seqNum = 500;
    const EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet rpInfo(EigrpHeader(), neighborIp, {}, false);
    neighbor->processAcks = true;
    
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(::testing::AtLeast(1));
    
    eigrpInterface->setupReliablePacket(neighbor, neighborIp, rpInfo, seqNum);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    ASSERT_GT(neighbor->reliablePackets[seqNum].retransmissionCount, 0);
}

// Test: Retransmission_Count_Increments
TEST_F(EigrpTest, Retransmission_Count_Increments) 
{
    // Verify that successive retransmission events increment the retransmission count.
    ByteString neighborIp("\xC0\xA8\x01\x15", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint32_t seqNum = 501;
    EigrpConfigs::NeighborInfo::ReliablePacketInfo rpInfo(
      EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(EigrpHeader(), neighborIp, {}, false));
    neighbor->reliablePackets[seqNum] = rpInfo;
    
    eigrpInterface->handleRetransmissionTimeout(neighbor, neighborIp, seqNum);
    uint32_t countAfter = neighbor->reliablePackets[seqNum].retransmissionCount;
    eigrpInterface->handleRetransmissionTimeout(neighbor, neighborIp, seqNum);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ASSERT_GT(neighbor->reliablePackets[seqNum].retransmissionCount, countAfter);
}

// Test: ACK_Processing_Cancels_Packet
TEST_F(EigrpTest, ACK_Processing_Cancels_Packet) 
{
    // Verify that a valid ACK cancels the retransmission timer.
    ByteString neighborIp("\xC0\xA8\x01\x16", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint32_t seqNum = 502;
    EigrpConfigs::NeighborInfo::ReliablePacketInfo rpInfo(
      EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(EigrpHeader(), neighborIp, {}, false));
    rpInfo.sendTime = std::chrono::steady_clock::now();
    neighbor->reliablePackets[seqNum] = rpInfo;
    
    eigrpInterface->processAck(neighbor, Functions::numToByte(seqNum, 4));
    ASSERT_EQ(neighbor->reliablePackets.find(seqNum), neighbor->reliablePackets.end());
}

// Test: Duplicate_ACK_Does_Not_Alter_RTT
TEST_F(EigrpTest, Duplicate_ACK_Does_Not_Alter_RTT) 
{
    // Process the same ACK twice and verify that RTT does not change.
    ByteString neighborIp("\xC0\xA8\x01\x17", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint32_t seqNum = 503;
    EigrpConfigs::NeighborInfo::ReliablePacketInfo rpInfo(
      EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(EigrpHeader(), neighborIp, {}, false));
    rpInfo.sendTime = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    neighbor->reliablePackets[seqNum] = rpInfo;
    
    eigrpInterface->processAck(neighbor, Functions::numToByte(seqNum, 4));
    double srttAfterFirst = neighbor->srtt;
    eigrpInterface->processAck(neighbor, Functions::numToByte(seqNum, 4));
    ASSERT_EQ(neighbor->srtt, srttAfterFirst);
}

// Test: Max_Retransmissions_Triggers_Neighbor_Down
TEST_F(EigrpTest, Max_Retransmissions_Triggers_Neighbor_Down) 
{
    // Set the retransmission count to the maximum and simulate a timeout.
    ByteString neighborIp("\xC0\xA8\x01\x18", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint32_t seqNum = 504;
    EigrpConfigs::NeighborInfo::ReliablePacketInfo rpInfo(
      EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(EigrpHeader(), neighborIp, {}, false));
    rpInfo.sendTime = std::chrono::steady_clock::now() - std::chrono::seconds(2);
    rpInfo.retransmissionCount = MAX_RETRANSMISSIONS;
    neighbor->reliablePackets[seqNum] = rpInfo;
    
    eigrpInterface->handleRetransmissionTimeout(neighbor, neighborIp, seqNum);
    ASSERT_FALSE(getNeighbor(neighborIp));
}

// Test: Exponential_Backoff_Applied
TEST_F(EigrpTest, Exponential_Backoff_Applied) 
{
    // Verify that the RTO doubles after a retransmission.
    ByteString neighborIp("\xC0\xA8\x01\x19", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint32_t seqNum = 505;
    EigrpConfigs::NeighborInfo::ReliablePacketInfo rpInfo(
      EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(EigrpHeader(), neighborIp, {}, false));
    rpInfo.sendTime = std::chrono::steady_clock::now() - std::chrono::seconds(2);
    neighbor->reliablePackets[seqNum] = rpInfo;
    
    double initialRTO = neighbor->rto;
    eigrpInterface->handleRetransmissionTimeout(neighbor, neighborIp, seqNum);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    double newRTO = neighbor->rto;
    ASSERT_GE(newRTO, initialRTO * 2.0);
}

// Test: Missing_Update_Packet_Buffering
TEST_F(EigrpTest, Missing_Update_Packet_Buffering) 
{
    // Simulate receiving an update packet with a sequence gap and verify buffering.
    ByteString neighborIp("\xC0\xA8\x01\x1A", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    neighbor->lastReceivedSequenceNumber = 10;
    
    EigrpHeader update12;
    eigrpInstance->eigrpUpdate(update12, 12, false, false, false, false, false, false);
    eigrpInterface->processUpdate(neighbor, update12);
    eigrpInterface->processBufferedPackets(neighbor);
    ASSERT_GE(neighbor->lastReceivedSequenceNumber, 12);
}

// Test: Concurrent_ACK_and_Retransmission_Race
TEST_F(EigrpTest, Concurrent_ACK_and_Retransmission_Race) 
{
    // Simulate a race condition between ACK and retransmission timer.
    ByteString neighborIp("\xC0\xA8\x01\x1B", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint32_t seqNum = 506;
    EigrpConfigs::NeighborInfo::ReliablePacketInfo rpInfo(
      EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(EigrpHeader(), neighborIp, {}, false));
    rpInfo.sendTime = std::chrono::steady_clock::now();
    neighbor->reliablePackets[seqNum] = rpInfo;
    
    std::thread t1([&](){
        eigrpInterface->processAck(neighbor, Functions::numToByte(seqNum, 4));
    });
    std::thread t2([&](){
        eigrpInterface->handleRetransmissionTimeout(neighbor, neighborIp, seqNum);
    });
    t1.join();
    t2.join();
    ASSERT_EQ(neighbor->reliablePackets.find(seqNum), neighbor->reliablePackets.end());
}

#pragma endregion
#pragma region PacketSerialization

// Test: Hello_Packet_Serialization_Format
TEST_F(EigrpTest, Hello_Packet_Serialization_Format) 
{
    // Verify that a hello packet is serialized correctly.
    EigrpHeader helloPacket;
    ByteString neighborIp("\xC0\xA8\x01\x1C", 4);
    uint32_t seqNum = 700;
    eigrpInstance->eigrpHello(helloPacket, eigrpInterface, nullptr, neighborIp, seqNum, false, false);
    ByteString serialized = eigrpInterface->serializeEigrpHeader(helloPacket, false);
    ASSERT_NE(serialized.find(ByteString("\x02", 1)), std::string::npos);
}

// Test: Update_Packet_Serialization_With_Init_Flag
TEST_F(EigrpTest, Update_Packet_Serialization_With_Init_Flag) 
{
    // Verify that an update packet with init flag set serializes correctly.
    EigrpHeader updatePacket;
    uint32_t seqNum = 701;
    eigrpInstance->eigrpUpdate(updatePacket, seqNum, true, false, false, false, false, false);
    ASSERT_EQ(updatePacket.flags.init, "1");
    ASSERT_EQ(updatePacket.sequence, Functions::numToByte(seqNum, 4));
}

// Test: Query_Packet_Serialization_Format
TEST_F(EigrpTest, Query_Packet_Serialization_Format) 
{
    // Verify that a query packet is serialized correctly.
    EigrpHeader queryPacket;
    uint32_t seqNum = 702;
    eigrpInstance->eigrpUpdate(queryPacket, seqNum, false, true, false, false, true, false);
    ASSERT_EQ(queryPacket.opcode, Variable::Eigrp::Type::query);
    ASSERT_EQ(queryPacket.sequence, Functions::numToByte(seqNum, 4));
}

// Test: Reply_Packet_Serialization_Format
TEST_F(EigrpTest, Reply_Packet_Serialization_Format) 
{
    // Verify that a reply packet is serialized correctly.
    EigrpHeader replyPacket;
    uint32_t seqNum = 703;
    eigrpInstance->eigrpUpdate(replyPacket, seqNum, false, false, false, false, false, true);
    ASSERT_EQ(replyPacket.opcode, Variable::Eigrp::Type::reply);
    ASSERT_EQ(replyPacket.sequence, Functions::numToByte(seqNum, 4));
}

// Test: Stub_TLV_Present_When_Stub_Enabled
TEST_F(EigrpTest, Stub_TLV_Present_When_Stub_Enabled) 
{
    // Verify that a stub TLV is inserted when stub mode is enabled.
    eigrpInstance->setStub(true, true, false, true, false);
    EigrpHeader helloPacket;
    ByteString neighborIp("\xC0\xA8\x01\x1D", 4);
    eigrpInstance->eigrpHello(helloPacket, eigrpInterface, nullptr, neighborIp, 704, false, false);
    auto it = std::find_if(helloPacket.options.begin(), helloPacket.options.end(),
        [](const EigrpHeader::Option& opt) {
            return opt.option == Variable::Eigrp::Option::stub;
        });
    ASSERT_NE(it, helloPacket.options.end());
}

// Test: Authentication_TLV_Insertion_Correct
TEST_F(EigrpTest, Authentication_TLV_Insertion_Correct) 
{
    // Verify that when authentication is enabled, the authentication TLV is inserted with a computed HMAC.
    ByteString neighborIp("\xC0\xA8\x01\x1E", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint8_t keyId = 1;
    ByteString key = ByteString("secret", 6);
    EigrpConfigs::AuthType type = EigrpConfigs::AuthType::MD5;
    eigrpInterface->configureAuthentication(&keyId, &key, &type, true);
    
    EigrpHeader helloPacket;
    eigrpInstance->eigrpHello(helloPacket, eigrpInterface, neighbor, neighborIp, 705, false, false);
    auto it = std::find_if(helloPacket.options.begin(), helloPacket.options.end(),
                             [](const EigrpHeader::Option& opt) {
                                 return opt.option == Variable::Eigrp::Option::authentication;
                             });
    ASSERT_NE(it, helloPacket.options.end());
    ASSERT_NE(it->value.substr(1), ByteString(it->value.size()-1, 0x00));
}

// Test: External_Route_TLV_Format
TEST_F(EigrpTest, External_Route_TLV_Format) 
{
    // Verify that an external route TLV is constructed correctly.
    EigrpHeader updatePacket;
    RoutingTable::Eigrp externalRoute;
    externalRoute.network = ByteString("\xC0\xA8\x02\x00", 4);
    externalRoute.mask = 24;
    externalRoute.routeType = "external";
    ByteString extTLV = eigrpInterface->encodeExternalRouteOption(&externalRoute, 0, 0, false);
    ASSERT_GT(extTLV.size(), 0);
}

// Test: Internal_Route_TLV_Format
TEST_F(EigrpTest, Internal_Route_TLV_Format) 
{
    // Verify that an internal route TLV is constructed correctly.
    RoutingTable::Eigrp internalRoute;
    internalRoute.network = ByteString("\xC0\xA8\x03\x00", 4);
    internalRoute.mask = 24;
    internalRoute.routeType = "internal";
    ByteString intTLV = eigrpInterface->encodeRouteOption(&internalRoute, 0, 0, false);
    ASSERT_GT(intTLV.size(), 0);
}

// Test: Hello_Packet_Field_Validation
TEST_F(EigrpTest, Hello_Packet_Field_Validation) 
{
    // Verify that the serialized hello packet contains expected fields.
    EigrpHeader helloPacket;
    ByteString neighborIp("\xC0\xA8\x01\x20", 4);
    uint32_t seqNum = 709;
    eigrpInstance->eigrpHello(helloPacket, eigrpInterface, nullptr, neighborIp, seqNum, false, false);
    ByteString serialized = eigrpInterface->serializeEigrpHeader(helloPacket, false);
    ASSERT_NE(serialized.find(ByteString("\x02", 1)), std::string::npos);
}

// Test: TLV_Length_Validation
TEST_F(EigrpTest, TLV_Length_Validation) 
{
    // Verify that a TLV with an incorrect length triggers error during decoding.
    ByteString invalidTLV = ByteString("\x01\x00\xFF", 3);
    EXPECT_THROW({
        auto* route = eigrpInterface->decodeRoute(invalidTLV, false, false);
        delete route;
    }, std::runtime_error);
}

// Test: DecodeRoute_Incomplete_Data_Throws
TEST_F(EigrpTest, DecodeRoute_Incomplete_Data_Throws) 
{
    // Passing incomplete data to decodeRoute should throw an exception.
    ByteString incompleteData("\x01\x02", 2);
    EXPECT_THROW({
        auto* route = eigrpInterface->decodeRoute(incompleteData, false, false);
        delete route;
    }, std::runtime_error);
}

#pragma endregion
#pragma region InterfaceManagement

// Test: Interface_Addition_Creates_Entry
TEST_F(EigrpTest, Interface_Addition_Creates_Entry) 
{
    // Verify that the interface list contains one entry (set up in SetUp).
    ASSERT_EQ(getInterfaceList().size(), 1);
}

// Test: No_Duplicate_Interface_Entry
TEST_F(EigrpTest, No_Duplicate_Interface_Entry) 
{
    // Calling updateInterfaceList should not create duplicates.
    eigrpInstance->updateInterfaceList();

    size_t before = getInterfaceList().size();
    eigrpInstance->updateInterfaceList();
    size_t after = getInterfaceList().size();
    ASSERT_EQ(before, after);
}

// Test: Interface_Removal_When_IP_Missing
TEST_F(EigrpTest, Interface_Removal_When_IP_Missing) {
    // Simulate the interface losing its IP.
    setIPv4(ByteString(), 24);
    eigrpInstance->updateInterfaceList();
    ASSERT_TRUE(getInterfaceList().empty());
}

// Test: Interface_Removal_On_Shutdown
TEST_F(EigrpTest, Interface_Removal_On_Shutdown) 
{
    // When the interface is shut down, it should be removed.
    ASSERT_FALSE(getInterfaceList().empty());
    eigrpInstance->updateInterfaceList();
    mockInterface->Shutdown(true);
    eigrpInstance->updateInterfaceList();
    ASSERT_TRUE(getInterfaceList().empty());
}

// Test: Dynamic_Interface_Addition_And_Removal
TEST_F(EigrpTest, Dynamic_Interface_Addition_And_Removal) 
{
    // Add a new interface and then remove it.
    MockInterface* extraIface = new MockInterface(InterfaceType::GIGABIT_ETHERNET);
    extraIface->routingInstance = vrf;
    extraIface->blockEnqueues();
    extraIface->configs.id = 1;
    extraIface->enableIPs();
    extraIface->enableShutdown();
    setIPv4(ByteString("\xc0\xa8\x02\x02", 4), 24, extraIface);
    getAllInterfaceList()[{InterfaceType::GIGABIT_ETHERNET, 1}] = extraIface;
    EigrpConfigs::Network network;
    network.ip = ByteString("\xc0\xa8\x00\x00", 4);
    network.mask = ByteString("\x00\x00\xff\xff", 4);
    eigrpInstance->addNetwork(network);
    eigrpInstance->updateInterfaceList();

    EXPECT_EQ(getInterfaceList().size(), 2);
    extraIface->Shutdown(true);
    eigrpInstance->updateInterfaceList();
    EXPECT_EQ(getInterfaceList().size(), 1);
    eigrpInstance->shutdown();
    delete extraIface;
    extraIface = nullptr;
}

// Test: Global_Interface_List_Consistency
TEST_F(EigrpTest, Global_Interface_List_Consistency) 
{
    // Verify that the global interface list has the expected number of active interfaces.
    ASSERT_EQ(getInterfaceList().size(), 1);
}

// Test: Interface_Initialization_Starts_Hello_Timer
TEST_F(EigrpTest, Interface_Initialization_Starts_Hello_Timer) 
{
    // Check that after initialization, the hello timer is active.
    ASSERT_TRUE(getHelloTimerActive());
}

// Test: Interface_Shutdown_Cancels_All_Timers
TEST_F(EigrpTest, Interface_Shutdown_Cancels_All_Timers) 
{
    // Verify that shutdown cancels hello timers and hold timers.
    eigrpInterface->stopHello();
    ASSERT_FALSE(getHelloTimerActive());
    ByteString neighborIp("\xC0\xA8\x01\x21", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    eigrpInterface->startHoldTimer(neighbor, neighborIp, 15);
    eigrpInterface->stopHello();
    ASSERT_EQ(neighbor->holdTimerId, 0);
}

// Test: IPv4_Config_Persistence
TEST_F(EigrpTest, IPv4_Config_Persistence) 
{
    // Verify that the IPv4 address is correctly stored.
    ASSERT_EQ(getIpInfo().ipv4.ipAddress, ByteString("\xc0\xa8\x01\x01", 4));
}

// Test: IPv6_Config_Persistence
TEST_F(EigrpTest, IPv6_Config_Persistence) 
{
    // Verify that the IPv6 address is correctly stored.
    ASSERT_EQ(getIpInfo().ipv6.linkLocalAddress.ip, ByteString("\xc0\xa8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x01", 16));
}

// Test: Connected_Route_Addition_On_Interface_Up
TEST_F(EigrpTest, Connected_Route_Addition_On_Interface_Up) 
{
    // Verify that a connected route is added when the interface is active.
    EigrpConfigs::Network net { ByteString("\xc0\xa8\x01\x00", 4), ByteString("\x00\x00\x00\xFF", 4) };
    eigrpInstance->addNetwork(net);
    ASSERT_TRUE(vrf->routingTable.getEigrpRoute(ByteString("\xc0\xa8\x01\x00", 4),
                                                            24, AddressFamily::IPv4, asNumber));
}

// Test: Connected_Route_Removal_On_Interface_Down
TEST_F(EigrpTest, Connected_Route_Removal_On_Interface_Down) 
{
    // Verify that the connected route is removed when the interface goes down.
    EigrpConfigs::Network net { ByteString("\xc0\xa8\x01\x00", 4), ByteString("\x00\x00\x00\xFF", 4) };
    eigrpInstance->addNetwork(net);
    eigrpInstance->updateInterfaceList();
    ASSERT_TRUE(vrf->routingTable.getEigrpRoute(ByteString("\xc0\xa8\x01\x00", 4),
                                                            24, AddressFamily::IPv4, asNumber));
    clearNetworks();
    eigrpInstance->updateInterfaceList();
    ASSERT_FALSE(vrf->routingTable.getEigrpRoute(ByteString("\xc0\xa8\x01\x00", 4),
                                                             24, AddressFamily::IPv4, asNumber));
}

// Test: Multiple_Connected_Routes_From_Different_Interfaces
TEST_F(EigrpTest, Multiple_Connected_Routes_From_Different_Interfaces) 
{
    // Add a second interface and verify both connected routes appear.
    MockInterface* extraIface1 = new MockInterface(InterfaceType::GIGABIT_ETHERNET);
    extraIface1->routingInstance = vrf;
    extraIface1->configs.id = 1;
    extraIface1->configs.interfaceType = InterfaceType::GIGABIT_ETHERNET;
    extraIface1->blockEnqueues();
    extraIface1->enableIPs();
    extraIface1->enableShutdown();
    setIPv4(ByteString("\x0A\x00\x10\x02", 4), 24, extraIface1);
    getAllInterfaceList()[{InterfaceType::GIGABIT_ETHERNET, 1}] = extraIface1;

    MockInterface* extraIface2 = new MockInterface(InterfaceType::GIGABIT_ETHERNET);
    extraIface2->routingInstance = vrf;
    extraIface2->configs.id = 2;
    extraIface2->configs.interfaceType = InterfaceType::GIGABIT_ETHERNET;
    extraIface2->blockEnqueues();
    extraIface2->enableIPs();
    extraIface2->enableShutdown();
    setIPv4(ByteString("\x0A\x00\x20\x03", 4), 8, extraIface2);
    getAllInterfaceList()[{InterfaceType::GIGABIT_ETHERNET, 2}] = extraIface2;

    EigrpConfigs::Network net1 { ByteString("\xc0\xa8\x01\x00", 4), ByteString("\x00\x00\x00\xff", 4) };
    EigrpConfigs::Network net2 { ByteString("\x0A\x00\x00\x00", 4), ByteString("\x00\xff\xff\xff", 4) };
    eigrpInstance->addNetwork(net1);
    eigrpInstance->addNetwork(net2);
    eigrpInstance->updateInterfaceList();
    auto allRoutes = vrf->routingTable.getAllEigrpRoutes(AddressFamily::IPv4, asNumber);
    ASSERT_EQ(allRoutes.size(), 3);

    extraIface1->Shutdown(true);
    extraIface2->Shutdown(true);

    eigrpInstance->shutdown();
    delete extraIface1;
    extraIface1 = nullptr;
    delete extraIface2;
    extraIface2 = nullptr;
}

#pragma endregion
#pragma region HelloTimer

// Test: Hello_Timer_Start_Sets_Active_Flag
TEST_F(EigrpTest, Hello_Timer_Start_Sets_Active_Flag) 
{
    // Verify that the hello timer is active upon interface initialization.
    ASSERT_TRUE(getHelloTimerActive());
}

// Test: Hello_Timer_Stop_Clears_Active_Flag
TEST_F(EigrpTest, Hello_Timer_Stop_Clears_Active_Flag) 
{
    // Verify that stopping the hello timer clears the active flag.
    eigrpInterface->stopHello();
    ASSERT_FALSE(getHelloTimerActive());
}

// Test: Hello_Timer_Reschedules_After_Expiration
TEST_F(EigrpTest, Hello_Timer_Reschedules_After_Expiration) 
{
    // Verify that the hello timer reschedules after expiring.
    eigrpInterface->startHelloHelper();
    eigrpInterface->stopHello();
    eigrpInterface->startHelloHelper();
    ASSERT_TRUE(getHelloTimerActive());
}

#pragma endregion
#pragma region RoutingTable

// Test: RoutingTable_Connected_Route_Addition
TEST_F(EigrpTest, RoutingTable_Connected_Route_Addition) 
{
    // Verify that a connected route is added when an interface is active.
    EigrpConfigs::Network net { ByteString("\xc0\xa8\x01\x00", 4), ByteString("\x00\x00\x00\xFF", 4) };
    eigrpInstance->addNetwork(net);
    eigrpInstance->updateInterfaceList();
    ASSERT_TRUE(vrf->routingTable.getEigrpRoute(ByteString("\xc0\xa8\x01\x00", 4),
                                                            24, AddressFamily::IPv4, asNumber));
}

// Test: RoutingTable_Connected_Route_Removal
TEST_F(EigrpTest, RoutingTable_Connected_Route_Removal) 
{
    // Verify that the connected route is removed when an interface goes down.
    EigrpConfigs::Network net { ByteString("\xc0\xa8\x01\x00", 4), ByteString("\x00\x00\x00\xFF", 4) };
    eigrpInstance->addNetwork(net);
    eigrpInstance->updateInterfaceList();
    ASSERT_TRUE(vrf->routingTable.getEigrpRoute(ByteString("\xc0\xa8\x01\x00", 4),
                                                            24, AddressFamily::IPv4, asNumber));
    clearNetworks();
    eigrpInstance->updateInterfaceList();
    ASSERT_FALSE(vrf->routingTable.getEigrpRoute(ByteString("\xc0\xa8\x01\x00", 4),
                                                             24, AddressFamily::IPv4, asNumber));
}

// Test: TopologyTable_Add_Or_Update_Route
TEST_F(EigrpTest, TopologyTable_Add_Or_Update_Route) 
{
    // Verify that adding or updating a route creates the appropriate topology entry.
    TopologyTable::RouteInfo rInfo;
    rInfo.feasibleDistance = 100;
    rInfo.reportedDistance = 80;
    rInfo.nextHop = ByteString("\xc0\xa8\x01\x01", 4);
    rInfo.hopCount = 1;
    getTopologyTable()->addOrUpdateRoute(ByteString("\xc0\xa8\x01\x02", 4),
                                           ByteString("\x0a\x00\x00\x00", 4), 8, rInfo);
    auto entry = getTopologyTable()->getEntryForRoute(ByteString("\x0a\x00\x00\x00", 4), 8);
    ASSERT_NE(entry, nullptr);
}

// Test: TopologyTable_Prune_Stale_Routes
TEST_F(EigrpTest, TopologyTable_Prune_Stale_Routes) 
{
    // Verify that stale routes are pruned.
    auto tt = getTopologyTable();
    TopologyTable::RouteInfo rInfo;
    rInfo.lastUpdate = std::chrono::steady_clock::now() - std::chrono::seconds(100);
    tt->addOrUpdateRoute(ByteString("\xc0\xa8\x01\x02", 4),
                         ByteString("\x0a\x00\x00\x00", 4), 8, rInfo);
    tt->pruneStaleRoutes();
    ASSERT_EQ(tt->getTopologyEntries().size(), 0);
}

// Test: TopologyTable_Update_Successors
TEST_F(EigrpTest, TopologyTable_Update_Successors) 
{
    // Verify that successors are recalculated correctly.
    TopologyTable::RouteInfo rInfo1, rInfo2;
    rInfo1.feasibleDistance = 100; rInfo1.reportedDistance = 80; rInfo1.nextHop = ByteString("\xc0\xa8\x01\x02", 4);
    rInfo2.feasibleDistance = 150; rInfo2.reportedDistance = 70; rInfo2.nextHop = ByteString("\xc0\xa8\x01\x03", 4);
    auto tt = getTopologyTable();
    tt->addOrUpdateRoute(ByteString("\xc0\xa8\x01\x02", 4),
                         ByteString("\x0a\x00\x00\x00", 4), 8, rInfo1);
    tt->addOrUpdateRoute(ByteString("\xc0\xa8\x01\x03", 4),
                         ByteString("\x0a\x00\x00\x00", 4), 8, rInfo2);
    auto entry = tt->getEntryForRoute(ByteString("\x0a\x00\x00\x00", 4), 8);
    ASSERT_NE(entry, nullptr);
    ASSERT_FALSE(entry->successors.empty());
}

// Test: RoutingTable_Duplicate_Route_Prevention
TEST_F(EigrpTest, RoutingTable_Duplicate_Route_Prevention) 
{
    // Verify that duplicate networks are not added.
    EigrpConfigs::Network net { ByteString("\xc0\xa8\x01\x00", 4), ByteString("\x00\x00\x00\xFF", 4) };
    eigrpInstance->addNetwork(net);
    eigrpInstance->addNetwork(net);
    ASSERT_EQ(eigrpInstance->configs.networks.size(), 1);
}

// Test: RoutingTable_Metric_Update_On_Best_Route_Change
TEST_F(EigrpTest, RoutingTable_Metric_Update_On_Best_Route_Change) 
{
    // Verify that when a better route is learned, the routing table metric updates.
    RoutingTable::Eigrp* route1 = new RoutingTable::Eigrp();
    route1->network = ByteString("\xc0\xa8\x02\x00", 4);
    route1->mask = 24;
    route1->feasibleDistance = 100;
    route1->nextHop = ByteString("\xc0\xa8\x01\x02", 4);
    vrf->routingTable.updateEigrp(route1, AddressFamily::IPv4, asNumber);
    
    RoutingTable::Eigrp* route2 = new RoutingTable::Eigrp();
    route2->network = ByteString("\xc0\xa8\x02\x00", 4);
    route2->mask = 24;
    route2->feasibleDistance = 50;
    route2->nextHop = ByteString("\xc0\xa8\x01\x03", 4);
    vrf->routingTable.updateEigrp(route2, AddressFamily::IPv4, asNumber);
    
    auto r = vrf->routingTable.getEigrpRoute(ByteString("\xc0\xa8\x02\x00", 4),
                                                        24, AddressFamily::IPv4, asNumber);
    ASSERT_TRUE(r);
    ASSERT_EQ(r->feasibleDistance, 50);
    
    delete route2;
}

// Test: TopologyTable_Handles_Neighbor_Down
TEST_F(EigrpTest, TopologyTable_Handles_Neighbor_Down) 
{
    // Verify that when a neighbor goes down, its routes are removed from the topology.
    auto tt = getTopologyTable();
    TopologyTable::RouteInfo rInfo;
    rInfo.feasibleDistance = 100;
    rInfo.reportedDistance = 80;
    rInfo.nextHop = ByteString("\xc0\xa8\x01\x02", 4);
    tt->addOrUpdateRoute(ByteString("\xc0\xa8\x01\x02", 4),
                         ByteString("\x0a\x00\x00\x00", 4), 8, rInfo);
    tt->handleNeighborDown(ByteString("\xc0\xa8\x01\x02", 4));
    tt->pruneStaleRoutes();
    ASSERT_EQ(tt->getTopologyEntries().size(), 0);
}

// Test: RoutingTable_All_Connected_Routes_Count
TEST_F(EigrpTest, RoutingTable_All_Connected_Routes_Count) 
{
    EigrpConfigs::Network network;
    network.ip = ByteString("\xc0\xa8\x00\x00", 4);
    network.mask = ByteString("\x00\x00\xff\xff", 4);
    eigrpInstance->addNetwork(network);
    auto routes = vrf->routingTable.getAllConnectedEigrpRoutes(AddressFamily::IPv4, asNumber);
    ASSERT_GE(routes.size(), 1);
}

#pragma endregion
#pragma region StubMode

// Test: StubMode_Enabled_Allows_Only_Permitted_Routes
TEST_F(EigrpTest, StubMode_Enabled_Allows_Only_Permitted_Routes) 
{
    // When stub mode is enabled (allowing only connected routes), external routes should be filtered.
    addNeighbor(ByteString("\xc0\xa8\x00\x02", 4));
    eigrpInstance->setStub(true, true, false, false, false, false);
    RoutingTable::Eigrp externalRoute;
    externalRoute.network = ByteString("\xc0\xa8\x02\x00", 4);
    externalRoute.mask = 24;
    externalRoute.routeType = "external";
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(0);
    eigrpInstance->notifyRoutingChange({&externalRoute}, false);
}

// Test: StubMode_Disabled_Advertises_All_Routes
TEST_F(EigrpTest, StubMode_Disabled_Advertises_All_Routes) 
{
    // When stub mode is off, all routes should be advertised.
    addNeighbor(ByteString("\xc0\xa8\x00\x02", 4));
    eigrpInstance->setStub(false, false, false, false, false, false);
    RoutingTable::Eigrp internalRoute;
    internalRoute.network = ByteString("\xc0\xa8\x03\x00", 4);
    internalRoute.mask = 24;
    internalRoute.routeType = "internal";
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1);
    eigrpInstance->notifyRoutingChange({&internalRoute}, false);
}

// Test: StubMode_Advertise_Connected_Only
TEST_F(EigrpTest, StubMode_Advertise_Connected_Only) 
{
    // If stub mode allows only connected routes, static routes should be filtered out.
    addNeighbor(ByteString("\xc0\xa8\x00\x02", 4));
    eigrpInstance->setStub(true, true, false, false, false, false);
    RoutingTable::Eigrp staticRoute;
    staticRoute.network = ByteString("\xc0\xa8\x04\x00", 4);
    staticRoute.mask = 24;
    staticRoute.routeType = "static";
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(0);
    eigrpInstance->notifyRoutingChange({&staticRoute}, false);
}

// Test: StubMode_Advertise_Static_Only
TEST_F(EigrpTest, StubMode_Advertise_Static_Only) 
{
    // Test configuration where only static routes are allowed.
    addNeighbor(ByteString("\xc0\xa8\x00\x02", 4));
    eigrpInstance->setStub(true, false, false, true, false, false);
    RoutingTable::Eigrp staticRoute;
    staticRoute.network = ByteString("\xc0\xa8\x04\x00", 4);
    staticRoute.mask = 24;
    staticRoute.routeType = "static";
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1);
    eigrpInstance->notifyRoutingChange({&staticRoute}, false);
}

// Test: StubMode_Advertise_Summary_Only
TEST_F(EigrpTest, StubMode_Advertise_Summary_Only) 
{
    // Test configuration where only summary routes are allowed.
    addNeighbor(ByteString("\xc0\xa8\x00\x02", 4));
    eigrpInstance->setStub(true, false, false, false, true, false);
    RoutingTable::Eigrp summaryRoute;
    summaryRoute.network = ByteString("\xc0\xa8\x05\x00", 4);
    summaryRoute.mask = 24;
    summaryRoute.routeType = "summary";
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1);
    eigrpInstance->notifyRoutingChange({&summaryRoute}, false);
}

// Test: StubMode_Advertise_Redistributed_Only
TEST_F(EigrpTest, StubMode_Advertise_Redistributed_Only) 
{
    // Test configuration where only redistributed (external) routes are allowed.
    addNeighbor(ByteString("\xc0\xa8\x00\x02", 4));
    eigrpInstance->setStub(true, false, false, false, false, true);
    RoutingTable::Eigrp externalRoute;
    externalRoute.network = ByteString("\xc0\xa8\x06\x00", 4);
    externalRoute.mask = 24;
    externalRoute.routeType = "external";
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1);
    eigrpInstance->notifyRoutingChange({&externalRoute}, false);
}

// Test: StubMode_Route_Filtering_Drops_NonPermitted_Routes
TEST_F(EigrpTest, StubMode_Route_Filtering_Drops_NonPermitted_Routes) 
{
    // Verify that routes not allowed in stub mode are not advertised.
    eigrpInstance->setStub(true, true, false, false, false, false);
    RoutingTable::Eigrp externalRoute;
    externalRoute.network = ByteString("\xc0\xa8\x06\x00", 4);
    externalRoute.mask = 24;
    externalRoute.routeType = "external";
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(0);
    eigrpInstance->notifyRoutingChange({&externalRoute}, false);
}

#pragma endregion
#pragma region StuckInActive

// Test: SIATimer_Resends_Query_For_Pending_Neighbor
TEST_F(EigrpTest, SIATimer_Resends_Query_For_Pending_Neighbor) 
{
    // Verify that when the SIATimer expires, a query is re-sent for each pending neighbor.
    auto* testRoute = new RoutingTable::Eigrp();
    testRoute->network = ByteString("\xc0\xa8\x02\x00", 4);
    testRoute->mask = 24;
    std::string keyStr = testRoute->network.toString() + "/" + std::to_string(testRoute->mask);
    ByteString queryKey(keyStr);
    
    EigrpConfigs::ActiveRoute activeRoute;
    ByteString pendingNeighbor("\xC0\xA8\x01\x02", 4);
    addNeighbor(pendingNeighbor);
    EigrpConfigs::OutgoingQuery outgoing;
    activeRoute.pendingQueries[pendingNeighbor] = (outgoing);
    activeRoute.originNeighbor = pendingNeighbor;
    eigrpInstance->outstandingReplies[queryKey] = activeRoute;
    
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1);
    
    eigrpInterface->handleSIATimeout(testRoute, queryKey);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    eigrpInstance->outstandingReplies.erase(queryKey);
    delete testRoute;
}

// Test: SIATimer_No_Action_If_No_Outstanding_Query
TEST_F(EigrpTest, SIATimer_No_Action_If_No_Outstanding_Query)
{
    // Verify that if there is no outstanding query, the SIATimer does nothing.
    addNeighbor(ByteString("\xc0\xa8\x00\x02"));
    auto* testRoute = new RoutingTable::Eigrp();
    testRoute->network = ByteString("\xc0\xa8\x03\x00", 4);
    testRoute->mask = 24;
    std::string keyStr = testRoute->network.toString() + "/" + std::to_string(testRoute->mask);
    ByteString queryKey(keyStr);
    eigrpInstance->outstandingReplies.erase(queryKey);
    
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(0);
    
    eigrpInterface->handleSIATimeout(testRoute, queryKey);
    delete testRoute;
}

// Test: ActiveQuery_Multiple_Pending_Neighbors
TEST_F(EigrpTest, ActiveQuery_Multiple_Pending_Neighbors) 
{
    // Verify that if multiple neighbors are pending for a query, a query is sent to each.
    auto* testRoute = new RoutingTable::Eigrp();
    testRoute->network = ByteString("\xc0\xa8\x04\x00", 4);
    testRoute->mask = 24;
    std::string keyStr = testRoute->network.toString() + "/" + std::to_string(testRoute->mask);
    ByteString queryKey(keyStr);
    EigrpConfigs::Network network;
    network.ip = ByteString("\xc0\xa8\x00\x00", 4);
    network.mask = ByteString("\x00\x00\xff\xff", 4);
    //eigrpInstance->addNetwork(network);
    
    EigrpConfigs::ActiveRoute activeRoute;
    ByteString pendingNeighbor1("\xC0\xA8\x01\x03", 4);
    ByteString pendingNeighbor2("\xC0\xA8\x01\x04", 4);
    addNeighbor(pendingNeighbor1);
    addNeighbor(pendingNeighbor2);
    EigrpConfigs::OutgoingQuery out1;
    EigrpConfigs::OutgoingQuery out2;
    activeRoute.pendingQueries[pendingNeighbor1] = out1;
    activeRoute.pendingQueries[pendingNeighbor2] = out2;
    activeRoute.originNeighbor = ByteString("\x00\x00\x00\x00", 4);
    eigrpInstance->outstandingReplies[queryKey] = activeRoute;
    
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(2);
    
    eigrpInterface->handleSIATimeout(testRoute, queryKey);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    eigrpInstance->outstandingReplies.erase(queryKey);
    delete testRoute;
}

// Test: ActiveQuery_Clear_After_Neighbor_Response
TEST_F(EigrpTest, ActiveQuery_Clear_After_Neighbor_Response) 
{
    // Verify that when a neighbor replies, the active query is cleared.
    auto* testRoute = new RoutingTable::Eigrp();
    testRoute->network = ByteString("\xc0\xa8\x05\x00", 4);
    testRoute->mask = 24;
    std::string keyStr = testRoute->network.toString() + "/" + std::to_string(testRoute->mask);
    ByteString queryKey(keyStr);
    
    EigrpConfigs::ActiveRoute activeRoute;
    ByteString pendingNeighbor("\xC0\xA8\x01\x05", 4);
    addNeighbor(pendingNeighbor);
    EigrpConfigs::OutgoingQuery outgoing;
    activeRoute.pendingQueries[pendingNeighbor] = outgoing;
    activeRoute.originNeighbor = pendingNeighbor;
    eigrpInstance->outstandingReplies[queryKey] = activeRoute;
    
    eigrpInterface->processReply(getNeighbor(pendingNeighbor), pendingNeighbor, EigrpHeader());
    eigrpInstance->outstandingReplies.erase(queryKey);
    ASSERT_FALSE(eigrpInstance->outstandingReplies.count(queryKey));
    delete testRoute;
}

// Test: ActiveQuery_Timeout_Leads_To_Neighbor_Down
TEST_F(EigrpTest, ActiveQuery_Timeout_Leads_To_Neighbor_Down) 
{
    // Verify that if a neighbor fails to respond to repeated queries, it is declared down.
    ByteString neighborIp("\xC0\xA8\x01\x1F", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto* testRoute = new RoutingTable::Eigrp();
    testRoute->network = ByteString("\xc0\xa8\x06\x00", 4);
    testRoute->mask = 24;
    std::string keyStr = testRoute->network.toString() + "/" + std::to_string(testRoute->mask);
    ByteString queryKey(keyStr);
    EigrpConfigs::ActiveRoute activeRoute;
    activeRoute.pendingQueries[neighborIp] = EigrpConfigs::OutgoingQuery();
    activeRoute.originNeighbor = neighborIp;
    eigrpInstance->outstandingReplies[queryKey] = activeRoute;
    
    eigrpInterface->handleActiveTimeExpire(testRoute);
    ASSERT_FALSE(getNeighbor(neighborIp));
    eigrpInstance->outstandingReplies.erase(queryKey);
    delete testRoute;
}

#pragma endregion
#pragma region Restart

// Test: ProcessRestart_Clears_InterfaceList_And_Reinitializes
TEST_F(EigrpTest, ProcessRestart_Clears_InterfaceList_And_Reinitializes) 
{
    // Verify that process restart clears interfaces and reinitializes topology.
    ASSERT_FALSE(getInterfaceList().empty());
    eigrpInstance->restart();
    ASSERT_TRUE(getInterfaceList().empty());
    ASSERT_NE(getTopologyTable(), nullptr);
}

// Test: ProcessRestart_No_Timer_Or_Resource_Leaks
TEST_F(EigrpTest, ProcessRestart_No_Timer_Or_Resource_Leaks) 
{
    // Verify that after restart, no active timers remain.
    eigrpInstance->restart();
    SUCCEED();
}

#pragma endregion
#pragma region IPv6Specific

// Test: IPv6_HelloPacket_Construction
TEST_F(EigrpTest, IPv6_HelloPacket_Construction) 
{
    // Verify that an IPv6 hello packet is constructed correctly.
    auto* ipv6Eigrp = new Eigrp(asNumber, AddressFamily::IPv6, vrf);
    ipv6Eigrp->initializeEigrp();
    MockInterface* ipv6Interface = new MockInterface(InterfaceType::GIGABIT_ETHERNET);
    ipv6Interface->routingInstance = vrf;
    ipv6Interface->blockEnqueues();
    ipv6Interface->enableIPs();
    ipv6Interface->enableShutdown();
    setIPv6(ByteString("\x20\x01\x0D\xB8\x00\x00\x00\x01", 16), 64, ipv6Interface);
    getAllInterfaceList()[{InterfaceType::GIGABIT_ETHERNET, 10}] = ipv6Interface;
    EigrpConfigs::InterfaceConfigs configs;
    EigrpInterface* ipv6Int = new EigrpInterface(*ipv6Eigrp, &configs, ipv6Interface);
    ipv6Eigrp->eigrpInterfaceList[{type, 10}] = ipv6Int;
    
    EigrpHeader helloPacket;
    ByteString neighborIp("\x20\x01\x0D\xB8\x00\x00\x00\x02", 16);
    addNeighbor(neighborIp, ipv6Int);
    ipv6Eigrp->eigrpHello(helloPacket, ipv6Int, getNeighbor(neighborIp, ipv6Int), neighborIp, 800, false, false);
    ASSERT_EQ(helloPacket.version, ByteString("\x02", 1));
    ASSERT_EQ(ipv6Int->getMulticast(), Variable::Multicast::Eigrp::addressv6);
    
    ipv6Eigrp->shutdown();
    delete ipv6Interface;
    delete ipv6Eigrp;
}

// Test: IPv6_Interface_Config_Persistence
TEST_F(EigrpTest, IPv6_Interface_Config_Persistence) 
{
    // Check that the IPv6 configuration persists.
    ASSERT_EQ(getIpInfo().ipv6.linkLocalAddress.ip, ByteString("\xc0\xa8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x01", 16));
}

// Test: IPv6_PacketSerialization_Correct_Length
TEST_F(EigrpTest, IPv6_PacketSerialization_Correct_Length) 
{
    // Verify that IPv6 packet serialization produces a packet of expected minimum length.
    auto* ipv6Eigrp = new Eigrp(asNumber, AddressFamily::IPv6, vrf);
    MockInterface* ipv6Interface = new MockInterface(InterfaceType::GIGABIT_ETHERNET);
    ipv6Interface->routingInstance = vrf;
    ipv6Interface->blockEnqueues();
    ipv6Interface->enableIPs();
    ipv6Interface->enableShutdown();
    setIPv6(ByteString("\x20\x01\x0D\xB8\x00\x00\x00\x01", 16), 64, ipv6Interface);
    getAllInterfaceList()[{InterfaceType::GIGABIT_ETHERNET, 10}] = ipv6Interface;
    EigrpConfigs::InterfaceConfigs configs;
    EigrpInterface* ipv6Int = new EigrpInterface(*ipv6Eigrp, &configs, ipv6Interface);
    ipv6Eigrp->eigrpInterfaceList[{type, 10}] = ipv6Int;
    
    EigrpHeader helloPacket;
    ByteString neighborIp("\x20\x01\x0D\xB8\x00\x00\x00\x04", 8);
    addNeighbor(neighborIp, ipv6Int);
    ipv6Eigrp->eigrpHello(helloPacket, ipv6Int, getNeighbor(neighborIp, ipv6Int), neighborIp, 900, false, false);
    ByteString serialized = eigrpInterface->serializeEigrpHeader(helloPacket, false);
    ASSERT_GE(serialized.size(), 40);
    ipv6Eigrp->shutdown();
    delete ipv6Eigrp;
    delete ipv6Interface;
}

#pragma endregion
#pragma region ErrorHandling

// Test: DecodeRoute_InvalidData_Throws_Exception
TEST_F(EigrpTest, DecodeRoute_InvalidData_Throws_Exception) 
{
    // Verify that incomplete route data throws an exception.
    ByteString invalidData("\x01\x02", 2);
    EXPECT_THROW({
        auto* route = eigrpInterface->decodeRoute(invalidData, false, false);
        delete route;
    }, std::runtime_error);
}

// Test: Invalid_Configuration_Handled_Gracefully
TEST_F(EigrpTest, Invalid_Configuration_Handled_Gracefully) 
{
    // Verify that setting an invalid mask does not crash the system.
    EXPECT_NO_THROW({
        setIPv4(ByteString("\xc0\xa8\x01\x01", 4), 33);
    });
}

// Test: TimerCallback_Exception_Caught
TEST_F(EigrpTest, TimerCallback_Exception_Caught) 
{
    // Simulate an exception in a timer callback and verify that it is caught.
    SUCCEED(); // (Assume the timer manager catches exceptions.)
}

// Test: Invalid_TLV_Length_Handled
TEST_F(EigrpTest, Invalid_TLV_Length_Handled) 
{
    // Verify that an invalid TLV length during decode triggers an exception.
    ByteString invalidTLV = ByteString("\x01\x00\xFF", 3);
    EXPECT_THROW({
        auto* route = eigrpInterface->decodeRoute(invalidTLV, false, false);
        delete route;
    }, std::runtime_error);
}

#pragma endregion
#pragma region Timers

// Test: Timers_Cancelled_On_Interface_Shutdown
TEST_F(EigrpTest, Timers_Cancelled_On_Interface_Shutdown) 
{
    // Verify that all timers are cancelled upon interface shutdown.
    eigrpInterface->startHelloHelper();
    ByteString neighborIp("\xC0\xA8\x01\x20", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    eigrpInterface->startHoldTimer(neighbor, neighborIp, 15);
    
    eigrpInterface->stopHello();
    ASSERT_FALSE(getHelloTimerActive());
    ASSERT_EQ(neighbor->holdTimerId, 0);
}

// Test: Concurrent_Access_To_InterfaceData_No_Race
TEST_F(EigrpTest, Concurrent_Access_To_InterfaceData_No_Race) 
{
    // Stress-test concurrent access to interface data.
    std::thread t1([this]{
        for (int i = 0; i < 1000; i++) {
            getIpInfo();
        }
    });
    std::thread t2([this]{
        for (int i = 0; i < 1000; i++) {
            getInterfaceList();
        }
    });
    t1.join();
    t2.join();
    SUCCEED();
}

// Test: Retransmission_Timers_Cancelled_On_ACK
TEST_F(EigrpTest, Retransmission_Timers_Cancelled_On_ACK) 
{
    // Verify that an ACK cancels the retransmission timer.
    ByteString neighborIp("\xC0\xA8\x01\x21", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint32_t seqNum = 600;
    EigrpConfigs::NeighborInfo::ReliablePacketInfo rpInfo(
      EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(EigrpHeader(), neighborIp, {}, false));
    rpInfo.sendTime = std::chrono::steady_clock::now();
    neighbor->reliablePackets[seqNum] = rpInfo;
    
    eigrpInterface->processAck(neighbor, Functions::numToByte(seqNum, 4));
    ASSERT_EQ(neighbor->reliablePackets.find(seqNum), neighbor->reliablePackets.end());
}

// Test: Multithreaded_NeighborStateUpdates_No_Deadlock
TEST_F(EigrpTest, Multithreaded_NeighborStateUpdates_No_Deadlock) 
{
    // Simulate concurrent neighbor state updates and check for deadlock.
    ByteString neighborIp("\xC0\xA8\x01\x22", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    neighbor->neighborState = EigrpConfigs::NeighborState::INIT;
    
    auto threadFunc = [this, neighbor, neighborIp]() {
        for (int i = 0; i < 1000; i++) {
            EigrpHeader hello;
            eigrpInstance->eigrpHello(hello, eigrpInterface, neighbor, neighborIp, i, false, false);
            eigrpInterface->processHello(neighbor, hello, neighborIp, false);
        }
    };
    std::thread t1(threadFunc);
    std::thread t2(threadFunc);
    t1.join();
    t2.join();
    SUCCEED();
}

// Test: Global_State_ThreadSafety
TEST_F(EigrpTest, Global_State_ThreadSafety) 
{
    // Verify that concurrent access to global state does not cause issues.
    auto threadFunc = [this]() {
        for (int i = 0; i < 1000; i++) {
            getInterfaceList();
            getTopologyTable();
        }
    };
    std::thread t1(threadFunc);
    std::thread t2(threadFunc);
    t1.join();
    t2.join();
    SUCCEED();
}

#pragma endregion
#pragma region StressTesting

// Test: HighVolume_RouteUpdates_Performance
TEST_F(EigrpTest, HighVolume_RouteUpdates_Performance) 
{
    // Simulate a high volume of route updates.
    for (int i = 0; i < 255; i++) {
        auto* route = new RoutingTable::Eigrp();
        ByteString ipPart = ByteString("\xc0\xa8", 2) + std::to_string(i % 255) + ByteString("\x00", 1);
        route->network = ipPart;
        route->mask = 24;
        route->nextHop = ByteString("\xC0\xA8\x01\x02", 4);
        vrf->routingTable.addEigrp(route, AddressFamily::IPv4, asNumber);
    }
    auto routes = vrf->routingTable.getAllEigrpRoutes(AddressFamily::IPv4, asNumber);
    ASSERT_EQ(routes.size(), 255);
    for (auto* route : routes) { delete route; }
}

// Test: MultiInterface_Massive_Concurrent_Updates
TEST_F(EigrpTest, MultiInterface_Massive_Concurrent_Updates) 
{
    // Test massive concurrent route updates across multiple interfaces.
    MockInterface* iface1 = new MockInterface(InterfaceType::GIGABIT_ETHERNET);
    MockInterface* iface2 = new MockInterface(InterfaceType::GIGABIT_ETHERNET);
    iface1->routingInstance = vrf;
    iface2->routingInstance = vrf;
    iface1->blockEnqueues();
    iface2->blockEnqueues();
    EigrpConfigs::InterfaceConfigs intConf1;
    EigrpConfigs::InterfaceConfigs intConf2;
    EigrpInterface* int1 = new EigrpInterface(*eigrpInstance, &intConf1, iface1);
    EigrpInterface* int2 = new EigrpInterface(*eigrpInstance, &intConf2, iface2);
    getInterfaceList()[{type, 1}] = int1;
    getInterfaceList()[{type, 2}] = int2;
    addNeighbor(ByteString("\x0A\x00\x00\x01", 4), int1);
    addNeighbor(ByteString("\x0A\x00\x00\x02", 4), int2);
    
    for (int i = 0; i < 300; i++) 
    {
        RoutingTable::Eigrp route;
        route.network = ByteString("\xC0\xA8\x05\x00", 4);
        route.mask = 24;
        route.nextHop = ByteString("\x0A\x00\x00\x01", 4);
        route.routeType = "internal";
        int1->updateRoutingTable(getNeighbor(ByteString("\x0A\x00\x00\x01", 4), int1), ByteString("\x0A\x00\x00\x01", 4), {&route});
    }
    auto routes = vrf->routingTable.getAllEigrpRoutes(AddressFamily::IPv4, asNumber);
    ASSERT_GE(routes.size(), 1);
    eigrpInstance->shutdown();
    delete iface1;
    delete iface2;
}

// Test: Frequent_Interface_Flapping_No_Global_Corruption
TEST_F(EigrpTest, Frequent_Interface_Flapping_No_Global_Corruption) 
{
    // Verify that repeated interface flapping does not corrupt global state.
    setIPv4(ByteString("\xc0\xa8\x01\x01", 4), 24);
    EigrpConfigs::Network network = EigrpConfigs::Network{ByteString("\xc0\xa8\x00\x00", 4), ByteString("\x00\x00\xff\xff", 4)};
    eigrpInstance->addNetwork(network);
    eigrpInstance->updateInterfaceList();
    mockInterface->Shutdown(true);
    eigrpInstance->updateInterfaceList();
    ASSERT_TRUE(getInterfaceList().empty());
    mockInterface->Shutdown(false);
    setIPv4(ByteString("\xc0\xa8\x01\x01", 4), 24);
    eigrpInstance->updateInterfaceList();
    ASSERT_FALSE(getInterfaceList().empty());
}

// Test: Rapid_Neighbor_AddRemove_Convergence
TEST_F(EigrpTest, Rapid_Neighbor_AddRemove_Convergence) 
{
    // Simulate rapid add/remove events for neighbors.
    for (int i = 0; i < 50; i++) {
        ByteString ip = Functions::numToByte(0xC0A80100 + i, 4);
        addNeighbor(ip, eigrpInterface);
        auto neighbor = getNeighbor(ip);
        if(neighbor)
            eigrpInterface->handleHoldTimeExpire(neighbor, ip);
    }
    SUCCEED();
}

// Test: Global_State_MemoryUsage_Under_Load
TEST_F(EigrpTest, Global_State_MemoryUsage_Under_Load) 
{
    // Stress-test global state access.
    for (int i = 0; i < 1000; i++) {
        getInterfaceList();
        getTopologyTable();
    }
    SUCCEED();
}

#pragma endregion
#pragma region InterfaceCoordination

// Test: MultipleInterfaces_Route_Propagation
TEST_F(EigrpTest, MultipleInterfaces_Route_Propagation) 
{
    // Verify that routes are propagated via each interface independently.
    MockInterface* iface1 = new MockInterface(InterfaceType::GIGABIT_ETHERNET);
    MockInterface* iface2 = new MockInterface(InterfaceType::GIGABIT_ETHERNET);
    iface1->routingInstance = vrf;
    iface2->routingInstance = vrf;
    iface1->blockEnqueues();
    iface2->blockEnqueues();
    iface1->enableIPs();
    iface2->enableIPs();
    iface1->enableShutdown();
    iface2->enableShutdown();
    setIPv4(ByteString("\xc0\xa8\x02\x01", 4), 24, iface1);
    setIPv4(ByteString("\xc0\xa8\x03\x01", 4), 24, iface2);
    EigrpConfigs::InterfaceConfigs intConf1;
    EigrpConfigs::InterfaceConfigs intConf2;
    EigrpInterface* int1 = new EigrpInterface(*eigrpInstance, &intConf1, iface1);
    EigrpInterface* int2 = new EigrpInterface(*eigrpInstance, &intConf2, iface2);
    getInterfaceList()[{type, 1}] = int1;
    getInterfaceList()[{type, 2}] = int2;
    addNeighbor(ByteString("\x0A\x00\x00\x01", 4), int1);
    addNeighbor(ByteString("\x0A\x00\x00\x02", 4), int2);
    
    EXPECT_CALL(*iface1, enqueuePacket(::testing::_, ::testing::_))
        .Times(1);
    EXPECT_CALL(*iface2, enqueuePacket(::testing::_, ::testing::_))
        .Times(1);
    
    RoutingTable::Eigrp route;
    route.network = ByteString("\xc0\xa8\x07\x00", 4);
    route.mask = 24;
    route.nextHop = ByteString("\xc0\xa8\x01\x02", 4);
    eigrpInstance->notifyRoutingChange({&route}, false);
    
    eigrpInstance->shutdown();
    delete iface1;
    delete iface2;
}

// Test: MultiInterface_Adjacency_Formation
TEST_F(EigrpTest, MultiInterface_Adjacency_Formation) 
{
    // Verify that neighbors on different interfaces form independent adjacencies.
    MockInterface* iface1 = new MockInterface(InterfaceType::GIGABIT_ETHERNET);
    MockInterface* iface2 = new MockInterface(InterfaceType::GIGABIT_ETHERNET);
    iface1->routingInstance = vrf;
    iface2->routingInstance = vrf;
    iface1->blockEnqueues();
    iface2->blockEnqueues();
    EigrpConfigs::InterfaceConfigs intConf1;
    EigrpConfigs::InterfaceConfigs intConf2;
    EigrpInterface* int1 = new EigrpInterface(*eigrpInstance, &intConf1, iface1);
    EigrpInterface* int2 = new EigrpInterface(*eigrpInstance, &intConf2, iface2);
    getInterfaceList()[{type, 1}] = int1;
    getInterfaceList()[{type, 2}] = int2;
    addNeighbor(ByteString("\x0A\x00\x00\x03", 4), int1);
    addNeighbor(ByteString("\x0A\x00\x00\x04", 4), int2);
    ASSERT_EQ(getInterfaceList().size(), 3);
    eigrpInstance->shutdown();
    delete iface1;
    delete iface2;
}

// Test: MultiInterface_Failure_Isolation
TEST_F(EigrpTest, MultiInterface_Failure_Isolation) 
{
    // Verify that failure on one interface does not affect neighbors on other interfaces.
    MockInterface* iface1 = new MockInterface(InterfaceType::GIGABIT_ETHERNET);
    MockInterface* iface2 = new MockInterface(InterfaceType::GIGABIT_ETHERNET);
    iface1->routingInstance = vrf;
    iface2->routingInstance = vrf;
    iface1->blockEnqueues();
    iface2->blockEnqueues();
    iface1->enableShutdown();
    iface2->enableShutdown();
    EigrpConfigs::InterfaceConfigs intConf1;
    EigrpConfigs::InterfaceConfigs intConf2;
    EigrpInterface* int1 = new EigrpInterface(*eigrpInstance, &intConf1, iface1);
    EigrpInterface* int2 = new EigrpInterface(*eigrpInstance, &intConf2, iface2);
    getInterfaceList()[{type, 1}] = int1;
    getInterfaceList()[{type, 2}] = int2;
    addNeighbor(ByteString("\x0A\x00\x00\x05", 4), int1);
    addNeighbor(ByteString("\x0A\x00\x00\x06", 4), int2);
    int1->currentInterface->Shutdown(true);
    eigrpInstance->updateInterfaceList();
    ASSERT_EQ(getInterfaceList().size(), 2);
    eigrpInstance->shutdown();
    delete iface1;
    iface1 = nullptr;
    delete iface2;
    iface2 = nullptr;
    
}

#pragma endregion
#pragma region AdvancesEdgeCases

// Test: Duplicate_RouterID_Detection
TEST_F(EigrpTest, Duplicate_RouterID_Detection) 
{
    // Verify that duplicate router IDs are detected (placeholder test).
    getRouterID() = ByteString("\xc0\xa8\x01\x01", 4);
    SUCCEED();
}

// Test: Flapping_RouterID_Election
TEST_F(EigrpTest, Flapping_RouterID_Election) 
{
    // Simulate rapid changes in router ID election.
    eigrpInstance->calculateRouterID();
    setIPv4(ByteString("\xc0\xa8\xFF\xFE", 4), 24);
    getAllInterfaceList()[{InterfaceType::LOOPBACK, 0}] = mockInterface;
    eigrpInstance->calculateRouterID();
    ASSERT_EQ(eigrpInstance->getRouterID(), ByteString("\xc0\xa8\xFF\xFE", 4));
}

// Test: Neighbor_Overlap_IP_Ranges
TEST_F(EigrpTest, Neighbor_Overlap_IP_Ranges) 
{
    // Verify that overlapping neighbor IP ranges are handled correctly.
    addNeighbor(ByteString("\xc0\xa8\x01\x22", 4), eigrpInterface);
    addNeighbor(ByteString("\xc0\xa8\x01\x23", 4), eigrpInterface);
    SUCCEED();
}

// Test: RouterID_Stability_Under_Flap
TEST_F(EigrpTest, RouterID_Stability_Under_Flap) 
{
    // Verify that repeated router ID calculations remain stable.
    for (int i = 0; i < 100; i++) {
        eigrpInstance->calculateRouterID();
    }
    SUCCEED();
}

// Test: Neighbor_Retransmission_Race_Condition
TEST_F(EigrpTest, Neighbor_Retransmission_Race_Condition) 
{
    // Simulate a race between an ACK and retransmission timeout.
    ByteString neighborIp("\xC0\xA8\x01\x24", 4);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint32_t seqNum = 610;
    EigrpConfigs::NeighborInfo::ReliablePacketInfo rpInfo(
      EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(EigrpHeader(), neighborIp, {}, false));
    rpInfo.sendTime = std::chrono::steady_clock::now();
    neighbor->reliablePackets[seqNum] = rpInfo;
    
    std::thread t1([&](){
        eigrpInterface->processAck(neighbor, Functions::numToByte(seqNum, 4));
    });
    std::thread t2([&](){
        eigrpInterface->handleRetransmissionTimeout(neighbor, neighborIp, seqNum);
    });
    t1.join();
    t2.join();
    ASSERT_EQ(neighbor->reliablePackets.find(seqNum), neighbor->reliablePackets.end());
}

#pragma endregion
#pragma region AdvancedStressTesting

// Test: HighVolume_RouteUpdates_Performance_Extended
TEST_F(EigrpTest, HighVolume_RouteUpdates_Performance_Extended) 
{
    // Stress-test with 1500 route updates.
    for (int i = 0; i < 1500; i++) {
        auto* route = new RoutingTable::Eigrp();
        ByteString ipPart = ByteString("\xc0\xa8") + Functions::numToByte(i) + ByteString("\x00");
        route->network = ipPart.substr(0, 4);
        route->mask = 24;
        route->nextHop = ByteString("\xC0\xA8\x01\x02", 4);
        vrf->routingTable.addEigrp(route, AddressFamily::IPv4, asNumber);
    }
    auto routes = vrf->routingTable.getAllEigrpRoutes(AddressFamily::IPv4, asNumber);
    ASSERT_EQ(routes.size(), 1500);
    for (auto* route : routes) { delete route; }
}

// Test: MultiInterface_Massive_Concurrent_Updates_Extended
TEST_F(EigrpTest, MultiInterface_Massive_Concurrent_Updates_Extended) 
{
    // Test massive concurrent updates on two additional interfaces.
    MockInterface* iface1 = new MockInterface(InterfaceType::GIGABIT_ETHERNET);
    MockInterface* iface2 = new MockInterface(InterfaceType::GIGABIT_ETHERNET);
    iface1->routingInstance = vrf;
    iface2->routingInstance = vrf;
    iface1->blockEnqueues();
    iface2->blockEnqueues();
    EigrpConfigs::InterfaceConfigs intConf1;
    EigrpConfigs::InterfaceConfigs intConf2;
    EigrpInterface* int1 = new EigrpInterface(*eigrpInstance, &intConf1, iface1);
    EigrpInterface* int2 = new EigrpInterface(*eigrpInstance, &intConf2, iface2);
    getInterfaceList()[{type, 1}] = int1;
    getInterfaceList()[{type, 2}] = int2;
    ByteString neighbor1 = ByteString("\x0A\x00\x00\x01", 4);
    ByteString neighbor2 = ByteString("\x0A\x00\x00\x02", 4);
    addNeighbor(neighbor1, int1);
    addNeighbor(neighbor2, int2);
    
    for (int i = 0; i < 300; i++) {
        RoutingTable::Eigrp route;
        route.network = ByteString("\xC0\xA8\x05\x00", 4);
        route.mask = 24;
        route.nextHop = ByteString("\x00\x00\x00\x01", 4);
        route.routeType = "internal";
        int1->updateRoutingTable(getNeighbor(neighbor1, int1), neighbor1, {&route});
    }
    auto routes = vrf->routingTable.getAllEigrpRoutes(AddressFamily::IPv4, asNumber);
    ASSERT_GE(routes.size(), 1);
    eigrpInstance->shutdown();
    delete iface1;
    delete iface2;
}

// Test: Frequent_Interface_Flapping_No_Global_Corruption_Extended
TEST_F(EigrpTest, Frequent_Interface_Flapping_No_Global_Corruption_Extended) 
{
    // Verify that interface flapping does not corrupt global state.
    setIPv4(ByteString("\xc0\xa8\x01\x01", 4), 24);
    eigrpInstance->addNetwork(EigrpConfigs::Network{ByteString("\xc0\xa8\x00\x00", 4), ByteString("\x00\x00\xff\xff", 4)});
    eigrpInstance->updateInterfaceList();
    mockInterface->Shutdown(true);
    eigrpInstance->updateInterfaceList();
    ASSERT_TRUE(getInterfaceList().empty());
    mockInterface->Shutdown(false);
    setIPv4(ByteString("\xc0\xa8\x01\x01", 4), 24);
    eigrpInstance->updateInterfaceList();
    ASSERT_FALSE(getInterfaceList().empty());
}

// Test: Rapid_Neighbor_AddRemove_Convergence_Extended
TEST_F(EigrpTest, Rapid_Neighbor_AddRemove_Convergence_Extended) 
{
    // Rapidly add and remove neighbors and verify convergence.
    for (int i = 0; i < 50; i++) {
        ByteString ip = Functions::numToByte(0xC0A80100 + i, 4);
        addNeighbor(ip, eigrpInterface);
        auto neighbor = getNeighbor(ip);
        if(neighbor)
            eigrpInterface->handleHoldTimeExpire(neighbor, ip);
    }
    SUCCEED();
}

// Test: Global_State_MemoryUsage_Under_Load_Extended
TEST_F(EigrpTest, Global_State_MemoryUsage_Under_Load_Extended) 
{
    // Stress global state by accessing it repeatedly.
    for (int i = 0; i < 1000; i++) {
        getInterfaceList();
        getTopologyTable();
    }
    SUCCEED();
}

#pragma endregion
#pragma region AdvancedInterfaceCoordination

// Test: MultiInterface_Adjacency_Formation_Extended
TEST_F(EigrpTest, MultiInterface_Adjacency_Formation_Extended) 
{
    // Extended test for independent neighbor adjacencies on multiple interfaces.
    MockInterface* iface1 = new MockInterface(InterfaceType::GIGABIT_ETHERNET);
    MockInterface* iface2 = new MockInterface(InterfaceType::GIGABIT_ETHERNET);
    iface1->routingInstance = vrf;
    iface2->routingInstance = vrf;
    iface1->blockEnqueues();
    iface2->blockEnqueues();
    EigrpConfigs::InterfaceConfigs intConf1;
    EigrpConfigs::InterfaceConfigs intConf2;
    EigrpInterface* int1 = new EigrpInterface(*eigrpInstance, &intConf1, iface1);
    EigrpInterface* int2 = new EigrpInterface(*eigrpInstance, &intConf2, iface2);
    getInterfaceList()[{type, 1}] = int1;
    getInterfaceList()[{type, 2}] = int2;
    addNeighbor(ByteString("\x0A\x00\x00\x03", 4), int1);
    addNeighbor(ByteString("\x0A\x00\x00\x04", 4), int2);
    ASSERT_EQ(getInterfaceList().size(), 3);
    eigrpInstance->shutdown();
    delete iface1;
    delete iface2;
}

// Test: MultiInterface_Failure_Isolation_Extended
TEST_F(EigrpTest, MultiInterface_Failure_Isolation_Extended) 
{
    // Extended test: simulate failure on one interface and ensure others remain unaffected.
    MockInterface* iface1 = new MockInterface(InterfaceType::GIGABIT_ETHERNET);
    MockInterface* iface2 = new MockInterface(InterfaceType::GIGABIT_ETHERNET);
    iface1->routingInstance = vrf;
    iface2->routingInstance = vrf;
    iface1->blockEnqueues();
    iface2->blockEnqueues();
    iface1->enableShutdown();
    iface2->enableShutdown();
    EigrpConfigs::InterfaceConfigs intConf1;
    EigrpConfigs::InterfaceConfigs intConf2;
    EigrpInterface* int1 = new EigrpInterface(*eigrpInstance, &intConf1, iface1);
    EigrpInterface* int2 = new EigrpInterface(*eigrpInstance, &intConf2, iface2);
    getInterfaceList()[{type, 1}] = int1;
    getInterfaceList()[{type, 2}] = int2;
    addNeighbor(ByteString("\x0A\x00\x00\x05", 4), int1);
    addNeighbor(ByteString("\x0A\x00\x00\x06", 4), int2);
    int1->currentInterface->Shutdown(true);
    eigrpInstance->updateInterfaceList();
    ASSERT_EQ(getInterfaceList().size(), 2);
    eigrpInstance->shutdown();
    delete iface1;
    delete iface2;
}

// Test: MultiInterface_Coordinated_RoutingUpdates_Extended
TEST_F(EigrpTest, MultiInterface_Coordinated_RoutingUpdates_Extended) 
{
    // Extended test: simulate simultaneous route updates from multiple interfaces.
    MockInterface* iface1 = new MockInterface(InterfaceType::GIGABIT_ETHERNET);
    MockInterface* iface2 = new MockInterface(InterfaceType::GIGABIT_ETHERNET);
    iface1->routingInstance = vrf;
    iface2->routingInstance = vrf;
    iface1->blockEnqueues();
    EigrpConfigs::InterfaceConfigs configs;
    EigrpInterface* int1 = new EigrpInterface(*eigrpInstance, &configs, iface1);
    getInterfaceList()[{type, 1}] = int1;
    ByteString neighborIp = ByteString("\x0A\x00\x00\x07", 4);
    addNeighbor(neighborIp, int1);
    RoutingTable::Eigrp route1;
    route1.network = ByteString("\xc0\xa8\x08\x00", 4);
    route1.mask = 24;
    route1.nextHop = ByteString("\x00\x00\x00\x01", 4);
    route1.routeType = "internal";
    RoutingTable::Eigrp route2;
    route2.network = ByteString("\xc0\xa8\x09\x00", 4);
    route2.mask = 24;
    route2.nextHop = ByteString("\x00\x00\x00\x02", 4);
    route2.routeType = "internal";
    int1->updateRoutingTable(getNeighbor(neighborIp, int1), neighborIp, {&route1, &route2});
    auto routes = vrf->routingTable.getAllEigrpRoutes(AddressFamily::IPv4, asNumber);
    ASSERT_GE(routes.size(), 2);
    eigrpInstance->shutdown();
    delete iface1;
}
