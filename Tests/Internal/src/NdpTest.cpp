#include <gtest/gtest.h>
#include <MockInterface.hpp>
#include <MockFileSystem.hpp>
#include <infrastructure/Ndp.h>
#include <Global.h>
#include <NetworkSpan.hpp>
#include <chrono>
#include <processing/PacketBuilder.hpp>
#include <infrastructure/IPPacket.h>
#include <configs/FieldAccessor.hpp>

class Internal_NdpTest : public ::testing::Test
{
protected:
    interface::MockInterface* iface;
    infrastructure::Ndp* ndp;
    core::Global* global;
    cli::MockFileSystem fs;
    alignas(64) uint8_t buf[128];

    types::IPv6Prefix ip = { (static_cast<__uint128_t>(0xFe80000000000000) << 64) | 0x030b0c0d0e0f1001, 64, true };
    types::IPv6Prefix otherGlobal = { (static_cast<__uint128_t>(0xFD12000000000000) << 64) | 0x0000000000000102, 64, true };

    types::IPv6Prefix localLinkIp = { (static_cast<__uint128_t>(0xFe80000000000000) << 64) | 0x020a0b0c0d0e0f01, 64, true };
    types::IPv6Prefix globalIp = { (static_cast<__uint128_t>(0xFD12000000000000) << 64) | 0x0000000000000101, 64, true };

    types::IPv6Prefix prefix = { (static_cast<__uint128_t>(0xFD12000000000000) << 64) | 0x0000000000000000, 64, true };
    types::Mac mac = { 0x0A0B0C0D0E0F };
    types::Mac mac2 = { 0x0A0B0C0D0Ea0A };

    void SetUp() override
    {
        std::memset(buf, 0, 128);
        global = new core::Global(fs, {}, true, true);
        iface = new interface::MockInterface(*global);
        iface->enableShutdown();
        iface->configs.ipv6.addAddress(localLinkIp, true);
        iface->configs.ipv6.linkLocalAddress->valid = true;
        iface->configs.ipv6.linkLocalAddress->valid = true;
        iface->configs.ipv6.linkLocalAddress->tentative= false;
        iface->configs.ipv6.addAddress(globalIp, false);
        iface->configs.ipv6.globalAddresses[0]->valid = true;
        iface->configs.ipv6.globalAddresses[0]->tentative = false;
        ndp = &iface->ndp;
        ndp->configs.get<config::Ndp::BASE>().get().get<config::NdpBase::DAD_TIME>().set(10);
    }

    void TearDown() override
    {
        ndp->shutdown();
        delete iface;
        delete global;
    }

    void addNdpEntry(const types::IPv6Address& targetIp, types::Mac mac)
    {
        ndp->addNdpEntry(targetIp, mac);
        auto& entry = getNdpCache().at(targetIp);
        ndp->completeNdpEntry(targetIp, entry, mac);
    }

    void onReachableTimeout(const types::IPv6Address& targetIp) { ndp->onReachableTimeout(targetIp); }
    void scheduleNextRA() { ndp->scheduleNextRA(); }
    std::unordered_map<types::IPv6Address, infrastructure::Ndp::NdpCacheEntry>& getNdpCache() { return ndp->ndpCache; }
    std::vector<interface::InterfaceConfigs::IPv6State::IPv6Address*>& getIPv6s() { return iface->configs.ipv6.globalAddresses; }
    interface::InterfaceConfigs::IPv6State::IPv6Address* getLinkLocal() { return iface->configs.ipv6.linkLocalAddress; }
    std::mutex& getIPv6Mutex() { return iface->configs.ipv6.ipMutex; }
    void clearIPv6s() { std::lock_guard<std::mutex> lock(iface->configs.ipv6.ipMutex); iface->configs.ipv6.globalAddresses.clear(); }
    void clearUnsolidated() { std::lock_guard<std::mutex> lock(iface->configs.ipv6.ipMutex); ndp->lastUnsolicitedNaTime.clear(); }
    processing::PacketBuilder& routeAdvertisment(processing::PacketBuilder& pkt, types::Mac* raMac = nullptr) { ndp->routeAdvertisement(pkt, raMac ? *raMac : iface->configs.getMac()); return pkt; }
    config::NdpRegistry& getConfigs() { return ndp->configs; }
};

// Test: SendNS_ReceiveNA_CreatesEntry
TEST_F(Internal_NdpTest, SendNS_ReceiveNA_CreatesEntry)
{
    EXPECT_CALL(*iface, enqueuePacket(::testing::_)).Times(2); // failed

    processing::PacketBuilder dummy(iface);
    dummy.reserveHeader(packet::HeaderType::ETHERNET, packet::EthernetHeader::fixedSize);
    dummy.reserveHeader(packet::HeaderType::IPV6, packet::IPv6Header::fixedSize);
    dummy.nextBuildHeader();
    ndp->resolveAndSend(ip, dummy);

    uint8_t res[4] = { 0xE0, 0x00, 0x00, 0x00 };
    uint8_t trail[24] = {0};
    utils::writeU128(trail, ip.addr);
    packet::TLV8BufferManager opt(trail + 16, 8);
    utils::writeU48(opt.getNextValBuf(6), mac);
    opt.append(ICMPV6_OPTION_NDP_TARGET, 1, nullptr, 6);

    packet::Icmpv6Header na;
    na.setBuffer(buf);
    na.setType(ICMPV6_OPCODE_NDP_NEIGHBOR_ADVERTISEMENT);
    na.setCode(0x00);
    na.setReserved(res);
    na.setTrail(trail, 24);

    ndp->receiveNeighborAdvertisement(na, ip);

    uint8_t resolvedMac[6];
    EXPECT_TRUE(ndp->getMac(resolvedMac, ip)); // failed
    EXPECT_EQ(types::Mac{utils::readU48(resolvedMac)}, mac);
}

// Test: UnsolicitedNA_CreatesEntry
TEST_F(Internal_NdpTest, UnsolicitedNA_CreatesEntry)
{
    iface->configs.getConfigs().get<config::Interface::IPV6_ND>().get().get<config::Ndp::NA_GLEAN>().set(true);
    uint8_t res[4] = { 0xA0, 0x00, 0x00, 0x00 };
    uint8_t trail[24];
    utils::writeU128(trail, ip.addr);
    packet::TLV8BufferManager opt(trail + 16, 8);
    utils::writeU48(opt.getNextValBuf(6), mac);
    opt.append(ICMPV6_OPTION_NDP_TARGET, 1, nullptr, 6);

    packet::Icmpv6Header na;
    na.setBuffer(buf);
    na.setCode(ICMPV6_OPCODE_NDP_NEIGHBOR_ADVERTISEMENT);
    na.setReserved(res);
    na.setTrail(trail, 24);

    ndp->receiveNeighborAdvertisement(na, ip);

    uint8_t resolvedMac[6];
    ASSERT_TRUE(ndp->getMac(resolvedMac, ip));
    EXPECT_EQ(types::Mac{utils::readU48(resolvedMac)}, mac);
}

