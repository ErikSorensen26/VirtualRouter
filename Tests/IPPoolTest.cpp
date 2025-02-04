// IPPoolTest.cpp

#include <gtest/gtest.h>
#include <Dhcp.h>
#include <Interface.h>
#include <MockInterface.hpp>
#include <ByteString.hpp>

using namespace Protocol;

// Test Fixture
class IPPoolTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        mockInterface = new ::testing::NiceMock<MockInterface>();
        dhcpServer = new DhcpServer();
    }

    void TearDown() override
    {
        delete dhcpServer;
        delete mockInterface;
    }

    // Mock Interface and DhcpServer instance
    MockInterface* mockInterface;
    DhcpServer* dhcpServer;

    // Helper to add a network
    void addTestNetwork(const ByteString& network, const ByteString& subnetMask, const ByteString& gateway, const std::vector<ByteString>& dnsServers, uint32_t leaseTime)
    {
        DhcpServer::NetworkConfig config;
        config.network = network;
        config.subnetMask = subnetMask;
        config.defaultGateway = gateway;
        config.dnsServer = dnsServers;
        config.leaseTime = leaseTime;
        config.renewalTime = ByteString("\x00\x00\x07\x08", 4); // 1800 seconds
        config.rebindingTime = ByteString("\x00\x00\x0b\xb8", 4); // 3000 seconds
        
        dhcpServer->addNetwork(config);
    }

    ByteString allocateIPAddress(const ByteString& network, const ByteString& mac) {return dhcpServer->allocateIPAddress(network, mac);}
    void releaseIPAddress(ByteString& network, ByteString& mac) {dhcpServer->releaseIPAddress(network, mac);}
    std::unordered_map<ByteString, IPPool>& getIPPools() {return dhcpServer->ipPools;}
    uint32_t getPoolSize(IPPool& pool) {return pool.poolSize;}
    ByteString findNextAvailableIP(IPPool& pool) {return pool.findNextAvailableIP();}
};

// Test IPPool Allocation
TEST_F(IPPoolTest, IPPool_AllocateIP_Success) {
    ByteString network = ByteString("\xc0\xa8\x00\x00", 4); // 192.168.0.0
    ByteString subnetMask = ByteString("\xff\xff\xff\x00", 4); // 255.255.255.0
    ByteString gateway = ByteString("\xc0\xa8\x00\x01", 4); // 192.168.0.1
    std::vector<ByteString> dnsServers = { ByteString("\x08\x08\x08\x08", 4), ByteString("\x08\x08\x04\x04", 4) }; // 8.8.8.8 and 8.8.4.4
    uint32_t leaseTime = 3600;

    addTestNetwork(network, subnetMask, gateway, dnsServers, leaseTime);

    ByteString mac1 = ByteString("\x00\x11\x22\x33\x44\x55", 6);
    ByteString mac2 = ByteString("\x66\x77\x88\x99\xAA\xBB", 6);

    ByteString ip1 = allocateIPAddress(network, mac1);
    ByteString ip2 = allocateIPAddress(network, mac2);

    EXPECT_EQ(ip1, ByteString("\xc0\xa8\x00\x02", 4)); // First IP after network (assuming network address is 192.168.0.0)
    EXPECT_EQ(ip2, ByteString("\xc0\xa8\x00\x03", 4)); // Next IP

    ByteString mac = ByteString("\x00\x00\x00\x00\x00\x00", 6);

    // Allocate all possible IPs
    for (int i = 4; i <= 254; ++i) {
        ByteString ip = allocateIPAddress(network, Functions::numToByte(i, 6));
        EXPECT_FALSE(ip.empty());
    }

    // No more IPs available
    ByteString ipOverflow = allocateIPAddress(network, mac);
    EXPECT_TRUE(ipOverflow.empty());
}

// Test IPPool Release IP
TEST_F(IPPoolTest, IPPool_ReleaseIP_Success) {
    ByteString network = ByteString("\xc0\xa8\x00\x00", 4); // 192.168.0.0
    ByteString subnetMask = ByteString("\xff\xff\xff\x00", 4); // 255.255.255.0
    ByteString gateway = ByteString("\xc0\xa8\x00\x01", 4); // 192.168.0.1
    std::vector<ByteString> dnsServers = { ByteString("\x08\x08\x08\x08", 4), ByteString("\x08\x08\x04\x04", 4) };
    uint32_t leaseTime = 3600;

    addTestNetwork(network, subnetMask, gateway, dnsServers, leaseTime);

    ByteString mac = ByteString("\x00\x11\x22\x33\x44\x55", 6);
    ByteString ip = allocateIPAddress(network, mac);

    EXPECT_EQ(ip, ByteString("\xc0\xa8\x00\x02", 4));

    // Release the IP
    releaseIPAddress(network, ip);

    // Allocate again, should get the same IP
    ByteString ipReallocated = allocateIPAddress(network, mac);
    EXPECT_EQ(ipReallocated, ip);
}

