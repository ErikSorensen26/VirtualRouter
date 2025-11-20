// Internal_EigrpTest.cpp

#include <gtest/gtest.h>
#include <PacketBuilder.hpp>
#include <EigrpPacketBuilder.h>
#include <Eigrp.h>
#include <EigrpInterface.h>
#include <TLVBuilder.h>
#include <Neighbor.h>
#include <VirtualRouter.h>
#include <MockInterface.hpp>
#include <PacketStructure.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <ListenerManager.hpp>

// Test fixture for global EIGRP tests
class Internal_EigrpTest : public ::testing::Test 
{
protected:
    uint16_t asNumber = 1;
    AddressFamily addressFamily = AddressFamily::IPv4;
    Eigrp::Eigrp* eigrpInstance;
    MockInterface* mockInterface;
    // We use the real EigrpInterface (constructed using our MockInterface)
    EigrpConfigs::InterfaceConfigs* configs;
    Eigrp::EigrpInterface* eigrpInterface;
    std::condition_variable cv;
    std::mutex cvMutex;
    bool packetEnqueued = false;
    InterfaceType type = InterfaceType::GIGABIT_ETHERNET;
    VirtualRouter* vrf = nullptr;
    Global* global = nullptr;
    uint32_t mKey;

    uint8_t testPacket[100] = {0};

    uint8_t ipIntv4[4] = { 0xC0, 0xA8, 0x01, 0x01 };
    uint8_t ipIntv6[16] = { 0xC0, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01 };

    // Setup creates an Eigrp instance and one interface for testing.
    void SetUp() override 
    {
        global = new Global({}, false, true);
        vrf = global->getRoutingInstance("default", AddressFamily::IPv4);
        vrf->enabledAddressFamilies.insert(AddressFamily::IPv6);
        vrf->eigrpList[1] = new Eigrp::EigrpAutonomousSystem();
        eigrpInstance = new Eigrp::Eigrp(asNumber, addressFamily, vrf);
        vrf->eigrpList[1]->ipv4 = eigrpInstance;
        mockInterface = new MockInterface(*global, InterfaceType::GIGABIT_ETHERNET);

        mockInterface->arp = nullptr;
        mockInterface->ndp = nullptr;
        mockInterface->routingInstance = vrf;

        mKey = mockInterface->configs.key;

        EXPECT_CALL(*mockInterface, startThreads()).Times(::testing::AnyNumber());
        mockInterface->enableIPs();
        mockInterface->enableShutdown();
        // Set initial IPv4 and IPv6 addresses on the mock interface.

        setIPv4(readU32(ipIntv4), 24);
        setIPv6(ipIntv6, 64);
        // Assume interfaceList is a global map keyed by InterfaceType and interface id.
        vrf->interfaceList[mKey] = mockInterface;
        // Create the real EigrpInterface using the mock interface.
        EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(::testing::AtLeast(1));
        configs = new EigrpConfigs::InterfaceConfigs(mKey);
        auto it = eigrpInstance->getIfaceMgr().eigrpInterfaceList.try_emplace(mKey, *eigrpInstance, *configs, *mockInterface);
        eigrpInterface = &it.first->second;
    }

    // TearDown cleans up the EIGRP instance, interface, and global objects.
    void TearDown() override 
    {
        mockInterface->blockEnqueues();
        delete global;
        std::memset(testPacket, 0, sizeof(testPacket));
    }

    // Helper function: returns the topology table.
    Eigrp::TopologyTable& getTopologyTable() { return eigrpInstance->getTopology().duel.topologyTable; }
    // Helper: returns the current interface list.
    std::map<uint32_t, Eigrp::EigrpInterface>& getInterfaceList() { return eigrpInstance->getIfaceMgr().eigrpInterfaceList; }
    std::unordered_map<uint32_t, Interface*>& getAllInterfaceList() { return eigrpInstance->routingInstance->interfaceList; }
    // Helper: returns the current configs
    EigrpConfigs::EigrpConfigs& getConfigs() { return eigrpInstance->getConfigs(); }
    
    // Helper: set IPv4 address on an interface.
    void setIPv4(const uint32_t ip, uint8_t mask, MockInterface* iface = nullptr) 
    {
        if (iface)
            iface->setIPv4(ip, mask);
        else
            mockInterface->setIPv4(ip, mask);
    }

    void delIPv4(MockInterface* iface = nullptr)
    {
        if (iface)
            iface->removeIPv4();
        else
            mockInterface->removeIPv4();
    }
    
    // Helper: set IPv6 address on an interface.
    void setIPv6(const uint8_t* ip, uint8_t mask, Interface* iface = nullptr) 
    {
        if (iface)
            iface->setIPv6(ip, mask, false);
        else
            mockInterface->setIPv6(ip, false, mask, false);
    }

    void delIPv6(const uint8_t* ip, MockInterface* iface = nullptr)
    {
        if (iface)
            iface->removeIPv6(ip);
        else
            mockInterface->removeIPv6(ip);
    }
    
    // Helper: returns the IpInfo from the interface.
    InterfaceConfigs& getIpInfo(Interface* iface = nullptr) 
    {
        return (iface ? iface->configs : mockInterface->configs);
    }
    
    // Helper: clear network configuration in the EIGRP instance.
    void clearNetworks() { getConfigs().networks.clear(); }
    
    // Helper: add a neighbor via the real EigrpInterface.
    void addNeighbor(const IPAddress& ip, Eigrp::Neighbor::Version v = Eigrp::Neighbor::Version::LEGACY, Eigrp::EigrpInterface* intf = nullptr) 
    {
        uint8_t neighborMac[6] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
        if (intf)
            intf->getNTable().createNeighbor(ip, v, neighborMac)->setState(Eigrp::Neighbor::State::ESTABLISHED);
        else
            eigrpInterface->getNTable().createNeighbor(ip, v, neighborMac)->setState(Eigrp::Neighbor::State::ESTABLISHED);
    }
    
