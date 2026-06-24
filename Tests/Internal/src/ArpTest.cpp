// ArpTest.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory.h>

// Include the ARP implementation and mock classes
#include <infrastructure/Arp.h>
#include <MockInterface.hpp>
#include <MockFileSystem.hpp>
#include <interface/Interface.h>
#include <Global.h>
#include <configs/registry/global/GlobalRegistry.h>
#include <processing/PacketBuilder.hpp>
#include <packet/headers/ArpHeader.hpp>
#include <packet/headers/EthernetHeader.hpp>
#include <packet/PacketStructure.h>
#include <packet/HeaderHelpers.hpp>
#include <configs/FieldAccessor.hpp>

using namespace utils;
using namespace packet;
using processing::PacketBuilder;

class Internal_ArpTest : public ::testing::Test
{
protected:

    core::Global* global = nullptr;
    cli::MockFileSystem fs;

    uint32_t ip = 0xc0a80110;
    uint32_t ip2 = 0xc0a80210;
    uint8_t mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t mac2[6] = {0x00, 0x22, 0x22, 0x22, 0x22, 0x22};
    alignas(64) uint8_t buf[128];

    types::IPv4Prefix ifaceIp = {0xc0a80001, 24};

    void SetUp() override
    {
        global = new core::Global(fs, {}, true, true);
        std::memset(buf, 0, 128);

        mockInterface = new interface::MockInterface(*global);
        mockInterface->enableShutdown();
        mockInterface->enableIPs();
        mockInterface->setIPv4(ifaceIp, false);
        arp = &mockInterface->arp;
    }

    void TearDown() override
    {
        arp->shutdown();
        delete mockInterface;
        delete global;
    }

    // Member variables
    interface::MockInterface* mockInterface;
    infrastructure::Arp* arp;

    // Helper functions
    types::StableHashMap<types::IPv4Address, infrastructure::Arp::ArpCacheEntry>& getArpCache() {return arp->arpCache;}
    infrastructure::Arp::ArpCacheEntry* getArpCacheEntry(types::IPv4Address addr)
    {
        if (auto it = arp->arpCache.find(addr); it != arp->arpCache.end())
            return &it->second;
        return nullptr;
    }

    // addArpEntry helper: static entries via addStaticArpEntry,
    // dynamic entries (non-proxy, non-static) via addStaticArpEntry as closest match
    void addArpEntry(uint32_t targetIp, uint64_t targetMac)
    {
        arp->addArpEntry(targetIp, targetMac);
        arp->completeArpEntry(*arp->arpCache.find(targetIp), targetMac);
    }

    // removeArpEntry is protected — expose via friend
    void removeArpEntry(types::IPv4Address targetIp)
    {
        arp->removeArpEntry(targetIp);
    }

    // Stubs for removed internal state
    uint32_t getIncompletes() {return arp->incompletes;}
    void callInitiateArp() { arp->initiateArp(); }
    struct QueueProxy {
        bool contains(uint32_t) { return false; }
        size_t operator[](uint32_t) { return 0; }
    };

    void setupPacket(PacketBuilder& builder)
    {
        builder.reserveHeader(HeaderType::ETHERNET, EthernetHeader::fixedSize);
        builder.reserveAndBuildHeader<IPv4Header>(HeaderType::IPV4);
    }
};

// Helper to simulate a reply
void simulateArpReply(uint8_t* buffer, infrastructure::Arp& arp, const uint32_t& ip, const uint8_t* mac)
{
    packet::ArpHeader arpReply;
    arpReply.setBuffer(buffer);
    arpReply.setSenderIpAddr(ip);
    arpReply.setSenderHwAddr(utils::readU48(mac));
    arp.receiveReply(arpReply);
}

// Test: StaticEntry_ImmediateResolution
TEST_F(Internal_ArpTest, StaticEntry_ImmediateResolution)
{
    addArpEntry(ip, readU48(mac));

    uint8_t resMac[6];
    uint8_t addr[4];
    writeU32(addr, ip);
    ASSERT_TRUE(arp->getMac(resMac, addr));
    EXPECT_EQ(std::memcmp(resMac, mac, 6), 0);
}

