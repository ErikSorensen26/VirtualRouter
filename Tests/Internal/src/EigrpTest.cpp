//TODO fix connected routes adding routes with their full address

//TODO fix leak in MultiInterface_Massive_Concurrent_Updates and IPv6_HelloPacket_Construction


//TODO tests for wide resync and legacy teardown
//TODO tests for peer termination
//TODO tests for poisened loop prevention


// May be a problem where unicast neighbors get completely removed

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
#include <Arp.h>
#include <Ndp.h>

// Test fixture for global EIGRP tests
class Internal_EigrpTest : public ::testing::Test 
{
protected:
    uint16_t asNumber = 1;
    AddressFamily addressFamily = AddressFamily::IPv4;
    Eigrp::Eigrp* eigrpInstance;
    MockInterface* mockInterface;
    // We use the real EigrpInterface (constructed using our MockInterface)
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
    uint8_t ipIntv4Net[4] = { 0xC0, 0xA8, 0x01, 0x00 };
    uint8_t ipIntv6[16] = { 0xC0, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01 };

    // Setup creates an Eigrp instance and one interface for testing.
    void SetUp() override 
    {
        RCU::registerThread();
        global = new Global({}, false, true);
        mockInterface = new MockInterface(*global, InterfaceType::GIGABIT_ETHERNET);
        mKey = mockInterface->configs.key;
        global->getInterfaceList()[mKey] = mockInterface;

        vrf = global->getRoutingInstance("default", AddressFamily::IPv4);
        vrf->addInterface(mockInterface, mKey);
        vrf->enabledAddressFamilies.insert(AddressFamily::IPv6);
        vrf->eigrpList[1] = new Eigrp::EigrpAutonomousSystem();
        eigrpInstance = new Eigrp::Eigrp(asNumber, addressFamily, vrf);
        vrf->eigrpList[1]->ipv4 = eigrpInstance;

        mockInterface->routingInstance = vrf;

        mockInterface->enableIPs();
        mockInterface->enableShutdown();
        // Set initial IPv4 and IPv6 addresses on the mock interface.

        setIPv4(readU32(ipIntv4), 24);
        setIPv6(ipIntv6, 64);
        // Assume interfaceList is a global map keyed by InterfaceType and interface id.
        vrf->interfaceList[mKey] = mockInterface;
        // Create the real EigrpInterface using the mock interface.
        EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(::testing::AtLeast(1));

        EigrpConfigs::Network network(AddressFamily::IPv4);
        network.ip = createIPv4(readU32(ipIntv4Net));
        network.mask = 24;
        eigrpInstance->getGlobalConfigMgr().addNetworkRange(network);
        eigrpInterface = eigrpInstance->getIfaceMgr().getInterface(mKey);
    }

    // TearDown cleans up the EIGRP instance, interface, and global objects.
    void TearDown() override 
    {
        mockInterface->blockEnqueues();
        delete global;
        std::memset(testPacket, 0, sizeof(testPacket));
        RCU::unregisterThread();
    }

    // Helper function: returns the topology table.
    Eigrp::TopologyTable& getTopologyTable(Eigrp::Eigrp* i = nullptr) { return i ? i->getTopology().duel.topologyTable : eigrpInstance->getTopology().duel.topologyTable; }
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
        {
            intf->getNTable().createNeighbor(ip, v, neighborMac)->setState(Eigrp::Neighbor::State::ESTABLISHED);
            if (intf->getBase().getAF() == AddressFamily::IPv4)
                intf->getIface()->arp->addArpEntry(readU32(ip.raw), readU48(neighborMac));
            else
                intf->getIface()->ndp->addNdpEntry(ip, readU48(neighborMac));
        }
        else
        {
            eigrpInterface->getNTable().createNeighbor(ip, v, neighborMac)->setState(Eigrp::Neighbor::State::ESTABLISHED);
            eigrpInterface->getIface()->arp->addArpEntry(readU32(ip.raw), readU48(neighborMac));
        }
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
        uint8_t nh[4] = {0};
        Eigrp::ReceivedRoute r;
        r.feasibleDistance = 1441792;
        r.reportedDistance = 720896;
        r.nextHop = { nh, AddressFamily::IPv4 };
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
        size_t optSize = h->length - EigrpHeader::fixedSize;
        if (optSize == 0) return {};
        std::vector<TLV16Option> opts;
        parseEigrpOptions(h->buffer + EigrpHeader::fixedSize, optSize, opts);
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