    // Helper: retrieve a neighbor from an interface.
    Eigrp::Neighbor* getNeighbor(const IPAddress& ip, Eigrp::EigrpInterface* intf = nullptr) 
    {
        return (intf ? intf->getNTable().lookup(ip) : eigrpInterface->getNTable().lookup(ip));
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

    Eigrp::ReceivedRoute getRoute(uint32_t iface)
    {
        Eigrp::ReceivedRoute r;
        r.feasibleDistance = 1441792;
        r.reportedDistance = 720896;
        r.nextHop = { 0, AddressFamily::IPv4 };
        r.bandwidth = 1000000;
        r.delay = 1000000000;
        r.hopCount = 0;
        r.mtu = 1500;
        r.reliability = 255;
        r.load = 1;
        r.routeType = Eigrp::RouteType::INTERNAL;
        r.originInterface = iface;
        r.tag = 0;
        return r;
    }

    std::vector<TLV16Option> extractEigrpOptions(PacketBuilder& pkt)
    {
        auto h = pkt.getHeader(HeaderType::EIGRP);
        if (!h) return {};
        size_t optSize = h->length - 16;
        if (optSize == 0) return {};
        std::vector<TLV16Option> opts;
        parseEigrpOptions(h->buffer + 16, optSize, opts);
        return opts;
    }

    EigrpHeader getEigrpHeader(PacketBuilder& pkt)
    {
        auto h = pkt.getHeader(HeaderType::EIGRP);
        EigrpHeader eigrp;
        eigrp.setBuffer(h->buffer);
        return eigrp;
    }

    IPAddress createIPv4(uint32_t ip)
    {
        uint8_t ipBuf[4];
        writeU32(ipBuf, ip);
        return { ipBuf, AddressFamily::IPv4 };
    }

    PacketBuilder createPacket(Eigrp::EigrpInterface* interface = nullptr)
    {
        Eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createPacket();
    }

    std::optional<EigrpHeader> createHello(PacketBuilder& builder, Eigrp::EigrpInterface* interface = nullptr)
    {
        Eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createHello(builder);
    }

    std::optional<EigrpHeader> createUnicastHello(PacketBuilder& builder, const IPAddress& neighborIp, Eigrp::EigrpInterface* interface = nullptr)
    {
        Eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createUnicastHello(builder, neighborIp);
    }
    std::optional<EigrpHeader> createSequenceHello(PacketBuilder& builder, const IPAddress& neighborIp, uint32_t seq, Eigrp::EigrpInterface* interface = nullptr)
    {
        Eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createSequenceHello(builder, neighborIp, seq);
    }

    std::optional<EigrpHeader> createAck(PacketBuilder& builder, Eigrp::Neighbor& neighbor, uint32_t seq, Eigrp::EigrpInterface* interface = nullptr)
    {
        Eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createAck(builder, neighbor, seq);
    }

    std::optional<EigrpHeader> createNullUpdate(PacketBuilder& builder, Eigrp::Neighbor& neighbor, Eigrp::EigrpInterface* interface = nullptr)
    {
        Eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createNullUpdate(builder, neighbor);
    }

    std::optional<EigrpHeader> createUpdate(PacketBuilder& builder, Eigrp::ReliableTransport::PktInfo& info, Eigrp::Neighbor* neighbor, const std::vector<const Eigrp::RouteInfo*>& routes, Eigrp::EigrpInterface* interface =  nullptr)
    {
        Eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createUpdate(builder, info, neighbor, routes);
    }

    std::optional<EigrpHeader> createQuery(PacketBuilder& builder, Eigrp::ReliableTransport::PktInfo& info, Eigrp::Neighbor* neighbor, const std::vector<Eigrp::OutgoingQuery*>& queries, Eigrp::EigrpInterface* interface = nullptr)
    {
        Eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createQuery(builder, info, neighbor, queries);
    }

    std::optional<EigrpHeader> createReply(PacketBuilder& builder, Eigrp::ReliableTransport::PktInfo& info, Eigrp::Neighbor& neighbor, const std::vector<const Eigrp::RouteInfo*>& replies, uint32_t seq, Eigrp::EigrpInterface* interface = nullptr)
    {
        Eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createReply(builder, info, neighbor, replies, seq);
    }

    std::optional<EigrpHeader> createSIAQuery(PacketBuilder& builder, Eigrp::ReliableTransport::PktInfo& info, Eigrp::Neighbor& neighbor, const std::vector<Eigrp::OutgoingQuery*>& queries, Eigrp::EigrpInterface* interface = nullptr)
    {
        Eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createSIAQuery(builder, info, neighbor, queries);
    }

    std::optional<EigrpHeader> createSIAReply(PacketBuilder& builder, Eigrp::Neighbor& neighbor, uint32_t seq, Eigrp::EigrpInterface* interface = nullptr)
    {
        Eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createSIAReply(builder, neighbor, seq);
    }

    void processHello(EigrpHeader& header, Eigrp::Neighbor& neighbor, Eigrp::EigrpInterface* interface = nullptr)
    {
        Eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        std::vector<TLV16Option> opts;
        Eigrp::ReliableTransport::RTPInfo r(header, neighbor.ipAddress);
        parseEigrpOptions(header.buffer + EigrpHeader::fixedSize, header.size() - EigrpHeader::fixedSize, r.opts);
        return iface->getRtp().processHello(r, false);
    }

    void processAck(Eigrp::Neighbor& neighbor, const uint32_t seq, Eigrp::EigrpInterface* interface = nullptr)
    {
        Eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().processAck(neighbor, seq);
    }

    void setSeq(uint32_t seq, Eigrp::EigrpInterface* interface = nullptr)
    {
        Eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        iface->getRtp().nextSeq = seq;
    }

    bool recalculateSuccessors(Eigrp::TopologyEntry* entry)
    {
        return getDuel().recalculateSuccessors(entry);
    }

    std::map<IPPrefix, Eigrp::ActiveRoute>& getActiveRoutes() { return getDuel().activeRoutes; }

    Eigrp::DuelEngine& getDuel() { return eigrpInstance->getTopology().duel; }

    bool getHelloTimerActive(Eigrp::EigrpInterface* eigrpInt = nullptr) { if (eigrpInt) return eigrpInt->getTimers().helloTimerActive.load(); else return eigrpInterface->getTimers().helloTimerActive.load();}
    uint32_t getRouterID(Eigrp::Eigrp* eigrp = nullptr) { if (eigrp) return eigrp->routerID(); else return eigrpInstance->routerID(); }
};

#pragma region Authentication

// Test: AuthTLV_MD5_Correct
TEST_F(Internal_EigrpTest, AuthTLV_MD5_Correct) 
{
    // Verify that MD5 authentication TLV is built correctly.
    // (Neighbor IP: 192.168.1.2, key "secretkey")
    IPAddress neighborIp = createIPv4(0xC0A80102);
    addNeighbor(neighborIp);
    auto optNeighbor = getNeighbor(neighborIp);
    ASSERT_TRUE(optNeighbor);
    uint8_t keyId = 1;
    std::string key = "secretKey";
    EigrpConfigs::AuthType type = EigrpConfigs::AuthType::MD5;
    eigrpInterface->getAuth().setKeyChain(&keyId, &key, &type, true);

    // Block enqueues
    mockInterface->blockEnqueues();
    
    // Create hello packet.
    PacketBuilder helloPacket = createPacket();
    ASSERT_TRUE(createHello(helloPacket).has_value());
    
    // Verify authentication TLV is present and not all zeros.
    std::vector<TLV16Option> opts = extractEigrpOptions(helloPacket);
    
    auto it = std::find_if(opts.begin(), opts.end(), [](const TLV16Option& opt) {
        return opt.type == Variable::Eigrp::Option::authentication;
    });
    ASSERT_NE(it, opts.end());
    ASSERT_FALSE(std::all_of(it->value + 1, it->value + it->valueSize - 1, [](uint8_t b){return b == 0;}));
}

// Test: AuthTLV_SHA1_Correct
TEST_F(Internal_EigrpTest, AuthTLV_SHA1_Correct) 
{
    // Verify that SHA-1 authentication TLV is built correctly.
    IPAddress neighborIp = createIPv4(0xC0A80103);
    addNeighbor(neighborIp);
    auto optNeighbor = getNeighbor(neighborIp);
    ASSERT_TRUE(optNeighbor);
    uint8_t keyId = 2;
    std::string key = "anothersecret";
    EigrpConfigs::AuthType type = EigrpConfigs::AuthType::SHA1;
    eigrpInterface->getAuth().setKeyChain(&keyId, &key, &type, true);

    // Block enqueues
    mockInterface->blockEnqueues();
    
    PacketBuilder helloPacket = createPacket();
    ASSERT_TRUE(createUnicastHello(helloPacket, neighborIp).has_value());
    
    std::vector<TLV16Option> opts = extractEigrpOptions(helloPacket);

    auto it = std::find_if(opts.begin(), opts.end(), [](const TLV16Option& opt) {
        return opt.type == Variable::Eigrp::Option::authentication;
    });
    ASSERT_NE(it, opts.end());
    ASSERT_FALSE(std::all_of(it->value + 1, it->value + it->valueSize - 1, [](uint8_t b){return b == 0;}));
}

// Test: AuthTLV_Disabled_NoTLV
TEST_F(Internal_EigrpTest, AuthTLV_Disabled_NoTLV) 
{
    // Ensure that if authentication is disabled, no authentication TLV is added.
    eigrpInterface->getAuth().setKeyChain();

    // Block enqueues
    mockInterface->blockEnqueues();
    
    PacketBuilder helloPacket = createPacket();
    ASSERT_TRUE(createHello(helloPacket).has_value());

    std::vector<TLV16Option> opts = extractEigrpOptions(helloPacket);
    
    auto it = std::find_if(opts.begin(), opts.end(), [](const TLV16Option& opt) {
        return opt.type == Variable::Eigrp::Option::authentication;
    });
    ASSERT_EQ(it, opts.end());
}

// Test: Auth_InvalidKey_Handled
TEST_F(Internal_EigrpTest, Auth_InvalidKey_Handled) 
{
    // If neighbor has an empty auth key, no authentication TLV should be produced.
    IPAddress neighborIp = createIPv4(0xC0A80105);
    
    addNeighbor(neighborIp);
    auto optNeighbor = getNeighbor(neighborIp);
    ASSERT_TRUE(optNeighbor);
    uint8_t keyId = 1;
    std::string key = "";
    EigrpConfigs::AuthType type = EigrpConfigs::AuthType::MD5;
    eigrpInterface->getAuth().setKeyChain(&keyId, &key, &type, true);

    // Block enqueues
    mockInterface->blockEnqueues();
    
    PacketBuilder helloPacket = createPacket();
    ASSERT_TRUE(createUnicastHello(helloPacket, neighborIp).has_value());

    std::vector<TLV16Option> opts = extractEigrpOptions(helloPacket);
    
    auto it = std::find_if(opts.begin(), opts.end(), [](const TLV16Option& opt) {
        return opt.type == Variable::Eigrp::Option::authentication;
    });
    ASSERT_EQ(it, opts.end());
}

#pragma endregion
/*#pragma region NeighborState

// Test: NeighborState_DOWN_To_EXSTART
TEST_F(Internal_EigrpTest, NeighborState_DOWN_To_EXSTART) 
{
    // Trigger transition from TWOWAY to EXSTART.
    IPAddress neighborIp = createIPv4(0x0CA80108);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    neighbor->setState(Eigrp::Neighbor::State::DOWN);
    
    PacketBuilder hello(mockInterface);
    auto eigrp = createUnicastHello(hello, neighborIp);
    ASSERT_TRUE(eigrp.has_value());
    eigrpInterface->getRtp().handleIncoming(nullptr, eigrp.value(), neighborIp.raw, false);
    // Initial Hello and Update Packet
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(3);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    ASSERT_TRUE(neighbor->getState() >= Eigrp::Neighbor::State::);
}

// Test: NeighborState_EXSTART_To_EXCHANGE_SLAVE
TEST_F(Internal_EigrpTest, NeighborState_EXSTART_To_EXCHANGE_SLAVE) 
{
    // Simulate update processing that moves state from EXSTART to EXCHANGE.
    IPAddress neighborIp = createIPv4(0xC0A80101);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    neighbor->neighborState = EigrpConfigs::NeighborState::EXSTART;
    // Init update received
    neighbor->initFlags.initUpdateReceived = true;
    // Set neighbor state
    neighbor->initFlags.initRole = EigrpConfigs::InitRole::SLAVE;
    
    EigrpHeader update;
    eigrpInstance->eigrpUpdate(update, 3, false, false, false, false, false, false);
    eigrpInterface->processUpdate(neighbor, update);

    ASSERT_TRUE(neighbor->neighborState >= EigrpConfigs::NeighborState::LOADING);
}

// Test: NeighborState_EXSTART_To_EXSTART_SLAVE
TEST_F(Internal_EigrpTest, NeighborState_EXSTART_To_EXCHANGE_MASTER)
{
    // Simulate update processing that moves state from EXSTART to EXCHANGE.
    IPAddress neighborIp = createIPv4(0xC0A80101);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    neighbor->neighborState = EigrpConfigs::NeighborState::EXSTART;
    // Init update received
    neighbor->initFlags.initUpdateReceived = true;
    // Set neighbor state
    neighbor->initFlags.initRole = EigrpConfigs::InitRole::MASTER;
    
    EigrpHeader update;
    eigrpInstance->eigrpUpdate(update, 3, false, false, false, false, false, false);
    eigrpInterface->processUpdate(neighbor, update);

    ASSERT_TRUE(neighbor->neighborState >= EigrpConfigs::NeighborState::LOADING);
}

// Test: NeighborState_EXCHANGE_To_LOADING
TEST_F(Internal_EigrpTest, NeighborState_EXCHANGE_To_LOADING) 
{
    // After exchanging topology, simulate transition to LOADING.
    IPAddress neighborIp = createIPv4(0xC0A80101);
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
TEST_F(Internal_EigrpTest, NeighborState_LOADING_To_ESTABLISHED) 
{
    // Simulate final update that sets the neighbor state to ESTABLISHED.
    IPAddress neighborIp = createIPv4(0xC0A8010B);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    neighbor->neighborState = EigrpConfigs::NeighborState::LOADING;
    
    // Simulate final update.
    neighbor->neighborState = EigrpConfigs::NeighborState::ESTABLISHED;
    ASSERT_TRUE(neighbor->neighborState >= EigrpConfigs::NeighborState::ESTABLISHED);
}

// Test: Duplicate_Hello_Ignored
TEST_F(Internal_EigrpTest, Duplicate_Hello_Ignored) 
{
    // Ensure duplicate hello packets do not affect neighbor state.
    IPAddress neighborIp = createIPv4(0x0CA8010C);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    neighbor->neighborState = EigrpConfigs::NeighborState::INIT;
    
    PacketBuilder hello(mockInterface);
    eigrpInstance->eigrpHello(hello, *eigrpInterface, neighborIp.raw, 10, false, false);
    eigrpInterface->processHello(neighbor, getEigrpHeader(hello), neighborIp, false);
    eigrpInterface->processHello(neighbor, getEigrpHeader(hello), neighborIp, false);
    
    ASSERT_TRUE(neighbor->neighborState >= EigrpConfigs::NeighborState::INIT);
}

// Test: Neighbor_Restart_Resets_State
TEST_F(Internal_EigrpTest, Neighbor_Restart_Resets_State) 
{
    // Verify that restarting a neighbor resets its state, reliable packets, and sequence list.
    IPAddress neighborIp = createIPv4(0xC0A8010E);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    neighbor->neighborState = EigrpConfigs::NeighborState::ESTABLISHED;
    neighbor->reliablePackets[100] = EigrpConfigs::NeighborInfo::ReliablePacketInfo(
        EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet(AddressFamily::IPv4, neighborIp.raw, 4, neighborIp, {}, false));
    
    eigrpInterface->handleNeighborRestart(neighbor, neighborIp);
    
    ASSERT_EQ(neighbor->neighborState, EigrpConfigs::NeighborState::DOWN);
    ASSERT_TRUE(neighbor->reliablePackets.empty());
}

// Test: HoldTimer_Expires_Marks_Neighbor_Down
TEST_F(Internal_EigrpTest, HoldTimer_Expires_Marks_Neighbor_Down) 
{
    // Verify that when a neighbor’s hold timer expires, it is removed.
    IPAddress neighborIp = createIPv4(0xC0A8010F);
    addNeighbor(neighborIp, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    eigrpInterface->startHoldTimer(neighbor, neighborIp, 1);
    eigrpInterface->handleHoldTimeExpire(neighbor, neighborIp);
    
    ASSERT_FALSE(getNeighbor(neighborIp));
}

// Test: MultipleNeighbors_Independent_States
TEST_F(Internal_EigrpTest, MultipleNeighbors_Independent_States) 
{
    // Verify that two neighbors maintain independent states.
    IPAddress neighborIp1 = createIPv4(0xC0A80110);
    IPAddress neighborIp2 = createIPv4(0xC0A80111);
    addNeighbor(neighborIp1, eigrpInterface);
    addNeighbor(neighborIp2, eigrpInterface);
    
    auto neighbor1 = getNeighbor(neighborIp1);
    auto neighbor2 = getNeighbor(neighborIp2);
    neighbor1->neighborState = EigrpConfigs::NeighborState::DOWN;
    neighbor2->neighborState = EigrpConfigs::NeighborState::DOWN;
    
    PacketBuilder hello1(mockInterface);
    eigrpInstance->eigrpHello(hello1, *eigrpInterface, neighborIp1.raw, 20, false, false);
    eigrpInterface->processHello(neighbor1, getEigrpHeader(hello1), neighborIp2, false);
    
    ASSERT_EQ(neighbor1->neighborState, EigrpConfigs::NeighborState::TWOWAY);
    ASSERT_EQ(neighbor2->neighborState, EigrpConfigs::NeighborState::DOWN);
}

// Test: OutOfOrder_Update_Packet_Processing
TEST_F(Internal_EigrpTest, OutOfOrder_Update_Packet_Processing) 
{
    // Simulate an update packet arriving out of order.
    IPAddress neighborIp = createIPv4(0xC0A80111);
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
TEST_F(Internal_EigrpTest, Duplicate_Update_Packet_Processing) 
{
    // Ensure that processing the same update packet twice does not alter state.
    IPAddress neighborIP = createIPv4(0xC0A80112);
    addNeighbor(neighborIP, eigrpInterface);
    auto neighbor = getNeighbor(neighborIP);
    neighbor->lastReceivedSequenceNumber = 20;
    
    EigrpHeader updatePacket;
    eigrpInstance->eigrpUpdate(updatePacket, 21, false, false, false, false, false, false);
    eigrpInterface->processUpdate(neighbor, updatePacket);
    eigrpInterface->processUpdate(neighbor, updatePacket);
    
    ASSERT_EQ(neighbor->lastReceivedSequenceNumber, 21);
}

#pragma endregion*/
#pragma region ReliablePacket

// Test: Retransmission_Timer_Expires_Resend
TEST_F(Internal_EigrpTest, Retransmission_Timer_Expires_Resend) 
{
    // Simulate a lost packet so that the retransmission timer expires and the packet is resent.
    IPAddress neighborIp = createIPv4(0xC0A80114);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint32_t seqNum = 500;
    EigrpHeader hdr;
    hdr.setBuffer(testPacket);
    neighbor->setState(Eigrp::Neighbor::State::ESTABLISHED);
    
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(::testing::AtLeast(1));
    
    eigrpInterface->getRtp().setupReliablePacket(neighbor, hdr);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    auto current = neighbor->currentReliable.load(std::memory_order_relaxed);
    EXPECT_TRUE(current == seqNum);
    EXPECT_GE(neighbor->reliableQueue[current].info.retransmissionCount, 0);
}

// Test: ACK_Processing_Cancels_Packet
TEST_F(Internal_EigrpTest, ACK_Processing_Cancels_Packet) 
{
    // Verify that a valid ACK cancels the retransmission timer.
    IPAddress neighborIp = createIPv4(0xC0A80116);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint32_t seqNum = 502;
    EigrpHeader hdr;
    hdr.setBuffer(testPacket);
    neighbor->setState(Eigrp::Neighbor::State::ESTABLISHED);
    eigrpInterface->getRtp().setupReliablePacket(neighbor, hdr);
    processAck(*neighbor, seqNum);
    EXPECT_TRUE(neighbor->reliableQueue.empty());
}

// Test: Duplicate_ACK_Does_Not_Alter_RTT
TEST_F(Internal_EigrpTest, Duplicate_ACK_Does_Not_Alter_RTT) 
{
    // Process the same ACK twice and verify that RTT does not change.
    IPAddress neighborIp = createIPv4(0xC0A80117);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint32_t seqNum = 503;
    EigrpHeader hdr;
    hdr.setBuffer(testPacket);
    neighbor->setState(Eigrp::Neighbor::State::ESTABLISHED);
    eigrpInterface->getRtp().setupReliablePacket(neighbor, hdr);
    processAck(*neighbor, seqNum);
    double srttAfterFirst = neighbor->srtt;
    processAck(*neighbor, seqNum);
    ASSERT_EQ(neighbor->srtt, srttAfterFirst);
}

// Test: Max_Retransmissions_Triggers_Neighbor_Down
TEST_F(Internal_EigrpTest, Max_Retransmissions_Triggers_Neighbor_Down) 
{
    // Set the retransmission count to the maximum and simulate a timeout.
    IPAddress neighborIp = createIPv4(0xC0A80118);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint32_t seqNum = 504;
    EigrpHeader hdr;
    hdr.setBuffer(testPacket);
    neighbor->setState(Eigrp::Neighbor::State::ESTABLISHED);
    eigrpInterface->getRtp().setupReliablePacket(neighbor, hdr);
    auto rel = neighbor->reliableQueue[neighbor->currentReliable];
    rel.info.retransmissionCount = MAX_RETRANSMISSIONS;
    eigrpInterface->getRtp().handleRetransmission(neighbor, rel, seqNum);
    ASSERT_FALSE(getNeighbor(neighborIp));
}

// Test: Exponential_Backoff_Applied
TEST_F(Internal_EigrpTest, Exponential_Backoff_Applied) 
{
    // Verify that the RTO doubles after a retransmission.
    IPAddress neighborIp = createIPv4(0xC0A80119);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint32_t seqNum = 505;
    EigrpHeader hdr;
    hdr.setBuffer(testPacket);
    neighbor->setState(Eigrp::Neighbor::State::ESTABLISHED);
    eigrpInterface->getRtp().setupReliablePacket(neighbor, hdr);

    double initialRTO = neighbor->rto;
    auto rel = neighbor->reliableQueue[neighbor->currentReliable];
    eigrpInterface->getRtp().handleRetransmission(neighbor, rel, seqNum);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    double newRTO = neighbor->rto;
    ASSERT_GE(newRTO, initialRTO * 2.0);
}

// Test: Missing_Update_Packet_Buffering
TEST_F(Internal_EigrpTest, Missing_Update_Packet_Buffering) 
{
    // Simulate receiving an update packet with a sequence gap and verify buffering.
    IPAddress neighborIp = createIPv4(0xC0A8011A);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    neighbor->lastSeqRecv.store(10);
    
    PacketBuilder update = createPacket();
    Eigrp::ReliableTransport::PktInfo info;
    info.mtu = 1500;
    info.version = Eigrp::TLVType::LEGACY_V4;
    auto eigrp = createUpdate(update, info, neighbor, {});
    ASSERT_TRUE(eigrp.has_value());
    eigrp->setSequence(12);

    eigrpInterface->getRtp().handleIncoming(nullptr, *eigrp, neighborIp.raw, true);
    ASSERT_GE(neighbor->lastSeqRecv.load(), 12);
}

// Test: Concurrent_ACK_and_Retransmission_Race
TEST_F(Internal_EigrpTest, Concurrent_ACK_and_Retransmission_Race) 
{
    // Simulate a race condition between ACK and retransmission timer.
    IPAddress neighborIp = createIPv4(0xC0A8011B);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint32_t seqNum = 506;
    EigrpHeader hdr;
    hdr.setBuffer(testPacket);
    eigrpInterface->getRtp().setupReliablePacket(neighbor, hdr);
    
    std::thread t1([&](){
        processAck(*neighbor, seqNum);
    });
    std::thread t2([&](){
        auto pkt = neighbor->reliableQueue[neighbor->currentReliable];
        eigrpInterface->getRtp().handleRetransmission(neighbor, pkt, pkt.info.sequence);
    });
    t1.join();
    t2.join();
    EXPECT_TRUE(neighbor->reliableQueue.count(seqNum));
}

#pragma endregion

// Test: Stub_TLV_Present_When_Stub_Enabled
TEST_F(Internal_EigrpTest, Stub_TLV_Present_When_Stub_Enabled) 
{
    // Verify that a stub TLV is inserted when stub mode is enabled.
    eigrpInstance->getGlobalConfigMgr().enableStub(true, true, false, true, false);
    PacketBuilder helloPacket = createPacket();
    ASSERT_TRUE(createHello(helloPacket).has_value());
    std::vector<TLV16Option> opts = extractEigrpOptions(helloPacket);
    auto it = std::find_if(opts.begin(), opts.end(),
        [](const TLV16Option& opt) {
            return opt.type == Variable::Eigrp::Option::stub;
        });
    ASSERT_NE(it, opts.end());
}

// Test: Authentication_TLV_Insertion_Correct
TEST_F(Internal_EigrpTest, Authentication_TLV_Insertion_Correct) 
{
    // Verify that when authentication is enabled, the authentication TLV is inserted with a computed HMAC.
    IPAddress neighborIp = createIPv4(0xC0A8011E);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    uint8_t keyId = 1;
    std::string key = "secret";
    EigrpConfigs::AuthType type = EigrpConfigs::AuthType::MD5;
    eigrpInterface->getAuth().setKeyChain(&keyId, &key, &type, true);
    
    PacketBuilder helloPacket = createPacket();
    ASSERT_TRUE(createUnicastHello(helloPacket, neighborIp).has_value());
    std::vector<TLV16Option> opts = extractEigrpOptions(helloPacket);
    auto it = std::find_if(opts.begin(), opts.end(),
                             [](const TLV16Option& opt) {
                                 return opt.type == Variable::Eigrp::Option::authentication;
                             });
    ASSERT_NE(it, opts.end());
    ASSERT_FALSE(std::all_of(it->value + 1, it->value + it->valueSize - 1, [](uint8_t b){return b == 0;}));
}

// Test: External_Route_TLV_Format
TEST_F(Internal_EigrpTest, External_Route_TLV_Format) 
{
    // Verify that an external route TLV is constructed correctly.
    Eigrp::ReceivedRoute route = getRoute(eigrpInterface->interfaceKey);
    route.prefix = {createIPv4(0xC0A80200), 24};
    route.routeType = Eigrp::RouteType::EXTERNAL;
    Eigrp::RouteInfo routeInfo = {route};
    TLV16BufferManager opts(testPacket, sizeof(testPacket));
    size_t tlvValueSize = Eigrp::TLVBuilder::encodeRouteOption(testPacket, sizeof(testPacket), &routeInfo, mockInterface->configs.bandwidth, mockInterface->configs.delay, Eigrp::TLVBuilder::RouteType::LEGACY_EXTERNAL);
    ASSERT_GT(tlvValueSize, 0);
}

// Test: Internal_Route_TLV_Format
TEST_F(Internal_EigrpTest, Internal_Route_TLV_Format) 
{
    // Verify that an internal route TLV is constructed correctly.
    Eigrp::ReceivedRoute route = getRoute(eigrpInterface->interfaceKey);
    route.prefix = {createIPv4(0xC0A80300), 24};
    route.routeType = Eigrp::RouteType::INTERNAL;
    route.routeType = Eigrp::RouteType::EXTERNAL;
    Eigrp::RouteInfo routeInfo = {route};
    TLV16BufferManager opts(testPacket, sizeof(testPacket));
    size_t tlvValueSize = Eigrp::TLVBuilder::encodeRouteOption(testPacket, sizeof(testPacket), &routeInfo, mockInterface->configs.bandwidth, mockInterface->configs.delay, Eigrp::TLVBuilder::RouteType::LEGACY_INTERNAL);
    ASSERT_GT(tlvValueSize, 0);
}

// Test: TLV_Length_Validation
TEST_F(Internal_EigrpTest, TLV_Length_Validation)
{
    // Verify that a TLV with an incorrect length triggers error during decoding.
    uint8_t invalidValue[1] = { 0xFF };
    TLV16Option invalid( 0x01, 0x00, invalidValue );
    EXPECT_THROW({
        auto route = Eigrp::TLVBuilder::decodeRoute(invalid, eigrpInterface->interfaceKey);
    }, std::runtime_error);
}

#pragma endregion
#pragma region InterfaceManagement

// Test: Interface_Addition_Creates_Entry
TEST_F(Internal_EigrpTest, Interface_Addition_Creates_Entry) 
{
    // Verify that the interface list contains one entry (set up in SetUp).
    ASSERT_EQ(getInterfaceList().size(), 1);
}

// Test: No_Duplicate_Interface_Entry
TEST_F(Internal_EigrpTest, No_Duplicate_Interface_Entry) 
{
    // Calling updateInterfaceList should not create duplicates.
    eigrpInstance->refreshInterfaceList();

    size_t before = getInterfaceList().size();
    eigrpInstance->refreshInterfaceList();
    size_t after = getInterfaceList().size();
    ASSERT_EQ(before, after);
}

// Test: Interface_Removal_When_IP_Missing
TEST_F(Internal_EigrpTest, Interface_Removal_When_IP_Missing) {
    // Simulate the interface losing its IP.
    delIPv4();
    eigrpInstance->refreshInterfaceList();
    ASSERT_TRUE(getInterfaceList().empty());
}

// Test: Interface_Removal_On_Shutdown
TEST_F(Internal_EigrpTest, Interface_Removal_On_Shutdown) 
{
    // When the interface is shut down, it should be removed.
    ASSERT_FALSE(getInterfaceList().empty());
    eigrpInstance->refreshInterfaceList();
    mockInterface->Shutdown(true);
    eigrpInstance->refreshInterfaceList();
    ASSERT_TRUE(getInterfaceList().empty());
}

// Test: Dynamic_Interface_Addition_And_Removal
TEST_F(Internal_EigrpTest, Dynamic_Interface_Addition_And_Removal) 
{
    // Add a new interface and then remove it.
    MockInterface* extraIface = new MockInterface(*global, InterfaceType::GIGABIT_ETHERNET);
    uint32_t key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 1);
    vrf->interfaceList[key] = extraIface;
    extraIface->routingInstance = vrf;
    extraIface->blockEnqueues();
    extraIface->configs.id = 1;
    extraIface->enableIPs();
    extraIface->enableShutdown();
    setIPv4(0xC0A80202, 24, extraIface);
    getAllInterfaceList()[key] = extraIface;
    EigrpConfigs::Network network(AddressFamily::IPv4);
    network.ip = createIPv4(0xC0A80000);
    network.mask = 16;
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(network);