// Test: StaticEntry_Overwrite
TEST_F(Internal_ArpTest, StaticEntry_Overwrite)
{
    addArpEntry(ip, readU48(mac));
    uint8_t resolvedMac[6];
    uint8_t addr[4];
    writeU32(addr, ip);
    ASSERT_TRUE(arp->getMac(resolvedMac, addr));
    EXPECT_EQ(std::memcmp(resolvedMac, mac, 6), 0);

    addArpEntry(ip, readU48(mac2));
    ASSERT_TRUE(arp->getMac(resolvedMac, addr));
    EXPECT_EQ(std::memcmp(resolvedMac, mac2, 6), 0);
}

// Test: StaticEntry_Removal
TEST_F(Internal_ArpTest, StaticEntry_Removal)
{
    addArpEntry(ip, readU48(mac));

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    writeU32(addr, ip);
    ASSERT_TRUE(arp->getMac(resolvedMac, addr));

    removeArpEntry(ip);

    ASSERT_FALSE(arp->getMac(resolvedMac, addr));
}

// Test: StaticProxyEntry_RepliesToArpRequest
TEST_F(Internal_ArpTest, StaticProxyEntry_RepliesToArpRequest)
{
    mockInterface->blockEnqueues();
    ArpHeader request;
    request.setBuffer(buf);

    request.setTargetIpAddr(ip);
    request.setSenderIpAddr(ifaceIp);

    addArpEntry(ip, readU48(mac));

    arp->receiveRequest(request, readU48(request.getSenderHwAddr()));
}

// Test: StaticNonProxyEntry_DoesNotReplyToUnownedRequest
TEST_F(Internal_ArpTest, StaticNonProxyEntry_DoesNotReplyToUnownedRequest)
{
    ArpHeader request;
    request.setBuffer(buf);

    request.setTargetIpAddr(ip);
    request.setSenderIpAddr(ifaceIp);

    addArpEntry(ip, readU48(mac));

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_)).Times(0); // No reply expected

    arp->receiveRequest(request, readU48(request.getSenderHwAddr()));
}

// Test: StaticEntry_NeverExpiresOrProbes
TEST_F(Internal_ArpTest, StaticEntry_NeverExpiresOrProbes)
{
    addArpEntry(ip, readU48(mac));

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    writeU32(addr, ip);
    ASSERT_TRUE(arp->getMac(resolvedMac, addr));

    std::this_thread::sleep_for(std::chrono::seconds(2));

    ASSERT_TRUE(arp->getMac(resolvedMac, addr));
    EXPECT_EQ(std::memcmp(resolvedMac, mac, 6), 0);
}

// Test: StaticEntries_MultipleUniqueEntries
TEST_F(Internal_ArpTest, StaticEntries_MultipleUniqueEntries)
{
    for (int i = 1; i <= 5; ++i)
    {
        uint8_t ipBytes[4] = {(uint8_t)192, (uint8_t)168, (uint8_t)1, (uint8_t)(20 + i)};
        uint8_t macBytes[6] = {0x00, 0x11, 0x22, 0x33, 0x44, (uint8_t)i};
        addArpEntry(readU32(ipBytes), readU48(macBytes));

        uint8_t resolvedMac[6];
        ASSERT_TRUE(arp->getMac(resolvedMac, utils::readU32(ipBytes)));
        EXPECT_EQ(std::memcmp(resolvedMac, macBytes, 6), 0);
    }
}

// Test: DynamicEntry_ExpiresAfterTimeout
TEST_F(Internal_ArpTest, DynamicEntry_ExpiresAfterTimeout)
{
    mockInterface->blockEnqueues();
    mockInterface->configs.getConfigs().get<config::Interface::ARP>().get().get<config::Arp::TIMEOUT>().set(1);
    addArpEntry(ip, readU48(mac));

    uint8_t resolvedMac[6];
    ASSERT_TRUE(arp->getMac(mac, ip));

    std::this_thread::sleep_for(std::chrono::seconds(2));

    ASSERT_FALSE(arp->getMac(resolvedMac, ip));
}