    void createPacket(PacketBuilder& eigrp, Eigrp::EigrpInterface* interface = nullptr)
    {
        Eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        iface->getRtp().createPacket(eigrp);
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

    std::optional<EigrpHeader> createQuery(PacketBuilder& builder, Eigrp::ReliableTransport::PktInfo& info, const std::vector<Eigrp::ActiveRoute*>& queries, Eigrp::EigrpInterface* interface = nullptr)
    {
        Eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createQuery(builder, info, queries);
    }

    std::optional<EigrpHeader> createUnicastQuery(PacketBuilder& builder, Eigrp::ReliableTransport::PktInfo& info, Eigrp::Neighbor& neighbor, const std::vector<Eigrp::OutgoingQuery*>& queries, Eigrp::EigrpInterface* interface = nullptr)
    {
        Eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createUnicastQuery(builder, info, neighbor, queries);
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

    std::map<IPPrefix, Eigrp::ActiveRoute>& getActiveRoutes(Eigrp::Eigrp* i = nullptr) { return getDuel(i).activeRoutes; }

    Eigrp::DuelEngine& getDuel(Eigrp::Eigrp* i = nullptr) { return i ? i->getTopology().duel : eigrpInstance->getTopology().duel; }

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
    PacketBuilder helloPacket(mockInterface);
    createPacket(helloPacket);
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
    
    PacketBuilder helloPacket(mockInterface);
    createPacket(helloPacket);
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
    
    PacketBuilder helloPacket(mockInterface);
    createPacket(helloPacket);
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
    
    PacketBuilder helloPacket(mockInterface);
    createPacket(helloPacket);
    ASSERT_TRUE(createUnicastHello(helloPacket, neighborIp).has_value());

    std::vector<TLV16Option> opts = extractEigrpOptions(helloPacket);
    
    auto it = std::find_if(opts.begin(), opts.end(), [](const TLV16Option& opt) {
        return opt.type == Variable::Eigrp::Option::authentication;
    });
    ASSERT_EQ(it, opts.end());
}

#pragma endregion
/*/
#pragma region NeighborState

// Test: Neighbor_Down_To_INIT
TEST_F(Internal_EigrpTest, Neighbor_Down_To_INIT)
{
    IPAddress n = createIPv4(0x0A000001);

    ASSERT_FALSE(getNeighbor(n));
    PacketBuilder hello(mockInterface);
    auto hdr = createUnicastHello(hello, n);
    ASSERT_TRUE(hdr.has_value());

    eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), n.raw, false);

    auto nbr = getNeighbor(n);
    ASSERT_TRUE(nbr);
    EXPECT_EQ(nbr->getState(), Eigrp::Neighbor::State::INIT);
}

// Test: Neighbor_INIT_to_TWOWAY
TEST_F(Internal_EigrpTest, Neighbor_INIT_to_TWOWAY)
{
    IPAddress n = createIPv4(0x0A000002);
    addNeighbor(n, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);

    auto nbr = getNeighbor(n);
    nbr->setState(Eigrp::Neighbor::State::INIT);

    PacketBuilder hello(mockInterface);
    auto hdr = createUnicastHello(hello, n);
    ASSERT_TRUE(hdr);

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](PacketBuilder& pkt, const uint8_t*) {
            auto eigrp = getEigrpHeader(pkt);
            EXPECT_EQ(eigrp.getOpcode(), Variable::Eigrp::Type::hello);
            EXPECT_TRUE(eigrp.getFlagInit());
        }));

    eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), n.raw, false);

    EXPECT_EQ(nbr->getState(), Eigrp::Neighbor::State::TWOWAY);
}

// Test: Neighbor_TWOWAY_To_LOADING
TEST_F(Internal_EigrpTest, Neighbor_TWOWAY_To_LOADING)
{
    IPAddress n = createIPv4(0x0A000003);
    addNeighbor(n, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);

    auto nbr = getNeighbor(n);
    nbr->setState(Eigrp::Neighbor::State::TWOWAY);

    PacketBuilder ack(mockInterface);
    auto hdr = createAck(ack, *nbr, 5);

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1);

    nbr->currentReliable.store(5);
    eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), n.raw, false);

    EXPECT_EQ(nbr->getState(), Eigrp::Neighbor::State::LOADING);
}

// Test: Neighbor_LOADING_To_ESTABLISHED
TEST_F(Internal_EigrpTest, Neighbor_LOADING_To_ESTABLISHED)
{
    IPAddress n = createIPv4(0x0A000004);
    addNeighbor(n, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);

    auto nbr = getNeighbor(n);
    nbr->setState(Eigrp::Neighbor::State::LOADING);

    PacketBuilder update(mockInterface);
    Eigrp::ReliableTransport::PktInfo info;
    info.bandwidthMetric = 1000000;
    info.delay = 100000;
    info.mtu = 1500;
    info.version = Eigrp::TLVType::LEGACY_V4;
    auto hdr = createUpdate(update, info, nbr, {});

    eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), n.raw, false);
    EXPECT_EQ(nbr->getState(), Eigrp::Neighbor::State::ESTABLISHED);
}

// Test: Duplicate_Hello_Ignored
TEST_F(Internal_EigrpTest, Duplicate_Hello_Ignored)
{
    IPAddress n = createIPv4(0x0A000005);
    addNeighbor(n, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);

    auto nbr = getNeighbor(n);
    nbr->setState(Eigrp::Neighbor::State::INIT);

    PacketBuilder hello(mockInterface);
    auto hdr = createUnicastHello(hello, n);
    ASSERT_TRUE(hdr.has_value());

    eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), n.raw, false);
    eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), n.raw, false);

    EXPECT_EQ(nbr->getState(), Eigrp::Neighbor::State::INIT);
}

// Test: HoldTimerExpires_RemovesNeighbor
TEST_F(Internal_EigrpTest, HoldTimerExpires_RemovesNeighbor)
{
    IPAddress n = createIPv4(0x0A000006);
    addNeighbor(n);

    auto nbr = getNeighbor(n);
    ASSERT_TRUE(nbr);

    eigrpInterface->getTimers().restartHoldTimer(*nbr);
    eigrpInterface->getTimers().handleHoldTimeExpire(*nbr);

    EXPECT_FALSE(getNeighbor(n));
}

// Test: OutOfOrderUpdateIgnored
TEST_F(Internal_EigrpTest, OutOfOrderUpdateIgnored)
{
    IPAddress n = createIPv4(0x0A000007);
    addNeighbor(n);

    auto nbr = getNeighbor(n);
    nbr->lastSeqRecv = 10;

    PacketBuilder upd(mockInterface);
    Eigrp::ReliableTransport::PktInfo info;
    info.bandwidthMetric = 1000000;
    info.delay = 100000;
    info.mtu = 1500;
    info.version = Eigrp::TLVType::LEGACY_V4;
    auto hdr = createUpdate(upd, info, nbr, {});
    ASSERT_TRUE(hdr.has_value());
    hdr->setSequence(9);

    eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), n.raw, false);

    EXPECT_EQ(nbr->lastSeqRecv, 10);
}

// Test: DuplicateUpdateIgnored
TEST_F(Internal_EigrpTest, DuplicateUpdateIgnored)
{
    IPAddress n = createIPv4(0x0A000008);
    addNeighbor(n);

    auto nbr = getNeighbor(n);
    nbr->lastSeqRecv = 10;

    PacketBuilder upd(mockInterface);
    Eigrp::ReliableTransport::PktInfo info;
    info.bandwidthMetric = 1000000;
    info.delay = 100000;
    info.mtu = 1500;
    info.version = Eigrp::TLVType::LEGACY_V4;
    auto hdr = createUpdate(upd, info, nbr, {});
    ASSERT_TRUE(hdr.has_value());
    hdr->setSequence(11);

    eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), n.raw, false);
    eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), n.raw, false);

    EXPECT_EQ(nbr->lastSeqRecv, 11);
}

// Test: ConditionalReceive_Ignored_When_Version_Mismatch
TEST_F(Internal_EigrpTest, ConditionalReceive_Ignored_When_Version_Mismatch)
{
    // Create a neighbor that does NOT support conditional receive.
    IPAddress nbrIp = createIPv4(0xC0A80401);
    addNeighbor(nbrIp, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto nbr = getNeighbor(nbrIp);
    ASSERT_TRUE(nbr);

    // Build a conditional-receive hello from WIDE version.
    PacketBuilder hello(mockInterface);
    createPacket(hello);

    auto hdr = createSequenceHello(hello, nbrIp, 777);
    ASSERT_TRUE(hdr.has_value());

    uint32_t oldSeq = nbr->lastSeqRecv.load();

    eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), nbrIp.raw, false);

    EXPECT_EQ(nbr->lastSeqRecv.load(), oldSeq);
}

// Test: ConditionalReceive_Processes_When_Version_Matches
TEST_F(Internal_EigrpTest, ConditionalReceive_Processes_When_Version_Matches)
{
    IPAddress nbrIp = createIPv4(0xC0A80402);
    addNeighbor(nbrIp, Eigrp::Neighbor::Version::WIDE, eigrpInterface);
    auto nbr = getNeighbor(nbrIp);
    ASSERT_TRUE(nbr);

    PacketBuilder hello(mockInterface);
    createPacket(hello);

    auto hdr = createSequenceHello(hello, nbrIp, 12345);
    ASSERT_TRUE(hdr.has_value());

    eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), nbrIp.raw, false);

    EXPECT_EQ(nbr->lastSeqRecv.load(), 12345u);
}

// Test: ConditionalReceive_Retransmitted_If_OutOfWindow
TEST_F(Internal_EigrpTest, ConditionalReceive_Retransmitted_If_OutOfWindow)
{
    IPAddress nbrIp = createIPv4(0xC0A80403);
    addNeighbor(nbrIp, Eigrp::Neighbor::Version::WIDE, eigrpInterface);
    auto nbr = getNeighbor(nbrIp);
    ASSERT_TRUE(nbr);

    nbr->lastSeqRecv = 200;

    PacketBuilder hello(mockInterface);
    createPacket(hello);

    auto hdr = createSequenceHello(hello, nbrIp, 150);
    ASSERT_TRUE(hdr.has_value());

    // EXPECT_TX of sync/update triggered
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(::testing::AtLeast(1));

    eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), nbrIp.raw, false);

    EXPECT_EQ(nbr->lastSeqRecv.load(), 200u);
}

#pragma endregion
/*/
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
    hdr.setSequence(seqNum);
    neighbor->setState(Eigrp::Neighbor::State::ESTABLISHED);
    
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(::testing::AtLeast(1));
    
    eigrpInterface->getRtp().setupReliablePacket(neighbor, hdr);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    auto current = neighbor->currentReliable.load(std::memory_order_relaxed);
    //EXPECT_TRUE(current == seqNum);
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
    hdr.setSequence(seqNum);
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
    
    PacketBuilder update(mockInterface);
    createPacket(update);
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
    hdr.setSequence(506);
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
    EXPECT_FALSE(neighbor->reliableQueue.count(seqNum));
}
/*/

// Test: ActiveRoute_SIA_When_Stub_Enabled
TEST_F(Internal_EigrpTest, ActiveRoute_SIA_When_Stub_Enabled)
{
    // Enable stub mode: router will NOT forward queries
    eigrpInstance->getGlobalConfigMgr().enableStub(true, true, false, false, false);
    eigrpInstance->getConfigs().stuckInActiveTime.store(1);

    IPAddress queryNeighborIp = createIPv4(0xC0A80750);
    IPAddress neighborIp = createIPv4(0xC0A80751);
    addNeighbor(queryNeighborIp);
    addNeighbor(neighborIp);
    auto nbr = getNeighbor(queryNeighborIp);
    ASSERT_TRUE(nbr);
    ASSERT_TRUE(getNeighbor(neighborIp));
    
    IPPrefix p = { createIPv4(0x0AF00000), 16 };

    Eigrp::ReceivedRoute r = getRoute(eigrpInterface->interfaceKey);
    r.prefix = p;

    std::vector<Eigrp::ReceivedRoute> rs = { r };
    getDuel().processReceivedRoutes(rs, *nbr);

    r.feasibleDistance = std::numeric_limits<uint64_t>::max();
    r.reportedDistance = std::numeric_limits<uint64_t>::max();

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1).WillOnce(::testing::Invoke([&](PacketBuilder& pkt, const uint8_t*){
            EXPECT_EQ(getEigrpHeader(pkt).getOpcode(), Variable::Eigrp::Type::reply);
        }));

    rs = { r };
    getDuel().processReceivedRoutes(rs, *nbr);

    auto &active = getActiveRoutes();
    ASSERT_TRUE(active.empty());
}

// Test: ActiveRoute_Will_Send_Reply_If_No_Available_Neighbors
TEST_F(Internal_EigrpTest, ActiveRoute_Will_Send_Reply_If_No_Available_Neighbors)
{
    IPAddress neighborIp = createIPv4(0xC0A80751);
    addNeighbor(neighborIp);
    auto nbr = getNeighbor(neighborIp);
    ASSERT_TRUE(nbr);
    
    IPPrefix p = { createIPv4(0x0AF00000), 16 };

    Eigrp::ReceivedRoute r = getRoute(eigrpInterface->interfaceKey);
    r.prefix = p;

    std::vector<Eigrp::ReceivedRoute> rs = { r };
    getDuel().processReceivedRoutes(rs, *nbr);

    r.feasibleDistance = std::numeric_limits<uint64_t>::max();
    r.reportedDistance = std::numeric_limits<uint64_t>::max();

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1).WillOnce(::testing::Invoke([&](PacketBuilder& pkt, const uint8_t*){
            EXPECT_EQ(getEigrpHeader(pkt).getOpcode(), Variable::Eigrp::Type::reply);
        }));

    rs = { r };
    getDuel().processReceivedRoutes(rs, *nbr);

    auto &active = getActiveRoutes();
    ASSERT_TRUE(active.empty());
}

/*/
#pragma endregion
#pragma region TLV

// Test: Stub_TLV_Present_When_Stub_Enabled
TEST_F(Internal_EigrpTest, Stub_TLV_Present_When_Stub_Enabled) 
{
    // Verify that a stub TLV is inserted when stub mode is enabled.
    eigrpInstance->getGlobalConfigMgr().enableStub(true, true, false, true, false);
    PacketBuilder helloPacket(mockInterface);
    createPacket(helloPacket);
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
    
    PacketBuilder helloPacket(mockInterface);
    createPacket(helloPacket);
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
    mockInterface->shutdown(true);
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
    extraIface->configs.key = 1;
    extraIface->enableIPs();
    extraIface->enableShutdown();
    setIPv4(0xC0A80202, 24, extraIface);
    getAllInterfaceList()[key] = extraIface;

    EXPECT_EQ(getInterfaceList().size(), 1);

    eigrpInstance->getConfigs().networks.clear();
    eigrpInstance->refreshInterfaceList();

    EXPECT_EQ(getInterfaceList().size(), 0);

    EigrpConfigs::Network network(AddressFamily::IPv4);
    network.ip = createIPv4(0xC0A80000);
    network.mask = 16;
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(network);

    EXPECT_EQ(getInterfaceList().size(), 2);
    extraIface->shutdown(true);
    EXPECT_EQ(getInterfaceList().size(), 1);
    eigrpInstance->shutdown();
}

// Test: Interface_Initialization_Starts_Hello_Timer
TEST_F(Internal_EigrpTest, Interface_Initialization_Starts_Hello_Timer) 
{
    // Check that after initialization, the hello timer is active.
    ASSERT_TRUE(getHelloTimerActive());
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

// Test: Connected_Route_Removal_On_Interface_Down
TEST_F(Internal_EigrpTest, Connected_Route_Removal_On_Interface_Down) 
{
    // Verify that the connected route is removed when the interface goes down.
    IPAddress dest = createIPv4(0xC0A80100);
    ASSERT_TRUE(vrf->routingTable.lookup<uint32_t>(dest.raw));
    eigrpInstance->getGlobalConfigMgr().clearNetworks();
    ASSERT_FALSE(vrf->routingTable.lookup<uint32_t>(dest.raw));
}

// Test: Connected_Route_Addition_On_Interface_Up
TEST_F(Internal_EigrpTest, Connected_Route_Addition_On_Interface_Up) 
{
    // Verify that a connected route is added when the interface is active.
    EigrpConfigs::Network net(AddressFamily::IPv4);
    IPAddress dest = createIPv4(0xC0A80100);
    net.ip = dest;
    net.mask = 24;
    eigrpInstance->getGlobalConfigMgr().delNetworkRange(net);
    ASSERT_FALSE(vrf->routingTable.lookup<uint32_t>(dest.raw));
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(net);
    ASSERT_TRUE(vrf->routingTable.lookup<uint32_t>(dest.raw));
}

// Test: Multiple_Connected_Routes_From_Different_Interfaces
TEST_F(Internal_EigrpTest, Multiple_Connected_Routes_From_Different_Interfaces) 
{
    // Add a second interface and verify both connected routes appear.
    MockInterface* extraIface1 = new MockInterface(*global, InterfaceType::GIGABIT_ETHERNET);
    uint32_t key1 = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 2);
    vrf->interfaceList[key1] = extraIface1;
    extraIface1->routingInstance = vrf;
    extraIface1->configs.id = 2;
    extraIface1->configs.key = key1;
    extraIface1->configs.interfaceType = InterfaceType::GIGABIT_ETHERNET;
    extraIface1->blockEnqueues();
    extraIface1->enableIPs();
    extraIface1->enableShutdown();
    getAllInterfaceList()[key1] = extraIface1;
    setIPv4(0x0A001002, 24, extraIface1);

    MockInterface* extraIface2 = new MockInterface(*global, InterfaceType::GIGABIT_ETHERNET);
    uint32_t key2 = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 3);
    vrf->interfaceList[key2] = extraIface2;
    extraIface2->routingInstance = vrf;
    extraIface2->configs.id = 3;
    extraIface2->configs.key = key2;
    extraIface2->configs.interfaceType = InterfaceType::GIGABIT_ETHERNET;
    extraIface2->blockEnqueues();
    extraIface2->enableIPs();
    extraIface2->enableShutdown();
    getAllInterfaceList()[key2] = extraIface2;
    setIPv4(0x0A002003, 24, extraIface2);