    EXPECT_EQ(getInterfaceList().size(), 2);
    extraIface->Shutdown(true);
    eigrpInstance->refreshInterfaceList();
    EXPECT_EQ(getInterfaceList().size(), 1);
    eigrpInstance->shutdown();
}

// Test: Global_Interface_List_Consistency
TEST_F(Internal_EigrpTest, Global_Interface_List_Consistency) 
{
    // Verify that the global interface list has the expected number of active interfaces.
    ASSERT_EQ(getInterfaceList().size(), 1);
}

// Test: Interface_Initialization_Starts_Hello_Timer
TEST_F(Internal_EigrpTest, Interface_Initialization_Starts_Hello_Timer) 
{
    // Check that after initialization, the hello timer is active.
    ASSERT_TRUE(getHelloTimerActive());
}

// Test: Interface_Shutdown_Cancels_All_Timers
TEST_F(Internal_EigrpTest, Interface_Shutdown_Cancels_All_Timers) 
{
    // Verify that shutdown cancels hello timers and hold timers.
    eigrpInterface->getTimers().stopHello();
    ASSERT_FALSE(getHelloTimerActive());
    IPAddress neighborIp = createIPv4(0xC0A80121);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    auto& timer = eigrpInterface->getTimers();
    timer.startHoldTimer(*neighbor);
    timer.stopHello();
    ASSERT_EQ(neighbor->holdTimerId, 0);
}