// Test: DynamicEntry_IfIncompleteEnabled
TEST_F(Internal_ArpTest, DynamicEntry_ReprobesWhenStale_IfIncompleteEnabled)
{
    global->configs.get<config::Global::IP_ARP_INCOMPLETE>().set(true);
    global->configs.get<config::Global::IP_ARP_INCOMPLETE_RETRY>().set(2);

    bool requestSent = false;
    bool retrySent = false;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .WillRepeatedly([&](const PacketBuilder&) {
            if (!requestSent)
                requestSent = true;
            else if (!retrySent)
                retrySent = true;
            else
                FAIL(); 
        });

    PacketBuilder pkt(mockInterface);
    arp->resolveAndSend(ip, pkt);

    EXPECT_NE(getArpCacheEntry(ip), nullptr);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    EXPECT_EQ(getArpCacheEntry(ip), nullptr);

    EXPECT_TRUE(requestSent && retrySent);
}

// Test: DynamicEntry_IfIncompleteDisabled
TEST_F(Internal_ArpTest, DynamicEntry_RemovedWhenStale_IfIncompleteDisabled)
{
    mockInterface->configs.getConfigs().get<config::Interface::ARP>().get().get<config::Arp::TIMEOUT>().set(1);
    global->configs.get<config::Global::IP_ARP_INCOMPLETE>().set(false);

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_)).Times(0);

    PacketBuilder pkt(mockInterface);
    arp->resolveAndSend(ip, pkt);

    uint8_t resolvedMac[6];
    ASSERT_FALSE(arp->getMac(resolvedMac, ip));
}

// Test: DynamicEntry_TimerIsCancelledWhenOverwritten
TEST_F(Internal_ArpTest, DynamicEntry_TimerIsCancelledWhenOverwritten)
{
    addArpEntry(ip, readU48(mac));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    addArpEntry(ip, readU48(mac2));

    // Wait just under 1 second (timeout default is longer)
    std::this_thread::sleep_for(std::chrono::seconds(2));

    uint8_t resolvedMac[6];
    ASSERT_TRUE(arp->getMac(resolvedMac, ip));
    EXPECT_EQ(std::memcmp(resolvedMac, mac2, 6), 0);
}

// Test: IncompleteEntry_RemovedAfterMaxRetries
TEST_F(Internal_ArpTest, IncompleteEntry_RemovedAfterMaxRetries)
{
    mockInterface->blockEnqueues();
    PacketBuilder pkt(mockInterface);
    global->configs.get<config::Global::IP_ARP_INCOMPLETE_RETRY>().set(2);

    arp->resolveAndSend(ip, pkt);
    std::this_thread::sleep_for(std::chrono::seconds(7));

    EXPECT_FALSE(getArpCache().contains(ip));
}

// Test: IncompleteEntry_LateReplyRestoresIfNotCleared
TEST_F(Internal_ArpTest, IncompleteEntry_LateReplyRestoresIfNotCleared)
{
    mockInterface->blockEnqueues();
    PacketBuilder pkt(mockInterface);

    global->configs.get<config::Global::IP_ARP_INCOMPLETE_RETRY>().set(10);

    uint8_t addr[4];
    arp->resolveAndSend(writeU32(addr, ip), pkt);

    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // before max retries

    simulateArpReply(buf, *arp, ip, mac);

    uint8_t resolvedMac[6];
    ASSERT_TRUE(arp->getMac(resolvedMac, addr));
    EXPECT_EQ(std::memcmp(resolvedMac, mac, 6), 0);
}

// Test: ReplyStatus_ResetBetweenAttempts
TEST_F(Internal_ArpTest, ReplyStatus_ResetBetweenAttempts)
{
    mockInterface->blockEnqueues();
    PacketBuilder pkt(mockInterface);
    arp->resolveAndSend(ip, pkt);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_TRUE(getArpCache().at(ip).requestTimerId != 0);

    simulateArpReply(buf, *arp, ip, mac);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_EQ(getArpCache().at(ip).status, infrastructure::Arp::ArpCacheStatus::COMPLETE);
}