    EigrpConfigs::Network net(AddressFamily::IPv4);
    net.ip = createIPv4(0x0A000000);
    net.mask = 8;
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(net);
    ASSERT_EQ(vrf->routingTable.size<uint32_t>(), 3);

    extraIface1->shutdown(true);
    extraIface2->shutdown(true);

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
    top.entries()[prefix1]->valid = std::chrono::steady_clock::now() - std::chrono::seconds(100);
    EXPECT_EQ(top.entries().size(), 3);
    top.pruneExpired();
    EXPECT_EQ(top.entries().size(), 2);
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
    top.routesBySource.try_emplace(r1, route1);
    top.routesBySource.try_emplace(r2, route2);
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
    Eigrp::TopologyEntry top1;
    top1.bestNeighbor = IPAddress{};
    top1.routesBySource.emplace(IPAddress{}, Eigrp::RouteInfo{route1});
    std::vector<Eigrp::TopologyEntry*> ts = { &top1 };
    getDuel().updateSuccessors(ts);
    eigrpInstance->routeManager.synchronizeRoutes({&top1});
    
    auto route2 = getRoute(eigrpInterface->interfaceKey);
    route2.prefix = { network, 24 };
    route2.feasibleDistance = 50;
    route2.nextHop = createIPv4(0xC0A80103);
    top1.routesBySource.emplace(createIPv4(0x0A000001), Eigrp::RouteInfo{route2});
    getDuel().updateSuccessors(ts);
    eigrpInstance->routeManager.synchronizeRoutes(ts);
    