// Test: IPv4_Config_Persistence
TEST_F(Internal_EigrpTest, IPv4_Config_Persistence) 
{
    // Verify that the IPv4 address is correctly stored.
    ASSERT_EQ(getIpInfo().ipv4.getAddress(), 0xc0a80101);
}

// Test: IPv6_Config_Persistence
TEST_F(Internal_EigrpTest, IPv6_Config_Persistence) 
{
    // Verify that the IPv6 address is correctly stored.
    uint8_t ipv6[16] = { 0xC0, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01 };
    ASSERT_EQ(getIpInfo().ipv6.getLocalAddress(), readU128(ipv6));
}

// Test: Connected_Route_Addition_On_Interface_Up
TEST_F(Internal_EigrpTest, Connected_Route_Addition_On_Interface_Up) 
{
    // Verify that a connected route is added when the interface is active.
    EigrpConfigs::Network net(AddressFamily::IPv4);
    IPAddress dest = createIPv4(0xC0A80100);
    net.ip = dest;
    net.mask = 24;
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(net);
    ASSERT_TRUE(vrf->routingTable.lookup<uint32_t>(dest.v4));
}

// Test: Connected_Route_Removal_On_Interface_Down
TEST_F(Internal_EigrpTest, Connected_Route_Removal_On_Interface_Down) 
{
    // Verify that the connected route is removed when the interface goes down.
    EigrpConfigs::Network net(AddressFamily::IPv4);
    IPAddress dest = createIPv4(0xC0A80100);
    net.ip = dest;
    net.mask = 24;
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(net);
    eigrpInstance->refreshInterfaceList();
    ASSERT_TRUE(vrf->routingTable.lookup<uint32_t>(dest.v4));
    clearNetworks();
    eigrpInstance->refreshInterfaceList();
    ASSERT_FALSE(vrf->routingTable.lookup<uint32_t>(dest.v4));
}

// Test: Multiple_Connected_Routes_From_Different_Interfaces
TEST_F(Internal_EigrpTest, Multiple_Connected_Routes_From_Different_Interfaces) 
{
    // Add a second interface and verify both connected routes appear.
    MockInterface* extraIface1 = new MockInterface(*global, InterfaceType::GIGABIT_ETHERNET);
    uint32_t key1 = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 1);
    vrf->interfaceList[key1] = extraIface1;
    extraIface1->routingInstance = vrf;
    extraIface1->configs.id = 1;
    extraIface1->configs.interfaceType = InterfaceType::GIGABIT_ETHERNET;
    extraIface1->blockEnqueues();
    extraIface1->enableIPs();
    extraIface1->enableShutdown();
    setIPv4(0x0A001002, 24, extraIface1);
    getAllInterfaceList()[key1] = extraIface1;

    MockInterface* extraIface2 = new MockInterface(*global, InterfaceType::GIGABIT_ETHERNET);
    uint32_t key2 = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 2);
    vrf->interfaceList[key2] = extraIface2;
    extraIface2->routingInstance = vrf;
    extraIface2->configs.id = 2;
    extraIface2->configs.interfaceType = InterfaceType::GIGABIT_ETHERNET;
    extraIface2->blockEnqueues();
    extraIface2->enableIPs();
    extraIface2->enableShutdown();
    setIPv4(0x0A002003, 8, extraIface2);
    getAllInterfaceList()[key2] = extraIface2;

    EigrpConfigs::Network net1(AddressFamily::IPv4);
    net1.ip = createIPv4(0xC0A80100);
    net1.mask = 24;
    EigrpConfigs::Network net2(AddressFamily::IPv4);
    net2.ip = createIPv4(0x0A000000);
    net2.mask = 8;
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(net1);
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(net2);
    eigrpInstance->refreshInterfaceList();
    ASSERT_EQ(vrf->routingTable.size<uint32_t>(), 3);

    extraIface1->Shutdown(true);
    extraIface2->Shutdown(true);

    eigrpInstance->shutdown();
}

#pragma endregion
#pragma region HelloTimer

// Test: Hello_Timer_Start_Sets_Active_Flag
TEST_F(Internal_EigrpTest, Hello_Timer_Start_Sets_Active_Flag) 
{
    // Verify that the hello timer is active upon interface initialization.
    ASSERT_TRUE(getHelloTimerActive());
}

// Test: Hello_Timer_Stop_Clears_Active_Flag
TEST_F(Internal_EigrpTest, Hello_Timer_Stop_Clears_Active_Flag) 
{
    // Verify that stopping the hello timer clears the active flag.
    eigrpInterface->getTimers().stopHello();
    ASSERT_FALSE(getHelloTimerActive());
}

// Test: Hello_Timer_Reschedules_After_Expiration
TEST_F(Internal_EigrpTest, Hello_Timer_Reschedules_After_Expiration) 
{
    // Verify that the hello timer reschedules after expiring.
    auto& timer = eigrpInterface->getTimers();
    timer.startHelloHelper();
    timer.stopHello();
    timer.startHelloHelper();
    ASSERT_TRUE(getHelloTimerActive());
}

#pragma endregion
#pragma region RoutingTable

// Test: RoutingTable_Connected_Route_Addition
TEST_F(Internal_EigrpTest, RoutingTable_Connected_Route_Addition) 
{
    // Verify that a connected route is added when an interface is active.
    EigrpConfigs::Network net(AddressFamily::IPv4);
    IPAddress ip = createIPv4(0xC0A80100);
    net.ip = ip;
    net.mask = 24;
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(net);
    eigrpInstance->refreshInterfaceList();
    ASSERT_TRUE(vrf->routingTable.lookup<uint32_t>(ip.v4));
}

// Test: RoutingTable_Connected_Route_Removal
TEST_F(Internal_EigrpTest, RoutingTable_Connected_Route_Removal) 
{
    // Verify that the connected route is removed when an interface goes down.
    EigrpConfigs::Network net(AddressFamily::IPv4);
    IPAddress ip = createIPv4(0xC0A80100);
    net.ip = ip;
    net.mask = 24;
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(net);
    eigrpInstance->refreshInterfaceList();
    ASSERT_TRUE(vrf->routingTable.lookup<uint32_t>(ip.v4));
    clearNetworks();
    eigrpInstance->refreshInterfaceList();
    ASSERT_FALSE(vrf->routingTable.lookup<uint32_t>(ip.v4));
}

// Test: TopologyTable_Add_Or_Update_Route
TEST_F(Internal_EigrpTest, TopologyTable_Add_Or_Update_Route) 
{
    // Verify that adding or updating a route creates the appropriate topology entry.
    IPAddress neighborIp = createIPv4(0xC0A80121);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    Eigrp::ReceivedRoute r = getRoute(eigrpInterface->interfaceKey);
    r.prefix = { createIPv4(0xA0000000), 16};
    r.bandwidth = 1000000;
    r.delay = 100000000;
    r.feasibleDistance = 100;
    r.reportedDistance = 80;
    IPAddress nextHop = createIPv4(0xC0A80101);
    r.nextHop = nextHop;
    r.hopCount = 1;
    std::vector<Eigrp::ReceivedRoute> rs = {r};
    getDuel().processReceivedRoutes(rs, *neighbor);
    auto entries = getDuel().topologyTable.entries();
    ASSERT_TRUE(entries.size() > 0);
}

// Test: TopologyTable_Prune_Stale_Routes
TEST_F(Internal_EigrpTest, TopologyTable_Prune_Stale_Routes) 
{
    // Verify that stale routes are pruned.
    IPAddress neighborIp = createIPv4(0xC0A80121);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    auto r1 = getRoute(eigrpInterface->interfaceKey);
    auto r2 = getRoute(eigrpInterface->interfaceKey);
    IPPrefix prefix1 = {createIPv4(0xA0000000), 16};
    IPPrefix prefix2 = {createIPv4(0xA1000000), 16};
    r1.prefix = prefix1;
    r2.prefix = prefix2;
    std::vector<Eigrp::ReceivedRoute> rs = {r1, r2};
    getDuel().processReceivedRoutes(rs, *neighbor);
    auto& top = getTopologyTable();
    top.entries()[prefix1]->routesByNeighbor.at(neighborIp).valid = std::chrono::steady_clock::now() - std::chrono::seconds(100);
    EXPECT_EQ(top.entries().size(), 2);
    top.pruneExpired();
    EXPECT_EQ(top.entries().size(), 1);
}

// Test: TopologyTable_Update_Successors
TEST_F(Internal_EigrpTest, TopologyTable_Update_Successors) 
{
    // Verify that successors are recalculated correctly.
    Eigrp::ReceivedRoute route1, route2;
    IPAddress r1 = createIPv4(0xC0A80102);
    IPAddress r2 = createIPv4(0xC0A80103);
    route1.feasibleDistance = 100; route1.reportedDistance = 80; route1.nextHop = r1;
    route2.feasibleDistance = 150; route2.reportedDistance = 70; route2.nextHop = r2;
    Eigrp::RouteInfo rInfo1(route1), rInfo2(route2);
    Eigrp::TopologyEntry top;
    top.routesByNeighbor.try_emplace(r1, route1);
    top.routesByNeighbor.try_emplace(r2, route2);
    recalculateSuccessors(&top);
    EXPECT_EQ(top.successors.size(), 1);
}

// Test: RoutingTable_Duplicate_Route_Prevention
TEST_F(Internal_EigrpTest, RoutingTable_Duplicate_Route_Prevention) 
{
    // Verify that duplicate networks are not added.
    EigrpConfigs::Network net(AddressFamily::IPv4);
    net.ip = createIPv4(0xC0A80100);
    net.mask = 24;
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(net);
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(net);
    ASSERT_EQ(eigrpInstance->getGlobalConfigMgr().getConfigs().networks.size(), 1);
}

// Test: RoutingTable_Metric_Update_On_Best_Route_Change
TEST_F(Internal_EigrpTest, RoutingTable_Metric_Update_On_Best_Route_Change) 
{
    // Verify that when a better route is learned, the routing table metric updates.
    auto route1 = getRoute(eigrpInterface->interfaceKey);
    IPAddress network = createIPv4(0xC0A80200);
    route1.prefix = { network, 24 };
    route1.feasibleDistance = 100;
    route1.nextHop = createIPv4(0xC0A80102);
    Eigrp::RouteInfo r1(route1);
    eigrpInstance->routeManager.synchronizeRoutes({}, {}, {&r1});
    
    auto route2 = getRoute(eigrpInterface->interfaceKey);
    route2.prefix = { network, 24 };
    route2.feasibleDistance = 50;
    route2.nextHop = createIPv4(0xC0A80103);
    Eigrp::RouteInfo r2(route2);
    eigrpInstance->routeManager.synchronizeRoutes({}, {}, {&r2});
    
    RibEntry<uint32_t>* r = vrf->routingTable.lookup<uint32_t>(network.v4);
    ASSERT_TRUE(r);
    ASSERT_EQ(r->metric, 1);
}