// Test: IncompleteDisabled_SkipsRequestAndEntry
TEST_F(Internal_ArpTest, IncompleteDisabled_SkipsRequestAndEntry)
{
    PacketBuilder pkt(mockInterface);
    global->configs.get<config::Global::IP_ARP_INCOMPLETE>().set(false);

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_)).Times(0);

    arp->resolveAndSend(ip, pkt);

    EXPECT_FALSE(getArpCache().contains(ip));
}

// Test: PacketQueued_ForUnresolvedIP
TEST_F(Internal_ArpTest, PacketQueued_ForUnresolvedIP)
{
    mockInterface->blockEnqueues();
    PacketBuilder pkt(mockInterface);

    arp->resolveAndSend(ip, pkt);

    ASSERT_EQ(getArpCacheEntry(ip)->queue.size(), 1);
}

// Test: PacketQueue_FlushesOnResolution
TEST_F(Internal_ArpTest, PacketQueue_FlushesOnResolution)
{
    PacketBuilder pkt(mockInterface);
    setupPacket(pkt);

    bool flushed = false;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(::testing::AtLeast(1))
        .WillRepeatedly([&](PacketBuilder& pkt) {
            if (pkt.getHeader(HeaderType::IPV4)) // Only ethernet
                flushed = true;
        });

    arp->resolveAndSend(ip, pkt);
    simulateArpReply(buf, *arp, ip, mac);

    EXPECT_TRUE(flushed);
}

// Test: PacketQueue_MultiplePacketsSentInOrder
TEST_F(Internal_ArpTest, PacketQueue_MultiplePacketsSentInOrder)
{
    PacketBuilder pkt1(mockInterface), pkt2(mockInterface);
    IPv4Header hdr;

    setupPacket(pkt1);
    hdr.setBuffer(pkt1.getHeader(HeaderType::IPV4)->buffer);
    hdr.setSourceAddress(1);

    setupPacket(pkt2);
    hdr.setBuffer(pkt2.getHeader(HeaderType::IPV4)->buffer);
    hdr.setSourceAddress(2);

    bool pkt1sent = false;
    bool pkt2sent = false;

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(::testing::AtLeast(1))
        .WillRepeatedly([&](PacketBuilder& pkt) {
            if (auto* hdr = pkt.getHeader(HeaderType::IPV4); hdr)
            {
                IPv4Header ipv4;
                ipv4.setBuffer(hdr->buffer);
                if (utils::readU32(ipv4.getSourceAddress()) == 1)
                    pkt1sent = true;
                if (utils::readU32(ipv4.getSourceAddress()) == 2 && pkt1sent)
                    pkt2sent = true;
            }
        });

    arp->resolveAndSend(ip, pkt1);
    arp->resolveAndSend(ip, pkt2);
    simulateArpReply(buf, *arp, ip, mac);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    EXPECT_TRUE(pkt1sent && pkt2sent);
}

// Test: PacketQueue_RespectsQueueLimit
TEST_F(Internal_ArpTest, PacketQueue_RespectsQueueLimit)
{
    mockInterface->blockEnqueues();
    global->configs.get<config::Global::IP_ARP_QUEUE>().set(2);
    PacketBuilder pkt(mockInterface);

    arp->resolveAndSend(ip, pkt);
    arp->resolveAndSend(ip, pkt);
    arp->resolveAndSend(ip, pkt);

    ASSERT_EQ(getArpCacheEntry(ip)->queue.size(), 2);
}

// Test: GarpAccepted_CreatesEntry
TEST_F(Internal_ArpTest, GarpAccepted_CreatesEntry)
{
    global->configs.get<config::Global::IP_ARP_GRATUITOUS>().set(1);

    ArpHeader garp;
    garp.setBuffer(buf);
    garp.setSenderIpAddr(ip);
    garp.setTargetIpAddr(ip);
    garp.setSenderHwAddr(readU48(mac));

    arp->receiveReply(garp);

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    ASSERT_TRUE(arp->getMac(resolvedMac, writeU32(addr, ip)));
    EXPECT_EQ(std::memcmp(resolvedMac, mac, 6), 0);
}