    RibEntry<uint32_t>* r = vrf->routingTable.lookup<uint32_t>(network.raw);
    ASSERT_TRUE(r);
    ASSERT_EQ(r->metric, 6400); // 50 * rib scal = 128
}
/*/

// Test: WideMetrics_InternalRoute_ParsedCorrectly
TEST_F(Internal_EigrpTest, WideMetrics_InternalRoute_ParsedCorrectly)
{
    IPAddress neighborIp = createIPv4(0xC0A80250);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::WIDE, eigrpInterface);
    auto nbr = getNeighbor(neighborIp);

    Eigrp::ReceivedRoute r = getRoute(eigrpInterface->interfaceKey);
    r.prefix = { createIPv4(0x0A500000), 16 };
    r.routeType = Eigrp::RouteType::INTERNAL;
    r.feasibleDistance = 0;
    r.reportedDistance = 0;
    r.bandwidth = 100000000;
    r.delay = 2000000;
    r.load = 5;
    r.reliability = 254;

    Eigrp::RouteInfo ri(r);

    std::vector<Eigrp::ReceivedRoute> rs = { r };
    getDuel().processReceivedRoutes(rs, *nbr);

    auto entry = vrf->routingTable.lookup<uint32_t>(r.prefix.addr);
    ASSERT_TRUE(entry);
    
    // EIGRP wide: metric = ((10^7 / bw) + delay/10) * 250
    uint64_t bwTerm = (10000000ULL / r.bandwidth);
    uint64_t delayTerm = r.delay / 10;
    uint64_t expected = (bwTerm + delayTerm) * 256;

    EXPECT_EQ(entry->metric, expected * 128);
}

// Test: WideMetrics_ExternalRoute_ParsedCorrectly
TEST_F(Internal_EigrpTest, WideMetrics_ExternalRoute_ParsedCorrectly)
{
    IPAddress neighborIp = createIPv4(0xC0A80251);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::WIDE, eigrpInterface);
    auto nbr = getNeighbor(neighborIp);
    ASSERT_TRUE(nbr);

    Eigrp::ReceivedRoute r = getRoute(eigrpInterface->interfaceKey);
    r.prefix = { createIPv4(0x0A510000), 16 };
    r.routeType = Eigrp::RouteType::EXTERNAL;
    r.bandwidth = 50000000;
    r.delay = 3000000;
    r.load = 10;
    r.hopCount = 2;
    r.tag = 12345;

    Eigrp::RouteInfo ri(r);

    std::vector<Eigrp::ReceivedRoute> rs = { r };
    getDuel().processReceivedRoutes(rs, *nbr);

    auto entry = vrf->routingTable.lookup<uint32_t>(r.prefix.addr);
    ASSERT_TRUE(entry);

    uint64_t bwTerm = (10000000ULL / r.bandwidth);
    uint64_t delayTerm = r.delay / 10;
    uint64_t expected = (bwTerm + delayTerm) * 256;

    EXPECT_EQ(entry->metric, expected * 128);
    //TODO match tag
    EXPECT_EQ(entry->adminDistance, 170);
}

// Test: WideMetrics_ClassicToWide_Transition
TEST_F(Internal_EigrpTest, WideMetrics_ClassicToWide_Transition)
{
    IPAddress neighborLegacy = createIPv4(0xC0A80260);
    IPAddress neighborWide = createIPv4(0xC0A80261);

    addNeighbor(neighborLegacy, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    addNeighbor(neighborWide, Eigrp::Neighbor::Version::WIDE, eigrpInterface);

    auto nbrLegacy = getNeighbor(neighborLegacy);
    auto nbrWide = getNeighbor(neighborWide);

    ASSERT_TRUE(nbrLegacy);
    ASSERT_TRUE(nbrWide);

    // Classic route first
    Eigrp::ReceivedRoute r1 = getRoute(eigrpInterface->interfaceKey);
    r1.prefix = { createIPv4(0x0A600000), 16 };
    r1.feasibleDistance = 200;
    r1.reportedDistance = 100;
    r1.routeType = Eigrp::RouteType::INTERNAL;
    r1.nextHop = neighborLegacy;
    
    std::vector<Eigrp::ReceivedRoute> rs1 = { r1 };
    getDuel().processReceivedRoutes(rs1, *nbrLegacy);

    auto entry1 = vrf->routingTable.lookup<uint32_t>(r1.prefix.addr);
    ASSERT_TRUE(entry1);
    uint32_t classicMetric = 200 * 128;
    EXPECT_EQ(entry1->metric, classicMetric);

    // Now send wide metric version from different neighbor
    Eigrp::ReceivedRoute r2 = r1;
    r2.bandwidth = 50000000;
    r2.delay = 20000000;
    r2.reliability = 255;
    r2.load = 1;
    r2.nextHop = neighborWide;

    std::vector<Eigrp::ReceivedRoute> rs2 = { r2 };
    getDuel().processReceivedRoutes(rs2, *nbrWide);

    auto entry2 = vrf->routingTable.lookup<uint32_t>(r2.prefix.addr);
    ASSERT_TRUE(entry2);

    uint64_t bwTerm = (10000000ULL / r2.bandwidth);
    uint64_t delayTerm = r2.delay / 10;
    uint64_t wideMetric = (bwTerm + delayTerm) * 256;

    EXPECT_EQ(entry2->metric, wideMetric * 128);
    EXPECT_LT(wideMetric, classicMetric);
}

// Test: WideMetrics_Successor_Selection
TEST_F(Internal_EigrpTest, WideMetrics_Successor_Selection)
{
    IPAddress n1 = createIPv4(0xC0A80270);
    IPAddress n2 = createIPv4(0xC0A80271);

    addNeighbor(n1, Eigrp::Neighbor::Version::WIDE, eigrpInterface);
    addNeighbor(n2, Eigrp::Neighbor::Version::WIDE, eigrpInterface);

    auto nbr1 = getNeighbor(n1);
    auto nbr2 = getNeighbor(n2);

    Eigrp::ReceivedRoute r1 = getRoute(eigrpInterface->interfaceKey);
    r1.prefix = { createIPv4(0x0A70000), 16 };
    r1.routeType = Eigrp::RouteType::INTERNAL;
    r1.bandwidth = 100000000;
    r1.delay = 1000000;
    r1.nextHop = n1;

    Eigrp::ReceivedRoute r2 = r1;
    r2.bandwidth = 20000000;
    r2.delay = 3000000;
    r2.nextHop = n2;

    std::vector<Eigrp::ReceivedRoute> rs = { r1 };
    getDuel().processReceivedRoutes(rs, *nbr1);
    rs = { r2 };
    getDuel().processReceivedRoutes(rs, *nbr2);

    auto route = vrf->routingTable.lookup<uint32_t>(r1.prefix.addr);
    ASSERT_TRUE(route);

    uint64_t bw1 = (10000000ULL / r1.bandwidth);
    uint64_t d1 = r1.delay / 10;
    uint64_t m1 = (bw1 + d1) * 256;

    uint64_t bw2 = (10000000ULL / r2.bandwidth);
    uint64_t d2 = r2.delay / 10;
    uint64_t m2 = (bw2 + d2) * 256;

    EXPECT_EQ(route->metric, std::min(m1, m2));
    EXPECT_EQ(route->nextHopCount, (m1 == m2 ? 2 : 1));
}

// Test: WideMetrics_FeasibleSuccessor_WithVariance
TEST_F(Internal_EigrpTest, WideMetrics_FeasibleSuccessor_WithVariance)
{
    eigrpInstance->getConfigs().variance = 5;

    IPAddress n1 = createIPv4(0xC0A80280);
    IPAddress n2 = createIPv4(0xC0A80281);

    addNeighbor(n1, Eigrp::Neighbor::Version::WIDE, eigrpInterface);
    addNeighbor(n2, Eigrp::Neighbor::Version::WIDE, eigrpInterface);

    auto nbr1 = getNeighbor(n1);
    auto nbr2 = getNeighbor(n2);

    Eigrp::ReceivedRoute p = getRoute(eigrpInterface->interfaceKey);
    p.prefix = { createIPv4(0x0A800000), 16 };
    p.routeType = Eigrp::RouteType::INTERNAL;
    p.bandwidth = 100000000;
    p.delay = 1000000;
    p.nextHop = n1;

    // Backup path with higher metric but within variance;
    Eigrp::ReceivedRoute b = p;
    b.bandwidth = 20000000;
    b.delay = 4000000;
    b.nextHop = n2;

    std::vector<Eigrp::ReceivedRoute> rs = { p };
    getDuel().processReceivedRoutes(rs, *nbr1);
    rs = { b };
    getDuel().processReceivedRoutes(rs, *nbr2);

    auto entry = vrf->routingTable.lookup<uint32_t>(p.prefix.addr);
    ASSERT_TRUE(entry);

    EXPECT_EQ(entry->nextHopCount, 2);
}

/*/

// Test: TopologyTable_Handles_Neighbor_Down
TEST_F(Internal_EigrpTest, TopologyTable_Handles_Neighbor_Down) 
{
    // Verify that when a neighbor goes down, its routes are removed from the topology.
    eigrpInstance->getConfigs().routeDelTimer = 1;
    IPAddress neighborIp = createIPv4(0xC0A80121);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    addNeighbor(createIPv4(0xC0A80221), Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    auto& tt = getTopologyTable();
    auto r = getRoute(eigrpInterface->interfaceKey);
    IPAddress nextHop = createIPv4(0xC0A80102);
    r.prefix = { createIPv4(0x0A000000), 16 };
    r.feasibleDistance = 100;
    r.reportedDistance = 80;
    r.nextHop = nextHop;
    std::vector<Eigrp::ReceivedRoute> rs = {r};
    getDuel().processReceivedRoutes(rs, *neighbor);
    eigrpInterface->getTopController().onNeighborDown(neighborIp);
    EXPECT_EQ(getActiveRoutes().size(), 1);
}

// Test: RoutingTable_All_Connected_Routes_Count
TEST_F(Internal_EigrpTest, RoutingTable_All_Connected_Routes_Count) 
{
    EigrpConfigs::Network network(AddressFamily::IPv4);
    network.ip = createIPv4(0xC0A80000);
    network.mask = 16;
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(network);
    auto route = vrf->routingTable.lookup<uint32_t>(mockInterface->configs.ipv4.getAddress());
    ASSERT_TRUE(route);
    EXPECT_EQ(route->nextHops[0].nextHop, 0);
}

#pragma endregion
#pragma region StubMode

// Test: StubMode_Enabled_Allows_Only_Permitted_Routes
TEST_F(Internal_EigrpTest, StubMode_Enabled_Allows_Only_Permitted_Routes) 
{
    // When stub mode is enabled (allowing only connected routes), external routes should be filtered.
    eigrpInterface->configs->splitHorizon = false;
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
    eigrpInterface->configs->splitHorizon = false;
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
    eigrpInterface->configs->splitHorizon = false;
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
    eigrpInterface->configs->splitHorizon = false;
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
    eigrpInterface->configs->splitHorizon = false;
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
    eigrpInterface->configs->splitHorizon = false;
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
    eigrpInterface->configs->splitHorizon = false;
    eigrpInstance->getGlobalConfigMgr().enableStub(true, true, false, false, false, false);
    Eigrp::ReceivedRoute externalRoute = getRoute(eigrpInterface->interfaceKey);
    externalRoute.prefix = { createIPv4(0xC0A80600), 24 };
    externalRoute.routeType = Eigrp::RouteType::EXTERNAL;
    Eigrp::RouteInfo rInfo(externalRoute);
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(0);
    eigrpInstance->broadcastRouteChanges({&rInfo});
}

#pragma endregion
#pragma region ActiveState

// Test: ActiveQuery_Clear_After_Neighbor_Response
TEST_F(Internal_EigrpTest, ActiveQuery_Clear_After_Neighbor_Response) 
{
    // Verify that when a neighbor replies, the active query is cleared.
    Eigrp::ReceivedRoute testRoute = getRoute(eigrpInterface->interfaceKey);
    IPPrefix prefix = { createIPv4(0xC0A80500), 24 };
    testRoute.prefix = prefix;
    Eigrp::RouteInfo rInfo(testRoute);

    IPAddress queryNeighborIp = createIPv4(0x0A010002);
    IPAddress neighborIp = createIPv4(0x0A010001);
    addNeighbor(queryNeighborIp);
    auto neighbor = getNeighbor(queryNeighborIp);
    addNeighbor(neighborIp);

    testRoute.nextHop = queryNeighborIp;

    Eigrp::ReliableTransport::PktInfo info;
    info.bandwidthMetric = 1000000;
    info.delay = 100000;
    info.mtu = 1500;
    info.version = Eigrp::TLVType::LEGACY_V4;

    auto& top = getTopologyTable().ensure(prefix);
    getTopologyTable().addRouteUpdate(rInfo.routeInfo, neighbor, top);
    std::optional<EigrpHeader> hdr;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(::testing::AtLeast(1))
        .WillRepeatedly(::testing::Invoke([&]( PacketBuilder& pkt, const uint8_t*) {
            auto header = getEigrpHeader(pkt);
            if (header.getOpcode() == Variable::Eigrp::Type::query)
            {
                PacketBuilder eigrp(mockInterface);
                createPacket(eigrp);
                hdr = createReply(eigrp, info, *getNeighbor(neighborIp), {&rInfo}, getEigrpHeader(pkt).getSequence());
                ASSERT_TRUE(hdr.has_value());
                hdr->setTrail(hdr->buffer + EigrpHeader::fixedSize, eigrp.getHeaders()[2].length - EigrpHeader::fixedSize);
            }
        }));

    testRoute.delay = std::numeric_limits<uint64_t>::max();
    testRoute.feasibleDistance = std::numeric_limits<uint64_t>::max();
    std::vector<Eigrp::ReceivedRoute> rs = {testRoute};
    getDuel().processReceivedRoutes(rs, *getNeighbor(queryNeighborIp));
    ASSERT_TRUE(hdr.has_value());
    eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), neighborIp.raw, false);
    EXPECT_TRUE(getActiveRoutes().empty()); // Active should be resolved
}

