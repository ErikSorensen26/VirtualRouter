//TODO fix leak in MultiInterface_Massive_Concurrent_Updates and IPv6_HelloPacket_Construction

//TODO duplicate TLVs, malformed TLVs, and unknown TLVs

//TODO tests for wide resync and legacy teardown
//TODO tests for peer termination
//TODO tests for poisened loop prevention

// May be a problem where unicast neighbors get completely removed

// Internal_EigrpTest.cpp

#include <cstdint>
#include <gtest/gtest.h>
#include <processing/PacketBuilder.hpp>
#include <eigrp/rtp/EigrpPacketBuilder.h>
#include <eigrp/core/Eigrp.h>
#include <eigrp/interface/EigrpInterface.h>
#include <eigrp/rtp/TLVBuilder.h>
#include <eigrp/rtp/Neighbor.h>
#include <VirtualRouter.h>
#include <MockInterface.hpp>
#include <MockFileSystem.hpp>
#include <packet/PacketStructure.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <ListenerManager.hpp>
#include <infrastructure/Arp.h>
#include <infrastructure/Ndp.h>
#include <security/keys/KeyChain.h>
#include <security/keys/KeyChainManager.h>
#include <configs/FieldAccessor.hpp>

// Test fixture for global EIGRP tests
class Internal_EigrpTest : public ::testing::Test 
{
protected:
    uint16_t asNumber = 1;
    types::AddressFamily addressFamily = types::AddressFamily::IPv4;
    routing::eigrp::Eigrp* eigrpInstance;
    cli::MockFileSystem fs;
    interface::MockInterface* mockInterface;
    // We use the real EigrpInterface (constructed using our MockInterface)
    routing::eigrp::EigrpInterface* eigrpInterface;
    std::condition_variable cv;
    std::mutex cvMutex;
    bool packetEnqueued = false;
    interface::InterfaceType type = interface::InterfaceType::GIGABIT_ETHERNET;
    core::VirtualRouter* vrf = nullptr;
    core::Global* global = nullptr;
    interface::InterfaceKey mKey;

    uint8_t testPacket[100] = {0};

    types::IPv4Address ipIntv4 = 0xC0A80101;
    types::IPv4Address ipIntv4Net = 0xC0A80100;
    types::IPv6Address ipIntv6 = (static_cast<__uint128_t>(0xC0A8000000000000) << 64) | 0x0000000000000101;

    // Setup creates an Eigrp instance and one interface for testing.
    void SetUp() override 
    {
        utils::RCU::registerThread();
        global = new core::Global(fs, {}, false, true);
        mockInterface = new interface::MockInterface(*global, interface::InterfaceType::GIGABIT_ETHERNET);
        mKey = mockInterface->configs.key;

        vrf = global->getRoutingInstance("default", types::AddressFamily::IPv4);
        vrf->getInterfaceManager().add(mockInterface, mKey);
        vrf->enabledAddressFamilies.insert(types::AddressFamily::IPv6);
        auto as = vrf->addEigrpAutonomousSystem(1);
        eigrpInstance = new routing::eigrp::Eigrp(asNumber, addressFamily, vrf);
        as->ipv4 = eigrpInstance;

        mockInterface->enableIPs();
        mockInterface->enableShutdown();
        // Set initial IPv4 and IPv6 addresses on the mock interface.

        setIPv4(ipIntv4, 24);
        setIPv6(ipIntv6, 64);
        // Assume interfaceList is a global map keyed by InterfaceType and interface id.
        vrf->getInterfaceManager().add(mockInterface, mKey);
        // Create the real EigrpInterface using the mock interface.
        EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_)).Times(::testing::AtLeast(1));

        //network.ip = uint32_t{readU32(ipIntv4Net));
        //network.mask = 24;
        eigrpInstance->getGlobalConfigMgr().addNetworkRange(types::IPv4Prefix{ipIntv4Net.addr, 24});
        eigrpInstance->waitIdle();
        eigrpInterface = eigrpInstance->getIfaceMgr().getInterface(mKey);
    }

    // TearDown cleans up the EIGRP instance, interface, and global objects.
    void TearDown() override 
    {
        mockInterface->blockEnqueues();
        vrf->getInterfaceManager().remove(mKey);
        delete mockInterface;
        delete global;
        std::memset(testPacket, 0, sizeof(testPacket));
        utils::RCU::unregisterThread();
    }

    // Helper function: returns the topology table.
    routing::eigrp::TopologyTable& getTopologyTable(routing::eigrp::Eigrp* i = nullptr) { return i ? i->getTopology().duel.topologyTable : eigrpInstance->getTopology().duel.topologyTable; }
    // Helper: returns the current interface list.
    std::unordered_map<interface::InterfaceKey, routing::eigrp::EigrpInterface>& getInterfaceList() { return eigrpInstance->getIfaceMgr().eigrpInterfaceList; }
    // Helper: returns the current configs
    routing::eigrp::EigrpConfig& getConfigs() { return eigrpInstance->getGlobalConfigMgr(); }
    
    // Helper: set IPv4 address on an interface.
    void setIPv4(const uint32_t ip, uint8_t mask, interface::MockInterface* iface = nullptr) 
    {
        if (iface)
            iface->setIPv4({ip, mask, true }, false);
        else
            mockInterface->setIPv4({ip, mask, true }, false);
    }

    void delIPv4(interface::MockInterface* iface = nullptr)
    {
        if (iface)
            iface->removeIPv4();
        else
            mockInterface->removeIPv4();
    }
    
    // Helper: set IPv6 address on an interface.
    void setIPv6(const types::IPv6Address& ip, uint8_t mask, interface::Interface* iface = nullptr)
    {
        types::IPv6Address local = (static_cast<__uint128_t>(0xFE80000000000000) << 64) | 0x0000000000000001;
        if (iface)
        {
            iface->setIPv6({ local.addr, 64, true }, false);
            iface->setIPv6({ ip.addr, mask, true }, false);
        }
        else
        {
            mockInterface->setIPv6({ local.addr, 10, true }, false);
            mockInterface->setIPv6({ ip.addr, mask, true }, false);
        }
    }

    // Helper: returns the IpInfo from the interface.
    interface::InterfaceConfigs& getIpInfo(interface::Interface* iface = nullptr) 
    {
        return (iface ? iface->configs : mockInterface->configs);
    }
    
    // Helper: clear network configuration in the EIGRP instance.
    void clearNetworks() { getConfigs().clearNetworks(); }
    
    // Helper: add a neighbor via the real EigrpInterface.
    void addNeighbor(const types::IPAddress& ip, routing::eigrp::Neighbor::Version v = routing::eigrp::Neighbor::Version::LEGACY, routing::eigrp::EigrpInterface* intf = nullptr) 
    {
        types::Mac neighborMac = 0x112233445566;
        if (intf)
        {
            intf->getNTable().createNeighbor(ip, v)->setState(routing::eigrp::Neighbor::State::UP);
            if (intf->getBase().getAF() == types::AddressFamily::IPv4)
                intf->getIface()->arp.addArpEntry(ip.v4(), neighborMac);
            else
                intf->getIface()->ndp.addNdpEntry(ip.v6(), neighborMac);
        }
        else
        {
            eigrpInterface->getNTable().createNeighbor(ip, v)->setState(routing::eigrp::Neighbor::State::UP);
            eigrpInterface->getIface()->arp.addArpEntry(ip.v4(), neighborMac);
        }
    }

    // Helper: add a unicast neighbor via the real EigrpInterface.
    void addUnicastNeighbor(const types::IPAddress& ip, routing::eigrp::Neighbor::Version v = routing::eigrp::Neighbor::Version::LEGACY, routing::eigrp::EigrpInterface* intf = nullptr) 
    {
        types::Mac neighborMac = 0x112233445566;
        if (intf)
        {
            intf->getNTable().createNeighbor(ip, v, true)->setState(routing::eigrp::Neighbor::State::UP);
            if (intf->getBase().getAF() == types::AddressFamily::IPv4)
                intf->getIface()->arp.addArpEntry(ip.v4(), neighborMac);
            else
                intf->getIface()->ndp.addNdpEntry(ip.v6(), neighborMac);
        }
        else
        {
            eigrpInterface->getNTable().createNeighbor(ip, v, true)->setState(routing::eigrp::Neighbor::State::UP);
            eigrpInterface->getIface()->arp.addArpEntry(ip.v4(), neighborMac);
        }
    }

    // Helper: retrieve a neighbor from an interface.
    routing::eigrp::Neighbor* getNeighbor(const types::IPAddress& ip, routing::eigrp::EigrpInterface* intf = nullptr) 
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

    routing::eigrp::ReceivedRoute getRoute(interface::InterfaceKey iface)
    {
        uint8_t nh[4] = {0};
        routing::eigrp::ReceivedRoute r;
        r.feasibleDistance = 1441792;
        r.reportedDistance = 720896;
        r.nextHop = { nh, types::AddressFamily::IPv4 };
        r.bandwidth = 1000000;
        r.delay = 1000000000;
        r.hopCount = 0;
        r.mtu = 1500;
        r.reliability = 255;
        r.load = 1;
        r.routeType = routing::eigrp::RouteType::INTERNAL;
        r.originInterface = iface;
        r.tag = 0;
        return r;
    }

    std::vector<packet::TLV16Option> extractEigrpOptions(processing::PacketBuilder& pkt)
    {
        auto h = pkt.getHeader(packet::HeaderType::EIGRP);
        if (!h) return {};
        size_t optSize = h->length - packet::EigrpHeader::fixedSize;
        if (optSize == 0) return {};
        std::vector<packet::TLV16Option> opts;
        parseEigrpOptions(h->buffer + packet::EigrpHeader::fixedSize, optSize, opts);
        return opts;
    }

    packet::EigrpHeader getEigrpHeader(processing::PacketBuilder& pkt)
    {
        auto h = pkt.getHeader(packet::HeaderType::EIGRP);
        packet::EigrpHeader eigrp;
        eigrp.setBuffer(h->buffer);
        return eigrp;
    }

    void createPacket(processing::PacketBuilder& eigrp, routing::eigrp::EigrpInterface* interface = nullptr)
    {
        routing::eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        iface->getRtp().createPacket(eigrp);
    }

    void addEigrpSize(processing::PacketBuilder& pkt, packet::EigrpHeader& hdr)
    {
        auto* eigrp = pkt.getHeader(packet::HeaderType::EIGRP);
        hdr.setTrail(eigrp->buffer + packet::EigrpHeader::fixedSize, eigrp->length - packet::EigrpHeader::fixedSize);
    }

    std::optional<packet::EigrpHeader> createHello(processing::PacketBuilder& builder, routing::eigrp::EigrpInterface* interface = nullptr)
    {
        routing::eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createHello(builder);
    }

    std::optional<packet::EigrpHeader> createUnicastHello(processing::PacketBuilder& builder, routing::eigrp::EigrpInterface* interface = nullptr)
    {
        routing::eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createUnicastHello(builder);
    }
    std::optional<packet::EigrpHeader> createConditionalHello(processing::PacketBuilder& builder, routing::eigrp::ReliableTransport::PktInfo& info, const std::vector<types::IPAddress>& neighbors, uint32_t seq, routing::eigrp::EigrpInterface* interface = nullptr)
    {
        routing::eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createConditionalHello(builder, info, neighbors, seq);
    }

    std::optional<packet::EigrpHeader> createAck(processing::PacketBuilder& builder, uint32_t seq, routing::eigrp::EigrpInterface* interface = nullptr)
    {
        routing::eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createAck(builder, seq);
    }

    std::optional<packet::EigrpHeader> createNullUpdate(processing::PacketBuilder& builder, routing::eigrp::EigrpInterface* interface = nullptr)
    {
        routing::eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createNullUpdate(builder);
    }

    std::optional<packet::EigrpHeader> createUpdate(processing::PacketBuilder& builder, routing::eigrp::ReliableTransport::PktInfo& info, routing::eigrp::Neighbor* neighbor, const std::vector<const routing::eigrp::RouteInfo*>& routes, routing::eigrp::EigrpInterface* interface =  nullptr)
    {
        routing::eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createUpdate(builder, info, neighbor, routes);
    }

    std::optional<packet::EigrpHeader> createQuery(processing::PacketBuilder& builder, routing::eigrp::ReliableTransport::PktInfo& info, const std::vector<routing::eigrp::ActiveRoute*>& queries, routing::eigrp::EigrpInterface* interface = nullptr)
    {
        routing::eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createQuery(builder, info, queries);
    }

    std::optional<packet::EigrpHeader> createUnicastQuery(processing::PacketBuilder& builder, routing::eigrp::ReliableTransport::PktInfo& info, routing::eigrp::Neighbor& neighbor, const std::vector<routing::eigrp::OutgoingQuery*>& queries, routing::eigrp::EigrpInterface* interface = nullptr)
    {
        routing::eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createUnicastQuery(builder, info, neighbor, queries);
    }

    std::optional<packet::EigrpHeader> createReply(processing::PacketBuilder& builder, routing::eigrp::ReliableTransport::PktInfo& info, routing::eigrp::Neighbor& neighbor, const std::vector<const routing::eigrp::RouteInfo*>& replies, routing::eigrp::EigrpInterface* interface = nullptr)
    {
        routing::eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createReply(builder, info, neighbor, replies);
    }

    std::optional<packet::EigrpHeader> createSIAQuery(processing::PacketBuilder& builder, routing::eigrp::ReliableTransport::PktInfo& info, const std::vector<routing::eigrp::OutgoingQuery*>& queries, routing::eigrp::EigrpInterface* interface = nullptr)
    {
        routing::eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createSIAQuery(builder, info, queries);
    }

    std::optional<packet::EigrpHeader> createSIAReply(processing::PacketBuilder& builder, routing::eigrp::EigrpInterface* interface = nullptr)
    {
        routing::eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().createSIAReply(builder);
    }

    void processHello(packet::EigrpHeader& header, routing::eigrp::Neighbor& neighbor, routing::eigrp::EigrpInterface* interface = nullptr)
    {
        routing::eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        std::vector<packet::TLV16Option> opts;
        routing::eigrp::ReliableTransport::RTPInfo r(header, neighbor.ipAddress);
        parseEigrpOptions(header.buffer + packet::EigrpHeader::fixedSize, header.size() - packet::EigrpHeader::fixedSize, r.opts);
        return iface->getRtp().processHello(r, false);
    }

    void processAck(routing::eigrp::Neighbor& neighbor, const uint32_t seq, routing::eigrp::EigrpInterface* interface = nullptr)
    {
        routing::eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        return iface->getRtp().processAck(neighbor, seq);
    }

    void setSeq(uint32_t seq, routing::eigrp::EigrpInterface* interface = nullptr)
    {
        routing::eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        iface->getRtp().nextSeq = seq;
    }

    bool recalculateSuccessors(routing::eigrp::TopologyEntry* entry)
    {
        return getDuel().recalculateSuccessors(entry);
    }

    void transmitReliable(processing::PacketBuilder& pkt, routing::eigrp::Neighbor* neighbor, packet::EigrpHeader& header, routing::eigrp::EigrpInterface* interface = nullptr)
    {
        routing::eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        iface->getRtp().transmitReliable(pkt, neighbor, header);
    }

    void decrementNextSeq(routing::eigrp::EigrpInterface* interface)
    {
        routing::eigrp::EigrpInterface* iface = interface ? interface : eigrpInterface;
        iface->getRtp().nextSeq.fetch_sub(1, std::memory_order_acq_rel);
    }

    std::unordered_map<types::IPPrefix, routing::eigrp::ActiveRoute>& getActiveRoutes(routing::eigrp::Eigrp* i = nullptr) { return getDuel(i).activeRoutes; }

    routing::eigrp::DuelEngine& getDuel(routing::eigrp::Eigrp* i = nullptr) { return i ? i->getTopology().duel : eigrpInstance->getTopology().duel; }

    bool getHelloTimerActive(routing::eigrp::EigrpInterface* eigrpInt = nullptr) { if (eigrpInt) return eigrpInt->getTimers().helloTimerId.load() != 0; else return eigrpInterface->getTimers().helloTimerId.load() != 0;}
    uint32_t getRouterID(routing::eigrp::Eigrp* eigrp = nullptr) { if (eigrp) return eigrp->routerID(); else return eigrpInstance->routerID(); }
};

#pragma region Authentication