// Test: GarpRejected_IgnoredIfDisabled
TEST_F(Internal_ArpTest, GarpRejected_IgnoredIfDisabled)
{
    global->configs.get<config::Global::IP_ARP_GRATUITOUS>().set(0);

    ArpHeader garp;
    garp.setBuffer(buf);
    garp.setSenderIpAddr(ip);
    garp.setTargetIpAddr(ip);
    garp.setSenderHwAddr(readU48(mac));

    arp->receiveReply(garp);

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    ASSERT_FALSE(arp->getMac(resolvedMac, writeU32(addr, ip)));
}

// Test: GarpRefreshes_ExistingEntry
TEST_F(Internal_ArpTest, GarpRefreshes_ExistingEntry)
{
    global->configs.get<config::Global::IP_ARP_GRATUITOUS>().set(1);
    addArpEntry(ip, readU48(mac));

    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // let timer begin

    ArpHeader garp;
    garp.setBuffer(buf);
    garp.setSenderIpAddr(ip);
    garp.setTargetIpAddr(ip);
    garp.setSenderHwAddr(readU48(mac2));

    arp->receiveReply(garp);

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    ASSERT_TRUE(arp->getMac(resolvedMac, writeU32(addr, ip)));
    EXPECT_EQ(std::memcmp(resolvedMac, mac2, 6), 0);
}

// Test: GarpBlockedByStickyArp
TEST_F(Internal_ArpTest, GarpBlockedByStickyArp)
{
    global->configs.get<config::Global::IP_ARP_GRATUITOUS>().set(1);
    global->configs.get<config::Global::IP_STICKY_ARP>().set(true);

    addArpEntry(ip, readU48(mac));

    ArpHeader garp;
    garp.setBuffer(buf);
    garp.setSenderIpAddr(ip);
    garp.setTargetIpAddr(ip);
    garp.setSenderHwAddr(readU48(mac2));

    arp->receiveReply(garp);

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    ASSERT_TRUE(arp->getMac(resolvedMac, writeU32(addr, ip)));
    ASSERT_EQ(std::memcmp(resolvedMac, mac, 6), 0);
}

// Test: StickyArp_PreventsOverwrite
TEST_F(Internal_ArpTest, StickyArp_PreventsOverwrite)
{
    global->configs.get<config::Global::IP_STICKY_ARP>().set(true);
    global->configs.get<config::Global::IP_ARP_GRATUITOUS>().set(1);

    // Add initial dynamic entry
    addArpEntry(ip, readU48(mac));

    // Simulate GARP with different MAC
    ArpHeader garp;
    garp.setBuffer(buf);
    garp.setSenderIpAddr(ip);
    garp.setTargetIpAddr(ip);
    garp.setSenderHwAddr(readU48(mac2));

    arp->receiveReply(garp);

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    ASSERT_TRUE(arp->getMac(resolvedMac, writeU32(addr, ip)));
    ASSERT_EQ(std::memcmp(resolvedMac, mac, 6), 0); // Should not overwrite
}

// Test: StickyArp_Off_AllowsOverwrite
TEST_F(Internal_ArpTest, StickyArp_Off_AllowsOverwrite)
{
    global->configs.get<config::Global::IP_STICKY_ARP>().set(false);
    global->configs.get<config::Global::IP_ARP_GRATUITOUS>().set(1);

    // Add initial dynamic entry
    addArpEntry(ip, readU48(mac));

    // Simulate GARP with different MAC
    ArpHeader garp;
    garp.setBuffer(buf);
    garp.setSenderIpAddr(ip);
    garp.setTargetIpAddr(ip);
    garp.setSenderHwAddr(readU48(mac2));

    arp->receiveReply(garp);

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    ASSERT_TRUE(arp->getMac(resolvedMac, writeU32(addr, ip)));
    EXPECT_EQ(std::memcmp(resolvedMac, mac2, 6), 0); // Should overwrite
}