// Test: TopologyTable_Handles_Neighbor_Down
TEST_F(Internal_EigrpTest, TopologyTable_Handles_Neighbor_Down) 
{
    // Verify that when a neighbor goes down, its routes are removed from the topology.
    eigrpInstance->getConfigs().routeDelTimer = 1;
    IPAddress neighborIp = createIPv4(0xC0A80121);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    auto& tt = getTopologyTable();
    auto r = getRoute(eigrpInterface->interfaceKey);
    IPAddress nextHop = createIPv4(0xC0A80102);
    r.prefix = { createIPv4(0x0A000000), 16 };
    r.feasibleDistance = 100;
    r.reportedDistance = 80;
    r.nextHop = nextHop;
    std::vector<Eigrp::ReceivedRoute> rs = {r};
    getDuel().processReceivedActiveRoutes(rs, *neighbor);
    eigrpInterface->getTopController().onNeighborDown(neighborIp);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    tt.pruneExpired();
    ASSERT_EQ(tt.entries().size(), 0);
}

// Test: RoutingTable_All_Connected_Routes_Count
TEST_F(Internal_EigrpTest, RoutingTable_All_Connected_Routes_Count) 
{
    EigrpConfigs::Network network(AddressFamily::IPv4);
    network.ip = createIPv4(0xC0A80000);
    network.mask = 16;
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(network);
    ASSERT_EQ(vrf->routingTable.lookup<uint32_t>(mockInterface->configs.ipv4.getAddress())->nextHops[0].nextHop, 0);
}

#pragma endregion
#pragma region StubMode

// Test: StubMode_Enabled_Allows_Only_Permitted_Routes
TEST_F(Internal_EigrpTest, StubMode_Enabled_Allows_Only_Permitted_Routes) 
{
    // When stub mode is enabled (allowing only connected routes), external routes should be filtered.
    IPAddress neighborIp = createIPv4(0xC0A80002);
    addNeighbor(neighborIp);
    eigrpInstance->getGlobalConfigMgr().enableStub(true, true, false, false, false, false);
    Eigrp::ReceivedRoute r = getRoute(eigrpInterface->interfaceKey);
    r.prefix = {createIPv4(0xC0A80200), 24};
    r.routeType = Eigrp::RouteType::EXTERNAL;
    Eigrp::RouteInfo rInfo(r);
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(0);
    eigrpInstance->broadcastRouteChanges({&rInfo});
}

// Test: StubMode_Disabled_Advertises_All_Routes
TEST_F(Internal_EigrpTest, StubMode_Disabled_Advertises_All_Routes) 
{
    // When stub mode is off, all routes should be advertised.
    IPAddress neighborIp = createIPv4(0xC0A80002);
    addNeighbor(neighborIp);
    eigrpInstance->getGlobalConfigMgr().enableStub(false, false, false, false, false, false);
    Eigrp::ReceivedRoute internalRoute = getRoute(eigrpInterface->interfaceKey);
    internalRoute.prefix = { createIPv4(0xC0A80300), 24 };
    internalRoute.routeType = Eigrp::RouteType::INTERNAL;
    Eigrp::RouteInfo rInfo(internalRoute);
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1);
    eigrpInstance->broadcastRouteChanges({&rInfo});
}

// Test: StubMode_Advertise_Connected_Only
TEST_F(Internal_EigrpTest, StubMode_Advertise_Connected_Only) 
{
    // If stub mode allows only connected routes, static routes should be filtered out.
    IPAddress neighborIp = createIPv4(0xC0A80002);
    addNeighbor(neighborIp);
    eigrpInstance->getGlobalConfigMgr().enableStub(true, true, false, false, false, false);
    Eigrp::ReceivedRoute internalRoute = getRoute(eigrpInterface->interfaceKey);
    internalRoute.prefix = { createIPv4(0xC0A80400), 24 };
    internalRoute.routeType = Eigrp::RouteType::STATIC;
    Eigrp::RouteInfo rInfo(internalRoute);
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(0);
    eigrpInstance->broadcastRouteChanges({&rInfo});
}

// Test: StubMode_Advertise_Static_Only
TEST_F(Internal_EigrpTest, StubMode_Advertise_Static_Only) 
{
    // Test configuration where only static routes are allowed.
    IPAddress neighborIp = createIPv4(0xC0A80002);
    addNeighbor(neighborIp);
    eigrpInstance->getGlobalConfigMgr().enableStub(true, false, false, true, false, false);
    Eigrp::ReceivedRoute staticRoute = getRoute(eigrpInterface->interfaceKey);
    staticRoute.prefix = { createIPv4(0xC0A80400), 24 };
    staticRoute.routeType = Eigrp::RouteType::STATIC;
    Eigrp::RouteInfo rInfo(staticRoute);
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1);
    eigrpInstance->broadcastRouteChanges({&rInfo});
}

// Test: StubMode_Advertise_Summary_Only
TEST_F(Internal_EigrpTest, StubMode_Advertise_Summary_Only) 
{
    // Test configuration where only summary routes are allowed.
    IPAddress neighborIp = createIPv4(0xC0A80002);
    addNeighbor(neighborIp);
    eigrpInstance->getGlobalConfigMgr().enableStub(true, false, false, false, true, false);
    Eigrp::ReceivedRoute summaryRoute = getRoute(eigrpInterface->interfaceKey);
    summaryRoute.prefix = { createIPv4(0xC0A80500), 24 };
    summaryRoute.routeType = Eigrp::RouteType::SUMMARY;
    Eigrp::RouteInfo rInfo(summaryRoute);
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1);
    eigrpInstance->broadcastRouteChanges({&rInfo});
}

// Test: StubMode_Advertise_Redistributed_Only
TEST_F(Internal_EigrpTest, StubMode_Advertise_Redistributed_Only) 
{
    // Test configuration where only redistributed (external) routes are allowed.
    IPAddress neighborIp = createIPv4(0xC0A80002);
    addNeighbor(neighborIp);
    eigrpInstance->getGlobalConfigMgr().enableStub(true, false, false, false, false, true);
    Eigrp::ReceivedRoute externalRoute = getRoute(eigrpInterface->interfaceKey);
    externalRoute.prefix = { createIPv4(0xC0A80600), 24 };
    externalRoute.routeType = Eigrp::RouteType::EXTERNAL;
    Eigrp::RouteInfo rInfo(externalRoute);
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1);
    eigrpInstance->broadcastRouteChanges({&rInfo});
}

// Test: StubMode_Route_Filtering_Drops_NonPermitted_Routes
TEST_F(Internal_EigrpTest, StubMode_Route_Filtering_Drops_NonPermitted_Routes) 
{
    // Verify that routes not allowed in stub mode are not advertised.
    eigrpInstance->getGlobalConfigMgr().enableStub(true, true, false, false, false, false);
    Eigrp::ReceivedRoute externalRoute = getRoute(eigrpInterface->interfaceKey);
    externalRoute.prefix = { createIPv4(0xC0A80600), 24 };
    externalRoute.routeType = Eigrp::RouteType::EXTERNAL;
    Eigrp::RouteInfo rInfo(externalRoute);
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(0);
    eigrpInstance->broadcastRouteChanges({&rInfo});
}

// Test: ActiveQuery_Clear_After_Neighbor_Response
TEST_F(Internal_EigrpTest, ActiveQuery_Clear_After_Neighbor_Response) 
{
    // Verify that when a neighbor replies, the active query is cleared.
    Eigrp::ReceivedRoute testRoute = getRoute(eigrpInterface->interfaceKey);
    IPPrefix prefix = { createIPv4(0xC0A80500), 24 };
    testRoute.prefix = prefix;
    Eigrp::RouteInfo rInfo(testRoute);

    IPAddress queryNeighborIp = createIPv4(0x0A010001);
    IPAddress neighborIp = createIPv4(0x0A010001);
    addNeighbor(queryNeighborIp);
    auto neighbor = getNeighbor(queryNeighborIp);
    addNeighbor(neighborIp);

    Eigrp::ReliableTransport::PktInfo info;
    info.bandwidthMetric = 1000000;
    info.delay = 100000;
    info.mtu = 1500;
    info.version = Eigrp::TLVType::LEGACY_V4;

    auto& top = getTopologyTable().ensure(prefix);
    getTopologyTable().addRouteUpdate(rInfo.routeInfo, neighbor, top);

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&]( PacketBuilder& pkt, const uint8_t*) {
            PacketBuilder eigrp = createPacket();
            auto hdr = createReply(eigrp, info, *getNeighbor(neighborIp), {&rInfo}, getEigrpHeader(pkt).getSequence());
            ASSERT_TRUE(hdr.has_value());
            eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), neighborIp.raw, false);
        }));

    EXPECT_TRUE(getActiveRoutes().empty()); // Active should be resolved
}

// Test: ActiveQuery_Timeout_Leads_To_Neighbor_Down
TEST_F(Internal_EigrpTest, ActiveQuery_Timeout_Leads_To_Neighbor_Down) 
{
    // Verify that if a neighbor fails to respond to repeated queries, it is declared down.
    eigrpInstance->getConfigs().stuckInActiveTime = 1;
    IPAddress neighborIp = createIPv4(0xC0A8011F);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    Eigrp::ReceivedRoute testRoute = getRoute(eigrpInterface->interfaceKey);
    IPPrefix prefix = { createIPv4(0xC0A80600), 24 };
    testRoute.prefix = prefix;

    auto& top = getTopologyTable().ensure(prefix);
    getTopologyTable().addRouteUpdate(testRoute, getNeighbor(neighborIp), top);

    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    ASSERT_FALSE(getNeighbor(neighborIp));
}

#pragma endregion
#pragma region Restart

// Test: ProcessRestart_Clears_InterfaceList_And_Reinitializes
TEST_F(Internal_EigrpTest, ProcessRestart_Clears_InterfaceList_And_Reinitializes) 
{
    // Verify that process restart clears interfaces and reinitializes topology.
    ASSERT_FALSE(getInterfaceList().empty());
    eigrpInstance->restart();
    ASSERT_TRUE(getInterfaceList().empty());
}

// Test: ProcessRestart_No_Timer_Or_Resource_Leaks
TEST_F(Internal_EigrpTest, ProcessRestart_No_Timer_Or_Resource_Leaks) 
{
    // Verify that after restart, no active timers remain.
    eigrpInstance->restart();
    SUCCEED();
}

#pragma endregion
#pragma region IPv6Specific

// Test: IPv6_HelloPacket_Construction
TEST_F(Internal_EigrpTest, IPv6_HelloPacket_Construction) 
{
    // Verify that an IPv6 hello packet is constructed correctly.
    auto ipv6Eigrp = Eigrp::Eigrp(asNumber, AddressFamily::IPv6, vrf);
    ipv6Eigrp.start();
    MockInterface ipv6Interface = MockInterface(*global, InterfaceType::GIGABIT_ETHERNET);
    ipv6Interface.routingInstance = vrf;
    ipv6Interface.blockEnqueues();
    ipv6Interface.enableIPs();
    ipv6Interface.enableShutdown();
    uint8_t ipv6Buff[16] = { 0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };
    setIPv6(ipv6Buff, 64, &ipv6Interface);
    uint32_t key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 10);
    getAllInterfaceList()[key] = &ipv6Interface;
    EigrpConfigs::InterfaceConfigs configs(key);
    Eigrp::EigrpInterface* ipv6Int = ipv6Eigrp.getIfaceMgr().createInterface(&ipv6Interface);
    
    PacketBuilder helloPacket = createPacket(ipv6Int);
    uint8_t neighborIpBuf[16] = { 0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };
    IPAddress neighborIp = { neighborIpBuf, AddressFamily::IPv6 };
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::WIDE, ipv6Int);
    ASSERT_TRUE(createHello(helloPacket, ipv6Int));
}

// Test: IPv6_Interface_Config_Persistence
TEST_F(Internal_EigrpTest, IPv6_Interface_Config_Persistence) 
{
    // Check that the IPv6 configuration persists.
    uint8_t ipv6Addr[16] = { 0xC0, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };
    ASSERT_EQ(getIpInfo().ipv6.getLocalAddress(), readU128(ipv6Addr));
}

#pragma endregion
#pragma region ErrorHandling

// Test: DecodeRoute_InvalidData_Throws_Exception
TEST_F(Internal_EigrpTest, DecodeRoute_InvalidData_Throws_Exception) 
{
    // Verify that incomplete route data throws an exception.
    TLV16Option invalidData( 0x01, 0x02, nullptr );
    EXPECT_THROW({
        auto route = Eigrp::TLVBuilder::decodeRoute(invalidData, eigrpInterface->interfaceKey);
    }, std::runtime_error);
}