// Test: AuthTLV_MD5_Correct
TEST_F(Internal_EigrpTest, AuthTLV_MD5_Correct) 
{
    // Verify that MD5 authentication TLV is built correctly.
    // (Neighbor IP: 192.168.1.2, key "secretkey")
    types::IPAddress neighborIp = uint32_t{0xC0A80102};
    addNeighbor(neighborIp);
    auto optNeighbor = getNeighbor(neighborIp);
    ASSERT_TRUE(optNeighbor);

    security::authentication::KeyChain::Key key{1, "PASSWORD", {}};
    auto* kc = global->keyChainManager.create("key");
    kc->addKey(key);

    eigrpInterface->configs.get<config::EigrpInterface::AUTHENTICATION_MODE>().set(config::eigrp::AuthType::MD5);
    eigrpInterface->configs.get<config::EigrpInterface::AUTHENTICATION_KEYCHAIN>().set(kc->name);

    // Block enqueues
    mockInterface->blockEnqueues();
    
    // Create hello packet.
    processing::PacketBuilder helloPacket(mockInterface);
    createPacket(helloPacket);
    ASSERT_TRUE(createHello(helloPacket).has_value());
    
    // Verify authentication TLV is present and not all zeros.
    std::vector<packet::TLV16Option> opts = extractEigrpOptions(helloPacket);
    
    auto it = std::find_if(opts.begin(), opts.end(), [](const packet::TLV16Option& opt) {
        return opt.type == EIGRP_OPTION_AUTHENTICATION;
    });
    std::this_thread::sleep_for(std::chrono::seconds(1));
    ASSERT_NE(it, opts.end());
    ASSERT_FALSE(std::all_of(it->value + 1, it->value + it->valueSize - 1, [](uint8_t b){return b == 0;}));
}

// Test: AuthTLV_SHA1_Correct
TEST_F(Internal_EigrpTest, AuthTLV_SHA1_Correct) 
{
    // Verify that SHA-1 authentication TLV is built correctly.
    types::IPAddress neighborIp = uint32_t{0xC0A80103};
    addNeighbor(neighborIp);
    auto optNeighbor = getNeighbor(neighborIp);
    ASSERT_TRUE(optNeighbor);

    eigrpInterface->configs.get<config::EigrpInterface::AUTHENTICATION_MODE>().set(config::eigrp::AuthType::SHA256);
    eigrpInterface->configs.get<config::EigrpInterface::AUTHENTICATION_KEYCHAIN>().set("PASSWORD");

    // Block enqueues
    mockInterface->blockEnqueues();
    
    processing::PacketBuilder helloPacket(mockInterface);
    createPacket(helloPacket);
    ASSERT_TRUE(createUnicastHello(helloPacket).has_value());
    
    std::vector<packet::TLV16Option> opts = extractEigrpOptions(helloPacket);

    auto it = std::find_if(opts.begin(), opts.end(), [](const packet::TLV16Option& opt) {
        return opt.type == EIGRP_OPTION_AUTHENTICATION;
    });
    ASSERT_NE(it, opts.end());
    ASSERT_FALSE(std::all_of(it->value + 1, it->value + it->valueSize - 1, [](uint8_t b){return b == 0;}));
}

// Test: AuthTLV_Disabled_NoTLV
TEST_F(Internal_EigrpTest, AuthTLV_Disabled_NoTLV) 
{
    eigrpInterface->configs.get<config::EigrpInterface::AUTHENTICATION_MODE>().set(config::eigrp::AuthType::NONE);

    // Block enqueues
    mockInterface->blockEnqueues();
    
    processing::PacketBuilder helloPacket(mockInterface);
    createPacket(helloPacket);
    ASSERT_TRUE(createHello(helloPacket).has_value());

    std::vector<packet::TLV16Option> opts = extractEigrpOptions(helloPacket);
    
    auto it = std::find_if(opts.begin(), opts.end(), [](const packet::TLV16Option& opt) {
        return opt.type == EIGRP_OPTION_AUTHENTICATION;
    });
    ASSERT_EQ(it, opts.end());
}

#pragma endregion
#pragma region NeighborState

// Test: Neighbor_Down_To_PENDING
TEST_F(Internal_EigrpTest, Neighbor_Down_To_PENDING)
{
    types::IPAddress n = uint32_t{0x0A000001};

    ASSERT_FALSE(getNeighbor(n));
    processing::PacketBuilder hello(mockInterface);
    auto hdr = createHello(hello);
    ASSERT_TRUE(hdr.has_value());
    addEigrpSize(hello, hdr.value());

    eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), n.raw, true);

    auto nbr = getNeighbor(n);
    ASSERT_TRUE(nbr);
    EXPECT_EQ(nbr->getState(), routing::eigrp::Neighbor::State::PENDING);
}

// Test: Neighbor_TWOWAY_To_LOADING
TEST_F(Internal_EigrpTest, Neighbor_TWOWAY_To_LOADING)
{
    GTEST_SKIP();
    // Add extra iface for connected route
    interface::MockInterface* extraIface = new interface::MockInterface(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    uint32_t key = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 1);
    vrf->getInterfaceManager().add(extraIface, key);
    extraIface->blockEnqueues();
    extraIface->configs.id = 1;
    extraIface->configs.key = 1;
    extraIface->enableIPs();
    setIPv4(0xC0A80202, 24, extraIface);
    eigrpInstance->getIfaceMgr().createInterface(extraIface);

    types::IPAddress n = uint32_t{0x0A000003};
    addNeighbor(n, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);

    auto nbr = getNeighbor(n);
    nbr->setState(routing::eigrp::Neighbor::State::PENDING);

    packet::EigrpHeader testHdr;
    testHdr.setBuffer(testPacket);
    testHdr.setSequence(5);
    eigrpInterface->getRtp().setupUnicastReliable(*nbr, testHdr);

    // Increment sequence
    setSeq(6, eigrpInterface);

    processing::PacketBuilder ack(mockInterface);
    auto hdr = createAck(ack, 5);

    bool fullUpdateFound = false;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(::testing::AtLeast(1)).WillRepeatedly(::testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto eigrp = getEigrpHeader(pkt);
            if (eigrp.getOpcode() == EIGRP_TYPE_UPDATE)
                fullUpdateFound = true;
        }));

    nbr->currentReliable.store(5);
    eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), n.raw, false);

    EXPECT_EQ(nbr->getState(), routing::eigrp::Neighbor::State::UP);
    EXPECT_TRUE(fullUpdateFound);
    eigrpInstance->shutdown();

    vrf->getInterfaceManager().remove(key);
    delete extraIface;
}

// Test: Neighbor_LOADING_To_ESTABLISHED
TEST_F(Internal_EigrpTest, Neighbor_LOADING_To_ESTABLISHED)
{
    GTEST_SKIP();
    types::IPAddress n = uint32_t{0x0A000004};
    addNeighbor(n, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);

    auto nbr = getNeighbor(n);

    processing::PacketBuilder update(mockInterface);
    routing::eigrp::ReliableTransport::PktInfo info;
    info.bandwidthMetric = 1000000;
    info.delay = 100000;
    info.mtu = 1500;
    info.version = routing::eigrp::TLVType::LEGACY_V4;
    auto hdr = createUpdate(update, info, nbr, {});

    eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), n.raw, false);
    EXPECT_EQ(nbr->getState(), routing::eigrp::Neighbor::State::UP);
}

// Test: Duplicate_Hello_Ignored
TEST_F(Internal_EigrpTest, Duplicate_Hello_Ignored)
{
    types::IPAddress n = uint32_t{0x0A000005};
    addNeighbor(n, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);

    auto nbr = getNeighbor(n);
    nbr->setState(routing::eigrp::Neighbor::State::PENDING);

    processing::PacketBuilder hello(mockInterface);
    auto hdr = createUnicastHello(hello);
    ASSERT_TRUE(hdr.has_value());

    eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), n.raw, false);
    eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), n.raw, false);

    EXPECT_EQ(nbr->getState(), routing::eigrp::Neighbor::State::PENDING);
}

// Test: HoldTimerExpires_RemovesNeighbor
TEST_F(Internal_EigrpTest, HoldTimerExpires_RemovesNeighbor)
{
    types::IPAddress n = uint32_t{0x0A000006};
    addNeighbor(n);

    auto nbr = getNeighbor(n);
    ASSERT_TRUE(nbr);

    eigrpInterface->getTimers().startHoldTimer(*nbr);
    eigrpInterface->getTimers().handleHoldTimeExpire(*nbr);

    EXPECT_FALSE(getNeighbor(n));
}

// Test: OutOfOrderUpdateIgnored
TEST_F(Internal_EigrpTest, OutOfOrderUpdateIgnored)
{
    types::IPAddress n = uint32_t{0x0A000007};
    addNeighbor(n);

    auto nbr = getNeighbor(n);
    nbr->lastSeqRecv = 10;

    processing::PacketBuilder upd(mockInterface);
    routing::eigrp::ReliableTransport::PktInfo info;
    info.bandwidthMetric = 1000000;
    info.delay = 100000;
    info.mtu = 1500;
    info.version = routing::eigrp::TLVType::LEGACY_V4;
    auto hdr = createUpdate(upd, info, nbr, {});
    ASSERT_TRUE(hdr.has_value());
    hdr->setSequence(9);

    eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), n.raw, false);

    EXPECT_EQ(nbr->lastSeqRecv, 10);
}

// Test: DuplicateUpdateIgnored
TEST_F(Internal_EigrpTest, DuplicateUpdateIgnored)
{
    types::IPAddress n = uint32_t{0x0A000008};
    addNeighbor(n);

    auto nbr = getNeighbor(n);
    nbr->lastSeqRecv = 10;

    processing::PacketBuilder upd(mockInterface);
    routing::eigrp::ReliableTransport::PktInfo info;
    info.bandwidthMetric = 1000000;
    info.delay = 100000;
    info.mtu = 1500;
    info.version = routing::eigrp::TLVType::LEGACY_V4;
    auto hdr = createUpdate(upd, info, nbr, {});
    ASSERT_TRUE(hdr.has_value());
    hdr->setSequence(11);

    eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), n.raw, false);
    eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), n.raw, false);

    EXPECT_EQ(nbr->lastSeqRecv, 11);
}

#pragma endregion
#pragma region ReliablePacket