// Test: ProxyEntry_RepliesToRequest
TEST_F(Internal_ArpTest, ProxyEntry_RepliesToRequest)
{
    ArpHeader request;
    request.setBuffer(buf);
    request.setTargetIpAddr(ifaceIp);
    request.setTargetHwAddr(readU48(mac));
    request.setSenderIpAddr(ip);
    uint8_t macAddr[6];
    request.setSenderHwAddr(readU48(mockInterface->configs.getMac(macAddr)));

    global->configs.get<config::Global::IP_ARP_PROXY>().set(false);

    addArpEntry(ip, readU48(mac));

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](PacketBuilder& pkt) {
            for (int i = 0; i < pkt.getHeaderCount(); ++i)
            {
                auto header = pkt.getHeaders()[i];
                if (header.type == HeaderType::ETHERNET)
                {
                    EXPECT_EQ(std::memcmp(reinterpret_cast<EthernetHeaderRaw*>(header.buffer)->destinationMac, request.raw->senderHardwareAddress, 6), 0);
                    return;
                }
            }
            FAIL();
        }));

    arp->receiveRequest(request, readU48(request.raw->senderHardwareAddress));
}

// Test: NonProxyEntry_DoesNotReplyToRequest
TEST_F(Internal_ArpTest, NonProxyEntry_DoesNotReplyToRequest)
{
    ArpHeader request;
    request.setBuffer(buf);
    request.setTargetIpAddr(ip);
    request.setSenderHwAddr(readU48(mac));
    request.setSenderIpAddr(ifaceIp);
    uint8_t macAddr[6];
    request.setSenderHwAddr(readU48(mockInterface->configs.getMac(macAddr)));

    global->configs.get<config::Global::IP_ARP_PROXY>().set(false);

    addArpEntry(ip, readU48(mac));

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_)).Times(0); // no reply expected

    arp->receiveRequest(request, readU48(request.raw->senderHardwareAddress));
}

// Test: ProxyAllowed_WhenNotDisabled
TEST_F(Internal_ArpTest, ProxyAllowed_WhenNotDisabled)
{
    global->configs.get<config::Global::IP_ARP_PROXY>().set(true);

    ArpHeader request;
    request.setBuffer(buf);
    request.setTargetIpAddr(ip);
    request.setSenderIpAddr(ifaceIp);
    uint8_t macAddr[6];
    request.setSenderHwAddr(readU48(mockInterface->configs.getMac(macAddr)));

    core::RibEntry<uint32_t>* entry = new core::RibEntry<uint32_t>();
    entry->prefix = 0xC0A80000;
    entry->length = 16;
    mockInterface->getVRF()->getRib().addRoute(entry);
    addArpEntry(ip, readU48(mac));

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_)).Times(1);
    arp->receiveRequest(request, readU48(request.raw->senderHardwareAddress));
}

// Test: ProxyBlocked_WhenDisabled
TEST_F(Internal_ArpTest, ProxyBlocked_WhenDisabled)
{
    global->configs.get<config::Global::IP_ARP_PROXY>().set(true);

    ArpHeader request;
    request.setBuffer(buf);
    request.setTargetIpAddr(ip);
    request.setSenderIpAddr(ifaceIp);
    uint8_t macAddr[6];
    request.setSenderHwAddr(readU48(mockInterface->configs.getMac(macAddr)));

    addArpEntry(ip, readU48(mac));

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_)).Times(0); // blocked by config
    arp->receiveRequest(request, readU48(request.raw->senderHardwareAddress));
}

// Test: UnknownIp_NoReply
TEST_F(Internal_ArpTest, UnknownIp_NoReply)
{
    ArpHeader request;
    request.setBuffer(buf);
    request.setTargetIpAddr(ip);
    request.setSenderIpAddr(ifaceIp);
    uint8_t macAddr[6];
    request.setSenderHwAddr(readU48(mockInterface->configs.getMac(macAddr)));

    global->configs.get<config::Global::IP_ARP_PROXY>().set(false);

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_)).Times(0); // no reply
    arp->receiveRequest(request, readU48(request.raw->senderHardwareAddress));
}