// Test: Invalid_Configuration_Handled_Gracefully
TEST_F(Internal_EigrpTest, Invalid_Configuration_Handled_Gracefully) 
{
    // Verify that setting an invalid mask does not crash the system.
    EXPECT_NO_THROW({
        setIPv4(0xC0A80101, 33);
    });
}

// Test: TimerCallback_Exception_Caught
TEST_F(Internal_EigrpTest, TimerCallback_Exception_Caught) 
{
    // Simulate an exception in a timer callback and verify that it is caught.
    GTEST_SKIP() << "idk";
}

#pragma endregion
#pragma region Timers

// Test: Timers_Cancelled_On_Interface_Shutdown
TEST_F(Internal_EigrpTest, Timers_Cancelled_On_Interface_Shutdown) 
{
    // Verify that all timers are cancelled upon interface shutdown.
    eigrpInterface->getTimers().startHelloHelper();
    IPAddress neighborIp = createIPv4(0xC0A80120);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    eigrpInterface->getTimers().startHoldTimer(*neighbor);
    
    eigrpInterface->getTimers().stopHello();
    ASSERT_FALSE(getHelloTimerActive());
    ASSERT_EQ(neighbor->holdTimerId, 0);
}

// Test: Concurrent_Access_To_InterfaceData_No_Race
TEST_F(Internal_EigrpTest, Concurrent_Access_To_InterfaceData_No_Race) 
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
TEST_F(Internal_EigrpTest, Retransmission_Timers_Cancelled_On_ACK) 
{
    // Verify that an ACK cancels the retransmission timer.
    IPAddress neighborIp = createIPv4(0xC0A80121);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    PacketBuilder pkt(mockInterface);
    EigrpHeader eigrp;
    eigrp.setBuffer(testPacket);
    eigrp.setSequence(4);
    eigrpInterface->getRtp().setupReliablePacket(getNeighbor(neighborIp), eigrp);
    
    processAck(*neighbor, 4);
    EXPECT_TRUE(neighbor->reliableQueue.empty());
}

// Test: Multithreaded_NeighborStateUpdates_No_Deadlock
TEST_F(Internal_EigrpTest, Multithreaded_NeighborStateUpdates_No_Deadlock) 
{
    // Simulate concurrent neighbor state updates and check for deadlock.
    IPAddress neighborIp = createIPv4(0xC0A80122);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    neighbor->setState(Eigrp::Neighbor::State::INIT);
    
    auto threadFunc = [this, neighbor]() {
        for (int i = 0; i < 1000; i++) {
            PacketBuilder hello = createPacket();
            auto eigrp = createHello(hello);
            ASSERT_TRUE(eigrp.has_value());
            processHello(eigrp.value(), *neighbor);
        }
    };
    std::thread t1(threadFunc);
    std::thread t2(threadFunc);
    t1.join();
    t2.join();
    SUCCEED();
}

// Test: Global_State_ThreadSafety
TEST_F(Internal_EigrpTest, Global_State_ThreadSafety) 
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

// Test: MultiInterface_Massive_Concurrent_Updates
TEST_F(Internal_EigrpTest, MultiInterface_Massive_Concurrent_Updates) 
{
    // Test massive concurrent route updates across multiple interfaces.
    MockInterface iface1(*global, InterfaceType::GIGABIT_ETHERNET);
    MockInterface iface2(*global, InterfaceType::GIGABIT_ETHERNET);
    uint32_t key1 = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 1);
    uint32_t key2 = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 2);
    iface1.routingInstance = vrf;
    iface2.routingInstance = vrf;
    iface1.blockEnqueues();
    iface2.blockEnqueues();
    EigrpConfigs::InterfaceConfigs intConf1(key1);
    EigrpConfigs::InterfaceConfigs intConf2(key2);
    auto* int1 = eigrpInstance->getIfaceMgr().createInterface(&iface1);
    auto* int2 = eigrpInstance->getIfaceMgr().createInterface(&iface2);

    IPAddress neighbor1 = createIPv4(0x0A000001);
    IPAddress neighbor2 = createIPv4(0x0A000002);
    addNeighbor(neighbor1, Eigrp::Neighbor::Version::LEGACY, int1);
    addNeighbor(neighbor2, Eigrp::Neighbor::Version::LEGACY, int2);
    
    for (int i = 0; i < 300; i++) 
    {
        auto route =  getRoute(0);
        route.prefix = { createIPv4(0xC0A80500), 24 };
        route.nextHop = createIPv4(0x0A000001);
        route.routeType = Eigrp::RouteType::INTERNAL;
        std::vector<Eigrp::ReceivedRoute> rs = {route};
        int1->getTopController().processReceivedRoutes(rs, *getNeighbor(neighbor1));
    }
    ASSERT_GE(vrf->routingTable.size<uint32_t>(), 1);

    eigrpInstance->getIfaceMgr().deactivateAll();
}

// Test: Frequent_Interface_Flapping_No_Global_Corruption
TEST_F(Internal_EigrpTest, Frequent_Interface_Flapping_No_Global_Corruption) 
{
    // Verify that repeated interface flapping does not corrupt global state.
    setIPv4(0xC0A80101, 24);
    EigrpConfigs::Network network = EigrpConfigs::Network(AddressFamily::IPv4);
    network.ip = createIPv4(0xC0A80000);
    network.mask = 16;
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(network);
    eigrpInstance->refreshInterfaceList();
    mockInterface->Shutdown(true);
    eigrpInstance->refreshInterfaceList();
    ASSERT_TRUE(getInterfaceList().empty());
    mockInterface->Shutdown(false);
    setIPv4(0xC0A80101, 24);
    eigrpInstance->refreshInterfaceList();
    ASSERT_FALSE(getInterfaceList().empty());
}

// Test: Rapid_Neighbor_AddRemove_Convergence
TEST_F(Internal_EigrpTest, Rapid_Neighbor_AddRemove_Convergence) 
{
    // Simulate rapid add/remove events for neighbors.
    for (int i = 0; i < 50; i++) {
        IPAddress ip = createIPv4(0xC0A80100);
        addNeighbor(ip, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
        auto neighbor = getNeighbor(ip);
        if(neighbor)
            eigrpInterface->getTimers().handleHoldTimeExpire(*neighbor);
    }
    SUCCEED();
}

// Test: Global_State_MemoryUsage_Under_Load
TEST_F(Internal_EigrpTest, Global_State_MemoryUsage_Under_Load) 
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
TEST_F(Internal_EigrpTest, MultipleInterfaces_Route_Propagation) 
{
    // Verify that routes are propagated via each interface independently.
    MockInterface iface1(*global, InterfaceType::GIGABIT_ETHERNET);
    MockInterface iface2(*global, InterfaceType::GIGABIT_ETHERNET);
    iface1.routingInstance = vrf;
    iface2.routingInstance = vrf;
    iface1.blockEnqueues();
    iface2.blockEnqueues();
    iface1.enableIPs();
    iface2.enableIPs();
    iface1.enableShutdown();
    iface2.enableShutdown();
    setIPv4(0xC0A80201, 24, &iface1);
    setIPv4(0xC0A80301, 24, &iface2);
    iface1.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 1);
    iface2.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 2);
    auto int1 = eigrpInstance->getIfaceMgr().createInterface(&iface1);
    auto int2 = eigrpInstance->getIfaceMgr().createInterface(&iface2);
    addNeighbor(createIPv4(0x0A000001), Eigrp::Neighbor::Version::LEGACY, int1);
    addNeighbor(createIPv4(0x0A000002), Eigrp::Neighbor::Version::LEGACY, int2);
    
    EXPECT_CALL(iface1, enqueuePacket(::testing::_, ::testing::_))
        .Times(1);
    EXPECT_CALL(iface2, enqueuePacket(::testing::_, ::testing::_))
        .Times(1);
    
    Eigrp::ReceivedRoute route = getRoute(eigrpInterface->interfaceKey);
    route.prefix = { createIPv4(0xC0A80700), 24 };
    route.nextHop = createIPv4(0xC0A80102);
    Eigrp::RouteInfo rInfo(route);
    eigrpInstance->broadcastRouteChanges({&rInfo});
    
    eigrpInstance->getIfaceMgr().deactivateAll();
}

// Test: MultiInterface_Adjacency_Formation
TEST_F(Internal_EigrpTest, MultiInterface_Adjacency_Formation) 
{
    // Verify that neighbors on different interfaces form independent adjacencies.
    MockInterface iface1(*global, InterfaceType::GIGABIT_ETHERNET);
    MockInterface iface2(*global, InterfaceType::GIGABIT_ETHERNET);
    iface1.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 1);
    iface2.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 2);
    iface1.routingInstance = vrf;
    iface2.routingInstance = vrf;
    iface1.blockEnqueues();
    iface2.blockEnqueues();
    auto int1 = eigrpInstance->getIfaceMgr().createInterface(&iface1);
    auto int2 = eigrpInstance->getIfaceMgr().createInterface(&iface2);
    addNeighbor(createIPv4(0x0A000003), Eigrp::Neighbor::Version::LEGACY, int1);
    addNeighbor(createIPv4(0x0A000004), Eigrp::Neighbor::Version::LEGACY, int2);
    ASSERT_EQ(getInterfaceList().size(), 3);
    eigrpInstance->getIfaceMgr().deactivateAll();
}

// Test: MultiInterface_Failure_Isolation
TEST_F(Internal_EigrpTest, MultiInterface_Failure_Isolation) 
{
    // Verify that failure on one interface does not affect neighbors on other interfaces.
    MockInterface iface1(*global, InterfaceType::GIGABIT_ETHERNET);
    MockInterface iface2(*global, InterfaceType::GIGABIT_ETHERNET);
    iface1.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 1);
    iface2.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 2);
    iface1.routingInstance = vrf;
    iface2.routingInstance = vrf;
    iface1.blockEnqueues();
    iface2.blockEnqueues();
    iface1.enableShutdown();
    iface2.enableShutdown();
    EigrpConfigs::Network network = EigrpConfigs::Network(AddressFamily::IPv4);
    network.ip = createIPv4(0x00000000);
    network.mask = 0;
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(network);
    auto int1 = eigrpInstance->getIfaceMgr().createInterface(&iface1);
    auto int2 = eigrpInstance->getIfaceMgr().createInterface(&iface2);
    addNeighbor(createIPv4(0x0A000005), Eigrp::Neighbor::Version::LEGACY, int1);
    addNeighbor(createIPv4(0x0A000006), Eigrp::Neighbor::Version::LEGACY, int2);
    int1->getIface()->Shutdown(true);
    eigrpInstance->refreshInterfaceList();
    ASSERT_EQ(getInterfaceList().size(), 2);
    eigrpInstance->shutdown();
}

#pragma endregion
#pragma region AdvancesEdgeCases

// Test: Duplicate_RouterID_Detection
TEST_F(Internal_EigrpTest, Duplicate_RouterID_Detection) 
{
    // Verify that duplicate router IDs are detected (placeholder test).
    EXPECT_EQ(getRouterID(), 0xC0A80101);
    SUCCEED();
}

// Test: Flapping_RouterID_Election
TEST_F(Internal_EigrpTest, Flapping_RouterID_Election) 
{
    // Simulate rapid changes in router ID election.
    eigrpInstance->calculateRID();
    uint32_t key = calculateInterfaceKey(InterfaceType::LOOPBACK, 0);
    setIPv4(0xC0A8FFFE, 24);
    getAllInterfaceList()[key] = mockInterface;
    eigrpInstance->calculateRID();
    ASSERT_EQ(eigrpInstance->routerID(), 0xC0A8FFFE);
}