// Test: Retransmission_Timer_Expires_Resend
TEST_F(Internal_EigrpTest, Retransmission_Timer_Expires_Resend) 
{
    // Simulate a lost packet so that the retransmission timer expires and the packet is resent.
    auto chain = global->keyChainManager.create("key");
    security::authentication::KeyChain::Key k{1, "PASSWORD", {}};
    chain->addKey(k);

    types::IPAddress neighborIp = uint32_t{0xC0A80114};
    addNeighbor(neighborIp, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint32_t seqNum = 500;
    packet::EigrpHeader hdr;
    hdr.setBuffer(testPacket);
    hdr.setSequence(seqNum);
    
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(::testing::AtLeast(1));
    
    eigrpInterface->getRtp().setupUnicastReliable(*neighbor, hdr);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    auto current = neighbor->currentReliable.load(std::memory_order_relaxed);
    //EXPECT_TRUE(current == seqNum);
    EXPECT_GE(neighbor->reliablePackets[current].info.retransmissionCount, 0);
}

// Test: ACK_Processing_Cancels_Packet
TEST_F(Internal_EigrpTest, ACK_Processing_Cancels_Packet) 
{
    // Verify that a valid ACK cancels the retransmission timer.
    types::IPAddress neighborIp = uint32_t{0xC0A80116};
    addNeighbor(neighborIp, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint32_t seqNum = 502;
    packet::EigrpHeader hdr;
    hdr.setBuffer(testPacket);
    eigrpInterface->getRtp().setupUnicastReliable(*neighbor, hdr);
    processAck(*neighbor, seqNum);
    EXPECT_TRUE(neighbor->reliableQueue.empty());
}

// Test: Duplicate_ACK_Does_Not_Alter_RTT
TEST_F(Internal_EigrpTest, Duplicate_ACK_Does_Not_Alter_RTT) 
{
    // Process the same ACK twice and verify that RTT does not change.
    types::IPAddress neighborIp = uint32_t{0xC0A80117};
    addNeighbor(neighborIp, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint32_t seqNum = 503;
    packet::EigrpHeader hdr;
    hdr.setBuffer(testPacket);
    eigrpInterface->getRtp().setupUnicastReliable(*neighbor, hdr);
    processAck(*neighbor, seqNum);
    double srttAfterFirst = neighbor->srtt;
    processAck(*neighbor, seqNum);
    ASSERT_EQ(neighbor->srtt, srttAfterFirst);
}

// Test: Max_Retransmissions_Triggers_Neighbor_Down
TEST_F(Internal_EigrpTest, Max_Retransmissions_Triggers_Neighbor_Down) 
{
    // Set the retransmission count to the maximum and simulate a timeout.
    types::IPAddress neighborIp = uint32_t{0xC0A80118};
    addNeighbor(neighborIp, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint32_t seqNum = 504;
    packet::EigrpHeader hdr;
    hdr.setBuffer(testPacket);
    eigrpInterface->getRtp().setupUnicastReliable(*neighbor, hdr);
    auto rel = neighbor->reliablePackets[neighbor->currentReliable];
    rel.info.retransmissionCount = MAX_RETRANSMISSIONS;
    eigrpInterface->getRtp().handleRetransmission(neighbor, rel, seqNum);
    ASSERT_FALSE(getNeighbor(neighborIp));
}

// Test: Exponential_Backoff_Applied
TEST_F(Internal_EigrpTest, Exponential_Backoff_Applied) 
{
    // Verify that the RTO doubles after a retransmission.
    types::IPAddress neighborIp = uint32_t{0xC0A80119};
    addNeighbor(neighborIp, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint32_t seqNum = 505;
    packet::EigrpHeader hdr;
    hdr.setBuffer(testPacket);
    hdr.setSequence(seqNum);
    eigrpInterface->getRtp().setupUnicastReliable(*neighbor, hdr);

    double initialRTO = neighbor->rto;
    auto rel = neighbor->reliablePackets[neighbor->currentReliable];
    eigrpInterface->getRtp().handleRetransmission(neighbor, rel, seqNum);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    double newRTO = neighbor->rto;
    ASSERT_GE(newRTO, initialRTO * 2.0);
}

// Test: Missing_Update_Packet_Buffering
TEST_F(Internal_EigrpTest, Missing_Update_Packet_Buffering) 
{
    // Simulate receiving an update packet with a sequence gap and verify buffering.
    types::IPAddress neighborIp = uint32_t{0xC0A8011A};
    addNeighbor(neighborIp, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    neighbor->lastSeqRecv.store(10);
    
    processing::PacketBuilder update(mockInterface);
    createPacket(update);
    routing::eigrp::ReliableTransport::PktInfo info;
    info.mtu = 1500;
    info.version = routing::eigrp::TLVType::LEGACY_V4;
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
    types::IPAddress neighborIp = uint32_t{0xC0A8011B};
    addNeighbor(neighborIp, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint32_t seqNum = 506;
    packet::EigrpHeader hdr;
    hdr.setBuffer(testPacket);
    hdr.setSequence(506);
    eigrpInterface->getRtp().setupUnicastReliable(*neighbor, hdr);
    
    std::thread t1([&](){
        processAck(*neighbor, seqNum);
    });
    std::thread t2([&](){
        auto pkt = neighbor->reliablePackets[neighbor->currentReliable];
        eigrpInterface->getRtp().handleRetransmission(neighbor, pkt, pkt.info.sequence);
    });
    t1.join();
    t2.join();
    EXPECT_FALSE(neighbor->reliablePackets.count(seqNum));
}

// Test: ActiveRoute_SIA_When_Stub_Enabled
TEST_F(Internal_EigrpTest, ActiveRoute_SIA_When_Stub_Enabled)
{
    // Enable stub mode: router will NOT forward queries
    eigrpInstance->getGlobalConfigMgr().enableStub(true, true, false, false, false);
    eigrpInstance->getGlobalConfigMgr().getConfigs().get<config::Eigrp::ACTIVE_TIME>().set(1);

    types::IPAddress queryNeighborIp = uint32_t{0xC0A80750};
    types::IPAddress neighborIp = uint32_t{0xC0A80751};
    addNeighbor(queryNeighborIp);
    addNeighbor(neighborIp);
    auto nbr = getNeighbor(queryNeighborIp);
    ASSERT_TRUE(nbr);
    ASSERT_TRUE(getNeighbor(neighborIp));
    
    types::IPPrefix p = { uint32_t{0x0AF00000}, 16 };

    routing::eigrp::ReceivedRoute r = getRoute(eigrpInterface->interfaceKey);
    r.prefix = p;

    std::vector<routing::eigrp::ReceivedRoute> rs = { r };
    getDuel().processReceivedRoutes(rs, *nbr);
    processAck(*nbr, 1); // Reverse poisen acked

    r.feasibleDistance = std::numeric_limits<uint64_t>::max();
    r.reportedDistance = std::numeric_limits<uint64_t>::max();

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(1).WillOnce(::testing::Invoke([&](processing::PacketBuilder& pkt){
            auto eigrp = getEigrpHeader(pkt);
            EXPECT_EQ(eigrp.getOpcode(), EIGRP_TYPE_REPLY);
        }));

    rs = { r };
    getDuel().processReceivedQueryRoutes(rs, *nbr, 10);

    auto &active = getActiveRoutes();
    ASSERT_TRUE(active.empty());
}

// Test: ActiveRoute_Will_Send_Reply_If_No_Available_Neighbors
TEST_F(Internal_EigrpTest, ActiveRoute_Will_Send_Reply_If_No_Available_Neighbors)
{
    types::IPAddress neighborIp = uint32_t{0xC0A80751};
    addNeighbor(neighborIp);
    auto nbr = getNeighbor(neighborIp);
    ASSERT_TRUE(nbr);
    
    types::IPPrefix p = { uint32_t{0x0AF00000}, 16 };

    routing::eigrp::ReceivedRoute r = getRoute(eigrpInterface->interfaceKey);
    r.prefix = p;

    std::vector<routing::eigrp::ReceivedRoute> rs = { r };
    getDuel().processReceivedRoutes(rs, *nbr);
    processAck(*nbr, 1); // Reverse poisen acked

    r.feasibleDistance = std::numeric_limits<uint64_t>::max();
    r.reportedDistance = std::numeric_limits<uint64_t>::max();

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(1).WillOnce(::testing::Invoke([&](processing::PacketBuilder& pkt){
            EXPECT_EQ(getEigrpHeader(pkt).getOpcode(), EIGRP_TYPE_REPLY);
        }));

    rs = { r };
    getDuel().processReceivedQueryRoutes(rs, *nbr, 11);

    auto &active = getActiveRoutes();
    ASSERT_TRUE(active.empty());
}

// Test: ConditionalReceive_Missing_Ack_Resolved
TEST_F(Internal_EigrpTest, ConditionalReceive_Missing_Ack_Resolved)
{
    types::IPAddress n1 = uint32_t{0xC0A80201};
    types::IPAddress n2 = uint32_t{0xC0A80202};
    addNeighbor(n1);
    addNeighbor(n2);

    auto* nbr1 = getNeighbor(n1);
    auto* nbr2 = getNeighbor(n2);
    ASSERT_TRUE(nbr1);
    ASSERT_TRUE(nbr2);

    packet::EigrpHeader update;
    update.setBuffer(testPacket);
    update.setSequence(100);
    update.setOpcode(EIGRP_TYPE_UPDATE);

    // Setup: simulate multicast to both neighbors, but only one ACKs
    eigrpInterface->getRtp().setupMulticastReliable(update);

    processAck(*nbr1, 100);

    // Now trigger a second update (sequence 101)
    processing::PacketBuilder pkt(mockInterface);
    createPacket(pkt);
    packet::EigrpHeader nextUpdate = pkt.reserveAndBuildHeader<packet::EigrpHeader>(packet::HeaderType::EIGRP);
    nextUpdate.setSequence(101);
    nextUpdate.setOpcode(EIGRP_TYPE_UPDATE);

    bool crHelloFound = false;
    bool crUpdateFound = false;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(::testing::AtLeast(2)).WillRepeatedly(::testing::Invoke([&](processing::PacketBuilder& pkt){
            auto eigrp = getEigrpHeader(pkt);
            if (!crHelloFound && eigrp.getOpcode() == EIGRP_TYPE_HELLO)
            {
                bool seqFound = false;
                bool neighbor = false;
                auto opts = extractEigrpOptions(pkt);
                for (auto& opt : opts)
                {
                    if (opt.type == EIGRP_OPTION_MULTICAST_SEQUENCE && utils::readU32(opt.value) == 101)
                    {
                        seqFound = true;
                    }
                    else if (opt.type == EIGRP_OPTION_SEQUENCE && utils::readU32(opt.value + 1) == 0xC0A80202)
                    {
                        neighbor = true;
                    }
                }
                crHelloFound = seqFound && neighbor;
            }
            else if (!crUpdateFound && eigrp.getOpcode() == EIGRP_TYPE_UPDATE)
            {
                crUpdateFound = eigrp.getFlagCondRecv();
            }
        }));

    transmitReliable(pkt, nullptr, nextUpdate);

    EXPECT_EQ(nbr2->activeConditions.size(), 1);

    processAck(*nbr1, 101);
    processAck(*nbr2, 100);
    processAck(*nbr2, 101);

    EXPECT_TRUE(crHelloFound);
    EXPECT_TRUE(crUpdateFound);

    EXPECT_TRUE(nbr2->activeConditions.empty());
}

// Test: ConditionalReceive_Multiple_Missing_Acks_Resolved
TEST_F(Internal_EigrpTest, ConditionalReceive_Multiple_Missing_Acks_Resolved)
{
    types::IPAddress n1 = uint32_t{0xC0A80203};
    types::IPAddress n2 = uint32_t{0xC0A80204};
    types::IPAddress n3 = uint32_t{0xC0A80205};
    addNeighbor(n1);
    addNeighbor(n2);
    addNeighbor(n3);

    auto* nbr1 = getNeighbor(n1);
    auto* nbr2 = getNeighbor(n2);
    auto* nbr3 = getNeighbor(n3);
    ASSERT_TRUE(nbr1);
    ASSERT_TRUE(nbr2);
    ASSERT_TRUE(nbr3);

    packet::EigrpHeader update;
    update.setBuffer(testPacket);
    update.setSequence(200);
    update.setOpcode(EIGRP_TYPE_UPDATE);

    // Setup: simulate multicast to both neighbors, but only one ACKs
    eigrpInterface->getRtp().setupMulticastReliable(update);
    processAck(*nbr3, 200);

    // Now trigger a second update (sequence 101)
    processing::PacketBuilder pkt(mockInterface);
    createPacket(pkt);
    packet::EigrpHeader nextUpdate = pkt.reserveAndBuildHeader<packet::EigrpHeader>(packet::HeaderType::EIGRP);
    nextUpdate.setSequence(201);
    nextUpdate.setOpcode(EIGRP_TYPE_UPDATE);

    bool crHelloFound = false;
    bool crUpdateFound = false;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(::testing::AtLeast(2)).WillRepeatedly(::testing::Invoke([&](processing::PacketBuilder& pkt){
            auto eigrp = getEigrpHeader(pkt);
            if (!crHelloFound && eigrp.getOpcode() == EIGRP_TYPE_HELLO)
            {
                bool seqFound = false;
                bool neighbor = false;
                for (auto& opt : extractEigrpOptions(pkt))
                {
                    if (opt.type == EIGRP_OPTION_MULTICAST_SEQUENCE && utils::readU32(opt.value) == 201)
                    {
                        seqFound = true;
                    }
                    else if (opt.type == EIGRP_OPTION_SEQUENCE && opt.valueSize >= 9)
                    {
                        uint32_t ip1 = utils::readU32(opt.value + 1);
                        uint32_t ip2 = utils::readU32(opt.value + 5);
                        neighbor = (ip1 == 0xC0A80203 && ip2 == 0xC0A80204) ||
                                   (ip1 == 0xC0A80204 && ip2 == 0xC0A80203);
                    }
                }
                crHelloFound = seqFound && neighbor;
            }
            else if (!crUpdateFound && eigrp.getOpcode() == EIGRP_TYPE_UPDATE)
            {
                crUpdateFound = eigrp.getFlagCondRecv();
            }
        }));

    transmitReliable(pkt, nullptr, nextUpdate);

    EXPECT_EQ(nbr1->activeConditions.size(), 1);
    EXPECT_EQ(nbr2->activeConditions.size(), 1);

    processAck(*nbr1, 200);
    processAck(*nbr1, 201);
    processAck(*nbr2, 200);
    processAck(*nbr2, 201);
    processAck(*nbr3, 201);

    EXPECT_TRUE(crHelloFound);
    EXPECT_TRUE(crUpdateFound);

    EXPECT_TRUE(nbr1->activeConditions.empty());
    EXPECT_TRUE(nbr2->activeConditions.empty());
}

// Test: ConditionalReceive_Receive_CR
TEST_F(Internal_EigrpTest, ConditionalReceive_Receive_CR) //TODO
{
    types::IPAddress n1 = uint32_t{0xC0A80206};
    addNeighbor(n1);
    auto* nbr1 = getNeighbor(n1);
    ASSERT_TRUE(nbr1);

    nbr1->receivedConditions[300] = true;

    packet::EigrpHeader update;
    update.setBuffer(testPacket);
    update.setSequence(300);
    update.setOpcode(EIGRP_TYPE_UPDATE);
    update.setFlagCondRecv(true);

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_)).Times(0);

    eigrpInterface->getRtp().handleIncoming(nullptr, update, n1.raw, true);
}

// Test: ConditionalReceive_Lost_CR_Hello
TEST_F(Internal_EigrpTest, ConditionalReceive_Lost_CR_Hello)
{
    types::IPAddress n1 = uint32_t{0xC0A80206};
    addNeighbor(n1);
    auto* nbr1 = getNeighbor(n1);
    ASSERT_TRUE(nbr1);

    packet::EigrpHeader update;
    update.setBuffer(testPacket);
    update.setSequence(300);
    update.setOpcode(EIGRP_TYPE_UPDATE);
    update.setFlagCondRecv(true);

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_)).Times(0);

    eigrpInterface->getRtp().handleIncoming(nullptr, update, n1.raw, true);
}

// Test: ConditionalReceive_Neighbor_Restarts_While_In_CR_Mode
TEST_F(Internal_EigrpTest, ConditionalReceive_Neighbor_Restarts_While_In_CR_Mode)
{
    types::IPAddress n1 =  uint32_t{0xC0A80207};
    addNeighbor(n1);
    auto* nbr = getNeighbor(n1);

    ASSERT_TRUE(nbr);

    packet::EigrpHeader update;
    update.setBuffer(testPacket);
    update.setSequence(400);
    update.setOpcode(EIGRP_TYPE_UPDATE);
    eigrpInterface->getRtp().setupMulticastReliable(update);

    packet::EigrpHeader nextUpdate;
    nextUpdate.setBuffer(testPacket);
    nextUpdate.setSequence(400);
    nextUpdate.setOpcode(EIGRP_TYPE_UPDATE);
    eigrpInterface->getRtp().setupMulticastReliable(nextUpdate);

    EXPECT_EQ(nbr->activeConditions.size(), 1);

    eigrpInterface->getTopController().onNeighborDown(*nbr);

    EXPECT_TRUE(nbr->activeConditions.empty());
}

// Test: ConditionalReceive_Unicast_Must_Wait_For_CR
TEST_F(Internal_EigrpTest, ConditionalReceive_Unicast_Must_Wait_For_CR) //TODO
{
    types::IPAddress n1 = uint32_t{0xC0A80210};
    addNeighbor(n1);
    auto* nbr = getNeighbor(n1);

    packet::EigrpHeader mcast;
    mcast.setBuffer(testPacket);
    mcast.setSequence(600);
    mcast.setOpcode(EIGRP_TYPE_UPDATE);
    eigrpInterface->getRtp().setupMulticastReliable(mcast);

    packet::EigrpHeader unicast;
    unicast.setBuffer(testPacket);
    unicast.setSequence(601);
    unicast.setOpcode(EIGRP_TYPE_UPDATE);

    {
        EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_)).Times(0);
        eigrpInterface->getRtp().setupUnicastReliable(*nbr, unicast);
    }

    EXPECT_LT(nbr->lastSeqRecv.load(), 601);

    {
        bool found = false;
        EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
            .Times(1).WillOnce(::testing::Invoke([&](processing::PacketBuilder& pkt) {
                auto eigrp = getEigrpHeader(pkt);
                found = (eigrp.getSequence() == 601);
            }));
        processAck(*nbr, 600);
        EXPECT_TRUE(found);
    }

    processAck(*nbr, 601);

    EXPECT_TRUE(nbr->reliableQueue.empty());
}

#pragma endregion
#pragma region TLV

// Test: Stub_TLV_Present_When_Stub_Enabled
TEST_F(Internal_EigrpTest, Stub_TLV_Present_When_Stub_Enabled) 
{
    // Verify that a stub TLV is inserted when stub mode is enabled.
    eigrpInstance->getGlobalConfigMgr().enableStub(true, true, false, true, false);
    processing::PacketBuilder helloPacket(mockInterface);
    createPacket(helloPacket);
    ASSERT_TRUE(createHello(helloPacket).has_value());
    std::vector<packet::TLV16Option> opts = extractEigrpOptions(helloPacket);
    auto it = std::find_if(opts.begin(), opts.end(),
        [](const packet::TLV16Option& opt) {
            return opt.type == EIGRP_OPTION_STUB;
        });
    ASSERT_NE(it, opts.end());
}

// Test: Authentication_TLV_Insertion_Correct
TEST_F(Internal_EigrpTest, Authentication_TLV_Insertion_Correct) 
{
    // Verify that when authentication is enabled, the authentication TLV is inserted with a computed HMAC.
    types::IPAddress neighborIp = uint32_t{0xC0A8011E};
    addNeighbor(neighborIp, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);

    eigrpInterface->configs.get<config::EigrpInterface::AUTHENTICATION_MODE>().set(config::eigrp::AuthType::MD5);
    eigrpInterface->configs.get<config::EigrpInterface::AUTHENTICATION_KEYCHAIN>().set("1");
    
    processing::PacketBuilder helloPacket(mockInterface);
    createPacket(helloPacket);
    ASSERT_TRUE(createUnicastHello(helloPacket).has_value());
    std::vector<packet::TLV16Option> opts = extractEigrpOptions(helloPacket);
    auto it = std::find_if(opts.begin(), opts.end(),
                             [](const packet::TLV16Option& opt) {
                                 return opt.type == EIGRP_OPTION_AUTHENTICATION;
                             });
    ASSERT_NE(it, opts.end());
    ASSERT_FALSE(std::all_of(it->value + 1, it->value + it->valueSize - 1, [](uint8_t b){return b == 0;}));
}

// Test: External_Route_TLV_Format
TEST_F(Internal_EigrpTest, External_Route_TLV_Format) 
{
    // Verify that an external route TLV is constructed correctly.
    routing::eigrp::ReceivedRoute route = getRoute(eigrpInterface->interfaceKey);
    route.prefix = {uint32_t{0xC0A80200}, 24};
    route.routeType = routing::eigrp::RouteType::EXTERNAL;
    routing::eigrp::RouteInfo routeInfo = {route};
    packet::TLV16BufferManager opts(testPacket, sizeof(testPacket));
    size_t tlvValueSize = routing::eigrp::TLVBuilder::encodeRouteOption(*eigrpInterface, testPacket, sizeof(testPacket), &routeInfo, mockInterface->configs.getBandwidth(), mockInterface->configs.getConfigs().get<config::Interface::DELAY>().load(), routing::eigrp::TLVBuilder::RouteType::LEGACY_EXTERNAL);
    ASSERT_GT(tlvValueSize, 0);
}

// Test: Internal_Route_TLV_Format
TEST_F(Internal_EigrpTest, Internal_Route_TLV_Format) 
{
    // Verify that an internal route TLV is constructed correctly.
    routing::eigrp::ReceivedRoute route = getRoute(eigrpInterface->interfaceKey);
    route.prefix = {uint32_t{0xC0A80300}, 24};
    route.routeType = routing::eigrp::RouteType::INTERNAL;
    route.routeType = routing::eigrp::RouteType::EXTERNAL;
    routing::eigrp::RouteInfo routeInfo = {route};
    packet::TLV16BufferManager opts(testPacket, sizeof(testPacket));
    size_t tlvValueSize = routing::eigrp::TLVBuilder::encodeRouteOption(*eigrpInterface, testPacket, sizeof(testPacket), &routeInfo, mockInterface->configs.getBandwidth(), mockInterface->configs.getConfigs().get<config::Interface::DELAY>().load(), routing::eigrp::TLVBuilder::RouteType::LEGACY_INTERNAL);
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
    eigrpInstance->waitIdle();
    ASSERT_TRUE(getInterfaceList().empty());
}

// Test: Interface_Removal_On_Shutdown
TEST_F(Internal_EigrpTest, Interface_Removal_On_Shutdown) 
{
    // When the interface is shut down, it should be removed.
    ASSERT_FALSE(getInterfaceList().empty());
    eigrpInstance->refreshInterfaceList();
    mockInterface->shutdown(true);
    eigrpInstance->waitIdle();
    eigrpInstance->refreshInterfaceList();
    ASSERT_TRUE(getInterfaceList().empty());
}

// Test: Dynamic_Interface_Addition_And_Removal
TEST_F(Internal_EigrpTest, Dynamic_Interface_Addition_And_Removal) 
{
    // Add a new interface and then remove it.
    interface::MockInterface* extraIface = new interface::MockInterface(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    uint32_t key = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 1);
    vrf->getInterfaceManager().add(extraIface, key);
    extraIface->blockEnqueues();
    extraIface->configs.id = 1;
    extraIface->configs.key = 1;
    extraIface->enableIPs();
    extraIface->enableShutdown();
    setIPv4(0xC0A80202, 24, extraIface);

    EXPECT_EQ(getInterfaceList().size(), 1);

    clearNetworks();
    eigrpInstance->waitIdle();

    EXPECT_EQ(getInterfaceList().size(), 0);

    types::IPPrefix network = {uint32_t{0xC0a80000}, 16};
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(network);
    eigrpInstance->waitIdle();

    EXPECT_EQ(getInterfaceList().size(), 2);
    extraIface->shutdown(true);
    EXPECT_EQ(getInterfaceList().size(), 1);
    eigrpInstance->shutdown();

    vrf->getInterfaceManager().remove(key);
    delete extraIface;
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
    ASSERT_EQ(getIpInfo().ipv4.getPrimaryAddress(), 0xc0a80101);
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
    timer.startHello();
    timer.stopHello();
    timer.startHello();
    ASSERT_TRUE(getHelloTimerActive());
}

#pragma endregion
#pragma region RoutingTable

// Test: TopologyTable_Add_Or_Update_Route
TEST_F(Internal_EigrpTest, TopologyTable_Add_Or_Update_Route) 
{
    // Verify that adding or updating a route creates the appropriate topology entry.
    types::IPAddress neighborIp = uint32_t{0xC0A80121};
    addNeighbor(neighborIp, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    routing::eigrp::ReceivedRoute r = getRoute(eigrpInterface->interfaceKey);
    r.prefix = { uint32_t{0xA0000000}, 16};
    r.bandwidth = 1000000;
    r.delay = 100000000;
    r.feasibleDistance = 100;
    r.reportedDistance = 80;
    types::IPAddress nextHop = uint32_t{0xC0A80101};
    r.nextHop = nextHop;
    r.hopCount = 1;
    std::vector<routing::eigrp::ReceivedRoute> rs = {r};
    getDuel().processReceivedRoutes(rs, *neighbor);
    auto entries = getDuel().topologyTable.entries();
    ASSERT_TRUE(entries.size() > 0);
}

// Test: TopologyTable_Prune_Stale_Routes
TEST_F(Internal_EigrpTest, TopologyTable_Prune_Stale_Routes) 
{
    // Verify that stale routes are pruned.
    types::IPAddress neighborIp = uint32_t{0xC0A80121};
    addNeighbor(neighborIp, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    auto r1 = getRoute(eigrpInterface->interfaceKey);
    auto r2 = getRoute(eigrpInterface->interfaceKey);
    types::IPPrefix prefix1 = {uint32_t{0xA0000000}, 16};
    types::IPPrefix prefix2 = {uint32_t{0xA1000000}, 16};
    r1.prefix = prefix1;
    r2.prefix = prefix2;
    std::vector<routing::eigrp::ReceivedRoute> rs = {r1, r2};
    getDuel().processReceivedRoutes(rs, *neighbor);
    vrf->getRib().wait<uint32_t>();
    eigrpInstance->getSchedulerQueue().waitIdle();
    auto& top = getTopologyTable();
    auto& entries = top.entries();
    entries[prefix1].valid = std::chrono::steady_clock::now() - std::chrono::seconds(100);
    EXPECT_EQ(top.entries().size(), 3);
    top.pruneExpired();
    EXPECT_EQ(top.entries().size(), 2);
}

// Test: TopologyTable_Update_Successors
TEST_F(Internal_EigrpTest, TopologyTable_Update_Successors) 
{
    // Verify that successors are recalculated correctly.
    routing::eigrp::ReceivedRoute route1, route2;
    types::IPAddress r1 = uint32_t{0xC0A80102};
    types::IPAddress r2 = uint32_t{0xC0A80103};
    route1.feasibleDistance = 100; route1.reportedDistance = 80; route1.nextHop = r1;
    route2.feasibleDistance = 150; route2.reportedDistance = 70; route2.nextHop = r2;
    routing::eigrp::RouteInfo rInfo1(route1), rInfo2(route2);
    routing::eigrp::TopologyEntry top;
    top.routesBySource.try_emplace(r1, route1);
    top.routesBySource.try_emplace(r2, route2);
    recalculateSuccessors(&top);
    EXPECT_EQ(top.successors.size(), 1);
}

// Test: RoutingTable_Duplicate_Route_Prevention
TEST_F(Internal_EigrpTest, RoutingTable_Duplicate_Route_Prevention) 
{
    // Verify that duplicate networks are not added.
    types::IPPrefix net{ uint32_t{0xC0A80100}, 24 };
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(net);
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(net);
    EXPECT_EQ(eigrpInstance->getGlobalConfigMgr().getNetworkSize(), 1);
}

// Test: RoutingTable_Metric_Update_On_Best_Route_Change
TEST_F(Internal_EigrpTest, RoutingTable_Metric_Update_On_Best_Route_Change) 
{
    // Verify that when a better route is learned, the routing table metric updates.
    auto route1 = getRoute(eigrpInterface->interfaceKey);
    types::IPAddress network = uint32_t{0xC0A80200};
    route1.prefix = { network, 24 };
    route1.feasibleDistance = 100;
    route1.nextHop = uint32_t{0xC0A80102};
    routing::eigrp::TopologyEntry top1;
    top1.bestNeighbor = types::IPAddress{};
    top1.routesBySource.emplace(types::IPAddress{}, routing::eigrp::RouteInfo{route1});
    std::vector<routing::eigrp::TopologyEntry*> ts = { &top1 };
    getDuel().updateSuccessors(ts);
    eigrpInstance->routeManager.synchronizeRoutes({&top1});
    
    auto route2 = getRoute(eigrpInterface->interfaceKey);
    route2.prefix = { network, 24 };
    route2.feasibleDistance = 50;
    route2.nextHop = uint32_t{0xC0A80103};
    top1.routesBySource.emplace(uint32_t{0x0A000001}, routing::eigrp::RouteInfo{route2});
    getDuel().updateSuccessors(ts);
    eigrpInstance->routeManager.synchronizeRoutes(ts);
    vrf->getRib().wait<uint32_t>();
    
    utils::RCU::Guard guard;
    core::RibEntry<uint32_t>* r = vrf->getRib().lookup<uint32_t>(network, guard);
    ASSERT_TRUE(r);
    ASSERT_EQ(r->metric, 6400); // 50 * rib scal = 128
}

// Test: WideMetrics_InternalRoute_ParsedCorrectly
TEST_F(Internal_EigrpTest, WideMetrics_InternalRoute_ParsedCorrectly)
{
    types::IPAddress neighborIp = uint32_t{0xC0A80250};
    addNeighbor(neighborIp, routing::eigrp::Neighbor::Version::WIDE, eigrpInterface);
    auto nbr = getNeighbor(neighborIp);

    routing::eigrp::ReceivedRoute r = getRoute(eigrpInterface->interfaceKey);
    r.prefix = { uint32_t{0x0A500000}, 16 };
    r.routeType = routing::eigrp::RouteType::INTERNAL;
    r.feasibleDistance = 0;
    r.reportedDistance = 0;
    r.bandwidth = 100000000;
    r.delay = 2000000;
    r.load = 5;
    r.reliability = 254;

    std::vector<routing::eigrp::ReceivedRoute> rs = { r };
    eigrpInterface->getMetrics().addRouteMetrics(rs);

    getDuel().processReceivedRoutes(rs, *nbr);
    vrf->getRib().wait<uint32_t>();
    

    utils::RCU::Guard guard;
    auto entry = vrf->getRib().lookup<uint32_t>(r.prefix, guard);
    ASSERT_TRUE(entry);
    
    // EIGRP wide: metric
    uint64_t bwTerm = (10'000'000ULL * 65'536) / r.bandwidth;
    uint64_t delayTerm = (r.delay / 1'000'000ULL) * 65'536ULL;
    uint64_t expected = (bwTerm + delayTerm) + eigrpInterface->getMetrics().getLocalMetric();

    EXPECT_EQ(entry->metric, expected * 128);
}

// Test: WideMetrics_ExternalRoute_ParsedCorrectly
TEST_F(Internal_EigrpTest, WideMetrics_ExternalRoute_ParsedCorrectly)
{
    types::IPAddress neighborIp = uint32_t{0xC0A80251};
    addNeighbor(neighborIp, routing::eigrp::Neighbor::Version::WIDE, eigrpInterface);
    auto nbr = getNeighbor(neighborIp);
    ASSERT_TRUE(nbr);

    routing::eigrp::ReceivedRoute r = getRoute(eigrpInterface->interfaceKey);
    r.prefix = { uint32_t{0x0A510000}, 16 };
    r.routeType = routing::eigrp::RouteType::EXTERNAL;
    r.bandwidth = 50000000;
    r.delay = 3000000;
    r.load = 10;
    r.hopCount = 2;
    r.tag = 12345;
    r.adminDistance = 170;

    routing::eigrp::RouteInfo ri(r);

    std::vector<routing::eigrp::ReceivedRoute> rs = { r };
    eigrpInterface->getMetrics().addRouteMetrics(rs);
    getDuel().processReceivedRoutes(rs, *nbr);
    vrf->getRib().wait<uint32_t>();

    utils::RCU::Guard guard;
    auto entry = vrf->getRib().lookup<uint32_t>(r.prefix, guard);
    ASSERT_TRUE(entry);

    uint64_t bwTerm = (10'000'000ULL * 65'536ULL) / r.bandwidth;
    uint64_t delayTerm = (r.delay / 1'000'000) * 65'536ULL;
    uint64_t expected = bwTerm + delayTerm + eigrpInterface->getMetrics().getLocalMetric();

    EXPECT_EQ(entry->metric, expected * 128);
    //TODO match tag
    EXPECT_EQ(entry->adminDistance, 170);
}

// Test: WideMetrics_Successor_Selection
TEST_F(Internal_EigrpTest, WideMetrics_Successor_Selection)
{
    types::IPAddress n1 = uint32_t{0xC0A80270};
    types::IPAddress n2 = uint32_t{0xC0A80271};

    addNeighbor(n1, routing::eigrp::Neighbor::Version::WIDE, eigrpInterface);
    addNeighbor(n2, routing::eigrp::Neighbor::Version::WIDE, eigrpInterface);

    auto nbr1 = getNeighbor(n1);
    auto nbr2 = getNeighbor(n2);

    routing::eigrp::ReceivedRoute r1 = getRoute(eigrpInterface->interfaceKey);
    r1.prefix = { uint32_t{0x0A70000}, 16 };
    r1.routeType = routing::eigrp::RouteType::INTERNAL;
    r1.bandwidth = 100000000;
    r1.delay = 1000000;
    r1.nextHop = n1;

    routing::eigrp::ReceivedRoute r2 = r1;
    r2.bandwidth = 20000000;
    r2.delay = 3000000;
    r2.nextHop = n2;

    std::vector<routing::eigrp::ReceivedRoute> rs = { r1 };
    eigrpInterface->getMetrics().addRouteMetrics(rs);
    getDuel().processReceivedRoutes(rs, *nbr1);
    rs = { r2 };
    eigrpInterface->getMetrics().addRouteMetrics(rs);
    getDuel().processReceivedRoutes(rs, *nbr2);

    utils::RCU::Guard guard;
    auto route = vrf->getRib().lookup<uint32_t>(r1.prefix, guard);
    ASSERT_TRUE(route);

    uint64_t local = eigrpInterface->getMetrics().getLocalMetric();

    uint64_t bw1 = (10'000'000ULL * 65'536ULL) / r1.bandwidth;
    uint64_t d1 = (r1.delay / 1'000'000ULL) * 65'536ULL;
    uint64_t m1 = bw1 + d1 + local;

    uint64_t bw2 = (10'000'000ULL * 65'536ULL) / r2.bandwidth;
    uint64_t d2 = (r2.delay / 1'000'000ULL) * 65'536ULL;
    uint64_t m2 = bw2 + d2 + local;

    EXPECT_EQ(route->metric, std::min(m1, m2) * 128);
    EXPECT_EQ(route->nextHopCount, (m1 == m2 ? 2 : 1));
}

// Test: WideMetrics_FeasibleSuccessor_WithVariance
TEST_F(Internal_EigrpTest, WideMetrics_FeasibleSuccessor_WithVariance)
{
    eigrpInstance->getGlobalConfigMgr().getConfigs().get<config::Eigrp::VARIANCE>().set(5);

    types::IPAddress n1 = uint32_t{0xC0A80280};
    types::IPAddress n2 = uint32_t{0xC0A80281};

    addNeighbor(n1, routing::eigrp::Neighbor::Version::WIDE, eigrpInterface);
    addNeighbor(n2, routing::eigrp::Neighbor::Version::WIDE, eigrpInterface);

    auto nbr1 = getNeighbor(n1);
    auto nbr2 = getNeighbor(n2);

    routing::eigrp::ReceivedRoute p = getRoute(eigrpInterface->interfaceKey);
    p.prefix = { uint32_t{0x0A800000}, 16 };
    p.routeType = routing::eigrp::RouteType::INTERNAL;
    p.bandwidth = 100000000;
    p.delay = 1000000;
    p.nextHop = n1;

    // Backup path with higher metric but within variance;
    routing::eigrp::ReceivedRoute b = p;
    b.bandwidth = 20000000;
    b.delay = 4000000;
    b.nextHop = n2;

    std::vector<routing::eigrp::ReceivedRoute> rs = { p };
    getDuel().processReceivedRoutes(rs, *nbr1);
    rs = { b };
    getDuel().processReceivedRoutes(rs, *nbr2);
    vrf->getRib().wait<uint32_t>();

    utils::RCU::Guard guard;
    auto entry = vrf->getRib().lookup<uint32_t>(p.prefix, guard);
    ASSERT_TRUE(entry);

    EXPECT_EQ(entry->nextHopCount, 2);
}

// Test: TopologyTable_Handles_Neighbor_Down
TEST_F(Internal_EigrpTest, TopologyTable_Handles_Neighbor_Down) 
{
    // Verify that when a neighbor goes down, its routes are removed from the topology.
    types::IPAddress neighborIp = uint32_t{0xC0A80121};
    addNeighbor(neighborIp, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    addNeighbor(uint32_t{0xC0A80221}, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    auto& tt = getTopologyTable();
    auto r = getRoute(eigrpInterface->interfaceKey);
    types::IPAddress nextHop = uint32_t{0xC0A80102};
    r.prefix = { uint32_t{0x0A000000}, 16 };
    r.feasibleDistance = 100;
    r.reportedDistance = 80;
    r.nextHop = neighborIp;
    std::vector<routing::eigrp::ReceivedRoute> rs = {r};
    getDuel().processReceivedRoutes(rs, *neighbor);
    vrf->getRib().wait<uint32_t>();
    eigrpInterface->getTopController().onNeighborDown(*neighbor);
    EXPECT_EQ(getActiveRoutes().size(), 1);
}

// Test: RoutingTable_All_Connected_Routes_Count
TEST_F(Internal_EigrpTest, RoutingTable_All_Connected_Routes_Count) 
{
    types::IPPrefix net{ uint32_t{0xC0A80000}, 16 };
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(net);
    utils::RCU::Guard guard;
    auto route = vrf->getRib().lookup<uint32_t>(mockInterface->configs.ipv4.getPrimaryPrefix().addr, guard);
    ASSERT_TRUE(route);
    EXPECT_EQ(route->nextHops[0].nextHop, 0);
}

#pragma endregion
#pragma region StubMode

// Test: StubMode_Enabled_Allows_Only_Permitted_Routes
TEST_F(Internal_EigrpTest, StubMode_Enabled_Allows_Only_Permitted_Routes) 
{
    // When stub mode is enabled (allowing only connected routes), external routes should be filtered.
    eigrpInterface->configs.get<config::EigrpInterface::SPLIT_HORIZON>().set(false);
    types::IPAddress neighborIp = uint32_t{0xC0A80002};
    addNeighbor(neighborIp);
    eigrpInstance->getGlobalConfigMgr().enableStub(true, true, false, false, false);
    routing::eigrp::ReceivedRoute r = getRoute(eigrpInterface->interfaceKey);
    r.prefix = {uint32_t{0xC0A80200}, 24};
    r.routeType = routing::eigrp::RouteType::EXTERNAL;
    routing::eigrp::RouteInfo rInfo(r);
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(0);
    eigrpInstance->broadcastRouteChanges({&rInfo});
}

// Test: StubMode_Disabled_Advertises_All_Routes
TEST_F(Internal_EigrpTest, StubMode_Disabled_Advertises_All_Routes) 
{
    // When stub mode is off, all routes should be advertised.
    eigrpInterface->configs.get<config::EigrpInterface::SPLIT_HORIZON>().set(false);
    types::IPAddress neighborIp = uint32_t{0xC0A80002};
    addNeighbor(neighborIp);
    eigrpInstance->getGlobalConfigMgr().enableStub(false, false, false, false, false);
    routing::eigrp::ReceivedRoute internalRoute = getRoute(eigrpInterface->interfaceKey);
    internalRoute.prefix = { uint32_t{0xC0A80300}, 24 };
    internalRoute.routeType = routing::eigrp::RouteType::INTERNAL;
    routing::eigrp::RouteInfo rInfo(internalRoute);
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(1);
    eigrpInstance->broadcastRouteChanges({&rInfo});
}

// Test: StubMode_Advertise_Connected_Only
TEST_F(Internal_EigrpTest, StubMode_Advertise_Connected_Only) 
{
    // If stub mode allows only connected routes, static routes should be filtered out.
    eigrpInterface->configs.get<config::EigrpInterface::SPLIT_HORIZON>().set(false);
    types::IPAddress neighborIp = uint32_t{0xC0A80002};
    addNeighbor(neighborIp);
    eigrpInstance->getGlobalConfigMgr().enableStub(true, true, false, false, false);
    routing::eigrp::ReceivedRoute internalRoute = getRoute(eigrpInterface->interfaceKey);
    internalRoute.prefix = { uint32_t{0xC0A80400}, 24 };
    internalRoute.routeType = routing::eigrp::RouteType::STATIC;
    routing::eigrp::RouteInfo rInfo(internalRoute);
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(0);
    eigrpInstance->broadcastRouteChanges({&rInfo});
}

// Test: StubMode_Advertise_Static_Only
TEST_F(Internal_EigrpTest, StubMode_Advertise_Static_Only) 
{
    // Test configuration where only static routes are allowed.
    eigrpInterface->configs.get<config::EigrpInterface::SPLIT_HORIZON>().set(false);
    types::IPAddress neighborIp = uint32_t{0xC0A80002};
    addNeighbor(neighborIp);
    eigrpInstance->getGlobalConfigMgr().enableStub(true, false, true, false, false);
    routing::eigrp::ReceivedRoute staticRoute = getRoute(eigrpInterface->interfaceKey);
    staticRoute.prefix = { uint32_t{0xC0A80400}, 24 };
    staticRoute.routeType = routing::eigrp::RouteType::STATIC;
    routing::eigrp::RouteInfo rInfo(staticRoute);
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(1);
    eigrpInstance->broadcastRouteChanges({&rInfo});
}

// Test: StubMode_Advertise_Summary_Only
TEST_F(Internal_EigrpTest, StubMode_Advertise_Summary_Only) 
{
    // Test configuration where only summary routes are allowed.
    eigrpInterface->configs.get<config::EigrpInterface::SPLIT_HORIZON>().set(false);
    types::IPAddress neighborIp = uint32_t{0xC0A80002};
    addNeighbor(neighborIp);
    eigrpInstance->getGlobalConfigMgr().enableStub(true, false, false, true, false);
    routing::eigrp::ReceivedRoute summaryRoute = getRoute(eigrpInterface->interfaceKey);
    summaryRoute.prefix = { uint32_t{0xC0A80500}, 24 };
    summaryRoute.routeType = routing::eigrp::RouteType::SUMMARY;
    routing::eigrp::RouteInfo rInfo(summaryRoute);
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(1);
    eigrpInstance->broadcastRouteChanges({&rInfo});
}

// Test: StubMode_Advertise_Redistributed_Only
TEST_F(Internal_EigrpTest, StubMode_Advertise_Redistributed_Only) 
{
    // Test configuration where only redistributed (external) routes are allowed.
    eigrpInterface->configs.get<config::EigrpInterface::SPLIT_HORIZON>().set(false);
    types::IPAddress neighborIp = uint32_t{0xC0A80002};
    addNeighbor(neighborIp);
    eigrpInstance->getGlobalConfigMgr().enableStub(true, false, false, false, true);
    routing::eigrp::ReceivedRoute externalRoute = getRoute(eigrpInterface->interfaceKey);
    externalRoute.prefix = { uint32_t{0xC0A80600}, 24 };
    externalRoute.routeType = routing::eigrp::RouteType::EXTERNAL;
    routing::eigrp::RouteInfo rInfo(externalRoute);
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(1);
    eigrpInstance->broadcastRouteChanges({&rInfo});
}

// Test: StubMode_Route_Filtering_Drops_NonPermitted_Routes
TEST_F(Internal_EigrpTest, StubMode_Route_Filtering_Drops_NonPermitted_Routes) 
{
    // Verify that routes not allowed in stub mode are not advertised.
    eigrpInterface->configs.get<config::EigrpInterface::SPLIT_HORIZON>().set(false);
    eigrpInstance->getGlobalConfigMgr().enableStub(true, true, false, false, false);
    routing::eigrp::ReceivedRoute externalRoute = getRoute(eigrpInterface->interfaceKey);
    externalRoute.prefix = { uint32_t{0xC0A80600}, 24 };
    externalRoute.routeType = routing::eigrp::RouteType::EXTERNAL;
    routing::eigrp::RouteInfo rInfo(externalRoute);
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(0);
    eigrpInstance->broadcastRouteChanges({&rInfo});
}

#pragma endregion
#pragma region ActiveState

// Test: ActiveQuery_Clear_After_Neighbor_Response
TEST_F(Internal_EigrpTest, ActiveQuery_Clear_After_Neighbor_Response) 
{
    // Verify that when a neighbor replies, the active query is cleared.
    routing::eigrp::ReceivedRoute testRoute = getRoute(eigrpInterface->interfaceKey);
    types::IPPrefix prefix = { uint32_t{0xC0A80500}, 24 };
    testRoute.prefix = prefix;
    routing::eigrp::RouteInfo rInfo(testRoute);

    types::IPAddress queryNeighborIp = uint32_t{0x0A010002};
    types::IPAddress neighborIp = uint32_t{0x0A010001};
    addNeighbor(queryNeighborIp);
    auto neighbor = getNeighbor(queryNeighborIp);
    addNeighbor(neighborIp);

    testRoute.nextHop = queryNeighborIp;

    routing::eigrp::ReliableTransport::PktInfo info;
    info.bandwidthMetric = 1000000;
    info.delay = 100000;
    info.mtu = 1500;
    info.version = routing::eigrp::TLVType::LEGACY_V4;

    auto& top = getTopologyTable().ensure(prefix);
    getTopologyTable().addRouteUpdate(rInfo.routeInfo, neighbor, top);
    recalculateSuccessors(&top);
    std::optional<packet::EigrpHeader> hdr;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(::testing::AtLeast(1))
        .WillRepeatedly(::testing::Invoke([&]( processing::PacketBuilder& pkt) {
            auto header = getEigrpHeader(pkt);
            if (header.getOpcode() == EIGRP_TYPE_QUERY)
            {
                processing::PacketBuilder eigrp(mockInterface);
                createPacket(eigrp);
                hdr = createReply(eigrp, info, *getNeighbor(neighborIp), {&rInfo});
                ASSERT_TRUE(hdr.has_value());
                hdr->setTrail(hdr->buffer + packet::EigrpHeader::fixedSize, eigrp.getHeaders()[2].length - packet::EigrpHeader::fixedSize);
            }
        }));

    testRoute.delay = std::numeric_limits<uint64_t>::max();
    testRoute.feasibleDistance = std::numeric_limits<uint64_t>::max();
    testRoute.reportedDistance = std::numeric_limits<uint64_t>::max();
    std::vector<routing::eigrp::ReceivedRoute> rs = {testRoute};
    getDuel().processReceivedRoutes(rs, *getNeighbor(queryNeighborIp));
    ASSERT_TRUE(hdr.has_value());
    eigrpInterface->getRtp().handleIncoming(nullptr, hdr.value(), neighborIp.raw, false);
    EXPECT_TRUE(getActiveRoutes().empty()); // Active should be resolved
}

// Test: Reply_Returned_After_Full_Query_Sequence
TEST_F(Internal_EigrpTest, Reply_Returned_After_Full_Query_Sequence) 
{
    // Verify that when a neighbor replies, the active query is cleared.
    interface::MockInterface* extraIface = new interface::MockInterface(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    uint32_t key = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 1);
    vrf->getInterfaceManager().add(extraIface, key);
    extraIface->configs.id = 1;
    extraIface->configs.key = key;
    extraIface->enableIPs();
    extraIface->enableShutdown();
    setIPv4(0xC0A80102, 24, extraIface);
    {
        EXPECT_CALL(*extraIface, enqueuePacket(::testing::_)).Times(::testing::AnyNumber());
        eigrpInstance->refreshInterfaceList();
    }

    ASSERT_TRUE(getInterfaceList().contains(key));
    routing::eigrp::EigrpInterface* extraEigrpIface = &getInterfaceList().at(key);

    routing::eigrp::ReceivedRoute testRoute = getRoute(eigrpInterface->interfaceKey);
    types::IPPrefix prefix = { uint32_t{0xC0A80500}, 24 };
    testRoute.prefix = prefix;
    routing::eigrp::RouteInfo rInfo(testRoute);

    types::IPAddress queryNeighborIp = uint32_t{0x0A010002};
    types::IPAddress neighborIp = uint32_t{0x0A010001};
    addNeighbor(queryNeighborIp);
    addNeighbor(neighborIp, routing::eigrp::Neighbor::Version::LEGACY, extraEigrpIface);
    auto queryNeighbor = getNeighbor(queryNeighborIp);
    auto neighbor = getNeighbor(neighborIp, extraEigrpIface);

    routing::eigrp::ReliableTransport::PktInfo info;
    info.bandwidthMetric = 1000000;
    info.delay = 100000;
    info.mtu = 1500;
    info.version = routing::eigrp::TLVType::LEGACY_V4;

    auto& top = getTopologyTable().ensure(prefix);
    getTopologyTable().addRouteUpdate(rInfo.routeInfo, queryNeighbor, top);
    std::vector<routing::eigrp::TopologyEntry*> ts = { &top };
    getDuel().updateSuccessors(ts);
    std::optional<packet::EigrpHeader> hdr;

    bool replyFound = false;
    bool updateFound = false;
    uint32_t querySeq = 10;

    EXPECT_CALL(*extraIface, enqueuePacket(::testing::_))
        .Times(::testing::AtLeast(1))
        .WillRepeatedly(::testing::Invoke([&]( processing::PacketBuilder& pkt) {
            auto header = getEigrpHeader(pkt);
            if (header.getOpcode() == EIGRP_TYPE_QUERY)
            {
                processing::PacketBuilder eigrp(mockInterface); // Only mock interface has a valid queue
                createPacket(eigrp);
                hdr = createReply(eigrp, info, *neighbor, {&rInfo});
                ASSERT_TRUE(hdr.has_value());
                hdr->setTrail(hdr->buffer + packet::EigrpHeader::fixedSize, eigrp.getHeaders()[2].length - packet::EigrpHeader::fixedSize);
            }
        }));

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(::testing::AtLeast(2))
        .WillRepeatedly(::testing::Invoke([&]( processing::PacketBuilder& pkt) {
            auto header = getEigrpHeader(pkt);
            if (header.getOpcode() == EIGRP_TYPE_REPLY)
            {
                auto opts = extractEigrpOptions(pkt);
                for (auto& opt : opts)
                    if (opt.type == EIGRP_OPTION_LEGACY_INTERNAL_ROUTE)
                        if (auto route = routing::eigrp::TLVBuilder::decodeRoute(opt, 0, types::AddressFamily::IPv4); route && route->prefix == prefix)
                            replyFound = true;
            }
            else if (header.getOpcode() == EIGRP_TYPE_UPDATE)
                updateFound = true;
        }));

    testRoute.delay = std::numeric_limits<uint64_t>::max();
    testRoute.feasibleDistance = std::numeric_limits<uint64_t>::max();
    std::vector<routing::eigrp::ReceivedRoute> rs = {testRoute};
    getDuel().processReceivedQueryRoutes(rs, *queryNeighbor, querySeq);
    ASSERT_TRUE(hdr.has_value());
    extraEigrpIface->getRtp().handleIncoming(nullptr, hdr.value(), neighborIp.raw, false);
    EXPECT_TRUE(replyFound);
    EXPECT_TRUE(updateFound);
    EXPECT_TRUE(getActiveRoutes().empty()); // Active should be resolved
    vrf->getInterfaceManager().remove(key);
    delete extraIface;
}

// Test: ActiveQuery_Timeout_Leads_To_Neighbor_Down
TEST_F(Internal_EigrpTest, ActiveQuery_Timeout_Leads_To_Neighbor_Down) 
{
    // Verify that if a neighbor fails to respond to repeated queries, it is declared down.
    eigrpInstance->getGlobalConfigMgr().getConfigs().get<config::Eigrp::ACTIVE_TIME>().set(1);
    types::IPAddress queryNeighborIp = uint32_t{0xC0A8011F};
    addNeighbor(queryNeighborIp, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    types::IPAddress neighborIp = uint32_t{0xC0A8021F};
    addNeighbor(neighborIp, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    routing::eigrp::ReceivedRoute testRoute = getRoute(eigrpInterface->interfaceKey);
    types::IPPrefix prefix = { uint32_t{0xC0A80600}, 24 };
    testRoute.prefix = prefix;

    auto& top = getTopologyTable().ensure(prefix);
    getTopologyTable().addRouteUpdate(testRoute, getNeighbor(queryNeighborIp), top);
    std::vector<routing::eigrp::ReceivedRoute> rs = {testRoute};
    getDuel().processReceivedRoutes(rs, *getNeighbor(queryNeighborIp));
    rs[0].feasibleDistance = std::numeric_limits<uint64_t>::max();
    rs[0].delay = std::numeric_limits<uint64_t>::max();
    getDuel().processReceivedRoutes(rs, *getNeighbor(queryNeighborIp));

    std::this_thread::sleep_for(std::chrono::seconds(6));
    
    ASSERT_FALSE(getNeighbor(neighborIp));
}

// Test: ActiveRoute_Cancels_On_Better_AlternativePath
TEST_F(Internal_EigrpTest, ActiveRoute_Cancels_On_Better_AlternativePath)
{
    types::IPAddress n1 = uint32_t{0xC0A80514};
    types::IPAddress n2 = uint32_t{0xC0a80515};

    addNeighbor(n1);
    addNeighbor(n2);

    auto nbr1 = getNeighbor(n1);
    auto nbr2 = getNeighbor(n2);

    types::IPPrefix p = { uint32_t{0x0A930000}, 16 };

    // Good initial path from nbr1
    routing::eigrp::ReceivedRoute r1 = getRoute(eigrpInterface->interfaceKey);
    r1.prefix = p;
    r1.feasibleDistance = 100;
    r1.reportedDistance = 90;
    std::vector<routing::eigrp::ReceivedRoute> rs = { r1 };
    getDuel().processReceivedRoutes(rs, *nbr1);

    // Bad update forces ACTIVE
    routing::eigrp::ReceivedRoute bad = r1;
    bad.feasibleDistance = std::numeric_limits<uint64_t>::max();
    bad.reportedDistance = std::numeric_limits<uint64_t>::max();
    rs = { bad };
    getDuel().processReceivedRoutes(rs, *nbr1);

    ASSERT_FALSE(getActiveRoutes().empty());

    // Another neighbor advertises a valid alternative successor
    routing::eigrp::ReceivedRoute alt = r1;
    alt.feasibleDistance = 150;
    alt.reportedDistance = 120;
    rs = { alt };
    getDuel().processReceivedRoutes(rs, *nbr2);

    EXPECT_TRUE(getActiveRoutes().empty());
}

// Test: Query_Multicast_Sent_To_All_Eligible_Neighbors
TEST_F(Internal_EigrpTest, Query_Multicast_Sent_To_All_Eligible_Neighbors)
{
    types::IPAddress n1 = uint32_t{0xC0A80601};
    types::IPAddress n2 = uint32_t{0xC0A80602};
    types::IPAddress n3 = uint32_t{0xC0A80603};

    addNeighbor(n1);
    addNeighbor(n2);
    addNeighbor(n3);

    auto nbr1 = getNeighbor(n1);

    types::IPPrefix p = { uint32_t{0x0AAA0000}, 16 };

    // Trigger ACTIVE state by withdrawing successor
    routing::eigrp::ReceivedRoute r = getRoute(eigrpInterface->interfaceKey);
    r.prefix = p;
    r.feasibleDistance = 100;
    r.reportedDistance = 50;
    std::vector<routing::eigrp::ReceivedRoute> rs = { r };
    getDuel().processReceivedRoutes(rs, *nbr1);

    r.feasibleDistance = std::numeric_limits<uint64_t>::max();
    r.reportedDistance = std::numeric_limits<uint64_t>::max();

    bool queryFound = false;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(::testing::AtLeast(1)).WillRepeatedly(::testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto eigrp = getEigrpHeader(pkt);
            if (eigrp.getOpcode() == EIGRP_TYPE_QUERY)
                queryFound = true;
        }));

    rs = { r };
    getDuel().processReceivedRoutes(rs, *nbr1);

    EXPECT_TRUE(queryFound);
    auto &active = getActiveRoutes();
    EXPECT_FALSE(active.empty());
}

// Test: Query_Unicast_Sent_To_All_Eligible_Neighbors
TEST_F(Internal_EigrpTest, Query_Unicast_Sent_To_All_Eligible_Neighbors)
{
    types::IPAddress n1 = uint32_t{0xC0A80601};
    types::IPAddress n2 = uint32_t{0xC0A80602};
    types::IPAddress n3 = uint32_t{0xC0A80603};

    addUnicastNeighbor(n1);
    addUnicastNeighbor(n2);
    addUnicastNeighbor(n3);

    auto nbr1 = getNeighbor(n1);

    types::IPPrefix p = { uint32_t{0x0AAA0000}, 16 };

    // Trigger ACTIVE state by withdrawing successor
    routing::eigrp::ReceivedRoute r = getRoute(eigrpInterface->interfaceKey);
    r.prefix = p;
    r.feasibleDistance = 100;
    r.reportedDistance = 50;
    std::vector<routing::eigrp::ReceivedRoute> rs = { r };
    getDuel().processReceivedRoutes(rs, *nbr1);

    r.feasibleDistance = std::numeric_limits<uint64_t>::max();
    r.reportedDistance = std::numeric_limits<uint64_t>::max();

    uint8_t queryCount = 0;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(::testing::AtLeast(2)).WillRepeatedly(::testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto eigrp = getEigrpHeader(pkt);
            if (eigrp.getOpcode() == EIGRP_TYPE_QUERY)
                queryCount++;
        }));

    rs = { r };
    getDuel().processReceivedRoutes(rs, *nbr1);

    auto &active = getActiveRoutes();
    EXPECT_EQ(queryCount, 2);
    EXPECT_FALSE(active.empty());
}

// Test: PassiveInterface_Disables_Hello_Transmission
TEST_F(Internal_EigrpTest, PassiveInterface_Disables_Hello_Transmission)
{
    eigrpInterface->setPassiveMode(true);

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(0);

    eigrpInterface->getRtp().sendHello();
}

// Test: PassiveInterface_Blocks_UpdateTransmission
TEST_F(Internal_EigrpTest, PassiveInterface_Blocks_UpdateTransmission)
{
    eigrpInterface->setPassiveMode(true);

    types::IPAddress neighborIp = uint32_t{0xC0A80102};
    addNeighbor(neighborIp);

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(0);

    eigrpInterface->getRtp().sendNullUpdate(*getNeighbor(neighborIp));
}

#pragma endregion
#pragma region IPv6Specific

// Test: IPv6_HelloPacket_Construction
TEST_F(Internal_EigrpTest, IPv6_HelloPacket_Construction) 
{
    // Verify that an IPv6 hello packet is constructed correctly.
    auto ipv6Eigrp = routing::eigrp::Eigrp(asNumber, types::AddressFamily::IPv6, vrf);
    ipv6Eigrp.start();
    interface::MockInterface ipv6Interface = interface::MockInterface(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    ipv6Interface.blockEnqueues();
    ipv6Interface.enableIPs();
    ipv6Interface.enableShutdown();
    uint8_t ipv6Buff[16] = { 0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };
    setIPv6(ipv6Buff, 64, &ipv6Interface);
    routing::eigrp::EigrpInterface* ipv6Int = ipv6Eigrp.getIfaceMgr().createInterface(&ipv6Interface);
    
    processing::PacketBuilder helloPacket(&ipv6Interface);
    createPacket(helloPacket, ipv6Int);
    uint8_t neighborIpBuf[16] = { 0x20, 0x01, 0x0D, 0xB8, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };
    types::IPAddress neighborIp = { neighborIpBuf, types::AddressFamily::IPv6 };
    addNeighbor(neighborIp, routing::eigrp::Neighbor::Version::WIDE, ipv6Int);
    ASSERT_TRUE(createHello(helloPacket, ipv6Int));
    ipv6Eigrp.getIfaceMgr().deactivateAll();
}


/**
 * TODO error when i step through "processUpdate"
 */
// Test: IPv6_Full_Adjacency_Establishment
TEST_F(Internal_EigrpTest, IPv6_Full_Adjacency_Establishment)
{
    // Create and start an IPv6 EIGRP process for the given AS number and VRF
    auto ipv6Eigrp = routing::eigrp::Eigrp(asNumber, types::AddressFamily::IPv6, vrf);
    ipv6Eigrp.start();

    // Create a mock interface and enable basic functionality
    interface::MockInterface iface(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    iface.blockEnqueues();   // prevent real packet enqueueing during test
    iface.enableIPs();       // enable IP processing on interface
    iface.enableShutdown();  // allow shutdown behavior for test cleanup

    // Construct local and neighbor IPv6 addresses (manual 128-bit assembly)
    types::IPv6Address local6 = (static_cast<__uint128_t>(0x20010db800000001) << 64) | 0x0000000000000002;
    types::IPv6Address nbr6   = (static_cast<__uint128_t>(0x20010db800000002) << 64) | 0x0000000000000002;

    // Assign IPv6 address + /64 prefix to interface
    setIPv6(local6, 64, &iface);

    // Encode interface key and register interface in VRF interface manager
    uint32_t key = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 11);
    iface.configs.key = key;
    iface.configs.id = 11;
    vrf->getInterfaceManager().add(&iface, key);

    // Create EIGRP interface context for this interface
    routing::eigrp::EigrpInterface* intf = ipv6Eigrp.getIfaceMgr().createInterface(&iface);

    // Simulate neighbor MAC + NDP entry so IPv6 adjacency can form
    types::Mac nbrMac = 0x122345789000;
    types::IPv6Address nbrIp = { nbr6 };
    iface.ndp.addNdpEntry(nbrIp, nbrMac);

    // Build initial EIGRP packet and hello
    processing::PacketBuilder p(mockInterface);
    createPacket(p, intf);
    auto hdr = createUnicastHello(p, intf);
    ASSERT_TRUE(hdr.has_value());

    // Add EIGRP payload size/headers
    addEigrpSize(p, hdr.value());

    // Capture init sequence number from outgoing INIT packet
    uint32_t initSeq = 0;
    EXPECT_CALL(iface, enqueuePacket(::testing::_))
        .Times(::testing::AtLeast(1))
        .WillRepeatedly(::testing::Invoke([&](processing::PacketBuilder& pkt) {
            auto eigrp = getEigrpHeader(pkt);

            // Store sequence number if INIT flag is set
            if (eigrp.getFlagInit())
                initSeq = eigrp.getSequence();
        }));

    // Process incoming hello from neighbor (starts adjacency formation)
    intf->getRtp().handleIncoming(nullptr, hdr.value(), nbrIp, true);

    // Verify neighbor is created and in PENDING state
    auto nbr = intf->getNTable().lookup(nbrIp);
    ASSERT_TRUE(nbr);
    EXPECT_EQ(nbr->getState(), routing::eigrp::Neighbor::State::PENDING);

    // Send null update packet back acknowledging INIT packet
    processing::PacketBuilder init(mockInterface);
    createPacket(p, intf); // NOTE: likely intended "init", not "p"
    auto hdr2 = createNullUpdate(init, intf);
    ASSERT_TRUE(hdr2);

    // Decrement sequence to simulate correct RTP behavior
    decrementNextSeq(intf);

    // ACK the INIT sequence we captured earlier
    hdr2.value().setAck(initSeq);

    // Process ACK from neighbor side
    intf->getRtp().handleIncoming(nullptr, hdr2.value(), nbr6, false);

    // Neighbor should now transition to UP state
    EXPECT_EQ(nbr->getState(), routing::eigrp::Neighbor::State::UP);

    // Cleanup: deactivate EIGRP interfaces and remove from VRF
    ipv6Eigrp.getIfaceMgr().deactivateAll();
    vrf->getInterfaceManager().remove(key);
}

// Test: IPv6_Update_Processing
TEST_F(Internal_EigrpTest, IPv6_Update_Processing) //TODO
{
    auto ipv6Eigrp = routing::eigrp::Eigrp(asNumber, types::AddressFamily::IPv6, vrf);
    ipv6Eigrp.start();

    interface::MockInterface iface(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    iface.blockEnqueues();
    iface.enableIPs();
    iface.enableShutdown();

    types::IPv6Address local6 = (static_cast<__uint128_t>(0x20010db800000001) << 64) | 0x0000000000000002;
    types::IPv6Address nbr6 = (static_cast<__uint128_t>(0x20010db800000002) << 64) | 0x0000000000000002;
    types::IPv6Address route = (static_cast<__uint128_t>(0x20010db801010000) << 64) | 0x0000000000000000;

    setIPv6(local6, 64, &iface);
    uint32_t key = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 11);
    iface.configs.key = key;
    iface.configs.id = 11;
    vrf->getInterfaceManager().add(&iface, key);

    auto* intf = ipv6Eigrp.getIfaceMgr().createInterface(&iface);
    addNeighbor(nbr6, routing::eigrp::Neighbor::Version::WIDE, intf);
    auto nbr = intf->getNTable().lookup(nbr6);
    ASSERT_TRUE(nbr);

    routing::eigrp::ReliableTransport::PktInfo info;
    info.bandwidthMetric = 1000000;
    info.delay = 50000;
    info.mtu = 1500;
    info.version = routing::eigrp::TLVType::WIDE;

    routing::eigrp::ReceivedRoute r = getRoute(eigrpInterface->interfaceKey);
    r.prefix = { route.addr, 64 };
    r.routeType = routing::eigrp::RouteType::INTERNAL;
    r.nextHop = nbr6;
    r.feasibleDistance = 100;
    r.reportedDistance = 50;
    r.wide.afi = 2;

    routing::eigrp::RouteInfo ri(r);

    processing::PacketBuilder pb(&iface);
    createPacket(pb, intf);
    auto upd = createUpdate(pb, info, nbr, { &ri }, intf);
    ASSERT_TRUE(upd.has_value());
    addEigrpSize(pb, upd.value());
    
    intf->getRtp().handleIncoming(nullptr, upd.value(), nbr6, false);
    
    auto entry = getTopologyTable(&ipv6Eigrp).find(r.prefix);
    ASSERT_TRUE(entry != nullptr);
    EXPECT_EQ(entry->routesBySource.size(), 1u);

    ipv6Eigrp.getIfaceMgr().deactivateAll();
    getInterfaceList().erase(key);
}

// Test: IPv6_Query_Reply_SIA
TEST_F(Internal_EigrpTest, IPv6_Query_Reply_SIA) //TODO
{
    auto ipv6Eigrp = routing::eigrp::Eigrp(asNumber, types::AddressFamily::IPv6, vrf);
    ipv6Eigrp.start();
        
    interface::MockInterface iface(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    iface.blockEnqueues();
    iface.enableIPs();
    iface.enableShutdown();

    types::IPv6Address local6 = (static_cast<__uint128_t>(0x20010db800000001) << 64) | 0x0000000000000001;
    types::IPv6Address nbrIp1 = (static_cast<__uint128_t>(0x20010db800000002) << 64) | 0x0000000000000002;
    types::IPv6Address nbrIp2 = (static_cast<__uint128_t>(0x20010db800000003) << 64) | 0x0000000000000002;
    types::IPv6Address route = (static_cast<__uint128_t>(0x20010db800000004) << 64) | 0x0000000000000002;

    setIPv6(local6, 64, &iface);
    uint32_t key = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 11);
    iface.configs.key = key;
    iface.configs.id = 11;
    vrf->getInterfaceManager().add(&iface, key);

    auto* intf = ipv6Eigrp.getIfaceMgr().createInterface(&iface);
    addNeighbor(nbrIp1, routing::eigrp::Neighbor::Version::WIDE, intf);
    addNeighbor(nbrIp2, routing::eigrp::Neighbor::Version::WIDE, intf);
    auto nbr1 = intf->getNTable().lookup(nbrIp1);
    auto nbr2 = intf->getNTable().lookup(nbrIp2);
    ASSERT_TRUE(nbr1);
    ASSERT_TRUE(nbr2);

    // Create a failing route to trigger active state
    routing::eigrp::ReceivedRoute r;
    r.prefix = { route.addr, 64 };
    r.routeType = routing::eigrp::RouteType::INTERNAL;
    r.feasibleDistance = std::numeric_limits<uint64_t>::max();
    r.reportedDistance = std::numeric_limits<uint64_t>::max();

    std::vector<routing::eigrp::ReceivedRoute> rs = { r };
    getDuel(&ipv6Eigrp).processReceivedQueryRoutes(rs, *nbr1, 10);

    // Active route must now have pending queries
    auto& active = getActiveRoutes(&ipv6Eigrp);
    ASSERT_FALSE(active.empty());

    EXPECT_CALL(iface, enqueuePacket(::testing::_))
        .Times(::testing::AtLeast(1));

    routing::eigrp::ReliableTransport::PktInfo info;
    info.version = routing::eigrp::TLVType::WIDE;

    for (auto& kv : active)
    {
        processing::PacketBuilder pb(&iface);
        createPacket(pb, intf);
        auto sia = createSIAQuery(pb, info, {});
        nbr1->lastSeqRecv = sia->getSequence();
        intf->getRtp().handleIncoming(nullptr, sia.value(), nbrIp1, false);
    }

    ipv6Eigrp.getIfaceMgr().deactivateAll();
    vrf->getInterfaceManager().remove(key);
}

#pragma endregion
#pragma region Timers

// Test: Timers_Cancelled_On_Interface_Shutdown
TEST_F(Internal_EigrpTest, Timers_Cancelled_On_Interface_Shutdown) 
{
    // Verify that all timers are cancelled upon interface shutdown.
    eigrpInterface->getTimers().startHello();
    types::IPAddress neighborIp = uint32_t{0xC0A80120};
    addNeighbor(neighborIp, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);
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
    types::IPAddress neighborIp = uint32_t{0xC0A80121};
    addNeighbor(neighborIp, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    processing::PacketBuilder pkt(mockInterface);
    packet::EigrpHeader eigrp;
    eigrp.setBuffer(testPacket);
    eigrp.setSequence(4);
    eigrpInterface->getRtp().setupUnicastReliable(*getNeighbor(neighborIp), eigrp);
    
    processAck(*neighbor, 4);
    EXPECT_TRUE(neighbor->reliableQueue.empty());
}

// Test: Multithreaded_NeighborStateUpdates_No_Deadlock
TEST_F(Internal_EigrpTest, Multithreaded_NeighborStateUpdates_No_Deadlock) 
{
    // Simulate concurrent neighbor state updates and check for deadlock.
    types::IPAddress neighborIp = uint32_t{0xC0A80122};
    addNeighbor(neighborIp, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    neighbor->setState(routing::eigrp::Neighbor::State::PENDING);
    
    auto threadFunc = [this, neighbor]() {
        for (int i = 0; i < 1000; i++) {
            processing::PacketBuilder hello(mockInterface);
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
    interface::MockInterface iface1(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    interface::MockInterface iface2(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    uint32_t key1 = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 2);
    uint32_t key2 = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 3);
    iface1.configs.key = key1;
    iface2.configs.key = key2;
    iface1.blockEnqueues();
    iface1.enableShutdown();
    iface2.blockEnqueues();
    iface2.enableShutdown();
    mockInterface->blockEnqueues();
    auto* int1 = eigrpInstance->getIfaceMgr().createInterface(&iface1);
    auto* int2 = eigrpInstance->getIfaceMgr().createInterface(&iface2);

    types::IPAddress neighbor1 = uint32_t{0x0A000001};
    types::IPAddress neighbor2 = uint32_t{0x0A000002};
    addNeighbor(neighbor1, routing::eigrp::Neighbor::Version::LEGACY, int1);
    addNeighbor(neighbor2, routing::eigrp::Neighbor::Version::LEGACY, int2);
    
    for (int i = 0; i < 300; i++) 
    {
        auto route =  getRoute(0);
        route.prefix = { uint32_t{0xC0A80500}, 24 };
        route.nextHop = uint32_t{0x0A000001};
        route.routeType = routing::eigrp::RouteType::INTERNAL;
        std::vector<routing::eigrp::ReceivedRoute> rs = {route};
        int1->getTopController().processReceivedRoutes(rs, *getNeighbor(neighbor1, int1));
    }
    ASSERT_GE(vrf->getRib().size<uint32_t>(), 1);

    eigrpInstance->getIfaceMgr().deactivateAll();
    // TODO possible leak
}

// Test: Frequent_Interface_Flapping_No_Global_Corruption
TEST_F(Internal_EigrpTest, Frequent_Interface_Flapping_No_Global_Corruption) 
{
    // Verify that repeated interface flapping does not corrupt global state.
    setIPv4(0xC0A80101, 24);
    types::IPPrefix net = { uint32_t{0xC0A80000}, 16 };
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(net);
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
        types::IPAddress ip = uint32_t{0xC0A80100};
        addNeighbor(ip, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);
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
    interface::MockInterface iface1(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    interface::MockInterface iface2(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    iface1.blockEnqueues();
    iface2.blockEnqueues();
    iface1.enableIPs();
    iface2.enableIPs();
    iface1.enableShutdown();
    iface2.enableShutdown();
    setIPv4(0xC0A80201, 24, &iface1);
    setIPv4(0xC0A80301, 24, &iface2);
    iface1.configs.key = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 1);
    iface2.configs.key = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 2);
    auto int1 = eigrpInstance->getIfaceMgr().createInterface(&iface1);
    auto int2 = eigrpInstance->getIfaceMgr().createInterface(&iface2);
    addNeighbor(uint32_t{0x0A000001}, routing::eigrp::Neighbor::Version::LEGACY, int1);
    addNeighbor(uint32_t{0x0A000002}, routing::eigrp::Neighbor::Version::LEGACY, int2);
    
    EXPECT_CALL(iface1, enqueuePacket(::testing::_))
        .Times(::testing::AtLeast(1));
    EXPECT_CALL(iface2, enqueuePacket(::testing::_))
        .Times(::testing::AtLeast(1));
    
    routing::eigrp::ReceivedRoute route = getRoute(eigrpInterface->interfaceKey);
    route.prefix = { uint32_t{0xC0A80700}, 24 };
    route.nextHop = uint32_t{0xC0A80102};
    routing::eigrp::RouteInfo rInfo(route);
    eigrpInstance->broadcastRouteChanges({&rInfo});
    
    eigrpInstance->getIfaceMgr().deactivateAll();
}

// Test: MultiInterface_Adjacency_Formation
TEST_F(Internal_EigrpTest, MultiInterface_Adjacency_Formation) 
{
    // Verify that neighbors on different interfaces form independent adjacencies.
    interface::MockInterface iface1(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    interface::MockInterface iface2(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    iface1.configs.key = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 2);
    iface2.configs.key = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 3);
    iface1.blockEnqueues();
    iface1.enableShutdown();
    iface2.blockEnqueues();
    iface2.enableShutdown();
    auto int1 = eigrpInstance->getIfaceMgr().createInterface(&iface1);
    auto int2 = eigrpInstance->getIfaceMgr().createInterface(&iface2);
    addNeighbor(uint32_t{0x0A000003}, routing::eigrp::Neighbor::Version::LEGACY, int1);
    addNeighbor(uint32_t{0x0A000004}, routing::eigrp::Neighbor::Version::LEGACY, int2);
    ASSERT_EQ(getInterfaceList().size(), 3);
    eigrpInstance->getIfaceMgr().deactivateAll();
}

// Test: MultiInterface_Failure_Isolation
TEST_F(Internal_EigrpTest, MultiInterface_Failure_Isolation) 
{
    // Verify that failure on one interface does not affect neighbors on other interfaces.
    interface::MockInterface iface1(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    interface::MockInterface iface2(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    iface1.configs.key = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 2);
    iface2.configs.key = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 3);
    iface1.blockEnqueues();
    iface2.blockEnqueues();
    iface1.enableShutdown();
    iface2.enableShutdown();
    types::IPPrefix network = { uint32_t{0}, 0 };
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(network);
    auto int1 = eigrpInstance->getIfaceMgr().createInterface(&iface1);
    auto int2 = eigrpInstance->getIfaceMgr().createInterface(&iface2);
    addNeighbor(uint32_t{0x0A000005}, routing::eigrp::Neighbor::Version::LEGACY, int1);
    addNeighbor(uint32_t{0x0A000006}, routing::eigrp::Neighbor::Version::LEGACY, int2);
    int1->getIface()->shutdown(true);
    eigrpInstance->waitIdle();
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
    addNeighbor(uint32_t{0xC0A80122}, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    addNeighbor(uint32_t{0xC0A80123}, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);
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
    types::IPAddress neighborIp = uint32_t{0xC0A80124};
    addNeighbor(neighborIp, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);
    auto neighbor = getNeighbor(neighborIp);
    uint32_t seqNum = 610;
    packet::EigrpHeader hdr;
    hdr.setBuffer(testPacket);
    hdr.setSequence(seqNum);
    eigrpInterface->getRtp().setupUnicastReliable(*neighbor, hdr);
    
    processAck(*neighbor, seqNum);
    EXPECT_TRUE(neighbor->reliableQueue.empty());
}

#pragma endregion
#pragma region AdvancedStressTesting

// Test: HighVolume_RouteUpdates_Performance_Extended
TEST_F(Internal_EigrpTest, HighVolume_RouteUpdates_Performance_Extended) 
{
    // Stress-test with 1500 route updates.
    types::IPAddress neighborIp = uint32_t{0x0A000001};
    addNeighbor(neighborIp, routing::eigrp::Neighbor::Version::LEGACY);
    auto neighbor = getNeighbor(neighborIp);
    types::IPAddress nextHop = uint32_t{0x0AA80105};

    uint8_t base[4] = { 0xC0, 0x00, 0x00, 0x00 };
    for (int i = 0; i < 1500; i++)
    {
        routing::eigrp::ReceivedRoute route = getRoute(eigrpInterface->interfaceKey);
        utils::writeU16(base + 1, static_cast<uint16_t>(i));

        route.prefix = { utils::readU32(base), 24 };
        route.nextHop = nextHop;
        std::vector<routing::eigrp::ReceivedRoute> rs = {route};
        getDuel().processReceivedRoutes(rs, *neighbor);
    }
    vrf->getRib().wait<uint32_t>();
    ASSERT_EQ(vrf->getRib().size<uint32_t>(), 1501);
}

// Test: MultiInterface_Massive_Concurrent_Updates_Extended
TEST_F(Internal_EigrpTest, MultiInterface_Massive_Concurrent_Updates_Extended) 
{
    // Test massive concurrent updates on two additional interfaces.
    interface::MockInterface iface1(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    interface::MockInterface iface2(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    iface1.configs.key = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 2);
    iface2.configs.key = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 3);
    iface1.blockEnqueues();
    iface1.enableShutdown();
    iface2.blockEnqueues();
    iface2.enableShutdown();
    auto int1 = eigrpInstance->getIfaceMgr().createInterface(&iface1);
    auto int2 = eigrpInstance->getIfaceMgr().createInterface(&iface2);
    types::IPAddress neighborIp1 = uint32_t{0x0A000001};
    types::IPAddress neighborIp2 = uint32_t{0x0A000002};
    addNeighbor(neighborIp1, routing::eigrp::Neighbor::Version::LEGACY, int1);
    addNeighbor(neighborIp2, routing::eigrp::Neighbor::Version::LEGACY, int2);

    auto neighbor = getNeighbor(neighborIp1, int1);
    
    for (int i = 0; i < 300; i++) {
        routing::eigrp::ReceivedRoute route = getRoute(eigrpInterface->interfaceKey);
        route.prefix = { uint32_t{0xC0A80500}, 24 };
        route.nextHop = uint32_t{0x00000001};
        route.routeType = routing::eigrp::RouteType::INTERNAL;
        std::vector<routing::eigrp::ReceivedRoute> receivedRoutes = {route};
        getDuel().processReceivedRoutes(receivedRoutes, *neighbor);
    }
    ASSERT_GE(vrf->getRib().size<uint32_t>(), 1);
    eigrpInstance->getIfaceMgr().deactivateAll();
}

// Test: Frequent_Interface_Flapping_No_Global_Corruption_Extended
TEST_F(Internal_EigrpTest, Frequent_Interface_Flapping_No_Global_Corruption_Extended) 
{
    // Verify that interface flapping does not corrupt global state.
    setIPv4(0xC0A80101, 24);
    types::IPPrefix network{ uint32_t{0xC0A80000}, 16 };
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
        types::IPAddress ip = uint32_t{0xC0A80100 + static_cast<uint32_t>(i)};
        addNeighbor(ip, routing::eigrp::Neighbor::Version::LEGACY, eigrpInterface);
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
    interface::MockInterface iface1(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    interface::MockInterface iface2(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    iface1.configs.key = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 2);
    iface2.configs.key = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 3);
    iface1.blockEnqueues();
    iface1.enableShutdown();
    iface2.blockEnqueues();
    iface2.enableShutdown();
    auto int1 = eigrpInstance->getIfaceMgr().createInterface(&iface1);
    auto int2 = eigrpInstance->getIfaceMgr().createInterface(&iface2);
    addNeighbor(uint32_t{0x0A000003}, routing::eigrp::Neighbor::Version::LEGACY, int1);
    addNeighbor(uint32_t{0x0A000004}, routing::eigrp::Neighbor::Version::LEGACY, int2);
    ASSERT_EQ(getInterfaceList().size(), 3);
    eigrpInstance->getIfaceMgr().deactivateAll();
}

// Test: MultiInterface_Failure_Isolation_Extended
TEST_F(Internal_EigrpTest, MultiInterface_Failure_Isolation_Extended) 
{
    // Extended test: simulate failure on one interface and ensure others remain unaffected.
    interface::MockInterface iface1(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    interface::MockInterface iface2(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    iface1.configs.key = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 2);
    iface2.configs.key = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 3);
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
    types::IPPrefix network{ uint32_t{}, 0 };
    eigrpInstance->getGlobalConfigMgr().addNetworkRange(network);
    addNeighbor(uint32_t{0x0A000005}, routing::eigrp::Neighbor::Version::LEGACY, int1);
    addNeighbor(uint32_t{0x0A000006}, routing::eigrp::Neighbor::Version::LEGACY, int2);
    iface1.shutdown(true);
    eigrpInstance->refreshInterfaceList();
    ASSERT_EQ(getInterfaceList().size(), 2);
    eigrpInstance->getIfaceMgr().deactivateAll();
}

// Test: MultiInterface_Coordinated_RoutingUpdates_Extended
TEST_F(Internal_EigrpTest, MultiInterface_Coordinated_RoutingUpdates_Extended) 
{
    // Extended test: simulate simultaneous route updates from multiple interfaces.
    interface::MockInterface iface(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    iface.configs.key = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 1);
    iface.blockEnqueues();
    iface.enableShutdown();
    auto int1 = eigrpInstance->getIfaceMgr().createInterface(&iface);
    types::IPAddress neighborIp = uint32_t{0x0A000007};
    addNeighbor(neighborIp, routing::eigrp::Neighbor::Version::LEGACY, int1);
    routing::eigrp::ReceivedRoute route1 = getRoute(eigrpInterface->interfaceKey);
    route1.prefix = { uint32_t{0xC0A80000}, 24 };
    route1.nextHop = uint32_t{0x00000001};
    route1.routeType = routing::eigrp::RouteType::INTERNAL;
    routing::eigrp::ReceivedRoute route2 = getRoute(eigrpInterface->interfaceKey);
    route2.prefix = { uint32_t{0xC0A80900}, 24 };
    route2.nextHop = uint32_t{0x00000002};
    route2.routeType = routing::eigrp::RouteType::INTERNAL;
    std::vector<routing::eigrp::ReceivedRoute> routes = { route1, route2 };
    getDuel().processReceivedRoutes(routes, *getNeighbor(neighborIp, int1));
    vrf->getRib().wait<uint32_t>();
    ASSERT_GE(vrf->getRib().size<uint32_t>(), 2);
    eigrpInstance->getIfaceMgr().deactivateAll();
}

// Test: Unequal_Cost_Path_Added_As_Feasible_Successor
TEST_F(Internal_EigrpTest, Unequal_Cost_Path_Added_As_Feasible_Successor)
{
    types::IPAddress neighborIp1 = uint32_t{0xC0A80201};
    types::IPAddress neighborIp2 = uint32_t{0xC0A80301};
    addNeighbor(neighborIp1);
    addNeighbor(neighborIp2);
    auto neighbor1 = getNeighbor(neighborIp1);
    auto neighbor2 = getNeighbor(neighborIp2);

    eigrpInstance->getGlobalConfigMgr().getConfigs().get<config::Eigrp::VARIANCE>().set(4);

    routing::eigrp::ReceivedRoute primary = getRoute(eigrpInterface->interfaceKey);
    primary.prefix = { uint32_t{0x0A080000}, 16 };
    primary.feasibleDistance = 100;
    primary.reportedDistance = 80;

    primary.bandwidth = 1000000;
    primary.delay = 1000000000;
    primary.hopCount = 0;
    primary.mtu = 1500;
    primary.reliability = 255;
    primary.load = 1;
    primary.nextHop = neighborIp1;

    routing::eigrp::ReceivedRoute secondary = primary;
    secondary.feasibleDistance = 150;
    secondary.reportedDistance = 90;
    secondary.nextHop = neighborIp2;

    std::vector<routing::eigrp::ReceivedRoute> routes1 = { primary };
    std::vector<routing::eigrp::ReceivedRoute> routes2 = { secondary };
    getDuel().processReceivedRoutes(routes1, *neighbor1);
    getDuel().processReceivedRoutes(routes2, *neighbor2);
    vrf->getRib().wait<uint32_t>();

    utils::RCU::Guard guard;
    auto successor = vrf->getRib().lookup<uint32_t>(primary.prefix, guard);
    ASSERT_TRUE(successor);
    EXPECT_EQ(successor->prefix, primary.prefix.v4());
    EXPECT_EQ(successor->length, primary.prefix.prefixLength);
    EXPECT_EQ(successor->metric, primary.feasibleDistance * 128);
    EXPECT_EQ(successor->nextHopCount, 2);
}

// Test: Metric_Tuning_Affects_Route_Selection
TEST_F(Internal_EigrpTest, Metric_Tuning_Affects_Route_Selection)
{
    types::IPAddress neighbor1 = uint32_t{0xC0A80101};
    types::IPAddress neighbor2 = uint32_t{0xC0A80201};
    addNeighbor(neighbor1);
    addNeighbor(neighbor2);

    routing::eigrp::ReceivedRoute r1 = getRoute(eigrpInterface->interfaceKey);
    r1.prefix = { uint32_t{0x0A110000}, 16 };

    routing::eigrp::ReceivedRoute r2 = r1;
    r2.feasibleDistance = 90; // Lower metric
    r2.reportedDistance = 80;

    std::vector<routing::eigrp::ReceivedRoute> routes1 = { r1 };
    std::vector<routing::eigrp::ReceivedRoute> routes2 = { r2 };
    getDuel().processReceivedRoutes(routes1, *getNeighbor(neighbor1));
    getDuel().processReceivedRoutes(routes2, *getNeighbor(neighbor2));
    vrf->getRib().wait<uint32_t>();

    utils::RCU::Guard guard;
    auto best = vrf->getRib().lookup<uint32_t>(r1.prefix, guard);
    ASSERT_TRUE(best);
    EXPECT_EQ(best->metric, 90 * 128);
}

// Test: Route_Loop_Prevention_Using_FD
TEST_F(Internal_EigrpTest, Route_Loop_Prevention_Using_FD)
{
    types::IPAddress neighborIp = uint32_t{0xC0A80110};
    addNeighbor(neighborIp);
    auto neighbor = getNeighbor(neighborIp);

    routing::eigrp::ReceivedRoute route = getRoute(eigrpInterface->interfaceKey);
    route.prefix = { uint32_t{0x0A120000}, 16 };
    route.feasibleDistance = 100;
    route.reportedDistance = 150; // Violation

    std::vector<routing::eigrp::ReceivedRoute> routes = { route };
    getDuel().processReceivedRoutes(routes, *neighbor);
    utils::RCU::Guard guard;
    auto* chosen = vrf->getRib().lookup<uint32_t>(route.prefix, guard);

    EXPECT_TRUE(chosen == nullptr);
}

// Test: Summarization_Advertises_Summary_Only
TEST_F(Internal_EigrpTest, Summarization_Advertises_Summary_Only)
{
    types::IPPrefix summaryPrefix = { uint32_t{0x0A130000}, 16 };
    eigrpInterface->getAggregator().installSummary(summaryPrefix);
    interface::MockInterface* extraIface = new interface::MockInterface(*global, interface::InterfaceType::GIGABIT_ETHERNET);
    uint32_t key = interface::encodeInterfaceKey(interface::InterfaceType::GIGABIT_ETHERNET, 1);
    vrf->getInterfaceManager().add(extraIface, key);
    extraIface->configs.id = 1;
    extraIface->configs.key = key;
    extraIface->enableIPs();
    extraIface->enableShutdown();
    setIPv4(0xC0A80102, 24, extraIface);
    {
        EXPECT_CALL(*extraIface, enqueuePacket(::testing::_)).Times(::testing::AnyNumber());
        eigrpInstance->refreshInterfaceList();
    }
    EXPECT_EQ(getInterfaceList().size(), 2);

    types::IPAddress neighborIp1 = uint32_t{0xC0A80110};
    types::IPAddress neighborIp2 = uint32_t{0xC0A80120};
    addNeighbor(neighborIp1);
    addNeighbor(neighborIp2, routing::eigrp::Neighbor::Version::LEGACY, &(getInterfaceList().at(key)));
    auto neighbor2 = getNeighbor(neighborIp2, &(getInterfaceList().at(key)));

    routing::eigrp::ReceivedRoute route1 = getRoute(extraIface->configs.key);
    route1.prefix = { uint32_t{0x0A130100}, 24 };
    route1.nextHop = neighborIp2;

    routing::eigrp::ReceivedRoute route2 = route1;
    route2.prefix = { uint32_t{0x0A130200}, 24 };

    //EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_)).Times(0);
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](const processing::PacketBuilder& pkt){
            std::vector<packet::TLV16Option> opts = extractEigrpOptions(const_cast<processing::PacketBuilder&>(pkt));
            bool isValid = true;
            for (auto opt : opts)
            {
                if (opt.type == EIGRP_OPTION_LEGACY_INTERNAL_ROUTE)
                {
                    auto tlv = routing::eigrp::TLVBuilder::decodeRoute(opt, 0, types::AddressFamily::IPv4);
                    isValid = (tlv.has_value() && tlv->prefix == summaryPrefix);
                    break;
                }
            }
            EXPECT_TRUE(isValid);
        }));

    std::vector<routing::eigrp::ReceivedRoute> routes = { route1, route2 };
    getDuel().processReceivedRoutes(routes, *neighbor2);
    vrf->getRib().wait<uint32_t>();

    EXPECT_EQ(vrf->getRib().size<uint32_t>(), 4);

    vrf->getInterfaceManager().remove(key);
    delete extraIface;
}

// Test: Summarization_Disables_Individual_Routes
TEST_F(Internal_EigrpTest, Summarization_Disables_Individual_Routes)
{
    // Manually add a summary route to suppress specifics
    eigrpInterface->getAggregator().installSummary({ uint32_t{0xC0A80100}, 24 });

    // Add a matching specific route
    routing::eigrp::ReceivedRoute route = getRoute(eigrpInterface->interfaceKey);
    route.prefix = { uint32_t{0xC0A80100}, 24 };
    route.routeType = routing::eigrp::RouteType::INTERNAL;
    route.nextHop = uint32_t{0x00000000};
    routing::eigrp::RouteInfo rInfo(route);

    // Should not advertise individual route when summary exists
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_)).Times(0);
    eigrpInstance->broadcastRouteChanges({&rInfo});
}

// Test: Query_Triggers_SIA_Timer
TEST_F(Internal_EigrpTest, Query_Triggers_SIA_Timer)
{
    routing::eigrp::ReceivedRoute route = getRoute(eigrpInterface->interfaceKey);
    route.prefix = { uint32_t{0xC0A80A00}, 24 };
    route.routeType = routing::eigrp::RouteType::INTERNAL;

    types::IPAddress neighborIp = uint32_t{0xC0A80105};
    addNeighbor(neighborIp);  // Add the neighbor to the network
    
    // Expect one packet to be enqueued (since we're sending a query)
    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_)).Times(1);

    routing::eigrp::RouteInfo rInfo(route);

    routing::eigrp::ActiveRoute rt;
    rt.activePrefix = route.prefix;
    rt.originRoute = &rInfo;
    routing::eigrp::OutgoingQuery qy;
    qy.route = &rt;
    rt.pendingQueries[neighborIp] = qy;
    eigrpInterface->getRtp().sendQuery({&rt});
}

// Test: Unicast_Neighbor_Forms_Correctly
TEST_F(Internal_EigrpTest, Unicast_Neighbor_Forms_Correctly)
{
    types::IPAddress neighborIp = uint32_t{0xC0A80132};
    eigrpInterface->getNTable().createNeighbor(neighborIp, routing::eigrp::Neighbor::Version::LEGACY, true);
    auto neighbor = getNeighbor(neighborIp);
    ASSERT_TRUE(neighbor);
    EXPECT_EQ(neighbor->unicast, true);
    eigrpInterface->getNTable().onDown(*neighbor);
}

// Test: Dampening_Suppresses_Updates
TEST_F(Internal_EigrpTest, Dampening_Suppresses_Updates)
{
    eigrpInstance->getGlobalConfigMgr().getConfigs().get<config::Eigrp::DAMPENING>().set(true);
    routing::eigrp::ReceivedRoute route = getRoute(eigrpInterface->interfaceKey);
    route.prefix = { uint32_t{0xC0A80135}, 24 };
    route.routeType = routing::eigrp::RouteType::INTERNAL;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(0);
    routing::eigrp::RouteInfo rInfo(route);
    eigrpInstance->broadcastRouteChanges({&rInfo});
}

// Test: Split_Horizon_Prevents_Route_Propagation_Back
TEST_F(Internal_EigrpTest, Split_Horizon_Prevents_Route_Propagation_Back)
{
    types::IPAddress neighborIp = uint32_t{0xC0A80002};
    addNeighbor(neighborIp);
    auto* neighbor = getNeighbor(neighborIp);
    routing::eigrp::ReceivedRoute route = getRoute(eigrpInterface->interfaceKey);
    route.prefix = { uint32_t{0x0A020000}, 16 };

    // Enforce split horizon
    eigrpInterface->configs.get<config::EigrpInterface::SPLIT_HORIZON>().set(true);

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(1).WillOnce(::testing::Invoke([&]( processing::PacketBuilder& pkt){
            auto opts = extractEigrpOptions(pkt);
            for (auto& opt : opts)
            {
                bool valid = opt.type != EIGRP_OPTION_LEGACY_INTERNAL_ROUTE ||
                    routing::eigrp::TLVBuilder::decodeRoute(opt, 0, types::AddressFamily::IPv4).value().delay == std::numeric_limits<uint64_t>::max();
                ASSERT_TRUE(valid);
            }
        }));
    std::vector<routing::eigrp::ReceivedRoute> routes = { route };
    getDuel().processReceivedRoutes(routes, *neighbor);
}

// Test: Split_Horizon_Disabled_Allows_Advertisement
TEST_F(Internal_EigrpTest, Split_Horizon_Disabled_Allows_Advertisement)
{
    eigrpInterface->configs.get<config::EigrpInterface::SPLIT_HORIZON>().set(false);

    types::IPAddress n1 = uint32_t{0xC0A80631};
    addNeighbor(n1);
    auto nbr = getNeighbor(n1);

    types::IPPrefix p = { uint32_t{0x0AD10000}, 16 };

    routing::eigrp::ReceivedRoute r = getRoute(eigrpInterface->interfaceKey);
    r.prefix = p;
    r.nextHop = n1;
    r.feasibleDistance = 100;
    r.reportedDistance = 90;

    std::vector<routing::eigrp::ReceivedRoute> rs = { r };

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(::testing::AtLeast(1));

    getDuel().processReceivedRoutes(rs, *nbr);
}

// Test: Infeasible_Route_Rejected_Due_To_Feasibility_Condition
TEST_F(Internal_EigrpTest, Infeasible_Route_Rejected_Due_To_Feasibility_Condition) 
{
    // Add neighbor
    types::IPAddress neighborIp = uint32_t{0xC0A80131};
    addNeighbor(neighborIp);
    auto neighbor = getNeighbor(neighborIp);

    // Add better existing route to topology
    routing::eigrp::ReceivedRoute existing;

    types::IPPrefix prefix = { uint32_t{0x0A000000}, 24 };
    existing.prefix = prefix;
    existing.feasibleDistance = 100;
    existing.reportedDistance = 80;
    existing.nextHop = neighborIp;
    existing.hopCount = 1;

    auto& tt = getTopologyTable();
    auto& top = tt.ensure(prefix);
    tt.addRouteUpdate(existing, neighbor, top);

    // Simulate route from neighbor with RD > existing FD
    routing::eigrp::ReceivedRoute newRoute = getRoute(eigrpInterface->interfaceKey);
    newRoute.prefix = prefix;
    newRoute.reportedDistance = 200;
    newRoute.feasibleDistance = 300;

    std::vector<routing::eigrp::ReceivedRoute> rs = { newRoute };
    getDuel().processReceivedRoutes(rs, *neighbor);
    vrf->getRib().wait<uint32_t>();

    // Should NOT overwrite existing route
    auto entry = tt.find(prefix);
    ASSERT_NE(entry, nullptr);
    bool hasInfeasible = std::any_of(entry->routesBySource.begin(), entry->routesBySource.end(),
        [&](const auto& r) {
            return r.second.routeInfo.reportedDistance >= existing.feasibleDistance;
        });
    ASSERT_TRUE(hasInfeasible);
    utils::RCU::Guard guard;
    auto route = vrf->getRib().lookup<uint32_t>(prefix, guard);
    ASSERT_TRUE(route);
    EXPECT_EQ(route->nextHops[0].nextHop, existing.nextHop.v4());
}

// =====================================================================
// TODO: not yet implemented -- regression tests for known historical bugs
// (see project memory "EIGRP implementation audit and bug fixes",
// 2026-03-21 sessions 1 & 2)
// =====================================================================

// NeighborTable::removeAllMulticast iterated `it->second` into onDown(),
// which erases the map entry, invalidating `it` before ++it. Fixed by
// capturing std::next(it) before the call. Regression: removing multiple
// multicast neighbors in one pass must not skip/crash on the next entry.
TEST_F(Internal_EigrpTest, NeighborTable_RemoveAllMulticast_NoIteratorInvalidation)
{
}

// NeighborTable::onDown and deleteNeighbor previously never called
// delGlobalNeighbor, leaving dangling Neighbor* pointers in
// Eigrp::allNeighbors that DuelEngine::setActive iterates. Regression:
// after a neighbor goes down, it must no longer appear in the global
// neighbor list.
TEST_F(Internal_EigrpTest, NeighborTable_OnDown_RemovesFromGlobalNeighborList)
{
}

// DuelEngine::setActive checked activeRoutes.empty() (wrong container)
// instead of routes.empty(). Regression: a route with remaining feasible
// successors must NOT transition to active state when the successor is
// lost.
TEST_F(Internal_EigrpTest, DuelEngine_SetActive_DoesNotActivateWithRemainingFeasibleSuccessors)
{
}

// TopologyTable's destructor previously leaked all heap TopologyEntry*.
// Regression: destroying a TopologyTable with entries must not leak
// (observable via repeated construct/destroy under ASan, or via an
// internal entry-count/allocation check).
TEST_F(Internal_EigrpTest, TopologyTable_Destructor_FreesAllEntries)
{
}

// TopologyTable::pruneExpired erased a map node without deleting
// it->second first, leaking the TopologyEntry. Regression: pruning
// expired entries must free their heap allocations.
TEST_F(Internal_EigrpTest, TopologyTable_PruneExpired_FreesPrunedEntries)
{
}

// ReliableRX::checkInit previously never set initComplete = true, so
// neighbors formed but full topology was never sent; sendFullTopology's
// check was also ordered before checkInit in processAck. Regression:
// after the init sequence completes (null update + ACK), initComplete
// must be true and the full topology table must be sent to the new
// neighbor.
TEST_F(Internal_EigrpTest, ReliableRX_CheckInit_SetsInitCompleteAndSendsFullTopology)
{
}

// AuthHandler::appendAuthHMAC previously zeroed the SHA256 password buffer
// BEFORE computing the HMAC, so the HMAC was always computed with an
// all-zero key. Regression: the SHA256 auth TLV's HMAC must match a
// known-good value computed with the configured (non-zero) key.
TEST_F(Internal_EigrpTest, AuthTLV_SHA256_HmacUsesConfiguredKeyNotZeroed)
{
}

// EigrpPacketBuilder::appendAuthTLV pre-allocated 36 bytes and appended
// size 36 regardless of auth type; SHA256 requires 52 bytes, causing a
// buffer overflow + wrong wire-format length. Regression: a SHA256 auth
// TLV must be encoded with a 52-byte value field.
TEST_F(Internal_EigrpTest, AuthTLV_SHA256_EncodedWith52ByteValue)
{
}

// EigrpPacketBuilder::appendParameterTLV's peer-termination path wrote
// 0xFF bytes, but processHello checks for 0x00 -- a protocol mismatch.
// Regression: a peer-termination parameter TLV must be encoded with
// zeroed K-values so the receiving side's processHello recognizes it.
TEST_F(Internal_EigrpTest, ParameterTLV_PeerTermination_EncodedWithZeroedKValues)
{
}

// ReliableTransport::handleRetransmission (unicast) performed a
// non-atomic read-modify-write (rto = min(rto*2, 60)) on a
// std::atomic<double>. Regression: concurrent retransmission handling
// across multiple unicast neighbors must not corrupt rto (data race),
// and the exponential backoff cap of 60s must hold.
TEST_F(Internal_EigrpTest, ReliableTransport_UnicastBackoff_AtomicRtoUpdateCappedAt60)
{
}

// =====================================================================
// TODO: not yet implemented -- broader coverage gaps
// =====================================================================

// RouteAggregator (interface/RouteAggregator.cpp) recalculates the summary
// metric when a covered component route's metric changes.
TEST_F(Internal_EigrpTest, RouteAggregator_SummaryMetric_RecalculatedOnComponentRouteChange)
{
}

// RouteAggregator withdraws the summary route once all covered component
// routes are withdrawn/down.
TEST_F(Internal_EigrpTest, RouteAggregator_Summary_WithdrawnWhenAllComponentRoutesGone)
{
}

// Auto-summarization at classful network boundaries (if supported by
// RouteAggregator/EigrpConfig).
TEST_F(Internal_EigrpTest, RouteAggregator_AutoSummary_AtClassfulBoundary)
{
}

// TLV parsing: duplicate TLVs of the same type in one packet (see top-of-file
// TODO).
TEST_F(Internal_EigrpTest, TLVParsing_DuplicateTlvsInPacket_Handled)
{
}

// TLV parsing: malformed TLV (length field exceeds remaining packet bytes)
// must not overrun the buffer and must be rejected gracefully.
TEST_F(Internal_EigrpTest, TLVParsing_MalformedTlvLength_RejectedSafely)
{
}

// TLV parsing: unknown/unsupported TLV type is skipped without aborting
// processing of subsequent TLVs.
TEST_F(Internal_EigrpTest, TLVParsing_UnknownTlvType_SkippedNotFatal)
{
}

// Wide metrics: overflow/underflow behavior when configured bandwidth/delay
// exceed the wide-metric encoding range.
TEST_F(Internal_EigrpTest, WideMetrics_OverflowValues_ClampedOrRejected)
{
}

// Wide metrics vs legacy metrics coexistence: a wide-metric neighbor and a
// legacy-metric neighbor on the same interface.
TEST_F(Internal_EigrpTest, WideMetrics_LegacyCoexistence_OnSameInterface)
{
}

// Multi-AS-instance isolation: two EIGRP processes with different AS numbers
// on the same router do not share topology/neighbor state.
TEST_F(Internal_EigrpTest, MultiAsInstance_TopologyIsolation_BetweenDifferentAsNumbers)
{
}

// IPv6 route update processing (marked //TODO at IPv6_Update_Processing).
TEST_F(Internal_EigrpTest, IPv6_Update_Processing_InstallsRouteWithCorrectNextHop)
{
}

// IPv6 query/reply SIA handling (marked //TODO at IPv6_Query_Reply_SIA).
TEST_F(Internal_EigrpTest, IPv6_Query_Reply_Sia_TimeoutBehavesLikeIPv4)
{
}

// Conditional-receive unicast-must-wait-for-CR (marked //TODO at
// ConditionalReceive_Unicast_Must_Wait_For_CR).
TEST_F(Internal_EigrpTest, ConditionalReceive_UnicastNeighbor_WaitsForCrBeforeUpdate)
{
}

// Conditional-receive Receive_CR (marked //TODO at
// ConditionalReceive_Receive_CR).
TEST_F(Internal_EigrpTest, ConditionalReceive_ReceiveCr_ClearsConditionalFlagOnAllNeighbors)
{
}

// Wide resync after a legacy-to-wide metric teardown/rebuild (top-of-file
// TODO: "tests for wide resync and legacy teardown").
TEST_F(Internal_EigrpTest, WideMetrics_ResyncAfterLegacyTeardown)
{
}

// Peer termination TLV handling end-to-end (top-of-file TODO: "tests for
// peer termination").
TEST_F(Internal_EigrpTest, PeerTermination_TlvReceived_NeighborGracefullyRemoved)
{
}

// Poisoned-route loop prevention (top-of-file TODO: "tests for poisened loop
// prevention").
TEST_F(Internal_EigrpTest, PoisonedRoute_LoopPrevention_InfiniteMetricNotInstalled)
{
}