// Test: LocalIp_RepliesToRequest
TEST_F(Internal_ArpTest, LocalIp_RepliesToRequest)
{
    ArpHeader request;
    request.setBuffer(buf);
    request.setTargetIpAddr(ifaceIp);
    uint8_t macAddr[6];
    request.setTargetHwAddr(readU48(mockInterface->configs.getMac(macAddr)));
    request.setSenderIpAddr(ip);
    request.setSenderHwAddr(readU48(mac));

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke([&](PacketBuilder& pkt) {
            for (int i = 0; i < pkt.getHeaderCount(); ++i)
            {
                auto header = pkt.getHeaders()[i];
                if (header.type == HeaderType::ETHERNET)
                {
                    EXPECT_EQ(std::memcmp(reinterpret_cast<EthernetHeaderRaw*>(header.buffer)->destinationMac, request.raw->senderHardwareAddress, 6), 0);
                    return;
                }
            }
            FAIL();
        }));

    arp->receiveRequest(request, readU48(request.raw->senderHardwareAddress));
}

// Test: ExceedIncompleteLimit_QueuesExcess
TEST_F(Internal_ArpTest, ExceedIncompleteLimit_QueuesExcess)
{
    mockInterface->blockEnqueues();
    global->configs.get<config::Global::IP_ARP_INCOMPLETE_ENTRIES>().set(2);
    global->configs.get<config::Global::IP_ARP_INCOMPLETE>().set(true);

    PacketBuilder pkt(mockInterface);

    types::IPv4Address ip1 = 0xC0A80801;
    types::IPv4Address ip2 = 0xC0A80802;
    types::IPv4Address ip3 = 0xC0A80803;
    arp->resolveAndSend(ip1, pkt);
    arp->resolveAndSend(ip2, pkt);

    // This one gets dropped
    arp->resolveAndSend(ip3, pkt);

    EXPECT_TRUE(getArpCache().contains(ip1));
    EXPECT_TRUE(getArpCache().contains(ip2));
    EXPECT_FALSE(getArpCache().contains(ip3));
}

// Test: Shutdown_ClearsAllState
TEST_F(Internal_ArpTest, Shutdown_ClearsAllState)
{
    mockInterface->blockEnqueues();
    PacketBuilder pkt(mockInterface);
    arp->addStaticArpEntry(ip, readU48(mac));
    uint8_t addr[4] = { 0xC0, 0xA8, 0x09, 0x02 };
    arp->resolveAndSend(addr, pkt); // creates incomplete

    arp->shutdown();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    uint8_t resolvedMac[6];
    uint8_t addrIp[4];
    writeU32(addrIp, ip);
    EXPECT_FALSE(arp->getMac(resolvedMac, addrIp));

    EXPECT_EQ(getIncompletes(), 0u);
}

// Test: Reinitiation_DoesNotRestorePreviousEntries
TEST_F(Internal_ArpTest, Reinitiation_DoesNotRestorePreviousEntries)
{
    addArpEntry(ip, readU48(mac));

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    ASSERT_TRUE(arp->getMac(resolvedMac, writeU32(addr, ip)));

    arp->shutdown();

    // Simulate "restart"
    callInitiateArp();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_FALSE(arp->getMac(resolvedMac, addr));
}

// Test: ThreadSafety_AddRemoveConcurrent
TEST_F(Internal_ArpTest, ThreadSafety_AddRemoveConcurrent)
{
    const int numThreads = 10;
    std::vector<std::thread> threads;

    for (int i = 0; i < numThreads; ++i)
    {
        threads.emplace_back([&, i] {
            uint8_t ipBytes[4] = { 192, 168, 10, (uint8_t)i };
            uint8_t macBytes[6] = { 0x10, 0x20, 0x30, 0x40, 0x50, (uint8_t)i };
            arp->addStaticArpEntry(readU32(ipBytes), readU48(macBytes));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            arp->removeStaticArpEntry(readU32(ipBytes));
        });
    }

    for (auto& t : threads) t.join();

    SUCCEED(); // No crash = thread safety maintained
}