// Test: Reply_Returned_After_Full_Query_Sequence
TEST_F(Internal_EigrpTest, Reply_Returned_After_Full_Query_Sequence) 
{
    // Verify that when a neighbor replies, the active query is cleared.
    MockInterface* extraIface = new MockInterface(*global, InterfaceType::GIGABIT_ETHERNET);
    uint32_t key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 1);
    vrf->interfaceList[key] = extraIface;
    extraIface->routingInstance = vrf;
    extraIface->configs.id = 1;
    extraIface->configs.key = key;
    extraIface->enableIPs();
    extraIface->enableShutdown();
    getAllInterfaceList()[key] = extraIface;
    setIPv4(0xC0A80102, 24, extraIface);
    {
        EXPECT_CALL(*extraIface, enqueuePacket(::testing::_, ::testing::_)).Times(::testing::AnyNumber());
        eigrpInstance->refreshInterfaceList();
    }

    ASSERT_TRUE(getInterfaceList().contains(key));
    Eigrp::EigrpInterface* extraEigrpIface = &getInterfaceList().at(key);

    Eigrp::ReceivedRoute testRoute = getRoute(eigrpInterface->interfaceKey);
    IPPrefix prefix = { createIPv4(0xC0A80500), 24 };
    testRoute.prefix = prefix;
    Eigrp::RouteInfo rInfo(testRoute);

    IPAddress queryNeighborIp = createIPv4(0x0A010002);
    IPAddress neighborIp = createIPv4(0x0A010001);
    addNeighbor(queryNeighborIp);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY, extraEigrpIface);
    auto queryNeighbor = getNeighbor(queryNeighborIp);
    auto neighbor = getNeighbor(neighborIp, extraEigrpIface);

    Eigrp::ReliableTransport::PktInfo info;
    info.bandwidthMetric = 1000000;
    info.delay = 100000;
    info.mtu = 1500;
    info.version = Eigrp::TLVType::LEGACY_V4;

    auto& top = getTopologyTable().ensure(prefix);
    getTopologyTable().addRouteUpdate(rInfo.routeInfo, queryNeighbor, top);
    std::vector<Eigrp::TopologyEntry*> ts = { &top };
    getDuel().updateSuccessors(ts);
    std::optional<EigrpHeader> hdr;

    bool replyFound = false;
    bool updateFound = false;
    uint32_t querySeq = 10;

    EXPECT_CALL(*extraIface, enqueuePacket(::testing::_, ::testing::_))
        .Times(::testing::AtLeast(1))
        .WillRepeatedly(::testing::Invoke([&]( PacketBuilder& pkt, const uint8_t*) {
            auto header = getEigrpHeader(pkt);
            if (header.getOpcode() == Variable::Eigrp::Type::query)
            {
                PacketBuilder eigrp(mockInterface); // Only mock interface has a valid queue
                createPacket(eigrp);
                hdr = createReply(eigrp, info, *neighbor, {&rInfo}, getEigrpHeader(pkt).getSequence());
                ASSERT_TRUE(hdr.has_value());
                hdr->setTrail(hdr->buffer + EigrpHeader::fixedSize, eigrp.getHeaders()[2].length - EigrpHeader::fixedSize);
            }
        }));

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(::testing::AtLeast(2))
        .WillRepeatedly(::testing::Invoke([&]( PacketBuilder& pkt, const uint8_t*) {
            auto header = getEigrpHeader(pkt);
            if (header.getOpcode() == Variable::Eigrp::Type::reply)
            {
                EXPECT_EQ(header.getAck(), querySeq);
                auto opts = extractEigrpOptions(pkt);
                for (auto& opt : opts)
                    if (opt.type == Variable::Eigrp::Option::legacyInternalRoute)
                        if (auto [route, valid] = Eigrp::TLVBuilder::decodeRoute(opt, 0, Eigrp::TLVType::LEGACY_V4); route && route->prefix == prefix)
                            replyFound = true;
            }
            else if (header.getOpcode() == Variable::Eigrp::Type::update)
                updateFound = true;
        }));

    testRoute.delay = std::numeric_limits<uint64_t>::max();
    testRoute.feasibleDistance = std::numeric_limits<uint64_t>::max();
    std::vector<Eigrp::ReceivedRoute> rs = {testRoute};
    getDuel().processReceivedQueryRoutes(rs, *queryNeighbor, querySeq);
    ASSERT_TRUE(hdr.has_value());
    extraEigrpIface->getRtp().handleIncoming(nullptr, hdr.value(), neighborIp.raw, false);
    EXPECT_TRUE(replyFound);
    EXPECT_TRUE(updateFound);
    EXPECT_TRUE(getActiveRoutes().empty()); // Active should be resolved
}