// Test: CacheEntryExpiresAfterReachableTime
TEST_F(Internal_NdpTest, CacheEntryExpiresAfterReachableTime)
{
    getConfigs().get<config::Ndp::BASE>().get().get<config::NdpBase::REACHABLE_TIME>().set(100);
    getConfigs().get<config::Ndp::BASE>().get().get<config::NdpBase::CACHE_EXPIRE>().set(1);

    addNdpEntry(ip, mac);

    uint8_t resolvedMac[6];
    ASSERT_TRUE(ndp->getMac(resolvedMac, ip));

    std::this_thread::sleep_for(std::chrono::seconds(2));

    ASSERT_FALSE(ndp->getMac(resolvedMac, ip));
}

// Test: NudTransitionsAndRemoval
TEST_F(Internal_NdpTest, NudTransitionsAndRemoval)
{
    iface->blockEnqueues();
    getConfigs().get<config::Ndp::BASE>().get().get<config::NdpBase::REACHABLE_TIME>().set(50);
    getConfigs().get<config::Ndp::BASE>().get().get<config::NdpBase::CACHE_EXPIRE>().set(1);
    getConfigs().get<config::Ndp::NUD_RETRY_INTERVAL>().set(1);
    getConfigs().get<config::Ndp::NUD_RETRY_ATTEMPTS>().set(1);

    ndp->addNdpEntry(ip, mac);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    onReachableTimeout(ip); // REACHABLE -> STALE
    std::this_thread::sleep_for(std::chrono::seconds(2));

    uint8_t resolvedMac[6];
    ASSERT_FALSE(ndp->getMac(resolvedMac, ip));
}