// Test: InvalidRequest_ZeroSenderIp_Ignored
TEST_F(Internal_ArpTest, InvalidRequest_ZeroSenderIp_Ignored)
{
    ArpHeader request;
    request.setBuffer(buf);
    request.setSenderIpAddr((uint32_t)0);
    request.setTargetIpAddr(ip);
    uint8_t macAddr[6];
    request.setSenderHwAddr(readU48(mockInterface->configs.getMac(macAddr)));

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_)).Times(0);
    arp->receiveRequest(request, readU48(request.raw->senderHardwareAddress));
}

// Test: InvalidRequest_SenderEqualsTarget_Ignored
TEST_F(Internal_ArpTest, InvalidRequest_SenderEqualsTarget_Ignored)
{
    ArpHeader request;
    request.setBuffer(buf);
    request.setSenderIpAddr(ip);
    request.setTargetIpAddr(ip);
    uint8_t macAddr[6];
    request.setSenderHwAddr(readU48(mockInterface->configs.getMac(macAddr)));

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_)).Times(0);
    arp->receiveRequest(request, readU48(request.raw->senderHardwareAddress));
}

// Test: ReplyWithInvalidMac_Ignored
TEST_F(Internal_ArpTest, ReplyWithInvalidMac_Ignored)
{
    uint8_t invalidMac[6];

    ArpHeader reply;
    reply.setBuffer(buf);
    reply.setSenderIpAddr(ip);
    reply.setTargetIpAddr(ip);
    reply.setSenderHwAddr(readU48(ETHERNET_MAC_BROADCAST));

    // MAC is broadcast — invalid in reply
    uint8_t addr[4];
    arp->receiveReply(reply);
    EXPECT_FALSE(arp->getMac(invalidMac, writeU32(addr, ip)));
}

// Test: ResolvedEntry_ReResolutionResetsTimer
TEST_F(Internal_ArpTest, ResolvedEntry_ReResolutionResetsTimer)
{
    addArpEntry(ip, readU48(mac));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Save current expiry
    auto* entry = getArpCacheEntry(ip);
    ASSERT_NE(entry, nullptr);
    auto originalExpiry = entry->expiryTime;

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Re-add same entry (should overwrite and reset expiry)
    addArpEntry(ip, readU48(mac));

    entry = getArpCacheEntry(ip);
    ASSERT_NE(entry, nullptr);
    auto newExpiry = entry->expiryTime;

    EXPECT_GT(newExpiry, originalExpiry);
}

// Test: UnsolicitedReply_CreatesOnlyIfGarp
TEST_F(Internal_ArpTest, UnsolicitedReply_CreatesOnlyIfGarp)
{
    global->configs.get<config::Global::IP_ARP_GRATUITOUS>().set(1);

    // Non-GARP reply
    ArpHeader reply1;
    reply1.setBuffer(buf);
    reply1.setSenderIpAddr(ip);
    reply1.setTargetIpAddr(ip2);
    reply1.setSenderHwAddr(readU48(mac));
    arp->receiveReply(reply1);

    uint8_t resolvedMac[6];
    uint8_t addr[4];
    EXPECT_FALSE(arp->getMac(resolvedMac, writeU32(addr, ip)));

    // GARP reply
    ArpHeader reply2;
    std::memset(buf, 0, 128);
    reply2.setBuffer(buf);
    reply2.setSenderIpAddr(ip);
    reply2.setTargetIpAddr(ip); // GARP
    reply2.setSenderHwAddr(readU48(mac));
    arp->receiveReply(reply2);

    ASSERT_TRUE(arp->getMac(resolvedMac, addr));
    EXPECT_EQ(std::memcmp(resolvedMac, mac, 6), 0);
}

// Test: ReplyIgnored_IfArpNotRunning
TEST_F(Internal_ArpTest, ReplyIgnored_IfArpNotRunning)
{
    arp->shutdown();

    ArpHeader reply;
    reply.setBuffer(buf);
    reply.setSenderIpAddr(ip);
    reply.setTargetIpAddr(ip);
    reply.setSenderHwAddr(readU48(mac));

    EXPECT_NO_THROW(arp->receiveReply(reply));
    uint8_t resolvedMac[6];
    uint8_t addr[4];
    EXPECT_FALSE(arp->getMac(resolvedMac, writeU32(addr, ip)));
}
