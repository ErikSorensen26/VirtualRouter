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
    DhcpNetworkConfig* addTestNetwork(const ByteString& network, uint8_t subnetMask, const ByteString& gateway, const std::vector<ByteString>& dnsServers, uint32_t leaseTime)
    {
        DhcpNetworkConfig* config = new DhcpNetworkConfig();
        config->network = network;
        config->subnetPrefix = subnetMask;
        config->defaultGateway = gateway;
        config->dnsServer = dnsServers;
        config->leaseTime = leaseTime;
        config->renewalTime = ByteString("\x00\x00\x07\x08", 4); // 1800 seconds
        config->rebindingTime = ByteString("\x00\x00\x0b\xb8", 4); // 3000 seconds
        
        dhcpServer->addNetwork(config);

        return config;
    }

    ByteString allocateIPAddress(const DhcpNetworkConfig* network, const ByteString& mac) {return dhcpServer->dhcpNetworks[network->network + "/" + std::to_string(network->subnetPrefix)]->lease->allocateIP(mac, network->leaseTime, 1, 1);}
    void releaseIPAddress(const DhcpNetworkConfig* network, ByteString& ip) {dhcpServer->dhcpNetworks[network->network + "/" + std::to_string(network->subnetPrefix)]->lease->releaseIP(ip);}
    std::unordered_map<ByteString, DhcpNetwork*>& getNetworks() {return dhcpServer->dhcpNetworks;}
    uint32_t getPoolSize(IPPool* pool) {return pool->poolSize;}
    ByteString getKey(const ByteString& network, uint8_t mask) {return network + "/" + std::to_string(mask);}
};

// Test IPPool Allocation
TEST_F(IPPoolTest, IPPool_AllocateIP_Success) {
    ByteString network = ByteString("\xc0\xa8\x00\x00", 4); // 192.168.0.0
    uint8_t subnetMask = 24; // 255.255.255.0
    ByteString gateway = ByteString("\xc0\xa8\x00\x01", 4); // 192.168.0.1
    std::vector<ByteString> dnsServers = { ByteString("\x08\x08\x08\x08", 4), ByteString("\x08\x08\x04\x04", 4) }; // 8.8.8.8 and 8.8.4.4
    uint32_t leaseTime = 3600;

    auto config = addTestNetwork(network, subnetMask, gateway, dnsServers, leaseTime);

    ByteString mac1 = ByteString("\x00\x11\x22\x33\x44\x55", 6);
    ByteString mac2 = ByteString("\x66\x77\x88\x99\xAA\xBB", 6);

    ByteString ip1 = allocateIPAddress(config, mac1);
    ByteString ip2 = allocateIPAddress(config, mac2);

    EXPECT_EQ(ip1, ByteString("\xc0\xa8\x00\x02", 4)); // First IP after network (assuming network address is 192.168.0.0)
    EXPECT_EQ(ip2, ByteString("\xc0\xa8\x00\x03", 4)); // Next IP

    ByteString mac = ByteString("\x00\x00\x00\x00\x00\x00", 6);

    // Allocate all possible IPs
    for (int i = 4; i <= 254; ++i) {
        ByteString ip = allocateIPAddress(config, Functions::numToByte(i, 6));
        EXPECT_FALSE(ip.empty());
    }

    // No more IPs available
    ByteString ipOverflow = allocateIPAddress(config, mac);
    EXPECT_TRUE(ipOverflow.empty());
}

// Test IPPool Release IP
TEST_F(IPPoolTest, IPPool_ReleaseIP_Success) {
    ByteString network = ByteString("\xc0\xa8\x00\x00", 4); // 192.168.0.0
    uint8_t subnetMask = 24; // 255.255.255.0
    ByteString gateway = ByteString("\xc0\xa8\x00\x01", 4); // 192.168.0.1
    std::vector<ByteString> dnsServers = { ByteString("\x08\x08\x08\x08", 4), ByteString("\x08\x08\x04\x04", 4) };
    uint32_t leaseTime = 3600;

    auto config = addTestNetwork(network, subnetMask, gateway, dnsServers, leaseTime);

    ByteString mac = ByteString("\x00\x11\x22\x33\x44\x55", 6);
    ByteString ip = allocateIPAddress(config, mac);

    EXPECT_EQ(ip, ByteString("\xc0\xa8\x00\x02", 4));

    // Release the IP
    releaseIPAddress(config, ip);

    // Allocate again, should get the same IP
    ByteString ipReallocated = allocateIPAddress(config, mac);
    EXPECT_EQ(ipReallocated, ip);
}