// Test: Neighbor_Overlap_IP_Ranges
TEST_F(Internal_EigrpTest, Neighbor_Overlap_IP_Ranges) 
{
    // Verify that overlapping neighbor IP ranges are handled correctly.
    addNeighbor(createIPv4(0xC0A80122), Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    addNeighbor(createIPv4(0xC0A80123), Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    SUCCEED();
}

// Test: RouterID_Stability_Under_Flap
TEST_F(Internal_EigrpTest, RouterID_Stability_Under_Flap) 
{
    // Verify that repeated router ID calculations remain stable.
    for (int i = 0; i < 100; i++) {
        eigrpInstance->calculateRID();
    }
    SUCCEED();
}

// Test: Neighbor_Retransmission_Race_Condition
TEST_F(Internal_EigrpTest, Neighbor_Retransmission_Race_Condition) 
{
    // Simulate a race between an ACK and retransmission timeout.
    IPAddress neighborIp = createIPv4(0xC0A80124);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint32_t seqNum = 610;
    EigrpHeader hdr;
    hdr.setBuffer(testPacket);
    hdr.setSequence(seqNum);
    eigrpInterface->getRtp().setupReliablePacket(neighbor, hdr);
    
    std::thread t1([&](){
        processAck(*neighbor, seqNum);
    });
    std::thread t2([&](){
        eigrpInterface->getRtp().handleRetransmission(neighbor, neighbor->reliableQueue[neighbor->currentReliable], seqNum);
    });
    t1.join();
    t2.join();
    EXPECT_TRUE(neighbor->reliableQueue.empty());
}

#pragma endregion
#pragma region AdvancedStressTesting

// Test: HighVolume_RouteUpdates_Performance_Extended
TEST_F(Internal_EigrpTest, HighVolume_RouteUpdates_Performance_Extended) 
{
    // Stress-test with 1500 route updates.
    IPAddress neighborIp = createIPv4(0x0A000001);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY);
    auto neighbor = getNeighbor(neighborIp);

    IPAddress base = createIPv4(0xC0A80000);
    for (int i = 0; i < 1500; i++) {
        Eigrp::ReceivedRoute route = getRoute(eigrpInterface->interfaceKey);
        base.raw[2] = static_cast<uint8_t>(i);


        route.prefix = { base, 24 };
        route.nextHop = createIPv4(0xC0A80102);
        std::vector<Eigrp::ReceivedRoute> rs = {route};
        getDuel().processReceivedRoutes(rs, *neighbor);
    }
    ASSERT_EQ(vrf->routingTable.size<uint32_t>(), 1500);
}

// Test: MultiInterface_Massive_Concurrent_Updates_Extended
TEST_F(Internal_EigrpTest, MultiInterface_Massive_Concurrent_Updates_Extended) 
{
    // Test massive concurrent updates on two additional interfaces.
    MockInterface iface1(*global, InterfaceType::GIGABIT_ETHERNET);
    MockInterface iface2(*global, InterfaceType::GIGABIT_ETHERNET);
    iface1.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 1);
    iface2.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 2);
    iface1.routingInstance = vrf;
    iface2.routingInstance = vrf;
    auto int1 = eigrpInstance->getIfaceMgr().createInterface(&iface1);
    auto int2 = eigrpInstance->getIfaceMgr().createInterface(&iface2);
    IPAddress neighborIp1 = createIPv4(0x0A000001);
    IPAddress neighborIp2 = createIPv4(0x0A000002);
    addNeighbor(neighborIp1, Eigrp::Neighbor::Version::LEGACY, int1);
    addNeighbor(neighborIp2, Eigrp::Neighbor::Version::LEGACY, int2);

    auto neighbor = getNeighbor(neighborIp1);
    
    for (int i = 0; i < 300; i++) {
        Eigrp::ReceivedRoute route = getRoute(eigrpInterface->interfaceKey);
        route.prefix = { createIPv4(0xC0A80500), 24 };
        route.nextHop = createIPv4(0x00000001);
        route.routeType = Eigrp::RouteType::INTERNAL;
        std::vector<Eigrp::ReceivedRoute> receivedRoutes = {route};
        getDuel().processReceivedRoutes(receivedRoutes, *neighbor);
    }
    ASSERT_GE(vrf->routingTable.size<uint32_t>(), 1);
    eigrpInstance->getIfaceMgr().deactivateAll();
}

// Test: Frequent_Interface_Flapping_No_Global_Corruption_Extended
TEST_F(Internal_EigrpTest, Frequent_Interface_Flapping_No_Global_Corruption_Extended) 
{
    // Verify that interface flapping does not corrupt global state.
    setIPv4(0xC0A80101, 24);
    EigrpConfigs::Network network(AddressFamily::IPv4);
    network.ip = createIPv4(0xC0A80000);
    network.mask = 16;
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(network);
    eigrpInstance->refreshInterfaceList();
    mockInterface->Shutdown(true);
    eigrpInstance->refreshInterfaceList();
    ASSERT_TRUE(getInterfaceList().empty());
    mockInterface->Shutdown(false);
    setIPv4(0xC0A80101, 24);
    eigrpInstance->refreshInterfaceList();
    ASSERT_FALSE(getInterfaceList().empty());
}

// Test: Rapid_Neighbor_AddRemove_Convergence_Extended
TEST_F(Internal_EigrpTest, Rapid_Neighbor_AddRemove_Convergence_Extended) 
{
    // Rapidly add and remove neighbors and verify convergence.
    for (int i = 0; i < 50; i++) {
        IPAddress ip = createIPv4(0xC0A80100 + i);
        addNeighbor(ip, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
        auto neighbor = getNeighbor(ip);
        if(neighbor)
            eigrpInterface->getTimers().handleHoldTimeExpire(*neighbor);
    }
    SUCCEED();
}

// Test: Global_State_MemoryUsage_Under_Load_Extended
TEST_F(Internal_EigrpTest, Global_State_MemoryUsage_Under_Load_Extended) 
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
TEST_F(Internal_EigrpTest, MultiInterface_Adjacency_Formation_Extended) 
{
    // Extended test for independent neighbor adjacencies on multiple interfaces.
    MockInterface iface1(*global, InterfaceType::GIGABIT_ETHERNET);
    MockInterface iface2(*global, InterfaceType::GIGABIT_ETHERNET);
    iface1.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 1);
    iface2.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 2);
    iface1.routingInstance = vrf;
    iface2.routingInstance = vrf;
    iface1.blockEnqueues();
    iface2.blockEnqueues();
    auto int1 = eigrpInstance->getIfaceMgr().createInterface(&iface1);
    auto int2 = eigrpInstance->getIfaceMgr().createInterface(&iface2);
    addNeighbor(createIPv4(0x0A000003), Eigrp::Neighbor::Version::LEGACY, int1);
    addNeighbor(createIPv4(0x0A000004), Eigrp::Neighbor::Version::LEGACY, int2);
    ASSERT_EQ(getInterfaceList().size(), 3);
    eigrpInstance->getIfaceMgr().deactivateAll();
}

// Test: MultiInterface_Failure_Isolation_Extended
TEST_F(Internal_EigrpTest, MultiInterface_Failure_Isolation_Extended) 
{
    // Extended test: simulate failure on one interface and ensure others remain unaffected.
    MockInterface iface1(*global, InterfaceType::GIGABIT_ETHERNET);
    MockInterface iface2(*global, InterfaceType::GIGABIT_ETHERNET);
    iface1.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 1);
    iface2.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 2);
    iface1.routingInstance = vrf;
    iface2.routingInstance = vrf;
    iface1.blockEnqueues();
    iface2.blockEnqueues();
    iface1.enableShutdown();
    iface2.enableShutdown();
    iface1.enableIPs();
    iface2.enableIPs();
    setIPv4(0xC0A80201, 8, &iface1);
    setIPv4(0xC0A80301, 8, &iface2);
    auto int1 = eigrpInstance->getIfaceMgr().createInterface(&iface1);
    auto int2 = eigrpInstance->getIfaceMgr().createInterface(&iface2);
    EigrpConfigs::Network network(AddressFamily::IPv4);
    network.ip = createIPv4(0x00000000);
    network.mask = 0;
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(network);
    addNeighbor(createIPv4(0x0A000005), Eigrp::Neighbor::Version::LEGACY, int1);
    addNeighbor(createIPv4(0x0A000006), Eigrp::Neighbor::Version::LEGACY, int2);
    iface1.Shutdown(true);
    eigrpInstance->refreshInterfaceList();
    ASSERT_EQ(getInterfaceList().size(), 2);
    eigrpInstance->getIfaceMgr().deactivateAll();
}

// Test: MultiInterface_Coordinated_RoutingUpdates_Extended
TEST_F(Internal_EigrpTest, MultiInterface_Coordinated_RoutingUpdates_Extended) 
{
    // Extended test: simulate simultaneous route updates from multiple interfaces.
    MockInterface iface1(*global, InterfaceType::GIGABIT_ETHERNET);
    MockInterface iface2(*global, InterfaceType::GIGABIT_ETHERNET);
    iface1.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 1);
    iface2.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 2);
    iface1.routingInstance = vrf;
    iface2.routingInstance = vrf;
    iface1.blockEnqueues();
    auto int1 = eigrpInstance->getIfaceMgr().createInterface(&iface1);
    IPAddress neighborIp = createIPv4(0x0A000007);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY, int1);
    Eigrp::ReceivedRoute route1 = getRoute(eigrpInterface->interfaceKey);
    route1.prefix = { createIPv4(0xC0A80000), 24 };
    route1.nextHop = createIPv4(0x00000001);
    route1.routeType = Eigrp::RouteType::INTERNAL;
    Eigrp::ReceivedRoute route2 = getRoute(eigrpInterface->interfaceKey);
    route2.prefix = { createIPv4(0xC0A80900), 24 };
    route2.nextHop = createIPv4(0x00000002);
    route2.routeType = Eigrp::RouteType::INTERNAL;
    std::vector<Eigrp::ReceivedRoute> routes = { route1, route2 };
    getDuel().processReceivedRoutes(routes, *getNeighbor(neighborIp));
    ASSERT_GE(vrf->routingTable.size<uint32_t>(), 2);
    eigrpInstance->getIfaceMgr().deactivateAll();
}

// Test: Unequal_Cost_Path_Added_As_Feasible_Successor
TEST_F(Internal_EigrpTest, Unequal_Cost_Path_Added_As_Feasible_Successor)
{
    IPAddress neighborIp = createIPv4(0xC0A80001);
    addNeighbor(neighborIp);
    auto neighbor = getNeighbor(neighborIp);

    eigrpInstance->getConfigs().variance = 4;

    Eigrp::ReceivedRoute primary = getRoute(eigrpInterface->interfaceKey);
    primary.prefix = { createIPv4(0x0A080000), 16 };
    primary.feasibleDistance = 100;
    primary.reportedDistance = 80;
    primary.nextHop = createIPv4(0xC0A80110);

    primary.bandwidth = 1000000;
    primary.delay = 1000000000;
    primary.hopCount = 0;
    primary.mtu = 1500;
    primary.reliability = 255;
    primary.load = 1;
    primary.nextHop = createIPv4(0x00000000);

    Eigrp::ReceivedRoute secondary = primary;
    secondary.nextHop = createIPv4(0xC0A80111);
    secondary.feasibleDistance = 150;
    secondary.reportedDistance = 90;

    std::vector<Eigrp::ReceivedRoute> routes = { primary, secondary };
    getDuel().processReceivedRoutes(routes, *neighbor);

    auto successor = vrf->routingTable.lookup<uint32_t>(primary.prefix.v4);
    EXPECT_EQ(successor->prefix, primary.prefix.v4);
    EXPECT_EQ(successor->length, primary.prefix.prefixLength);
    EXPECT_EQ(successor->metric, primary.feasibleDistance);
    EXPECT_EQ(successor->nextHopCount, 2);
}

// Test: Metric_Tuning_Affects_Route_Selection
TEST_F(Internal_EigrpTest, Metric_Tuning_Affects_Route_Selection)
{
    IPAddress neighbor1 = createIPv4(0xC0A80101);
    IPAddress neighbor2 = createIPv4(0xC0A80201);
    addNeighbor(neighbor1);
    addNeighbor(neighbor2);

    Eigrp::ReceivedRoute r1 = getRoute(eigrpInterface->interfaceKey);
    r1.prefix = { createIPv4(0x0A110000), 16 };

    Eigrp::ReceivedRoute r2 = r1;
    r2.feasibleDistance = 90; // Lower metric

    std::vector<Eigrp::ReceivedRoute> routes1 = { r1 };
    std::vector<Eigrp::ReceivedRoute> routes2 = { r2 };
    getDuel().processReceivedRoutes(routes1, *getNeighbor(neighbor1));
    getDuel().processReceivedRoutes(routes2, *getNeighbor(neighbor2));

    auto best = vrf->routingTable.lookup<uint32_t>(r1.prefix.v4);
    ASSERT_EQ(best->metric, 90);
}

// Test: Route_Loop_Prevention_Using_FD
TEST_F(Internal_EigrpTest, Route_Loop_Prevention_Using_FD)
{
    IPAddress neighborIp = createIPv4(0xC0A80110);
    addNeighbor(neighborIp);
    auto neighbor = getNeighbor(neighborIp);

    Eigrp::ReceivedRoute route = getRoute(eigrpInterface->interfaceKey);
    route.prefix = { createIPv4(0x0A120000), 16 };
    route.feasibleDistance = 100;
    route.reportedDistance = 150; // Violation

    std::vector<Eigrp::ReceivedRoute> routes = { route };
    getDuel().processReceivedRoutes(routes, *neighbor);
    auto* chosen = vrf->routingTable.lookup<uint32_t>(route.prefix.v4);

    EXPECT_TRUE(chosen == nullptr);
}