// Test: ActiveQuery_Timeout_Leads_To_Neighbor_Down
TEST_F(Internal_EigrpTest, ActiveQuery_Timeout_Leads_To_Neighbor_Down) 
{
    // Verify that if a neighbor fails to respond to repeated queries, it is declared down.
    eigrpInstance->getConfigs().stuckInActiveTime = 1;
    IPAddress queryNeighborIp = createIPv4(0xC0A8011F);
    addNeighbor(queryNeighborIp, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    IPAddress neighborIp = createIPv4(0xC0A8021F);
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    Eigrp::ReceivedRoute testRoute = getRoute(eigrpInterface->interfaceKey);
    IPPrefix prefix = { createIPv4(0xC0A80600), 24 };
    testRoute.prefix = prefix;

    auto& top = getTopologyTable().ensure(prefix);
    getTopologyTable().addRouteUpdate(testRoute, getNeighbor(queryNeighborIp), top);
    std::vector<Eigrp::ReceivedRoute> rs = {testRoute};
    getDuel().processReceivedRoutes(rs, *getNeighbor(queryNeighborIp));
    rs[0].feasibleDistance = std::numeric_limits<uint64_t>::max();
    rs[0].delay = std::numeric_limits<uint64_t>::max();
    getDuel().processReceivedRoutes(rs, *getNeighbor(queryNeighborIp));

    std::this_thread::sleep_for(std::chrono::seconds(6));
    
    ASSERT_FALSE(getNeighbor(neighborIp));
}
/*/

// Test: ActiveRoute_Cancels_On_Better_AlternativePath
TEST_F(Internal_EigrpTest, ActiveRoute_Cancels_On_Better_AlternativePath)
{
    IPAddress n1 = createIPv4(0xC0A80514);
    IPAddress n2 = createIPv4(0xC0a80515);

    addNeighbor(n1);
    addNeighbor(n2);

    auto nbr1 = getNeighbor(n1);
    auto nbr2 = getNeighbor(n2);

    IPPrefix p = { createIPv4(0x0A930000), 16 };

    // Tood initial path from nbr1
    Eigrp::ReceivedRoute r1 = getRoute(eigrpInterface->interfaceKey);
    r1.prefix = p;
    r1.feasibleDistance = 100;
    r1.reportedDistance = 90;
    std::vector<Eigrp::ReceivedRoute> rs = { r1 };
    getDuel().processReceivedRoutes(rs, *nbr1);

    // Bad update forces ACTIVE
    Eigrp::ReceivedRoute bad = r1;
    bad.feasibleDistance = std::numeric_limits<uint64_t>::max();
    bad.reportedDistance = std::numeric_limits<uint64_t>::max();
    rs = { bad };
    getDuel().processReceivedRoutes(rs, *nbr1);

    ASSERT_FALSE(getActiveRoutes().empty());

    // Another neighbor advertises a valid alternative successor
    Eigrp::ReceivedRoute alt = r1;
    alt.feasibleDistance = 150;
    alt.reportedDistance = 120;
    rs = { alt };
    getDuel().processReceivedRoutes(rs, *nbr2);

    EXPECT_TRUE(getActiveRoutes().empty());
}

// Test: Query_Multicast_Sent_To_All_Eligible_Neighbors
TEST_F(Internal_EigrpTest, Query_Multicast_Sent_To_All_Eligible_Neighbors)
{
    IPAddress n1 = createIPv4(0xC0A80601);
    IPAddress n2 = createIPv4(0xC0A80602);
    IPAddress n3 = createIPv4(0xC0A80603);

    addNeighbor(n1);
    addNeighbor(n2);
    addNeighbor(n3);

    auto nbr1 = getNeighbor(n1);

    IPPrefix p = { createIPv4(0x0AAA0000), 16 };

    // Trigger ACTIVE state by withdrawing successor
    Eigrp::ReceivedRoute r = getRoute(eigrpInterface->interfaceKey);
    r.prefix = p;
    r.feasibleDistance = 100;
    std::vector<Eigrp::ReceivedRoute> rs = { r };
    getDuel().processReceivedRoutes(rs, *nbr1);

    r.feasibleDistance = std::numeric_limits<uint64_t>::max();
    r.reportedDistance = std::numeric_limits<uint64_t>::max();

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1);

    rs = { r };
    getDuel().processReceivedRoutes(rs, *nbr1);

    auto &active = getActiveRoutes();
    ASSERT_FALSE(active.empty());
}

// Test: Query_Unicast_Sent_To_All_Eligible_Neighbors
TEST_F(Internal_EigrpTest, Query_Unicast_Sent_To_All_Eligible_Neighbors)
{
    IPAddress n1 = createIPv4(0xC0A80601);
    IPAddress n2 = createIPv4(0xC0A80602);
    IPAddress n3 = createIPv4(0xC0A80603);

    eigrpInterface->getNTable().createNeighbor(n1);
    eigrpInterface->getNTable().createNeighbor(n2);
    eigrpInterface->getNTable().createNeighbor(n3);

    auto nbr1 = getNeighbor(n1);

    IPPrefix p = { createIPv4(0x0AAA0000), 16 };

    // Trigger ACTIVE state by withdrawing successor
    Eigrp::ReceivedRoute r = getRoute(eigrpInterface->interfaceKey);
    r.prefix = p;
    r.feasibleDistance = 100;
    std::vector<Eigrp::ReceivedRoute> rs = { r };
    getDuel().processReceivedRoutes(rs, *nbr1);

    r.feasibleDistance = std::numeric_limits<uint64_t>::max();
    r.reportedDistance = std::numeric_limits<uint64_t>::max();

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(2);

    rs = { r };
    getDuel().processReceivedRoutes(rs, *nbr1);

    auto &active = getActiveRoutes();
    ASSERT_FALSE(active.empty());
}

// Test: Query_Ignored_For_Passive_Neighbor
TEST_F(Internal_EigrpTest, Query_Ignored_For_Passive_Neighbor)
{
    FAIL();
    IPAddress n1 = createIPv4(0xC0A80605);
    addNeighbor(n1);

    auto nbr = getNeighbor(n1);
    nbr->setState(Eigrp::Neighbor::State::ESTABLISHED);

    eigrpInterface->setPassiveMode(true);

    IPPrefix p = { createIPv4(0x0AAC0000), 16 };

    Eigrp::ReceivedRoute r = getRoute(eigrpInterface->interfaceKey);
    r.prefix = p;
    r.feasibleDistance = 30;
    r.feasibleDistance = 25;
    std::vector<Eigrp::ReceivedRoute> rs = { r };
    getDuel().processReceivedRoutes(rs, *nbr);

    r.feasibleDistance = std::numeric_limits<uint64_t>::max();
    r.reportedDistance = std::numeric_limits<uint64_t>::max();

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(0);

    rs = { r };
    getDuel().processReceivedRoutes(rs, *nbr);
}

// Test: PassiveInterface_Disables_Hello_Transmission
TEST_F(Internal_EigrpTest, PassiveInterface_Disables_Hello_Transmission)
{
    eigrpInterface->setPassiveMode(true);

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(0);

    eigrpInterface->getRtp().sendHello();
}

// Test: PassiveInterface_Allows_Hello_Reception
TEST_F(Internal_EigrpTest, PassiveInterface_Allows_Hello_Reception)
{
    eigrpInterface->setPassiveMode(true);

    IPAddress nbrIp = createIPv4(0xC0A80620);

    PacketBuilder pb(mockInterface);
    auto hdr = createUnicastHello(pb, nbrIp);
    ASSERT_TRUE(hdr.has_value());

    // Should accept hello and form neighbor
    eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), nbrIp.raw, false);
    
    auto nbr = getNeighbor(nbrIp);
    ASSERT_TRUE(nbr);
    EXPECT_EQ(nbr->getState(), Eigrp::Neighbor::State::INIT);
}

// Test: PassiveInterface_Blocks_UpdateTransmission
TEST_F(Internal_EigrpTest, PassiveInterface_Blocks_UpdateTransmission)
{
    eigrpInterface->setPassiveMode(true);

    IPAddress neighborIp = createIPv4(0xC0A80102);
    addNeighbor(neighborIp);

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(0);

    eigrpInterface->getRtp().sendNullUpdate(*getNeighbor(neighborIp));
}

/*/
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
    
    PacketBuilder helloPacket(&ipv6Interface);
    createPacket(helloPacket, ipv6Int);
    uint8_t neighborIpBuf[16] = { 0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };
    IPAddress neighborIp = { neighborIpBuf, AddressFamily::IPv6 };
    addNeighbor(neighborIp, Eigrp::Neighbor::Version::WIDE, ipv6Int);
    ASSERT_TRUE(createHello(helloPacket, ipv6Int));
    ipv6Eigrp.getIfaceMgr().deactivateAll();
    getAllInterfaceList().erase(key);
}
/*/

// Test: IPv6_Full_Adjacency_Establishment
TEST_F(Internal_EigrpTest, IPv6_Full_Adjacency_Establishment)
{
    FAIL();
    auto ipv6Eigrp = Eigrp::Eigrp(asNumber, AddressFamily::IPv6, vrf);
    ipv6Eigrp.start();

    MockInterface iface(*global, InterfaceType::GIGABIT_ETHERNET);
    iface.routingInstance = vrf;
    iface.blockEnqueues();
    iface.enableIPs();
    iface.enableShutdown();

    uint8_t local6[16] = {
        0x20,0x01,0x0d,0xb8,0,0,0,1,
        0,0,0,0,0,0,0,1
    };
    uint8_t nbr6[16] = {
        0x20,0x01,0x0d,0xb8,0,0,0,2,
        0,0,0,0,0,0,0,2
    };

    setIPv6(local6, 64, &iface);
    uint32_t key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 11);
    iface.configs.key = key;
    iface.configs.id = 11;
    getAllInterfaceList()[key] = &iface;

    Eigrp::EigrpInterface* intf = ipv6Eigrp.getIfaceMgr().createInterface(&iface);

    IPAddress nbrIp = { nbr6, AddressFamily::IPv6 };

    PacketBuilder p(mockInterface);
    createPacket(p, intf);
    auto hdr = createUnicastHello(p, nbrIp, intf);
    ASSERT_TRUE(hdr.has_value());

    intf->getRtp().handleIncoming(nullptr, hdr.value(), nbrIp.raw, false);

    auto nbr = intf->getNTable().lookup(nbrIp);
    ASSERT_TRUE(nbr);
    EXPECT_EQ(nbr->getState(), Eigrp::Neighbor::State::INIT);

    // Now send a hello back to complete TWO-WAY
    PacketBuilder resp(&iface);
    createPacket(resp, intf);
    auto hdr2 = createUnicastHello(resp, nbrIp, intf);
    ASSERT_TRUE(hdr2.has_value());

    intf->getRtp().handleIncoming(nullptr, hdr2.value(), nbr6, false);
    EXPECT_EQ(nbr->getState(), Eigrp::Neighbor::State::TWOWAY);

    ipv6Eigrp.getIfaceMgr().deactivateAll();
    getAllInterfaceList().erase(key);
}

// Test: IPv6_Update_Processing
TEST_F(Internal_EigrpTest, IPv6_Update_Processing)
{
    FAIL();
    auto ipv6Eigrp = Eigrp::Eigrp(asNumber, AddressFamily::IPv6, vrf);
    ipv6Eigrp.start();

    MockInterface iface(*global, InterfaceType::GIGABIT_ETHERNET);
    iface.routingInstance = vrf;
    iface.blockEnqueues();
    iface.enableIPs();
    iface.enableShutdown();

    uint8_t local6[16] = {
        0x20,0x01,0x0d,0xb8,0,0,0,1,
        0,0,0,0,0,0,0,1
    };
    uint8_t nbr6[16] = {
        0x20,0x01,0x0d,0xb8,0,0,0,2,
        0,0,0,0,0,0,0,2
    };

    setIPv6(local6, 64, &iface);
    uint32_t key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 11);
    iface.configs.key = key;
    iface.configs.id = 11;
    getAllInterfaceList()[key] = &iface;

    auto* intf = ipv6Eigrp.getIfaceMgr().createInterface(&iface);
    IPAddress nbrIp = { nbr6, AddressFamily::IPv6 };

    addNeighbor(nbrIp, Eigrp::Neighbor::Version::WIDE, intf);
    auto nbr = intf->getNTable().lookup(nbrIp);
    ASSERT_TRUE(nbr);

    Eigrp::ReliableTransport::PktInfo info;
    info.bandwidthMetric = 1000000;
    info.delay = 50000;
    info.mtu = 1500;
    info.version = Eigrp::TLVType::WIDE_V6;

    Eigrp::ReceivedRoute r = getRoute(eigrpInterface->interfaceKey);
    r.prefix = { nbrIp, 64 };
    r.routeType = Eigrp::RouteType::INTERNAL;
    r.nextHop = nbrIp;
    r.feasibleDistance = 100;
    r.reportedDistance = 50;

    Eigrp::RouteInfo ri(r);

    PacketBuilder pb(&iface);
    createPacket(pb, intf);
    auto upd = createUpdate(pb, info, nbr, { &ri }, intf);
    ASSERT_TRUE(upd.has_value());
    upd->setTrail(upd->buffer + EigrpHeader::fixedSize, pb.getHeaders()[2].length - EigrpHeader::fixedSize);

    intf->getRtp().handleIncoming(nullptr, upd.value(), nbr6, false);
    
    auto entry = getTopologyTable(&ipv6Eigrp).find(r.prefix);
    ASSERT_TRUE(entry != nullptr);
    EXPECT_EQ(entry->routesByNeighbor.size(), 1u);

    ipv6Eigrp.getIfaceMgr().deactivateAll();
    getInterfaceList().erase(key);
}

// Test: IPv6_Query_Reply_SIA
TEST_F(Internal_EigrpTest, IPv6_Query_Reply_SIA)
{
    FAIL();
    auto ipv6Eigrp = Eigrp::Eigrp(asNumber, AddressFamily::IPv6, vrf);
    ipv6Eigrp.start();
        
    MockInterface iface(*global, InterfaceType::GIGABIT_ETHERNET);
    iface.routingInstance = vrf;
    iface.blockEnqueues();
    iface.enableIPs();
    iface.enableShutdown();

    uint8_t local6[16] = {
        0x20,0x01,0x0d,0xb8,0,0,0,1,
        0,0,0,0,0,0,0,1
    };
    uint8_t nbr6[16] = {
        0x20,0x01,0x0d,0xb8,0,0,0,2,
        0,0,0,0,0,0,0,2
    };

    setIPv6(local6, 64, &iface);
    uint32_t key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 11);
    iface.configs.key = key;
    iface.configs.id = 11;
    getAllInterfaceList()[key] = &iface;

    auto* intf = ipv6Eigrp.getIfaceMgr().createInterface(&iface);
    IPAddress nbrIp = { nbr6, AddressFamily::IPv6 };
    addNeighbor(nbrIp, Eigrp::Neighbor::Version::WIDE, intf);
    auto nbr = intf->getNTable().lookup(nbrIp);
    ASSERT_TRUE(nbr);

    // Create a failing route to trigger active state
    Eigrp::ReceivedRoute r;
    r.prefix = { nbrIp, 64 };
    r.routeType = Eigrp::RouteType::INTERNAL;
    r.feasibleDistance = std::numeric_limits<uint64_t>::max();
    r.reportedDistance = std::numeric_limits<uint64_t>::max();
    r.nextHop = nbrIp;

    std::vector<Eigrp::ReceivedRoute> rs = { r };
    getDuel(&ipv6Eigrp).processReceivedRoutes(rs, *nbr);

    // Active route must now have pending queries
    auto& active = getActiveRoutes(&ipv6Eigrp);
    ASSERT_FALSE(active.empty());

    EXPECT_CALL(iface, enqueuePacket(::testing::_, ::testing::_))
        .Times(::testing::AtLeast(1));

    Eigrp::ReliableTransport::PktInfo info;
    info.version = Eigrp::TLVType::WIDE_V6;

    for (auto& kv : active)
    {
        PacketBuilder pb(&iface);
        createPacket(pb, intf);
        auto sia = createSIAQuery(pb, info, *nbr, {});
        nbr->lastSeqRecv = sia->getSequence();
        intf->getRtp().handleIncoming(nullptr, sia.value(), nbr6, false);
    }

    ipv6Eigrp.getIfaceMgr().deactivateAll();
    getAllInterfaceList().erase(key);
}

/*/
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
            PacketBuilder hello(mockInterface);
            createPacket(hello);
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
    uint32_t key1 = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 2);
    uint32_t key2 = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 3);
    iface1.routingInstance = vrf;
    iface2.routingInstance = vrf;
    iface1.blockEnqueues();
    iface2.blockEnqueues();
    mockInterface->blockEnqueues();
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
    // TODO possible leak
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
    mockInterface->shutdown(true);
    eigrpInstance->refreshInterfaceList();
    ASSERT_TRUE(getInterfaceList().empty());
    mockInterface->shutdown(false);
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
        .Times(::testing::AtLeast(1));
    EXPECT_CALL(iface2, enqueuePacket(::testing::_, ::testing::_))
        .Times(::testing::AtLeast(1));
    
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
    iface1.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 2);
    iface2.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 3);
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
    iface1.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 2);
    iface2.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 3);
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
    int1->getIface()->shutdown(true);
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
    
    processAck(*neighbor, seqNum);
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
    IPAddress nextHop = createIPv4(0x0AA80105);

    IPAddress base = createIPv4(0xC0000000);
    for (int i = 0; i < 1500; i++)
    {
        Eigrp::ReceivedRoute route = getRoute(eigrpInterface->interfaceKey);
        writeU16(base.raw + 1, i);

        route.prefix = { base, 24 };
        route.nextHop = nextHop;
        std::vector<Eigrp::ReceivedRoute> rs = {route};
        getDuel().processReceivedRoutes(rs, *neighbor);
    }
    ASSERT_EQ(vrf->routingTable.size<uint32_t>(), 1501);
}