// Test IPPool Exclude and Remove Exclusion
TEST_F(IPPoolTest, IPPool_ExcludeAndRemoveIP) {
    ByteString network = ByteString("\xc0\xa8\x00\x00", 4); // 192.168.0.0
    uint8_t subnetMask = 24; // 255.255.255.0
    ByteString gateway = ByteString("\xc0\xa8\x00\x01", 4); // 192.168.0.1
    std::vector<ByteString> dnsServers = { ByteString("\x08\x08\x08\x08"), ByteString("\x08\x08\x04\x04") };
    uint32_t leaseTime = 3600;

    auto config = addTestNetwork(network, subnetMask, gateway, dnsServers, leaseTime);
    ByteString key = getKey(network, subnetMask);

    ByteString excludeIP = ByteString("\xc0\xa8\x00\x02", 4); // 192.168.0.2
    bool excludeResult = getNetworks()[key]->pool->excludeIP(excludeIP);
    EXPECT_TRUE(excludeResult);

    // Attempt to allocate IP, should skip excluded IP
    ByteString mac = ByteString("\x00\x11\x22\x33\x44\x55", 6);
    ByteString allocatedIP = allocateIPAddress(config, Functions::numToByte(1, 6));
    EXPECT_EQ(allocatedIP, ByteString("\xc0\xa8\x00\x03", 4)); // First IP

    ByteString allocatedIP2 = allocateIPAddress(config, Functions::numToByte(2, 6));
    EXPECT_EQ(allocatedIP2, ByteString("\xc0\xa8\x00\x04", 4)); // Skips excluded IP

    // Remove exclusion
    bool removeResult = getNetworks()[key]->pool->removeExclusion(excludeIP);
    EXPECT_TRUE(removeResult);

    // Now, allocate should include the previously excluded IP
    ByteString allocatedIP3 = allocateIPAddress(config, Functions::numToByte(3, 6));
    EXPECT_EQ(allocatedIP3, ByteString("\xc0\xa8\x00\x02", 4)); // Previously excluded IP
}

// Test IPPool Adjust Pool
TEST_F(IPPoolTest, IPPool_AdjustPool_ResizesCorrectly) {
    ByteString originalNetwork = ByteString("\xc0\xa8\x00\x00", 4); // 192.168.0.0
    uint8_t originalSubnetMask = 24; // 255.255.255.0
    ByteString originalGateway = ByteString("\xc0\xa8\x00\x01", 4); // 192.168.0.1
    std::vector<ByteString> dnsServers = { ByteString("\x08\x08\x08\x08", 4) };
    uint32_t leaseTime = 3600;

    auto config = addTestNetwork(originalNetwork, originalSubnetMask, originalGateway, dnsServers, leaseTime);

    ByteString mac1 = ByteString("\x00\x11\x22\x33\x44\x55", 4);
    ByteString ip = allocateIPAddress(config, mac1);
    EXPECT_EQ(ip, ByteString("\xc0\xa8\x00\x02", 4));

    // Adjust the pool to a smaller subnet
    ByteString newNetwork = ByteString("\xc0\xa8\x00\x00", 4); // 192.168.0.0
    uint8_t newSubnetPrefix = 25; // 255.255.255.128 (smaller pool)
    DhcpNetworkConfig newConfig;
    newConfig.network = newNetwork;
    newConfig.subnetPrefix = newSubnetPrefix;
    newConfig.defaultGateway = originalGateway;
    newConfig.dnsServer = dnsServers;
    newConfig.leaseTime = leaseTime;
    newConfig.renewalTime = ByteString("\x00\x00\x07\x08", 4);
    newConfig.rebindingTime = ByteString("\x00\x00\x0b\xb8", 4);

    dhcpServer->updateNetworkConfig(getKey(originalNetwork, originalSubnetMask), newConfig, {}, {}, {});

    // The previously allocated IP should still be allocated if within the new pool
    bool isAllocated = getNetworks()[getKey(newNetwork, newSubnetPrefix)]->pool->isAllocated(ip);
    EXPECT_TRUE(isAllocated);

    // Attempt to allocate more IPs, ensuring they are within the new subnet
    ByteString mac2 = ByteString("\x55\x44\x33\x22\x11\x00", 6);
    ByteString ip2 = allocateIPAddress(&newConfig, mac2);
    EXPECT_EQ(ip2, ByteString("\xc0\xa8\x00\x03", 4));

    // The pool size for /25 is 126 usable IPs (192.168.0.1 to 192.168.0.126)
    // Verify that pool size is adjusted correctly
    EXPECT_EQ(getPoolSize(getNetworks()[getKey(newNetwork, newSubnetPrefix)]->pool), 126);
}