// Test IPPool Exclude and Remove Exclusion
TEST_F(IPPoolTest, IPPool_ExcludeAndRemoveIP) {
    ByteString network = ByteString("\xc0\xa8\x00\x00", 4); // 192.168.0.0
    ByteString subnetMask = ByteString("\xff\xff\xff\x00", 4); // 255.255.255.0
    ByteString gateway = ByteString("\xc0\xa8\x00\x01", 4); // 192.168.0.1
    std::vector<ByteString> dnsServers = { ByteString("\x08\x08\x08\x08"), ByteString("\x08\x08\x04\x04") };
    uint32_t leaseTime = 3600;

    addTestNetwork(network, subnetMask, gateway, dnsServers, leaseTime);

    ByteString excludeIP = ByteString("\xc0\xa8\x00\x02", 4); // 192.168.0.2
    bool excludeResult = getIPPools()[network].excludeIP(excludeIP);
    EXPECT_TRUE(excludeResult);

    // Attempt to allocate IP, should skip excluded IP
    ByteString mac = ByteString("\x00\x11\x22\x33\x44\x55", 6);
    ByteString allocatedIP = allocateIPAddress(network, Functions::numToByte(1, 6));
    EXPECT_EQ(allocatedIP, ByteString("\xc0\xa8\x00\x03", 4)); // First IP

    ByteString allocatedIP2 = allocateIPAddress(network, Functions::numToByte(2, 6));
    EXPECT_EQ(allocatedIP2, ByteString("\xc0\xa8\x00\x04", 4)); // Skips excluded IP

    // Remove exclusion
    bool removeResult = getIPPools()[network].removeExclusion(excludeIP);
    EXPECT_TRUE(removeResult);

    // Now, allocate should include the previously excluded IP
    ByteString allocatedIP3 = allocateIPAddress(network, Functions::numToByte(3, 6));
    EXPECT_EQ(allocatedIP3, ByteString("\xc0\xa8\x00\x02", 4)); // Previously excluded IP
}

// Test IPPool Adjust Pool
TEST_F(IPPoolTest, IPPool_AdjustPool_ResizesCorrectly) {
    ByteString originalNetwork = ByteString("\xc0\xa8\x00\x00", 4); // 192.168.0.0
    ByteString originalSubnetMask = ByteString("\xff\xff\xff\x00", 4); // 255.255.255.0
    ByteString originalGateway = ByteString("\xc0\xa8\x00\x01", 4); // 192.168.0.1
    std::vector<ByteString> dnsServers = { ByteString("\x08\x08\x08\x08", 4) };
    uint32_t leaseTime = 3600;

    addTestNetwork(originalNetwork, originalSubnetMask, originalGateway, dnsServers, leaseTime);

    ByteString mac1 = ByteString("\x00\x11\x22\x33\x44\x55", 4);
    ByteString ip = allocateIPAddress(originalNetwork, mac1);
    EXPECT_EQ(ip, ByteString("\xc0\xa8\x00\x02", 4));

    // Adjust the pool to a smaller subnet
    ByteString newNetwork = ByteString("\xc0\xa8\x00\x00", 4); // 192.168.0.0
    ByteString newSubnetMask = ByteString("\xff\xff\xff\x80", 4); // 255.255.255.128 (smaller pool)
    DhcpServer::NetworkConfig newConfig;
    newConfig.network = newNetwork;
    newConfig.subnetMask = newSubnetMask;
    newConfig.defaultGateway = originalGateway;
    newConfig.dnsServer = dnsServers;
    newConfig.leaseTime = leaseTime;
    newConfig.renewalTime = ByteString("\x00\x00\x07\x08", 4);
    newConfig.rebindingTime = ByteString("\x00\x00\x0b\xb8", 4);

    dhcpServer->updateNetworkConfig(originalNetwork, newConfig);

    // The previously allocated IP should still be allocated if within the new pool
    bool isAllocated = getIPPools()[newNetwork].isAllocated(ip);
    EXPECT_TRUE(isAllocated);

    // Attempt to allocate more IPs, ensuring they are within the new subnet
    ByteString mac2 = ByteString("\x55\x44\x33\x22\x11\x00", 6);
    ByteString ip2 = allocateIPAddress(newNetwork, mac2);
    EXPECT_EQ(ip2, ByteString("\xc0\xa8\x00\x03", 4));

    // The pool size for /25 is 126 usable IPs (192.168.0.1 to 192.168.0.126)
    // Verify that pool size is adjusted correctly
    EXPECT_EQ(getPoolSize(getIPPools()[newNetwork]), 126);
}

// Test IPPool Find Next Available IP
TEST_F(IPPoolTest, IPPool_FindNextAvailableIP_WrapAround) {
    ByteString network = ByteString("\xc0\xa8\x01\x00", 4); // 192.168.1.0
    ByteString subnetMask = ByteString("\xff\xff\xff\x00", 4); // 255.255.255.0
    ByteString gateway = ByteString("\xc0\xa8\x01\x01", 4); // 192.168.1.1
    std::vector<ByteString> dnsServers = { ByteString("\x08\x08\x08\x08", 4) };
    uint32_t leaseTime = 3600;

    addTestNetwork(network, subnetMask, gateway, dnsServers, leaseTime);

    ByteString mac1 = ByteString("\x00\x11\x22\x33\x44\x55", 6);
    ByteString mac2 = ByteString("\x00\x00\x00\x00\x00\x00", 6);

    // Allocate all IPs except the last one
    for (int i = 2; i < 254; ++i) {
        ByteString ip = allocateIPAddress(network, Functions::numToByte(i, 6));
        EXPECT_FALSE(ip.empty());
    }

    // The last IP should be allocated now
    ByteString lastIP = allocateIPAddress(network, mac1);
    EXPECT_EQ(lastIP, ByteString("\xc0\xa8\x01\xfe", 4)); // 192.168.1.254

    // No more IPs available
    ByteString overflowIP = allocateIPAddress(network, mac2);
    EXPECT_TRUE(overflowIP.empty());

    // Release the first IP and test wrap-around
    ByteString releaseIP = ByteString("\xc0\xa8\x01\x02", 4); // 192.168.1.2
    releaseIPAddress(network, releaseIP);

    ByteString wrappedIP = findNextAvailableIP(getIPPools()[network]);
    EXPECT_EQ(wrappedIP, ByteString("\xc0\xa8\x01\x02", 4)); // Should wrap around and find the released IP
}