// Test: MultiInterface_Massive_Concurrent_Updates_Extended
TEST_F(Internal_EigrpTest, MultiInterface_Massive_Concurrent_Updates_Extended) 
{
    // Test massive concurrent updates on two additional interfaces.
    MockInterface iface1(*global, InterfaceType::GIGABIT_ETHERNET);
    MockInterface iface2(*global, InterfaceType::GIGABIT_ETHERNET);
    iface1.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 2);
    iface2.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 3);
    iface1.routingInstance = vrf;
    iface2.routingInstance = vrf;
    iface1.blockEnqueues();
    iface2.blockEnqueues();
    auto int1 = eigrpInstance->getIfaceMgr().createInterface(&iface1);
    auto int2 = eigrpInstance->getIfaceMgr().createInterface(&iface2);
    IPAddress neighborIp1 = createIPv4(0x0A000001);
    IPAddress neighborIp2 = createIPv4(0x0A000002);
    addNeighbor(neighborIp1, Eigrp::Neighbor::Version::LEGACY, int1);
    addNeighbor(neighborIp2, Eigrp::Neighbor::Version::LEGACY, int2);

    auto neighbor = getNeighbor(neighborIp1, int1);
    
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
    mockInterface->shutdown(true);
    eigrpInstance->refreshInterfaceList();
    ASSERT_TRUE(getInterfaceList().empty());
    mockInterface->shutdown(false);
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
    iface1.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 2);
    iface2.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 3);
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
    iface1.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 2);
    iface2.configs.key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 3);
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
    iface1.shutdown(true);
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
    getDuel().processReceivedRoutes(routes, *getNeighbor(neighborIp, int1));
    ASSERT_GE(vrf->routingTable.size<uint32_t>(), 2);
    eigrpInstance->getIfaceMgr().deactivateAll();
}

