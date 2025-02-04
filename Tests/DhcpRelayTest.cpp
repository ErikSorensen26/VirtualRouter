// DhcpRelayTest.cpp

#include <gtest/gtest.h>
#include <Dhcp.h>
#include <Interface.h>
#include <MockInterface.hpp>
#include <ByteString.hpp>
#include <Functions.h>

using namespace Protocol;

// Test Fixture for DhcpRelay
class DhcpRelayTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        mockInterface = new ::testing::NiceMock<MockInterface>();
        dhcpRelay = new DhcpRelay(mockInterface);
    }

    void TearDown() override
    {
        delete dhcpRelay;
        delete mockInterface;
    }

    MockInterface* mockInterface;
    DhcpRelay* dhcpRelay;

    std::vector<ByteString>& getHelperAddresses() {return dhcpRelay->helperAddresses;}
    void forwardToHelper(PacketInfo& packet) {dhcpRelay->forwardToHelper(packet);}
    void forwardToClient(PacketInfo& packet) {dhcpRelay->forwardToClient(packet);}
};

// Test adding a helper address
TEST_F(DhcpRelayTest, AddHelperAddress)
{
    ByteString helperAddress = "\xc0\xa8\x00\x01";
    dhcpRelay->addHelperAddress(helperAddress);
    EXPECT_EQ(getHelperAddresses().size(), 1);
    EXPECT_EQ(getHelperAddresses().front(), helperAddress);
}

// Test removing a helper address
TEST_F(DhcpRelayTest, RemoveHelperAddress)
{
    ByteString helperAddress = "\xc0\xa8\x00\x01";
    dhcpRelay->addHelperAddress(helperAddress);
    dhcpRelay->removeHelperAddress(helperAddress);
    EXPECT_TRUE(getHelperAddresses().empty());
}

// Test forwarding to helper addresses
TEST_F(DhcpRelayTest, ForwardToHelper)
{
    PacketInfo packet;
    packet.Layer3.emplace_back(IPv4Header());
    packet.Layer5.emplace_back(DhcpHeader());
    ByteString helperAddress = "\xc0\xa8\x00\x01";
    dhcpRelay->addHelperAddress(helperAddress);

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1);

    forwardToHelper(packet);
}

// Test forwarding to client
TEST_F(DhcpRelayTest, ForwardToClient)
{
    PacketInfo packet;
    packet.Layer3.emplace_back(IPv4Header());
    packet.Layer5.emplace_back(DhcpHeader());

    EXPECT_CALL(*mockInterface, enqueuePacket(::testing::_, ::testing::_))
        .Times(1);

    forwardToClient(packet);
}