// Test: SLAAC_RSAndRA_CreatesAddress
TEST_F(Internal_NdpTest, SLAAC_RSAndRA_CreatesAddress)
{
    iface->configs.getConfigs().get<config::Interface::MAC_ADDRESS>().set(mac);
    iface->configs.syncMac();
    getConfigs().get<config::Ndp::AUTOCONFIG_PREFIX>().set(true);

    EXPECT_CALL(*iface, enqueuePacket(::testing::_)).Times(2);
    ndp->initiateSlaac(); // Would normally send RS

    uint8_t fullAddr[16];
    uint8_t euiAddress[16];
    uint8_t macAddr[6];
    utils::writeU128(euiAddress, prefix.addr);
    infrastructure::calculateEui64(fullAddr, euiAddress, iface->configs.getMac(macAddr));

    uint8_t trail[40] = {0};

    trail[8] = ICMPV6_OPTION_NDP_PREFIX;
    trail[9] = 0x04;
    trail[10] = 0x40; // 64-bit prefix length
    trail[11] = 0xC0; // L and A bits
    utils::writeU32(trail + 12, 1800);
    utils::writeU32(trail + 16, 900);
    utils::writeU128(trail + 24, prefix.addr);

    packet::Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(ICMPV6_OPCODE_NDP_ROUTE_ADVERTISEMENT);
    ra.setCode(0x00);
    ra.setTrail(trail, 40);
    
    ndp->receiveRouteAdvertisement(ra, ip, mac2);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    std::lock_guard<std::mutex> lock(getIPv6Mutex());
    bool found = false;
    for (auto* addr : getIPv6s())
    {
        if (addr->prefix.addr == utils::readU128(fullAddr) && addr->valid)
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// Test: SLAAC_DAD_Failure_MarksDuplicate
TEST_F(Internal_NdpTest, SLAAC_DAD_Failure_MarksDuplicate) 
{
    iface->blockEnqueues();
    iface->configs.getConfigs().get<config::Interface::MAC_ADDRESS>().set(mac);
    iface->configs.syncMac();
    getConfigs().get<config::Ndp::AUTOCONFIG_PREFIX>().set(true);
    getConfigs().get<config::Ndp::DAD_ATTEMPTS>().set(4);
    getConfigs().get<config::Ndp::BASE>().get().get<config::NdpBase::DAD_TIME>().set(50);

    uint8_t fullAddr[16];
    uint8_t euiAddress[16];
    uint8_t macAddress[6];
    utils::writeU128(euiAddress, prefix.addr);
    utils::writeU48(macAddress, mac);
    infrastructure::calculateEui64(fullAddr, euiAddress, macAddress);

    types::IPv6Prefix pref(utils::readU128(fullAddr), 64, true);
    auto* addr = iface->configs.ipv6.addAddress(pref, false);
    addr->tentative = true;

    ndp->duplicateAddressDetection(*addr);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    uint8_t res[4] = { 0xA0, 0x00, 0x00, 0x00 };
    uint8_t trail[24] = {0};
    packet::TLV8BufferManager opt(trail + 16, 8);
    utils::writeU48(opt.getNextValBuf(6), mac);
    opt.append(ICMPV6_OPTION_NDP_TARGET, 1, nullptr, 6);
    std::memcpy(trail, fullAddr, 16);

    packet::Icmpv6Header na;
    na.setBuffer(buf);

    na.setType(ICMPV6_OPCODE_NDP_NEIGHBOR_ADVERTISEMENT);
    na.setCode(0x00);
    na.setReserved(res);
    na.setTrail(trail, 24);

    ndp->receiveNeighborAdvertisement(na, IPV6_SOURCE);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::lock_guard<std::mutex> lock(getIPv6Mutex());
    EXPECT_FALSE(addr->valid);
    EXPECT_FALSE(addr->tentative);
}

// Test: SLAAC_ValidLifetimeExpires
TEST_F(Internal_NdpTest, SLAAC_ValidLifetimeExpires) 
{
    iface->blockEnqueues();
    iface->configs.getConfigs().get<config::Interface::MAC_ADDRESS>().set(mac);
    iface->configs.syncMac();
    getConfigs().get<config::Ndp::AUTOCONFIG_PREFIX>().set(true);

    uint8_t fullAddr[16];
    uint8_t euiPrefix[16];
    uint8_t euiMac[6];
    utils::writeU128(euiPrefix, prefix.addr);
    utils::writeU48(euiMac, mac);
    infrastructure::calculateEui64(fullAddr, euiPrefix, euiMac);
    auto* addr = iface->configs.ipv6.addAddress(types::IPv6Prefix{utils::readU128(fullAddr), 64}, false);
    addr->tentative = false;
    addr->valid = true;

    getConfigs().get<config::Ndp::RA_LIFETIME>().set(1);

    ndp->duplicateAddressDetection(*addr);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::lock_guard<std::mutex> lock(getIPv6Mutex());
    EXPECT_FALSE(addr->globalValid);
}

// Test: SLAAC_PreferredLifetimeExpires
TEST_F(Internal_NdpTest, SLAAC_PreferredLifetimeExpires) 
{
    iface->blockEnqueues();
    iface->configs.getConfigs().get<config::Interface::MAC_ADDRESS>().set(mac);
	iface->configs.syncMac();
    getConfigs().get<config::Ndp::AUTOCONFIG_PREFIX>().set(true);

    uint8_t trail[40] = {0};

    trail[8] = ICMPV6_OPTION_NDP_PREFIX;
    trail[9] = 0x04;
    trail[10] = 0x40; // 64-bit prefix length
    trail[11] = 0xC0; // L and A bits
    utils::writeU32(trail + 12, 100);
    utils::writeU32(trail + 16, 1);
    utils::writeU128(trail + 24, prefix.addr);

    packet::Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(ICMPV6_OPCODE_NDP_ROUTE_ADVERTISEMENT);
    ra.setCode(0x00);
    ra.setTrail(trail, 40);

    ndp->receiveRouteAdvertisement(ra, IPV6_SOURCE, mac2);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    auto* addr = getIPv6s().back();

    EXPECT_TRUE(addr->deprecated);
}

// Test: SLAAC_Disabled_IgnoresPrefixes
TEST_F(Internal_NdpTest, SLAAC_Disabled_IgnoresPrefixes) 
{
    iface->blockEnqueues();
    clearIPv6s();
    iface->configs.getConfigs().get<config::Interface::MAC_ADDRESS>().set(mac);
	iface->configs.syncMac();
    getConfigs().get<config::Ndp::AUTOCONFIG_PREFIX>().set(false);

    uint8_t trail[48] = {0};

    trail[16] = ICMPV6_OPTION_NDP_PREFIX;
    trail[17] = 0x04;
    trail[18] = 0x40; // 64-bit prefix length
    trail[19] = 0xC0; // L and A bits
	utils::writeU32(trail + 20, 100);
	utils::writeU32(trail + 24, 1);
    utils::writeU128(trail + 32, prefix.addr);

    packet::Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(ICMPV6_OPCODE_NDP_ROUTE_ADVERTISEMENT);
    ra.setCode(0x00);
    ra.setTrail(trail, 48);

    ndp->receiveRouteAdvertisement(ra, IPV6_SOURCE, mac2);

    EXPECT_TRUE(getIPv6s().empty());
}

// Test: DAD_NoConflict_MarksValid
TEST_F(Internal_NdpTest, DAD_NoConflict_MarksValid) 
{
    iface->blockEnqueues();
    auto* addr = iface->configs.ipv6.addAddress(ip, false);
    addr->tentative = true;
    getConfigs().get<config::Ndp::DAD_ATTEMPTS>().set(1);
    getConfigs().get<config::Ndp::BASE>().get().get<config::NdpBase::DAD_TIME>().set(100);

    ndp->duplicateAddressDetection(*addr);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::lock_guard<std::mutex> lock(getIPv6Mutex());
    EXPECT_FALSE(addr->tentative);
    EXPECT_TRUE(addr->valid);
}

// Test: DAD_ConflictFromNA_MarksDuplicate
TEST_F(Internal_NdpTest, DAD_ConflictFromNA_MarksDuplicate)
{
    iface->blockEnqueues();
    auto* addr = iface->configs.ipv6.addAddress(ip, false);
    addr->tentative = true;
    getConfigs().get<config::Ndp::DAD_ATTEMPTS>().set(1);
    getConfigs().get<config::Ndp::BASE>().get().get<config::NdpBase::DAD_TIME>().set(100);

    ndp->duplicateAddressDetection(*addr);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    uint8_t trail[24] = {0};
    utils::writeU128(trail, ip);
    trail[16] = ICMPV6_OPTION_NDP_TARGET;
    trail[17] = 0x01;
    utils::writeU48(trail + 18, mac);

    packet::Icmpv6Header na;
    na.setBuffer(buf);

    na.setType(ICMPV6_OPCODE_NDP_NEIGHBOR_ADVERTISEMENT);
    na.setCode(0x00);
    na.setTrail(trail, 24);

    ndp->receiveNeighborAdvertisement(na, ip);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::lock_guard<std::mutex> lock(getIPv6Mutex());
    EXPECT_FALSE(addr->tentative);
    EXPECT_FALSE(addr->valid);
}

// Test: DAD_MultipleAttemptsRequired
TEST_F(Internal_NdpTest, DAD_MultipleAttemptsRequired)
{
    iface->blockEnqueues();
    auto* addr = iface->configs.ipv6.addAddress(ip, false);
    addr->tentative = true;
    getConfigs().get<config::Ndp::DAD_ATTEMPTS>().set(2);
    getConfigs().get<config::Ndp::BASE>().get().get<config::NdpBase::DAD_TIME>().set(50);

    ndp->duplicateAddressDetection(*addr);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    {
        std::lock_guard<std::mutex> lock(getIPv6Mutex());
        EXPECT_TRUE(addr->tentative);  // 1st attempt done, not yet accepted
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    {
        std::lock_guard<std::mutex> lock(getIPv6Mutex());
        EXPECT_FALSE(addr->tentative);
        EXPECT_TRUE(addr->valid);
    }
}

// Test: DAD_Suppressed_NoDetectionOccurs
TEST_F(Internal_NdpTest, DAD_Suppressed_NoDetectionOccurs)
{
    auto* addr = iface->configs.ipv6.addAddress(ip, false);
    addr->tentative = true;
    getConfigs().get<config::Ndp::DAD_ATTEMPTS>().set(0);
    getConfigs().get<config::Ndp::BASE>().get().get<config::NdpBase::DAD_TIME>().set(100);

    ndp->duplicateAddressDetection(*addr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::lock_guard<std::mutex> lock(getIPv6Mutex());
    EXPECT_TRUE(addr->tentative);  // Should still be tentative due to suppression
}

// Test: ProxyNA_RespondsWithCorrectMAC
TEST_F(Internal_NdpTest, ProxyNA_RespondsWithCorrectMAC)
{
    uint8_t proxyIp[16];
    utils::writeU128(proxyIp, ip);
    uint8_t proxyMac[6];
    utils::writeU48(proxyMac, mac);
    proxyIp[15] = 'c';
    proxyIp[7] = '4';
    proxyMac[5] = 'c';
    ndp->addNdpEntry(utils::readU128(proxyIp), utils::readU48(proxyMac), true);

    packet::Icmpv6Header ns;
    ns.setBuffer(buf);

    ns.setType(ICMPV6_OPCODE_NDP_NEIGHBOR_SOLICITATION);
    ns.setCode(0x00);
    std::memcpy(ns.getTrailData(), proxyIp, 16);

    EXPECT_CALL(*iface, enqueuePacket(testing::_))
        .WillOnce(testing::Invoke([&](processing::PacketBuilder& pkt) {
            bool hasEth = false;
            bool hasIp = false;
            bool hasICMPv6 = false;
            for (size_t i = 0; i < pkt.getHeaderCount(); ++i)
            {
                auto header = pkt.getHeaders()[i];
                if (header.type == packet::HeaderType::ETHERNET)
                {
                    if (hasEth) FAIL();
                    hasEth = true;
                    auto eth = reinterpret_cast<packet::EthernetHeaderRaw*>(pkt.getHeaders()[i].buffer);
                    EXPECT_EQ(types::Mac{utils::readU48(eth->destinationMac)}, mac);
                }
                else if (header.type == packet::HeaderType::IPV6)
                {
                    if (hasIp) FAIL();
                    hasIp = true;
                }
                else if (header.type == packet::HeaderType::ICMPV6)
                {
                    if (hasICMPv6) FAIL();
                    hasICMPv6 = true;
                    auto icmp = reinterpret_cast<packet::Icmpv6HeaderRaw*>(header.buffer);
                    EXPECT_EQ(icmp->type, ICMPV6_OPCODE_NDP_NEIGHBOR_ADVERTISEMENT);
                    EXPECT_EQ(std::memcmp(icmp->reserved + 4, proxyIp, 16), 0);
                }
            }
            ASSERT_TRUE(hasEth && hasIp && hasICMPv6);
        }));

    ndp->receiveNeighborSolicitation(ns, ip, mac);
}

// Test: Proxy_UnsolicitedNASent
TEST_F(Internal_NdpTest, Proxy_UnsolicitedNASent)
{
    ndp->addNdpEntry(ip,mac);

    EXPECT_CALL(*iface, enqueuePacket(testing::_))
        .WillOnce(testing::Invoke([&](processing::PacketBuilder& pkt) {
            bool hasEth = false;
            bool hasIp = false;
            bool hasICMP = false;
            for (size_t i = 0; i < pkt.getHeaderCount(); ++i)
            {
                auto header = pkt.getHeaders()[i];
                if (header.type == packet::HeaderType::ETHERNET)
                {
                    if (hasEth) FAIL();
                    hasEth = true;
                    auto eth = reinterpret_cast<packet::EthernetHeaderRaw*>(header.buffer);
                    EXPECT_EQ(std::memcmp(eth->destinationMac, ETHERNET_MAC_BROADCAST, 6), 0);
                }
                else if (header.type == packet::HeaderType::IPV6)
                {
                    if (hasIp) FAIL();
                    hasIp = true;
                }
                else if (header.type == packet::HeaderType::ICMPV6)
                {
                    if (hasICMP) FAIL();
                    hasICMP = true;
                    auto icmp = reinterpret_cast<packet::Icmpv6HeaderRaw*>(header.buffer);
                    EXPECT_EQ(icmp->type, ICMPV6_OPCODE_NDP_NEIGHBOR_ADVERTISEMENT);
                    EXPECT_EQ(utils::readU128(icmp->reserved + 4), ip);
                }
            }
            ASSERT_TRUE(hasEth && hasIp && hasICMP);
        }));

    clearUnsolidated();
    ndp->sendNeighborAdvertisement(utils::readU48(ETHERNET_MAC_BROADCAST), ip);
}

// Test: DADProbe_TriggersProxyNA
TEST_F(Internal_NdpTest, DADProbe_TriggersProxyNA)
{
    ndp->addNdpEntry(ip, mac, true);

    uint8_t unspecified[16] = {0}; // ::

    packet::Icmpv6Header ns;
    ns.setBuffer(buf);
    ns.setType(ICMPV6_OPCODE_NDP_NEIGHBOR_SOLICITATION);
    utils::writeU128(ns.getTrailData(), ip);

    clearUnsolidated();

    EXPECT_CALL(*iface, enqueuePacket(testing::_))
        .WillOnce(testing::Invoke([&](processing::PacketBuilder& pkt) {
            bool hasEth = false;
            bool hasIp = false;
            bool hasICMP = false;
            for (size_t i = 0; i < pkt.getHeaderCount(); ++i)
            {
                auto header = pkt.getHeaders()[i];
                if (header.type == packet::HeaderType::ETHERNET)
                {
                    if (hasEth) FAIL();
                    hasEth = true;
                }
                else if (header.type == packet::HeaderType::IPV6)
                {
                    if (hasIp) FAIL();
                    hasIp = true;
                }
                else if (header.type == packet::HeaderType::ICMPV6)
                {
                    if (hasICMP) FAIL();
                    hasICMP = true;
                    auto icmp = reinterpret_cast<packet::Icmpv6HeaderRaw*>(header.buffer);
                    EXPECT_EQ(icmp->type, ICMPV6_OPCODE_NDP_NEIGHBOR_ADVERTISEMENT);
                    EXPECT_EQ(utils::readU128(icmp->reserved + 4), ip);
                }
            }
            ASSERT_TRUE(hasEth && hasIp && hasICMP);
        }));

    ndp->receiveNeighborSolicitation(ns, unspecified, utils::readU48(ETHERNET_MAC_SOURCE));
}

// Test: RA_MOFlagsUpdateConfig
TEST_F(Internal_NdpTest, RA_MOFlagsUpdateConfig)
{
    getConfigs().get<config::Ndp::MANAGED_CONFIG_FLAG>().set(true);
    getConfigs().get<config::Ndp::OTHER_CONFIG_FLAG>().set(true);

    processing::PacketBuilder ra(iface);
    routeAdvertisment(ra);
    
    bool hasEth = false;
    bool hasIp = false;
    bool hasICMP = false;
    for (size_t i = 0; i < ra.getHeaderCount(); ++i)
    {
        auto header = ra.getHeaders()[i];
        if (header.type == packet::HeaderType::ETHERNET)
        {
            if (hasEth) FAIL();
            hasEth = true;
        }
        else if (header.type == packet::HeaderType::IPV6)
        {
            if (hasIp) FAIL();
            hasIp = true;
        }
        else if (header.type == packet::HeaderType::ICMPV6)
        {
            if (hasICMP) FAIL();
            hasICMP = true;
            auto icmp = reinterpret_cast<packet::Icmpv6HeaderRaw*>(header.buffer);
            uint8_t flags = icmp->reserved[1];
            EXPECT_TRUE(flags & (1 << 6));
            EXPECT_TRUE(flags & (1 << 7));
        }
    }
    ASSERT_TRUE(hasEth && hasIp && hasICMP);
}

// Test: RA_NonICMPv6HeaderIsDropped
TEST_F(Internal_NdpTest, RA_NonICMPv6HeaderIsDropped)
{
    // This would normally be dropped before it reaches NDP
    // We simulate this by ensuring no effect happens
    getConfigs().get<config::Ndp::RA_SUPPRESS>().set(false);

    uint8_t trail[8] = {0};

    packet::Icmpv6Header ra;
    ra.setBuffer(buf);
    
    ra.setType(ICMPV6_OPCODE_NDP_ROUTE_ADVERTISEMENT);
    ra.setCode(0x00);
    ra.setTrail(trail, 8);

    // No extension headers included — simulate filtered result
    ndp->receiveRouteAdvertisement(ra, IPV6_SOURCE, mac2);
    SUCCEED(); // Reaching here = accepted or ignored, no crash
}

// Test: RA_ExcludedPrefixIgnored
TEST_F(Internal_NdpTest, RA_ExcludedPrefixIgnored)
{
    getConfigs().get<config::Ndp::AUTOCONFIG_PREFIX>().set(true);
    clearIPv6s();
    iface->configs.getConfigs().get<config::Interface::MAC_ADDRESS>().set(mac);
    iface->configs.syncMac();

    ndp->addSlaacExclusionPrefix(prefix, false); // exclude

    uint8_t trail[40] = {0};
    trail[8] = ICMPV6_OPTION_NDP_PREFIX;
    trail[9] = 0x04;
    trail[10] = 0x40; // /64
    trail[11] = 0xC0; // L + A
	utils::writeU32(trail + 12, 300);
	utils::writeU32(trail + 16, 200);
    utils::writeU128(trail + 24, prefix.addr);

    packet::Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(ICMPV6_OPCODE_NDP_ROUTE_ADVERTISEMENT);
    ra.setCode(0x00);
    ra.setTrail(trail, 40);

    ndp->receiveRouteAdvertisement(ra, IPV6_SOURCE, mac2);

    std::lock_guard<std::mutex> lock(getIPv6Mutex());
    EXPECT_TRUE(getIPv6s().empty());
}

// Test: RA_InvalidPrefixSizeIgnored
TEST_F(Internal_NdpTest, RA_InvalidPrefixSizeIgnored)
{
    getConfigs().get<config::Ndp::AUTOCONFIG_PREFIX>().set(true);
    clearIPv6s();
    iface->configs.getConfigs().get<config::Interface::MAC_ADDRESS>().set(mac);
	iface->configs.syncMac();

    uint8_t trail[28] = {0};
    trail[8] = ICMPV6_OPTION_NDP_PREFIX;
    trail[9] = 0x04;
    trail[10] = 0x40; // /64
    trail[11] = 0xC0; // L + A
	utils::writeU32(trail + 12, 300);
	utils::writeU32(trail + 16, 200);
	utils::writeU32(trail + 24, 0xFFFFFFFF); // to short - invalid
    
    packet::Icmpv6Header ra;
    ra.setBuffer(buf);
    
    ra.setType(ICMPV6_OPCODE_NDP_ROUTE_ADVERTISEMENT);
    ra.setCode(0x00);
    ra.setTrail(trail, 28);

    ndp->receiveRouteAdvertisement(ra, IPV6_SOURCE, mac2);

    std::lock_guard<std::mutex> lock(getIPv6Mutex());
    EXPECT_TRUE(getIPv6s().empty());
}

// Test: RA_FragmentedPacketIgnored
TEST_F(Internal_NdpTest, RA_FragmentedPacketIgnored)
{
    clearIPv6s();

    uint8_t trail[1] = {0};

    // Assume payload is too short to be valid RA
    packet::Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(ICMPV6_OPCODE_NDP_ROUTE_ADVERTISEMENT);
    ra.setCode(0x00);
    ra.setTrail(trail, 1);

    ndp->receiveRouteAdvertisement(ra, IPV6_SOURCE, mac2);

    std::lock_guard<std::mutex> lock(getIPv6Mutex());
    EXPECT_TRUE(getIPv6s().empty());
}

// Test: Redirect_MulticastDestinationIsIgnored
TEST_F(Internal_NdpTest, Redirect_MulticastDestinationIsIgnored)
{
    packet::PacketInfo pkt;
    pkt.headers[0] = { packet::HeaderType::IPV6, 0 };
    pkt.count = 1;
    pkt.offset = packet::IPv6Header::fixedSize;
    packet::IPv6Header ipHeader;
    ipHeader.setBuffer(buf);
    ipHeader.setSourceAddress(prefix);
    ipHeader.raw->sourceAddress[15] = 'A';
    ipHeader.setDestinationAddress(ip);

    // Should not enqueue
    EXPECT_CALL(*iface, enqueuePacket(testing::_, testing::_)).Times(0);
    ndp->sendRedirectIfNeeded(pkt, buf);
}

// Test: Redirect_IgnoresSelfToSelfTraffic
TEST_F(Internal_NdpTest, Redirect_IgnoresSelfToSelfTraffic)
{
    packet::PacketInfo pkt;
    pkt.headers[0] = { packet::HeaderType::IPV6, 0 };
    pkt.count = 1;
    pkt.offset = packet::IPv6Header::fixedSize;
    packet::IPv6Header ip6;
    ip6.setBuffer(buf);
    ip6.setSourceAddress(ip);
    ip6.setDestinationAddress(ip);

    EXPECT_CALL(*iface, enqueuePacket(testing::_, testing::_)).Times(0);
    ndp->sendRedirectIfNeeded(pkt, buf);
}

// Test: Redirect_CreatesEntryForBetterNextHop
TEST_F(Internal_NdpTest, Redirect_CreatesEntryForBetterNextHop)
{
    uint8_t betterHop[16];
    utils::writeU128(betterHop, ip);
    betterHop[15] = 'A';
    uint8_t destIp[16];
    utils::writeU128(destIp, ip);
    destIp[15] = 'B';

    uint8_t trail[40];
    std::memcpy(trail, betterHop, 16);
    std::memcpy(trail + 16, destIp, 16);
    trail[32] = ICMPV6_OPTION_NDP_TARGET;
    trail[33] = 0x01;
    utils::writeU48(trail + 34, mac);

    packet::Icmpv6Header redirect;
    redirect.setBuffer(buf);

    redirect.setType(ICMPV6_OPCODE_NDP_REDIRECT_MESSAGE);
    redirect.setCode(0x00);
    redirect.setTrail(trail, 40);

    ndp->receiveRedirectMessage(redirect, ip);

    uint8_t resolvedMac[6];
    EXPECT_TRUE(ndp->getMac(resolvedMac, betterHop));
    EXPECT_EQ(types::Mac{utils::readU48(resolvedMac)}, mac);
}

// Test: NA_UnsolicitedRateLimitEnforced
TEST_F(Internal_NdpTest, NA_UnsolicitedRateLimitEnforced)
{
    ndp->addNdpEntry(ip, mac);

    // First send should work
    EXPECT_CALL(*iface, enqueuePacket(testing::_)).Times(1);
    ndp->sendNeighborAdvertisement(utils::readU48(ETHERNET_MAC_BROADCAST), ip);

    // Immediate resend should be skipped
    EXPECT_CALL(*iface, enqueuePacket(testing::_)).Times(0);
    ndp->sendNeighborAdvertisement(utils::readU48(ETHERNET_MAC_BROADCAST), ip);
}

// Test: NS_RetriesStopAfterConfiguredAttempts
TEST_F(Internal_NdpTest, NS_RetriesStopAfterConfiguredAttempts)
{
    getConfigs().get<config::Ndp::NS_INTERVAL>().set(50);

    processing::PacketBuilder dummy(iface);

    EXPECT_CALL(*iface, enqueuePacket(::testing::_)).Times(3);

    ndp->resolveAndSend(ip, dummy);

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    auto entry = getNdpCache().find(ip);
    EXPECT_EQ(entry, getNdpCache().end());
}

// Test: EntryLimitEnforced_EvictsOldest
TEST_F(Internal_NdpTest, EntryLimitEnforced_DropsNewest)
{
    iface->blockEnqueues();
    getConfigs().get<config::Ndp::BASE>().get().get<config::NdpBase::CACHE_INTERFACE_LIMIT>().set(2);

    uint8_t ip1[16];
    utils::writeU128(ip1, ip);
    ip1[15] = 0x01;
    uint8_t ip2[16];
    utils::writeU128(ip2, ip);
    ip2[15] = 0x02;
    uint8_t ip3[16];
    utils::writeU128(ip3, ip);
    ip3[15] = 0x03;

    processing::PacketBuilder packet(iface);

    ndp->resolveAndSend(ip1, packet);
    ndp->resolveAndSend(ip2, packet);

    // This should drop
    ndp->resolveAndSend(ip3, packet);

    EXPECT_TRUE(getNdpCache().contains(ip1));
    EXPECT_TRUE(getNdpCache().contains(ip2));
    EXPECT_FALSE(getNdpCache().contains(ip3));
}

// Test: ManualProxyEntryIsStoredCorrectly
TEST_F(Internal_NdpTest, ManualProxyEntryIsStoredCorrectly)
{
    ndp->addNdpEntry(ip, mac); // Proxy = true

    uint8_t resolvedMac[6];
    EXPECT_TRUE(ndp->getMac(resolvedMac, ip));
    EXPECT_EQ(types::Mac{utils::readU48(resolvedMac)}, mac);

    auto it = getNdpCache().find(types::IPv6Address{ip});
    ASSERT_TRUE(it != getNdpCache().end());
}

// Test: RA_AddsPrefixWithCorrectTimers
TEST_F(Internal_NdpTest, RA_AddsPrefixWithCorrectTimers)
{
    iface->blockEnqueues();
    getConfigs().get<config::Ndp::AUTOCONFIG_PREFIX>().set(true);
    clearIPv6s();
    iface->configs.getConfigs().get<config::Interface::MAC_ADDRESS>().set(mac);
	iface->configs.syncMac();

    uint8_t trail[40];
    trail[8] = ICMPV6_OPTION_NDP_PREFIX;
    trail[9] = 0x04;
    trail[10] = 0x40; // /64
    trail[11] = 0xC0; // L + A bits
	utils::writeU32(trail + 12, 1);
	utils::writeU32(trail + 16, 1);
    utils::writeU128(trail + 24, prefix.addr);

    packet::Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(ICMPV6_OPCODE_NDP_ROUTE_ADVERTISEMENT);
    ra.setCode(0x00);
    ra.setTrail(trail, 40);

    ndp->receiveRouteAdvertisement(ra, IPV6_SOURCE, mac2);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::lock_guard<std::mutex> lock(getIPv6Mutex());
    ASSERT_FALSE(getIPv6s().empty());
    EXPECT_FALSE(getIPv6s()[0]->globalValid);
    EXPECT_TRUE(getIPv6s()[0]->deprecated);
}

// Test: RA_MalformedFieldsAreIgnored
TEST_F(Internal_NdpTest, RA_MalformedFieldsAreIgnored)
{
    uint8_t trail[4] = {0}; // too short - malformed

    packet::Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(ICMPV6_OPCODE_NDP_ROUTE_ADVERTISEMENT);
    ra.setCode(0x00);
    ra.setTrail(trail, 4);

    ndp->receiveRouteAdvertisement(ra, IPV6_SOURCE, mac2);
    SUCCEED(); // Drop w/o crash
}

// Test: NA_WithoutMACOptionIsIgnored
TEST_F(Internal_NdpTest, NA_WithoutMACOptionIsIgnored)
{
    uint8_t res[4] = { 0xE0, 0x00, 0x00, 0x00 };

    packet::Icmpv6Header na;
    na.setBuffer(buf);

    na.setType(ICMPV6_OPCODE_NDP_NEIGHBOR_ADVERTISEMENT);
    na.setCode(0x00);
    na.setReserved(res);
    utils::writeU128(na.getTrailData(), localLinkIp);

    ndp->receiveNeighborAdvertisement(na, ip);

    uint8_t resolvedMac[6];
    EXPECT_FALSE(ndp->getMac(resolvedMac, ip));
}

// Test: NS_UnknownTargetIsIgnored
TEST_F(Internal_NdpTest, NS_UnknownTargetIsIgnored)
{
    uint8_t unknownIp[16];
    utils::writeU128(unknownIp, ip);
    unknownIp[15] = 'A';

    packet::Icmpv6Header ns;
    ns.setBuffer(buf);

    ns.setType(ICMPV6_OPCODE_NDP_NEIGHBOR_SOLICITATION);
    ns.setCode(0x00);
    ns.setTrail(unknownIp, 16);

    // Should not send NA
    EXPECT_CALL(*iface, enqueuePacket(testing::_, testing::_)).Times(0);
    ndp->receiveNeighborSolicitation(ns, ip, mac);
}

// Test: NA_DuringProbe_ResetsToReachable
TEST_F(Internal_NdpTest, NA_DuringProbe_ResetsToReachable)
{
    iface->blockEnqueues();
    getConfigs().get<config::Ndp::BASE>().get().get<config::NdpBase::REACHABLE_TIME>().set(100);
    getConfigs().get<config::Ndp::BASE>().get().get<config::NdpBase::CACHE_EXPIRE>().set(1);

    addNdpEntry(ip, mac);
    processing::PacketBuilder pkt(iface);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ndp->resolveAndSend(ip, pkt);

    auto state = getNdpCache()[ip].state;
    ASSERT_EQ(state, infrastructure::Ndp::NudState::PROBE);

    uint8_t res[4] = { 0xE0, 0x00, 0x00, 0x00 };
    uint8_t trail[24] = {0};
    utils::writeU128(trail, ip);
    trail[16] = ICMPV6_OPTION_NDP_TARGET;
    trail[17] = 0x01;
    utils::writeU48(trail + 18, mac);

    packet::Icmpv6Header na;
    na.setBuffer(buf);
    na.setType(ICMPV6_OPCODE_NDP_NEIGHBOR_ADVERTISEMENT);
    na.setCode(0x00);
    na.setReserved(res);
    na.setTrail(trail, 24);

    ndp->receiveNeighborAdvertisement(na, ip);

    state = getNdpCache()[ip].state;
    EXPECT_EQ(state, infrastructure::Ndp::NudState::REACHABLE);
}

// Test: DAD_And_NS_DoNotCorruptState
TEST_F(Internal_NdpTest, DAD_And_NS_DoNotCorruptState)
{
    iface->blockEnqueues();
    auto* addr = iface->configs.ipv6.addAddress(ip, false);
    addr->tentative = true;
    getConfigs().get<config::Ndp::DAD_ATTEMPTS>().set(1);
    getConfigs().get<config::Ndp::BASE>().get().get<config::NdpBase::DAD_TIME>();

    std::thread dadThread([&]() {
        ndp->duplicateAddressDetection(*addr);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    packet::Icmpv6Header ns;

    ns.setBuffer(buf);
    ns.setType(ICMPV6_OPCODE_NDP_NEIGHBOR_SOLICITATION);
    utils::writeU128(ns.getTrailData(), ip);

    ndp->receiveNeighborSolicitation(ns, IPV6_SOURCE, utils::readU48(ETHERNET_MAC_SOURCE)); // DAD probe

    dadThread.join();

    std::lock_guard<std::mutex> lock(getIPv6Mutex());
    // Either duplicate or accepted — just no crash or invalid state
    SUCCEED();
}

// Test: ConcurrentAccessIsSafe
/*TEST_F(Internal_NdpTest, ConcurrentAccessIsSafe)
{
    std::atomic<bool> finished = false;
    std::thread writer([&]() {
        for (int i = 0; i < 100; ++i) {
            uint8_t ip[16];
            std::memcpy(ip, prefix, 16);
            ip[15] = static_cast<uint8_t>(i);
            uint8_t macAddr[6];
            std::memcpy(macAddr, mac, 6);
            macAddr[5] = static_cast<uint8_t>(i);
            ndp->addNdpEntry({ip, AddressFamily::IPv6}, readU48(mac));
        }
        finished = true;
    });

    std::thread reader([&]() {
        while (!finished) {
            for (int i = 0; i < 100; ++i) {
                uint8_t ip[16];
                std::memcpy(ip, prefix, 16);
                ip[15] = static_cast<uint8_t>(i);
                uint8_t macAddr[6];
                std::memcpy(macAddr, mac, 6);
                macAddr[5] = static_cast<uint8_t>(i);
                uint8_t resolvedMac[16];
                EXPECT_TRUE(ndp->getMac(resolvedMac, ip));
                EXPECT_EQ(std::memcmp(resolvedMac, macAddr, 6), 0);
            }
        }
    });

    writer.join();
    reader.join();

    SUCCEED(); // If no crash, mutexes are safe
}*/

// Test: UnknownICMPv6TypeIsIgnored
TEST_F(Internal_NdpTest, UnknownICMPv6TypeIsIgnored)
{
    uint8_t trail[16] = {0};

    packet::Icmpv6Header hdr;
    hdr.setBuffer(buf);

    hdr.setType(0xFF); // Unknown
    hdr.setCode(0x00);
    hdr.setTrail(trail, 16);

    // Send to all receive functions
    ndp->receiveNeighborAdvertisement(hdr, IPV6_SOURCE);
    ndp->receiveRouteAdvertisement(hdr, IPV6_SOURCE, utils::readU48(ETHERNET_MAC_SOURCE));
    ndp->receiveRedirectMessage(hdr, IPV6_SOURCE);

    SUCCEED(); // Ignored = passed
}

// Test: RA_InconsistentLifetimesAreIgnored
TEST_F(Internal_NdpTest, RA_InconsistentLifetimesAreIgnored)
{
    clearIPv6s();
    iface->blockEnqueues();
    getConfigs().get<config::Ndp::AUTOCONFIG_PREFIX>().set(true);

    uint8_t trail[40] = {0};
    trail[8] = ICMPV6_OPTION_NDP_PREFIX;
    trail[9] = 0x04;
    trail[10] = 0x40; // /64
    trail[11] = 0xC0; // L + A Bits
	utils::writeU32(trail + 12, 2000);
	utils::writeU32(trail + 16, 3000);
    utils::writeU128(trail + 24, prefix.addr);

    packet::Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(ICMPV6_OPCODE_NDP_ROUTE_ADVERTISEMENT);
    ra.setCode(0x00);
    ra.setTrail(trail, 40);

    ndp->receiveRouteAdvertisement(ra, IPV6_SOURCE, utils::readU48(ETHERNET_MAC_SOURCE));

    std::lock_guard<std::mutex> lock(getIPv6Mutex());
    EXPECT_TRUE(getIPv6s().empty());
}

// Test: SLAAC_ExclusionUpdate_AppliesImmediately
TEST_F(Internal_NdpTest, SLAAC_ExclusionUpdate_AppliesImmediately)
{
    clearIPv6s();
    getConfigs().get<config::Ndp::AUTOCONFIG_PREFIX>().set(true);
    iface->configs.getConfigs().get<config::Interface::MAC_ADDRESS>().set(mac);
	iface->configs.syncMac();

    // Add exclusion
    ndp->addSlaacExclusionPrefix(prefix, false);

    uint8_t trail[40];
    trail[8] = ICMPV6_OPTION_NDP_PREFIX;
    trail[9] = 0x04;
    trail[10] = 0x40; // /64
    trail[11] = 0xC0; // L + A Bits
	utils::writeU32(trail + 12, 1000);
	utils::writeU32(trail + 16, 800);
    utils::writeU128(trail + 24, prefix.addr);

    packet::Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(ICMPV6_OPCODE_NDP_ROUTE_ADVERTISEMENT);
    ra.setCode(0x00);
    ra.setTrail(trail, 40);
    ndp->receiveRouteAdvertisement(ra, IPV6_SOURCE, mac2);

    std::lock_guard<std::mutex> lock(getIPv6Mutex());
    EXPECT_TRUE(getIPv6s().empty());
}

// Test: Config_ReachableTimeAffectsNewEntries
TEST_F(Internal_NdpTest, Config_ReachableTimeAffectsNewEntries)
{
    getConfigs().get<config::Ndp::BASE>().get().get<config::NdpBase::REACHABLE_TIME>().set(50);
    getConfigs().get<config::Ndp::BASE>().get().get<config::NdpBase::CACHE_EXPIRE>().set(1);
    addNdpEntry(ip, mac);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto state = getNdpCache()[ip].state;
    EXPECT_EQ(state, infrastructure::Ndp::NudState::STALE);
}

// Test: Config_CacheExpireAffectsNewEntries
TEST_F(Internal_NdpTest, Config_CacheExpireAffectsNewEntries)
{
    iface->blockEnqueues();
    getConfigs().get<config::Ndp::BASE>().get().get<config::NdpBase::REACHABLE_TIME>().set(10);
    getConfigs().get<config::Ndp::BASE>().get().get<config::NdpBase::CACHE_EXPIRE>().set(1);
    addNdpEntry(ip, mac);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    uint8_t resolvedMac[6];
    EXPECT_FALSE(ndp->getMac(resolvedMac, ip));
}

// Test: Config_DadAttemptsUpdateImmediately
TEST_F(Internal_NdpTest, Config_DadAttemptsUpdateImmediately)
{
    iface->blockEnqueues();
    auto* addr = iface->configs.ipv6.addAddress(ip, false);
    addr->tentative = true;

    getConfigs().get<config::Ndp::DAD_ATTEMPTS>().set(1);
    getConfigs().get<config::Ndp::BASE>().get().get<config::NdpBase::DAD_TIME>().set(100);
    ndp->duplicateAddressDetection(*addr);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::lock_guard<std::mutex> lock(getIPv6Mutex());
    EXPECT_FALSE(addr->tentative);
    EXPECT_TRUE(addr->valid);
}

// Test: Config_PreferedLifetimeUpdatesWithRA
TEST_F(Internal_NdpTest, Config_PreferredLifetimeUpdatesWithRA)
{
    clearIPv6s();
    iface->blockEnqueues();
    getConfigs().get<config::Ndp::AUTOCONFIG_PREFIX>().set(true);
    iface->configs.getConfigs().get<config::Interface::MAC_ADDRESS>().set(mac);
    iface->configs.syncMac();

    getConfigs().get<config::Ndp::RA_LIFETIME>().set(100);

    uint8_t trail[40];
    trail[8] = ICMPV6_OPTION_NDP_PREFIX;
    trail[9] = 0x04;
    trail[10] = 0x40; // /64
    trail[11] = 0xC0; // L + A Bits
    utils::writeU32(trail + 12, 100);
    utils::writeU32(trail + 16, 1);
    utils::writeU128(trail + 24, prefix.addr);

    packet::Icmpv6Header ra;
    ra.setBuffer(buf);

    ra.setType(ICMPV6_OPCODE_NDP_ROUTE_ADVERTISEMENT);
    ra.setCode(0x00);
    ra.setTrail(trail, 40);

    ndp->receiveRouteAdvertisement(ra, IPV6_SOURCE, mac2);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::lock_guard<std::mutex> lock(getIPv6Mutex());
    EXPECT_TRUE(getIPv6s()[0]->deprecated);
}

// Test: NSF ResolutionTrottle_DropsExcess
/*
TEST_F(Internal_NdpTest, NSF_ResolutionThrottle_DropsExcess)
{
    iface->blockEnqueues();
    global->configs.nsfActive.store(true);
    global->configs.nsfStartTime = std::chrono::steady_clock::now();
    global->configs.ndp.nsfConvergenceTime.store(500); // 500s
    global->configs.ndp.nsfThrottleResolutions.store(1);

    PacketBuilder pkt1(iface), pkt2(iface);
    ndp->resolveAndSend(ip, pkt1);

    uint8_t newIp[16];
    std::memcpy(newIp, ip, 16);
    newIp[15] = '2';
    ndp->resolveAndSend(newIp, pkt2);
    
    std::lock_guard<std::mutex> lock(getRequestMutex());
    EXPECT_EQ(getPendingRequests().count({ip, AddressFamily::IPv6}), 1);
    EXPECT_EQ(getPendingRequests().count({newIp, AddressFamily::IPv6}), 0);
}

// Test: DAD_SuppressedDuringNSF
TEST_F(Internal_NdpTest, DAD_SuppressedDuringNSF)
{
    global->configs.nsfActive.store(true);
    global->configs.nsfStartTime = std::chrono::steady_clock::now();
    global->configs.ndp.nsfDadSupressionTime.store(500); // 500s

    iface->blockEnqueues();
    auto* addr = iface->configs.ipv6.addAddress(ip, false, 64);
    addr->tentative = true;
    ndp->configs.dadAttempts.store(1);

    ndp->duplicateAddressDetection(addr, false);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::lock_guard<std::mutex> lock(getIPv6Mutex());
    EXPECT_TRUE(addr->tentative); // Suppressed
}
*/

// Test: StaticNeighbor_OverridesDynamicResolution
TEST_F(Internal_NdpTest, StaticNeighbor_OverridesDynamicResolution)
{
    iface->blockEnqueues();
    uint8_t staticIp[16];
    utils::writeU128(staticIp, ip);
    staticIp[15] = '5';
    uint8_t staticMac[6];
    utils::writeU48(staticMac, mac);
    staticMac[5] = '5';

    ndp->addNdpEntry(utils::readU128(staticIp), utils::readU48(staticMac));

    processing::PacketBuilder pkt(iface);
    ndp->resolveAndSend(staticIp, pkt);

    uint8_t resolvedMac[6];
    EXPECT_TRUE(ndp->getMac(resolvedMac, staticIp));
    EXPECT_EQ(std::memcmp(resolvedMac, staticMac, 6), 0);
}