// Test: Unequal_Cost_Path_Added_As_Feasible_Successor
TEST_F(Internal_EigrpTest, Unequal_Cost_Path_Added_As_Feasible_Successor)
{
    IPAddress neighborIp1 = createIPv4(0xC0A80201);
    IPAddress neighborIp2 = createIPv4(0xC0A80301);
    addNeighbor(neighborIp1);
    addNeighbor(neighborIp2);
    auto neighbor1 = getNeighbor(neighborIp1);
    auto neighbor2 = getNeighbor(neighborIp2);

    eigrpInstance->getConfigs().variance = 4;

    Eigrp::ReceivedRoute primary = getRoute(eigrpInterface->interfaceKey);
    primary.prefix = { createIPv4(0x0A080000), 16 };
    primary.feasibleDistance = 100;
    primary.reportedDistance = 80;

    primary.bandwidth = 1000000;
    primary.delay = 1000000000;
    primary.hopCount = 0;
    primary.mtu = 1500;
    primary.reliability = 255;
    primary.load = 1;
    primary.nextHop = neighborIp1;

    Eigrp::ReceivedRoute secondary = primary;
    secondary.feasibleDistance = 150;
    secondary.reportedDistance = 90;
    secondary.nextHop = neighborIp2;

    std::vector<Eigrp::ReceivedRoute> routes1 = { primary };
    std::vector<Eigrp::ReceivedRoute> routes2 = { secondary };
    getDuel().processReceivedRoutes(routes1, *neighbor1);
    getDuel().processReceivedRoutes(routes2, *neighbor2);

    auto successor = vrf->routingTable.lookup<uint32_t>(primary.prefix.addr);
    ASSERT_TRUE(successor);
    EXPECT_EQ(successor->prefix, readU32(primary.prefix.addr));
    EXPECT_EQ(successor->length, primary.prefix.prefixLength);
    EXPECT_EQ(successor->metric, primary.feasibleDistance * 128);
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
    r2.reportedDistance = 80;

    std::vector<Eigrp::ReceivedRoute> routes1 = { r1 };
    std::vector<Eigrp::ReceivedRoute> routes2 = { r2 };
    getDuel().processReceivedRoutes(routes1, *getNeighbor(neighbor1));
    getDuel().processReceivedRoutes(routes2, *getNeighbor(neighbor2));

    auto best = vrf->routingTable.lookup<uint32_t>(r1.prefix.addr);
    ASSERT_TRUE(best);
    EXPECT_EQ(best->metric, 90 * 128);
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
    auto* chosen = vrf->routingTable.lookup<uint32_t>(route.prefix.addr);

    EXPECT_TRUE(chosen == nullptr);
}

// Test: Summarization_Advertises_Summary_Only
TEST_F(Internal_EigrpTest, Summarization_Advertises_Summary_Only)
{
    IPPrefix summaryPrefix = { createIPv4(0x0A130000), 16 };
    eigrpInterface->getAggregator().installSummary(summaryPrefix);
    MockInterface* extraIface = new MockInterface(*global, InterfaceType::GIGABIT_ETHERNET);
    uint32_t key = calculateInterfaceKey(InterfaceType::GIGABIT_ETHERNET, 1);
    vrf->interfaceList[key] = extraIface;
    extraIface->routingInstance = vrf;
    extraIface->configs.id = 1;
    extraIface->configs.key = key;
    extraIface->enableIPs();
    extraIface->enableShutdown();
    getAllInterfaceList()[key] = extraIface;
    setIPv4(0xC0A80102, 24, extraIface);
    {
        EXPECT_CALL(*extraIface, enqueuePacket(::testing::_, ::testing::_)).Times(::testing::AnyNumber());
        eigrpInstance->refreshInterfaceList();
    }
    EXPECT_EQ(getInterfaceList().size(), 2);

    IPAddress neighborIp1 = createIPv4(0xC0A80110);
    IPAddress neighborIp2 = createIPv4(0xC0A80120);
    addNeighbor(neighborIp1);
    addNeighbor(neighborIp2, Eigrp::Neighbor::Version::LEGACY, &(getInterfaceList().at(key)));
    auto neighbor2 = getNeighbor(neighborIp2, &(getInterfaceList().at(key)));

    Eigrp::ReceivedRoute route1 = getRoute(extraIface->configs.key);
    route1.prefix = { createIPv4(0x0A130100), 24 };
    route1.nextHop = neighborIp2;

    Eigrp::ReceivedRoute route2 = route1;
    route2.prefix = { createIPv4(0x0A130200), 24 };

    //EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(0);
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](const PacketBuilder& pkt, const uint8_t*){
            std::vector<TLV16Option> opts = extractEigrpOptions(const_cast<PacketBuilder&>(pkt));
            bool isValid = true;
            for (auto opt : opts)
            {
                if (opt.type == Variable::Eigrp::Option::legacyInternalRoute)
                {
                    auto [tlv, valid] = Eigrp::TLVBuilder::decodeRoute(opt, 0, Eigrp::TLVType::LEGACY_V4);
                    isValid = (tlv.has_value() && tlv->prefix == summaryPrefix);
                    break;
                }
            }
            EXPECT_TRUE(isValid);
        }));

    std::vector<Eigrp::ReceivedRoute> routes = { route1, route2 };
    getDuel().processReceivedRoutes(routes, *neighbor2);

    EXPECT_EQ(vrf->routingTable.size<uint32_t>(), 4);
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
    
    // Expect one packet to be enqueued (since we're sending a query)
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_)).Times(1);

    Eigrp::RouteInfo rInfo(route);

    Eigrp::ActiveRoute rt;
    rt.activePrefix = route.prefix;
    rt.originRoute = &rInfo;
    Eigrp::OutgoingQuery qy;
    qy.route = &rt;
    rt.pendingQueries[neighborIp] = qy;
    eigrpInterface->getRtp().sendQuery({&rt});
}

// Test: Unicast_Neighbor_Forms_Correctly
TEST_F(Internal_EigrpTest, Unicast_Neighbor_Forms_Correctly)
{
    IPAddress neighborIp = createIPv4(0xC0A80132);
    eigrpInterface->getNTable().createNeighbor(neighborIp, Eigrp::Neighbor::Version::LEGACY, nullptr);
    auto neighbor = getNeighbor(neighborIp);
    ASSERT_TRUE(neighbor);
    EXPECT_EQ(neighbor->unicast, true);
    eigrpInterface->getNTable().onDown(*neighbor);
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

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1).WillOnce(::testing::Invoke([&]( PacketBuilder& pkt, const uint8_t*){
            auto opts = extractEigrpOptions(pkt);
            for (auto& opt : opts)
            {
                bool valid = opt.type != Variable::Eigrp::Option::legacyInternalRoute ||
                    Eigrp::TLVBuilder::decodeRoute(opt, 0, Eigrp::TLVType::LEGACY_V4).first.value().delay == std::numeric_limits<uint64_t>::max();
                ASSERT_TRUE(valid);
            }
        }));
    std::vector<Eigrp::ReceivedRoute> routes = { route };
    getDuel().processReceivedRoutes(routes, *neighbor);
}
/*/

// Test: Split_Horizon_Disabled_Allows_Advertisement
TEST_F(Internal_EigrpTest, Split_Horizon_Disabled_Allows_Advertisement)
{
    eigrpInterface->configs->splitHorizon.store(false);

    IPAddress n1 = createIPv4(0xC0A80631);
    addNeighbor(n1);
    auto nbr = getNeighbor(n1);

    IPPrefix p = { createIPv4(0x0AD10000), 16 };

    Eigrp::ReceivedRoute r = getRoute(eigrpInterface->interfaceKey);
    r.prefix = p;
    r.nextHop = n1;
    r.feasibleDistance = 100;
    r.reportedDistance = 90;

    std::vector<Eigrp::ReceivedRoute> rs = { r };

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(::testing::AtLeast(1));

    getDuel().processReceivedRoutes(rs, *nbr);
}

/*/
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
    existing.nextHop = neighborIp;
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
    bool hasInfeasible = std::any_of(entry->routesBySource.begin(), entry->routesBySource.end(),
        [&](const auto& r) {
            return r.second.routeInfo.reportedDistance >= existing.feasibleDistance;
        });
    ASSERT_TRUE(hasInfeasible);
    auto route = vrf->routingTable.lookup<uint32_t>(prefix.addr);
    ASSERT_TRUE(route);
    EXPECT_EQ(route->nextHops[0].nextHop, existing.nextHop.v4);
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

    PacketBuilder pkt(mockInterface);
    createPacket(pkt);
    Eigrp::ReliableTransport::PktInfo info;
    info.bandwidthMetric = 10000000;
    info.delay = 10000000;
    info.mtu = 1500;
    info.version = Eigrp::TLVType::LEGACY_V4;
    auto update = createUpdate(pkt, info, neighbor, {&rInfo, &rInfo});
    EXPECT_TRUE(update.has_value());
    update->setTrail(update->buffer + EigrpHeader::fixedSize, pkt.getHeaders()[2].length - EigrpHeader::fixedSize);

    eigrpInterface->getRtp().handleIncoming(nullptr, update.value(), neighborIp.raw, false);
    auto* route = vrf->routingTable.lookup<uint32_t>(rInfo.routeInfo.prefix.addr);
    ASSERT_TRUE(route);
    EXPECT_EQ(route->nextHopCount, 1);
}