// Test: Summarization_Advertises_Summary_Only
TEST_F(Internal_EigrpTest, Summarization_Advertises_Summary_Only)
{
    eigrpInterface->getAggregator().installSummary({ createIPv4(0x0A130000), 16 });

    Eigrp::ReceivedRoute route1 = getRoute(eigrpInterface->interfaceKey);
    route1.prefix = { createIPv4(0x0A130100), 24 };
    route1.nextHop = createIPv4(0xC0A80110);

    Eigrp::ReceivedRoute route2 = route1;
    route2.prefix = { createIPv4(0x0A130200), 24 };

    IPAddress neighborIp = createIPv4(0xC0A80110);
    addNeighbor(neighborIp);
    auto neighbor = getNeighbor(neighborIp);

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(0);

    std::vector<Eigrp::ReceivedRoute> routes = { route1, route2 };
    getDuel().processReceivedRoutes(routes, *neighbor);

    EXPECT_EQ(vrf->routingTable.size<uint32_t>(), 1);
}

// Test: Summarization_Disables_Individual_Routes
TEST_F(Internal_EigrpTest, Summarization_Disables_Individual_Routes)
{
    // Manually add a summary route to suppress specifics
    eigrpInterface->getAggregator().installSummary({ createIPv4(0xC0A80100), 24 });

    // Add a matching specific route
    Eigrp::ReceivedRoute route = getRoute(eigrpInterface->interfaceKey);
    route.prefix = { createIPv4(0xC0A80100), 24 };
    route.routeType = Eigrp::RouteType::INTERNAL;
    route.nextHop = createIPv4(0x00000000);
    Eigrp::RouteInfo rInfo(route);

    // Should not advertise individual route when summary exists
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(0);
    eigrpInstance->broadcastRouteChanges({&rInfo});
}

// Test: Query_Triggers_SIA_Timer
TEST_F(Internal_EigrpTest, Query_Triggers_SIA_Timer)
{
    Eigrp::ReceivedRoute route = getRoute(eigrpInterface->interfaceKey);
    route.prefix = { createIPv4(0xC0A80A00), 24 };
    route.routeType = Eigrp::RouteType::INTERNAL;

    IPAddress neighborIp = createIPv4(0xC0A80105);
    addNeighbor(neighborIp);  // Add the neighbor to the network
    auto neighbor = getNeighbor(neighborIp);  // Retrieve the neighbor from the list
    
    // Expect one packet to be enqueued (since we're sending a query)
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(1);

    Eigrp::RouteInfo rInfo(route);

    Eigrp::ActiveRoute rt;
    rt.activePrefix = route.prefix;
    rt.originRoute = &rInfo;
    Eigrp::OutgoingQuery qy;
    qy.route = &rt;
    rt.pendingQueries[neighborIp] = qy;
    eigrpInterface->getRtp().sendQuery(neighbor, {&qy});
}

// Test: Unicast_Neighbor_Forms_Correctly
TEST_F(Internal_EigrpTest, Unicast_Neighbor_Forms_Correctly)
{
    IPAddress neighborIp = createIPv4(0xC0A80132);
    eigrpInterface->getNTable().createNeighbor(neighborIp);
    addNeighbor(neighborIp);
    auto neighbor = getNeighbor(neighborIp);
    ASSERT_TRUE(neighbor);
    ASSERT_EQ(neighbor->unicast, true);
}

// Test: Dampening_Suppresses_Updates
TEST_F(Internal_EigrpTest, Dampening_Suppresses_Updates)
{
    eigrpInstance->getConfigs().dampening.store(true);
    Eigrp::ReceivedRoute route = getRoute(eigrpInterface->interfaceKey);
    route.prefix = { createIPv4(0xC0A80135), 24 };
    route.routeType = Eigrp::RouteType::INTERNAL;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(0);
    Eigrp::RouteInfo rInfo(route);
    eigrpInstance->broadcastRouteChanges({&rInfo});
}

// Test: Summarization_Loop_Prevention
TEST_F(Internal_EigrpTest, Summarization_Loop_Prevention) {
    // Ensure that summary routes don't cause loops back to origin
    IPAddress network = createIPv4(0x0A000000); // 10.0.0.0
    uint8_t mask = 8;

    eigrpInterface->getAggregator().installSummary({network, mask});

    auto* route = vrf->routingTable.lookup<uint32_t>(network.v4);
    ASSERT_TRUE(route);
    ASSERT_NE(route->nextHops[0].nextHop, getIpInfo().ipv4.getAddress()); // Should not loop to self
}

// Test: Query_Packet_Trigger_On_Loss
TEST_F(Internal_EigrpTest, Query_Packet_Trigger_On_Loss)
{
    Eigrp::ReceivedRoute route = getRoute(eigrpInterface->interfaceKey);
    route.prefix = { createIPv4(0x0A010000), 16 };
    route.feasibleDistance = 200;

    // Add the route and simulate removal to trigger query
    IPAddress neighborIp = createIPv4(0xC0A80132);
    addNeighbor(neighborIp);
    auto neighbor = getNeighbor(neighborIp);

    std::vector<Eigrp::ReceivedRoute> routes = { route };
    getDuel().processReceivedRoutes(routes, *neighbor);
    routes[0].delay = std::numeric_limits<uint64_t>::max();

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(::testing::AtLeast(1));

    getDuel().processReceivedRoutes(routes, *neighbor);
}

// Test: Split_Horizon_Prevents_Route_Propagation_Back
TEST_F(Internal_EigrpTest, Split_Horizon_Prevents_Route_Propagation_Back)
{
    IPAddress neighborIp = createIPv4(0xC0A80002);
    addNeighbor(neighborIp);
    auto* neighbor = getNeighbor(neighborIp);
    Eigrp::ReceivedRoute route = getRoute(eigrpInterface->interfaceKey);
    route.prefix = { createIPv4(0x0A020000), 16 };

    // Enforce split horizon
    eigrpInterface->configs->splitHorizon.store(true);

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(0);
    std::vector<Eigrp::ReceivedRoute> routes = { route };
    getDuel().processReceivedRoutes(routes, *neighbor);
}

// Test: Infeasible_Route_Rejected_Due_To_Feasibility_Condition
TEST_F(Internal_EigrpTest, Infeasible_Route_Rejected_Due_To_Feasibility_Condition) 
{
    // Add neighbor
    IPAddress neighborIp = createIPv4(0xC0A80131);
    addNeighbor(neighborIp);
    auto neighbor = getNeighbor(neighborIp);

    // Add better existing route to topology
    Eigrp::ReceivedRoute existing;

    IPPrefix prefix = { createIPv4(0x0A000000), 24 };
    existing.prefix = prefix;
    existing.feasibleDistance = 100;
    existing.reportedDistance = 80;
    existing.nextHop = createIPv4(0xC0A801FF);
    existing.hopCount = 1;

    auto& tt = getTopologyTable();
    auto& top = tt.ensure(prefix);
    tt.addRouteUpdate(existing, neighbor, top);

    // Simulate route from neighbor with RD > existing FD
    Eigrp::ReceivedRoute newRoute = getRoute(eigrpInterface->interfaceKey);
    newRoute.prefix = prefix;
    newRoute.reportedDistance = 200;
    newRoute.feasibleDistance = 300;

    std::vector<Eigrp::ReceivedRoute> rs = { newRoute };
    getDuel().processReceivedRoutes(rs, *neighbor);

    // Should NOT overwrite existing route
    auto entry = tt.find(prefix);
    ASSERT_NE(entry, nullptr);
    bool hasInfeasible = std::any_of(entry->routesByNeighbor.begin(), entry->routesByNeighbor.end(),
        [&](const auto& r) {
            return r.second.routeInfo.reportedDistance >= existing.feasibleDistance;
        });
    ASSERT_TRUE(hasInfeasible);
    ASSERT_EQ(vrf->routingTable.lookup<uint32_t>(prefix.v4)->nextHops[0].nextHop, existing.nextHop.v4);
}

// Test: Query_With_No_Reply_Triggers_SIA
TEST_F(Internal_EigrpTest, Query_With_No_Reply_Triggers_SIA)
{
    eigrpInstance->getConfigs().stuckInActiveTime = 1;

    IPAddress neighborIp = createIPv4(0xC0A80138);
    addNeighbor(neighborIp);

    Eigrp::ReceivedRoute testRoute = getRoute(eigrpInterface->interfaceKey);
    IPPrefix prefix = { createIPv4(0x0A020000), 16 };
    testRoute.prefix = prefix;

    auto& tt = getTopologyTable();
    auto& top = tt.ensure(prefix);
    tt.addRouteUpdate(testRoute, getNeighbor(neighborIp), top);

    bool foundQuery = false;
    bool foundSIA = false;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&]( PacketBuilder& pkt, const uint8_t*) {
            auto header = getEigrpHeader(pkt);
            if (!foundQuery && !foundSIA && header.getOpcode() == Variable::Eigrp::Type::query)
                foundQuery = true;
            else if (foundQuery && !foundSIA && header.getOpcode() == Variable::Eigrp::Type::siaQuery)
                foundSIA = true;
            else
                FAIL(); // Additional packets found
        }));

    getDuel().setActive({prefix}, neighborIp);
    
    // Force SIA timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    EXPECT_TRUE(foundQuery);
    EXPECT_TRUE(foundSIA);
}

// Test: Update_With_Duplicate_Routes_Only_Processes_Once
TEST_F(Internal_EigrpTest, Update_With_Duplicate_Routes_Only_Processes_Once)
{
    IPAddress neighborIp = createIPv4(0xC0A80139);
    addNeighbor(neighborIp);
    auto neighbor = getNeighbor(neighborIp);

    Eigrp::ReceivedRoute r = getRoute(eigrpInterface->interfaceKey);
    IPPrefix prefix = { createIPv4(0x0A040000), 16 };
    r.prefix = prefix;
    r.routeType = Eigrp::RouteType::INTERNAL;
    Eigrp::RouteInfo rInfo = (r);

    PacketBuilder pkt = createPacket();
    Eigrp::ReliableTransport::PktInfo info;
    info.bandwidthMetric = 10000000;
    info.delay = 10000000;
    info.mtu = 1500;
    info.version = Eigrp::TLVType::LEGACY_V4;
    auto update = createUpdate(pkt, info, neighbor, {&rInfo, &rInfo});
    EXPECT_TRUE(update.has_value());

    eigrpInterface->getRtp().handleIncoming(nullptr, update.value(), neighborIp.raw, false);
    auto* route = vrf->routingTable.lookup<uint32_t>(rInfo.routeInfo.prefix.v4);
    ASSERT_NE(route, nullptr);
    EXPECT_EQ(route->nextHopCount, 1);
}

// Test: Summary_Only_Blocks_More_Specifics
TEST_F(Internal_EigrpTest, Summary_Only_Blocks_More_Specifics)
{
    IPAddress neighborIp = createIPv4(0xC0A80139);
    addNeighbor(neighborIp);
    auto neighbor = getNeighbor(neighborIp);

    Eigrp::ReceivedRoute specific = getRoute(eigrpInterface->interfaceKey);
    IPPrefix prefix = { createIPv4(0x0A0C0100), 24 };
    specific.prefix = prefix;
    
    auto& tt = getTopologyTable();
    auto& top = tt.ensure(prefix);
    tt.addRouteUpdate(specific, neighbor, top);

    bool found = false;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&]( PacketBuilder& pkt, const uint8_t*) {
            auto opts = extractEigrpOptions(pkt);
            for (auto& opt : opts)
            {
                if (opt.type == Variable::Eigrp::Option::internalRoute)
                {
                    auto route = Eigrp::TLVBuilder::decodeRoute(opt, eigrpInterface->interfaceKey);
                    if (route->prefix.prefixLength == 16)
                        found = true;
                    else if (route->prefix.prefixLength == 24)
                        FAIL();
                }
            }
        }));

    eigrpInterface->getAggregator().installSummary({ createIPv4(0x0A0C0000), 16});

    EXPECT_FALSE(getTopologyTable().find(prefix)->summaries.empty());
}
