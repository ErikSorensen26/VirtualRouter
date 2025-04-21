// Dhcpv6ServerTest.cpp

#include <gtest/gtest.h>
#include <Dhcpv6Server.h>
#include <MockInterface.hpp>
#include <functional>
#include <Configs.h>

using namespace Protocol;

class Dhcpv6ServerTest : public ::testing::Test
{
protected:
    Dhcpv6Server* server;
    MockInterface* iface;
    ByteString clientID;

    void SetUp() override
    {
        if (Configs::macAddressList.GigabitEthernet.size() == 0)
        {
            Configs::macAddressList.GigabitEthernet.push_back("\x11\x22\x33\x44\x55\x66");
        }
        
        server = new Dhcpv6Server();
        iface = new MockInterface();

        // Configure a basic network for testing
        auto* config = new Dhcp::DhcpNetworkConfig();

        config->interface = iface;
        ByteString network = ByteString("\x20\x01\x0D\xB8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 16);
        uint8_t subnetPrefix = 56;
        config->updateNetwork(&network, &subnetPrefix, nullptr, nullptr);
        config->leaseTime = 3600;
        config->t1Percentage = 0.5;
        config->t2Percentage = 0.8;
        config->serverPreference = 255;
        
        // Set up network
        server->addNetwork(config);
        net = getNetworks().begin()->second;
    }

    void TearDown() override
    {
        iface->blockEnqueues();
        server->stopServer();
        delete server;
        delete iface;
    }

    ByteString makeTransID() { return generateDhcpTransid(); }
    ByteString getDuid() { return server->dhcpUniqueIdentifier; }
    ByteString getClientDUID() { return ByteString("\x00\x01\x00\x11\x22\x33\x44\x55\x66\x77\x88", 11); }
    std::unordered_map<ByteString, Dhcp::DhcpNetwork*>& getNetworks() { return server->dhcpNetworks; }
    Protocol::Dhcp::DhcpNetwork* net = nullptr;
    ByteString localAddress = ByteString("\xfe\x80\x28\x03\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01", 16);

    ByteString generateIAID()
    {
        ByteString result;
        result.reserve(4);

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint8_t> dist (0, 255);

        for (int i = 0; i < 4; ++i)
        {
            result.push_back(static_cast<uint8_t>(dist(gen)));
        }

        return result;
    }

    void addClientID(const ByteString& duid, Dhcpv6Header& header)
    {
        header.options.push_back({
            Variable::Dhcpv6::Options::clientID,
            Functions::numToByte(duid.size(), 2),
            duid
        });
    }

    void addServerID(Dhcpv6Header& header)
    {
        ByteString duid = getDuid();
        header.options.push_back({
            Variable::Dhcpv6::Options::serverID,
            Functions::numToByte(duid.size(), 2),
            duid
        });
    }

    void addIANA(const ByteString& iaid, Dhcpv6Header& header, std::vector<ByteString> ips = {})
    {
        ByteString iaaddr = "";
        for (const auto& addr : ips)
        {
            iaaddr += Variable::Dhcpv6::Options::IAAddr +
                Functions::numToByte(24, 2) +
                Functions::numToByte(0, 4) +
                Functions::numToByte(0, 4) +
                addr;
        }

        ByteString ia_na = iaid + Functions::numToByte(0, 4) + Functions::numToByte(0, 4) + iaaddr;
        header.options.push_back({
            Variable::Dhcpv6::Options::IA_NA,
            Functions::numToByte(ia_na.size(), 2),
            ia_na
        });
    }

    void addIAPD(const ByteString& iaid, Dhcpv6Header& header, std::vector<std::pair<ByteString, uint8_t>> prefixes = {})
    {
        ByteString iaaddr = "";
        for (const auto& [prefix, length] : prefixes)
        {
            iaaddr += Variable::Dhcpv6::Options::IA_Prefix +
                Functions::numToByte(24, 2) +
                Functions::numToByte(0, 4) +
                Functions::numToByte(0, 4) +
                Functions::numToByte(length, 1) +
                ByteString(3, '\x00') +
                prefix +
                Functions::numToByte(length);
        }

        ByteString ia_pd = iaid + Functions::numToByte(0, 4) + Functions::numToByte(0, 4) + iaaddr;
        header.options.push_back({
            Variable::Dhcpv6::Options::IA_PD,
            Functions::numToByte(ia_pd.size(), 2),
            ia_pd
        });
    }

    void addIATA(const ByteString& iaid, Dhcpv6Header& header, std::vector<ByteString> ips = {})
    {
        ByteString iaaddr = "";
        for (const auto& addr : ips)
        {
            iaaddr += Variable::Dhcpv6::Options::IAAddr +
                Functions::numToByte(24, 2) +
                Functions::numToByte(0, 4) +
                Functions::numToByte(0, 4) +
                addr;
        }

        ByteString ia_ta = iaid + Functions::numToByte(0, 4) + Functions::numToByte(0, 4) + iaaddr;
        header.options.push_back({
            Variable::Dhcpv6::Options::IA_TA,
            Functions::numToByte(ia_ta.size(), 2),
            ia_ta
        });
    }

    void expectPacket(const ByteString& opcode, int num = 1, std::function<bool(const Dhcpv6Header&)> func = nullptr)
    {
        EXPECT_CALL(*iface, enqueuePacket(::testing::_, ::testing::_))
            .Times(num)
            .WillOnce(::testing::Invoke([&](const PacketInfo& pkt, const ByteString&) {
                // Verify that the packet is a DHCP Offer
                ASSERT_FALSE(pkt.Layer5.empty());
                const Dhcpv6Header& header = std::get<Dhcpv6Header>(pkt.Layer5[0]);
                EXPECT_EQ(header.type, opcode);
                if (func)
                {
                    EXPECT_TRUE(func(header));
                }
            }));
    }

    void expectNoPacket() {EXPECT_CALL(*iface, enqueuePacket(::testing::_, ::testing::_)).Times(0);}

    Dhcpv6Header makePacket(const ByteString& type, bool serverID = true)
    {
        Dhcpv6Header header;
        header.type = type;
        header.transactionID = makeTransID();
        addClientID(getClientDUID(), header);
        if (serverID) addServerID(header);
        return header;
    }

    Protocol::Dhcpv6::AuthConfig& enableDelayedAuth(const ByteString& key, uint64_t id, size_t offset = 0)
    {
        server->delayedAuthenticationEnabled.store(true, std::memory_order_release);
        server->authConfig.delayedKeys.emplace_back(id, key, std::chrono::steady_clock::now() + std::chrono::seconds(offset), std::chrono::seconds(60));
        return server->authConfig;
    }

    Protocol::Dhcpv6::AuthConfig& enableRKAP(const ByteString key, uint64_t id, size_t offset = 0)
    {
        server->rkapAuthenticationEnabled.store(true, std::memory_order_release);
        server->authConfig.delayedKeys.emplace_back(id, key, std::chrono::steady_clock::now() + std::chrono::seconds(offset), std::chrono::seconds(60));
        return server->authConfig;
    }

    std::map<Dhcp::TimerType, std::vector<Dhcp::TrackedTimer>>& getActiveTimers() { return server->activeTimers; }
    bool buildAuthOption(Dhcpv6Header& header, const ByteString& duid) {return server->addAuthenticationOption(header, duid);}
    void sendReconfigure(const ByteString& duid, const Dhcpv6::ReconfigReason reason, bool isRelay) {server->sendReconfigure(duid, reason, isRelay);}
    Dhcpv6::Configs& getConfigs() {return server->dhcpConfigs;}
    ByteString extractDUID(const Dhcpv6Header& header) {return server->extractDUID(header);}
    bool validateDUID(const Dhcpv6Header& header) {return server->validateServerID(header);}
    std::vector<Protocol::Dhcpv6::IANABlock> extractIANA(const Dhcpv6Header& header) {return server->extractIA_NA(header, iface);}
    std::vector<Protocol::Dhcpv6::IAPDBlock> extractIAPD(const Dhcpv6Header& header) {return server->extractIA_PD(header, iface);}
    std::vector<Protocol::Dhcpv6::IANABlock> extractIATA(const Dhcpv6Header& header) {return server->extractIA_TA(header, iface);}
    std::vector<ByteString> extractORO(const Dhcpv6Header& header) {return server->extractORO(header);}
    std::unordered_map<ByteString, std::pair<ByteString, Interface*>>& getClientAccepts() { return server->clientReconfAccept; }
    void allExcluded() {net->pool->baseAddress = net->pool->lastAddress; net->pool->currentAddress = net->pool->lastAddress;}
    std::vector<Dhcpv6Header::Option> buildOptions(Dhcp::DhcpNetworkConfig* net, std::vector<ByteString> opt) {return server->buildOptions(net, opt);}
    Dhcpv6Header buildResponse(
        const ByteString& type,
        const ByteString& transactionID,
        const ByteString& duid,
        const std::vector<Dhcpv6::IANABlock>& ianaBlocks,
        const std::vector<Dhcpv6::IAPDBlock>& iapdBlocks,
        const std::vector<Dhcpv6::IANABlock>& iataBlocks
    )
    {
        return server->buildResponse(type, transactionID, duid, ianaBlocks, iapdBlocks, iataBlocks);
    }
};

#pragma region Solicit

// Solicit: Valid IA_NA without Rapid Commit should allocate a temporary lease
TEST_F(Dhcpv6ServerTest, Solicit_ValidIA_NA_NoRapidCommit_AllocatesTempLease) 
{
    Dhcpv6Header solicit = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString iaid = generateIAID();

    addIANA(iaid, solicit);

    PacketInfo packet;
    packet.Layer5.push_back(solicit);
    expectPacket(Variable::Dhcpv6::Type::advertise);
    server->handleDhcpPacket(packet, iface, true, localAddress);

    // There should now be a temp lease allocated
    auto network = getNetworks().begin();
    auto tempLease = network->second->pool->getTempIP(getClientDUID() + iaid + ByteString("\x20\x01\x0D\xB8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01", 16));

    ASSERT_EQ(tempLease.size(), 16);
}

// Solicit: Valid IA_NA with Rapid Commit should enqueue a Reply and commit lease
TEST_F(Dhcpv6ServerTest, Solicit_ValidIA_NA_WithRapidCommit_CommitsLease) 
{
    Dhcpv6Header solicit = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString iaid = generateIAID();

    addIANA(iaid, solicit);

    // Add rapid commit
    solicit.options.push_back({
        Variable::Dhcpv6::Options::rapidCommit,
        Functions::numToByte(0, 2),
        {}
    });

    PacketInfo pkt;
    pkt.Layer5.push_back(solicit);
    expectPacket(Variable::Dhcpv6::Type::reply);
    server->handleDhcpPacket(pkt, iface, true, localAddress);
    
    // There should be a allocated lease
    auto network = getNetworks().begin();

    ASSERT_EQ(network->second->lease->getActiveLeases().size(), 1);
}

// Solicit: Requested IP should be honored if available
TEST_F(Dhcpv6ServerTest, Solicit_IA_NA_WithRequestedAddress_AllocatesIfAvailable) 
{
    Dhcpv6Header solicit = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString iaid = generateIAID();
    ByteString requested = ByteString("\x20\x01\x0d\xb8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x10", 16);

    // Build IA_NA + IAAddr
    ByteString ia_na = iaid + Functions::numToByte(0, 4) + Functions::numToByte(0, 4);
    ByteString iaaddr = requested + Functions::numToByte(3600, 4) + Functions::numToByte(5400, 4);
    ia_na += Variable::Dhcpv6::Options::IAAddr + Functions::numToByte(iaaddr.size(), 2) + iaaddr;

    solicit.options.push_back({
        Variable::Dhcpv6::Options::IA_NA,
        Functions::numToByte(ia_na.size(), 2),
        ia_na
    });

    // Add rapid commit
    solicit.options.push_back({
        Variable::Dhcpv6::Options::rapidCommit,
        Functions::numToByte(0, 2),
        {}
    });

    PacketInfo pkt;
    pkt.Layer5.push_back(solicit);
    expectPacket(Variable::Dhcpv6::Type::reply, 1, [&](const Dhcpv6Header& reply) -> bool {
        for (const auto& opt : reply.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::IA_NA && opt.value.find(requested) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    });

    server->handleDhcpPacket(pkt, iface, true, localAddress);
}

// Solicit: IA_PD without Rapid Commit should enqueue Advertise with prefix offered
TEST_F(Dhcpv6ServerTest, Solicit_IA_PD_AllocatesPrefix) 
{
    Dhcpv6Header solicit = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString iaid = generateIAID();

    addIAPD(iaid, solicit);

    PacketInfo pkt;
    pkt.Layer5.push_back(solicit);
    expectPacket(Variable::Dhcpv6::Type::advertise, 1, [](const Dhcpv6Header& advertise) -> bool {
        for (const auto& opt : advertise.options) 
        {
            if (opt.option == Variable::Dhcpv6::Options::IA_PD)
            {
                return true;
            }
        }
        return false;
    });

    server->handleDhcpPacket(pkt, iface, true, localAddress);
    
    // There should now be a temp lease allocated
    auto network = getNetworks().begin();
    auto tempLease = network->second->prefixPool->getTempPrefix(getClientDUID() + iaid + ByteString("\x20\x01\x0D\xB8\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00", 16));

    ASSERT_EQ(tempLease.first.size(), 16);
}

// Solicit: IA_TA without Rapid Commit should enqueue Advertise with temp address.
TEST_F(Dhcpv6ServerTest, Solicit_IA_TA_AllocatesTempAddress)
{
    Dhcpv6Header solicit = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString iaid = generateIAID();

    addIATA(iaid, solicit);

    PacketInfo pkt;
    pkt.Layer5.push_back(solicit);
    expectPacket(Variable::Dhcpv6::Type::advertise, 1, [](const Dhcpv6Header& advertise) -> bool {
        for (const auto& opt : advertise.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::IA_TA)
            {
                return true;
            }
        }
        return false;
    });

    server->handleDhcpPacket(pkt, iface, true, localAddress);
}

// Solicit: Valid IA_TA with Rapid Commit should enqueue a Reply and commit lease
TEST_F(Dhcpv6ServerTest, Solicit_ValidIA_TA_WithRapidCommit_CommitsLease) 
{
    Dhcpv6Header solicit = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString iaid = generateIAID();

    addIATA(iaid, solicit);

    // Add rapid commit
    solicit.options.push_back({
        Variable::Dhcpv6::Options::rapidCommit,
        Functions::numToByte(0, 2),
        {}
    });

    PacketInfo pkt;
    pkt.Layer5.push_back(solicit);
    expectPacket(Variable::Dhcpv6::Type::reply);
    server->handleDhcpPacket(pkt, iface, true, localAddress);
    
    // There should be a allocated lease
    auto network = getNetworks().begin();

    ASSERT_EQ(network->second->lease->getActiveLeases().size(), 1);
    ASSERT_TRUE(network->second->lease->getActiveLeases().begin()->second.clientID == "");
}

// Solicit: Missing DUID should be ignored
TEST_F(Dhcpv6ServerTest, Solicit_MissingDUID_Ignored) 
{
    Dhcpv6Header solicit;
    solicit.type = Variable::Dhcpv6::Type::solicit;
    solicit.transactionID = makeTransID();

    // IA_NA with dummy IAID
    ByteString iaid = generateIAID();
    addIANA(iaid, solicit);

    PacketInfo pkt;
    pkt.Layer5.push_back(solicit);
    expectNoPacket();
    server->handleDhcpPacket(pkt, iface, true, localAddress);

    // There should be no temp ips
    auto network = getNetworks().begin();

    ASSERT_FALSE(network->second->pool->isTemporarilyOffered(ByteString("\x20\x01\x0D\xB8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01", 16)));
}

// Solicit: Invalid authentication should be ignored
TEST_F(Dhcpv6ServerTest, Solicit_AuthInvalid_Ignored) 
{
    Dhcpv6Header solicit = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString iaid = generateIAID();

    addIANA(iaid, solicit);

    // Add corrupted authentication
    enableDelayedAuth("test", 1);
    ByteString badAuth = ByteString("\x00\x01\x00\x01\x00", 5);
    badAuth += Functions::numToByte(1000, 8);
    badAuth += ByteString(16, '\xff');
    solicit.options.push_back({
        Variable::Dhcpv6::Options::auth,
        Functions::numToByte(badAuth.size(), 2),
        badAuth
    });

    PacketInfo pkt;
    pkt.Layer5.push_back(solicit);
    expectNoPacket();

    server->handleDhcpPacket(pkt, iface, true, localAddress);
}

// Solicit: No available address should return noAddrsAvail status
TEST_F(Dhcpv6ServerTest, Solicit_NoAvailableAddress_ReturnsNoAddrsAvail) 
{
    Dhcpv6Header solicit = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString iaid = generateIAID();

    addIANA(iaid, solicit);

    // Exclude all addresses
    getNetworks().begin()->second->pool->allExcluded = true;
    allExcluded();

    PacketInfo pkt;
    pkt.Layer5.push_back(solicit);
    expectPacket(Variable::Dhcpv6::Type::advertise, 1, [](const Dhcpv6Header& pkt) -> bool {
        for (const auto& opt : pkt.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::statusCode &&
                opt.value.substr(0, 2) == Variable::Dhcpv6::Status::noAddrsAvail)
            {
                return true;
            }
        }
        return false;
    });
    
    server->handleDhcpPacket(pkt, iface, true, localAddress);
}

// Solicit: Should include preference option in Advertise
TEST_F(Dhcpv6ServerTest, Solicit_ValidOptions_ReturnsPreference)
{
    Dhcpv6Header solicit = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString iaid = generateIAID();

    addIANA(iaid, solicit);

    PacketInfo pkt;
    pkt.Layer5.push_back(solicit);

    expectPacket(Variable::Dhcpv6::Type::advertise, 1, [](const Dhcpv6Header& pkt) -> bool {
        for (const auto& opt : pkt.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::preference)
            {
                if (opt.value.size() == 1)
                {
                    return true;
                }
                return false;
            }
        }
        return false;
    });

    server->handleDhcpPacket(pkt, iface, true, localAddress);
}

// Solicit: Includes DNS and NTP in response if requested via ORO
TEST_F(Dhcpv6ServerTest, Solicit_RequestedOptions_IncludeDNS_NTP) 
{
    Dhcpv6Header solicit = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString iaid = generateIAID();

    addIANA(iaid, solicit);

    // Options request
    ByteString oro = Variable::Dhcpv6::Options::dnsServer + Variable::Dhcpv6::Options::ntpServer;
    solicit.options.push_back({
        Variable::Dhcpv6::Options::optionRequest,
        Functions::numToByte(oro.size(), 2),
        oro
    });

    PacketInfo pkt;
    pkt.Layer5.push_back(solicit);

    expectPacket(Variable::Dhcpv6::Type::advertise, 1, [](const Dhcpv6Header& pkt) -> bool {
        bool foundDNS = false;
        bool foundNTP = false;

        for (const auto& opt : pkt.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::dnsServer)
                foundDNS = true;
            if (opt.option == Variable::Dhcpv6::Options::ntpServer)
                foundNTP = true;
        }

        if (foundNTP && foundDNS) return true;
        return false;
    });

    server->handleDhcpPacket(pkt, iface, true, localAddress);
}


#pragma endregion
#pragma region Request
/*

// Request: Valid IA_NA with offered address commits lease
TEST_F(Dhcpv6ServerTest, Request_ValidIA_NA_ConfirmsLease) 
{
    Dhcpv6Header request = makePacket(Variable::Dhcpv6::Type::request);
    ByteString iaid = generateIAID();

    // Extract clientID
    ByteString id = getClientDUID() + iaid;
    ByteString offeredIP = net->pool->allocateTempIP(&id, true);

    addIANA(iaid, request, {offeredIP});

    PacketInfo pkt;
    pkt.Layer5.push_back(request);

    expectPacket(Variable::Dhcpv6::Type::reply, 1, [&](const Dhcpv6Header& reply) -> bool {
        for (const auto& opt : reply.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::IA_NA && opt.value.find(offeredIP) != std::string::npos)
                return true;
        }
        return false;
    });

    server->handleDhcpPacket(pkt, iface, false, localAddress);

    ASSERT_TRUE(net->lease->getActiveLeases().begin()->second.ipAddress.size() == 16);
}

// Request Valid IA_PD with previously offered prefix confirms lease
TEST_F(Dhcpv6ServerTest, Request_ValidIA_PD_ConfirmsPrefix)
{
    Dhcpv6Header request = makePacket(Variable::Dhcpv6::Type::request);
    ByteString iaid = generateIAID();

    auto offeredPrefix = net->prefixPool->allocateTempPrefix(getClientDUID() + iaid, net->config->subnetPrefix, true);

    addIAPD(iaid, request, {offeredPrefix});

    PacketInfo pkt;
    pkt.Layer5.push_back(request);

    expectPacket(Variable::Dhcpv6::Type::reply, 1, [&](const Dhcpv6Header& pkt) -> bool {
        for (const auto& opt : pkt.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::IA_PD && opt.value.find(offeredPrefix.first) != std::string::npos)
                return true;
        }
        return false;
    });

    server->handleDhcpPacket(pkt, iface, false, localAddress);

    ASSERT_TRUE(net->lease->getActiveLeases().begin()->second.ipAddress.size() == 16);
}

// Request: Validate IA_TA with prefiously offered address confirms lease
TEST_F(Dhcpv6ServerTest, Request_ValidIA_TA_ConfirmsTempAddress)
{
    Dhcpv6Header request = makePacket(Variable::Dhcpv6::Type::request);
    ByteString iaid = generateIAID();

    // Extract clientID
    ByteString id = getClientDUID() + iaid;
    ByteString offeredIP = net->pool->allocateTempIP(&id, true);

    addIATA(iaid, request, {offeredIP});

    PacketInfo pkt;
    pkt.Layer5.push_back(request);

    expectPacket(Variable::Dhcpv6::Type::reply, 1, [&](const Dhcpv6Header& reply) -> bool {
        for (const auto& opt : reply.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::IA_TA && opt.value.find(offeredIP) != std::string::npos)
                return true;
        }
        return false;
    });

    server->handleDhcpPacket(pkt, iface, false, localAddress);

    ASSERT_TRUE(net->lease->getActiveLeases().begin()->second.ipAddress.size() == 16);
}

// Request: With unrecognized address, server allcoates a new address
TEST_F(Dhcpv6ServerTest, Request_WithUnknownAddress_AllocatesNew) 
{
    Dhcpv6Header request = makePacket(Variable::Dhcpv6::Type::request);
    ByteString iaid = generateIAID();

    ByteString unknownAddr = ByteString("\xfd\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x09\x99", 16);

    addIANA(iaid, request, {unknownAddr});

    PacketInfo pkt;
    pkt.Layer5.push_back(request);

    expectPacket(Variable::Dhcpv6::Type::reply, 1, [&](const Dhcpv6Header& pkt) -> bool {
        for (const auto& opt : pkt.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::IA_NA && opt.value.find(unknownAddr) == std::string::npos)
                return true;
        }
        return false;
    });

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Request: With prefiously offered temp lease, server commits lease
TEST_F(Dhcpv6ServerTest, Request_WithTempLease_CommitsLease)
{
    Dhcpv6Header request = makePacket(Variable::Dhcpv6::Type::request);
    ByteString iaid = generateIAID();

    ByteString id = getClientDUID() + iaid;
    ByteString tempAddr = net->pool->allocateTempIP(&iaid, true);

    addIANA(iaid, request, {tempAddr});

    PacketInfo pkt;
    pkt.Layer5.push_back(request);

    expectPacket(Variable::Dhcpv6::Type::reply, 1, [&](const Dhcpv6Header& pkt) -> bool {
        for (const auto& opt : pkt.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::IA_NA && opt.value.find(tempAddr) != std::string::npos)
                return true;
        }
        return false;
    });

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Request: With invalid server ID, ignored by server
TEST_F(Dhcpv6ServerTest, Request_WithInvalidServerID_Ignored)
{
    Dhcpv6Header request = makePacket(Variable::Dhcpv6::Type::request, false);
    ByteString iaid = generateIAID();
    
    // Invalid server ID
    ByteString fakeID = ByteString("\x00\x03\x11\x22\x33\x44\x55\x66", 6);
    request.options.push_back({
        Variable::Dhcpv6::Options::serverID,
        Functions::numToByte(fakeID.size(), 2),
        fakeID
    });

    PacketInfo pkt;
    pkt.Layer5.push_back(request);

    expectNoPacket();

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Request: Missing client DUID, ignored by server
TEST_F(Dhcpv6ServerTest, Request_MissingDUID_Ignored)
{
    Dhcpv6Header request;
    request.type = Variable::Dhcpv6::Type::reply;
    request.transactionID = makeTransID();
    addServerID(request);
    ByteString iaid = generateIAID();

    PacketInfo pkt;
    pkt.Layer5.push_back(request);

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Request Auth option invalid (wrong MAC), ignored
TEST_F(Dhcpv6ServerTest, Request_AuthInvalid_Ignored)
{
    Dhcpv6Header request = makePacket(Variable::Dhcpv6::Type::request);

    // Add corrupted authentication
    enableDelayedAuth("test", 1);
    ByteString badAuth = ByteString("\x00\x01\x00\x01\x00", 5);
    badAuth += Functions::numToByte(1000, 8);
    badAuth += ByteString(16, '\xff');
    request.options.push_back({
        Variable::Dhcpv6::Options::auth,
        Functions::numToByte(badAuth.size(), 2),
        badAuth
    });

    PacketInfo pkt;
    pkt.Layer5.push_back(request);
    expectNoPacket();

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

*/
#pragma endregion
#pragma region Renew
/*

// Renew: Valid lease gets renewed
TEST_F(Dhcpv6ServerTest, Renew_ValidLease_Renews) 
{
    Dhcpv6Header renew = makePacket(Variable::Dhcpv6::Type::renew);
    ByteString iaid = generateIAID();

    ByteString id = getClientDUID() + iaid;
    ByteString ip = net->pool->allocateTempIP(&id, true);
    id += ip;
    net->lease->activateLeaseFromTemp(ip, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage, &id);

    // Add IA_NA with leased ip
    addIANA(iaid, renew, {ip});

    PacketInfo pkt;
    pkt.Layer5.push_back(renew);

    double before = net->lease->getActiveLeases().at(ip).leaseStart;

    expectPacket(Variable::Dhcpv6::Type::reply);
    server->handleDhcpPacket(pkt, iface, false, localAddress);

    double after = net->lease->getActiveLeases().at(ip).leaseStart;
    ASSERT_GT(after, before);
}

// Renew: Invalid (nonexistent) lease should be ignored
TEST_F(Dhcpv6ServerTest, Renew_InvalidLease_Ignored)
{
    Dhcpv6Header renew = makePacket(Variable::Dhcpv6::Type::renew);
    ByteString iaid = generateIAID();
    ByteString fakeIP = ByteString("\xfd\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01", 16);

    addIANA(iaid, renew, {fakeIP});

    PacketInfo pkt;
    pkt.Layer5.push_back(renew);
    expectPacket(Variable::Dhcpv6::Type::reply, 0);
    expectNoPacket();

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Renew: Missing client DUID should be ignored
TEST_F(Dhcpv6ServerTest, Renew_MissingDUID_Ignored) 
{
    Dhcpv6Header renew;
    renew.type = Variable::Dhcpv6::Type::renew;
    renew.transactionID = makeTransID();
    addServerID(renew);
    ByteString iaid = generateIAID();
    ByteString id = getClientDUID() + iaid;
    ByteString ip = net->pool->allocateTempIP(&id, true);
    id += ip;
    net->lease->activateLeaseFromTemp(ip, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage, &id);

    addIANA(iaid, renew, {ip});

    PacketInfo pkt;
    pkt.Layer5.push_back(renew);
    expectNoPacket();
    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Renew: Invalid authentication should be ignored
TEST_F(Dhcpv6ServerTest, Renew_AuthInvalid_Ignored)
{
    Dhcpv6Header renew = makePacket(Variable::Dhcpv6::Type::renew);
    ByteString iaid = generateIAID();

    ByteString id = getClientDUID() + iaid;
    ByteString ip = net->pool->allocateTempIP(&id, true);
    id += ip;
    net->lease->activateLeaseFromTemp(ip, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage, &id);

    // Add corrupted authentication
    enableDelayedAuth("test", 1);
    ByteString badAuth = ByteString("\x00\x01\x00\x01\x00", 5);
    badAuth += Functions::numToByte(1000, 8);
    badAuth += ByteString(16, '\xff');
    renew.options.push_back({
        Variable::Dhcpv6::Options::auth,
        Functions::numToByte(badAuth.size(), 2),
        badAuth
    });

    addIANA(iaid, renew, {ip});

    PacketInfo pkt;
    pkt.Layer5.push_back(renew);
    expectNoPacket();
    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Renew: IA_PD lease (prefix) gets renewes successully
TEST_F(Dhcpv6ServerTest, Renew_ValidIA_PD_PrefixRenews)
{
    Dhcpv6Header renew = makePacket(Variable::Dhcpv6::Type::renew);
    ByteString iaid = generateIAID();
    
    ByteString id = getClientDUID() + iaid;
    auto prefix = net->prefixPool->allocatePrefix(id, net->config->defaultSubnetPrefix, true);
    id += prefix.first;
    net->lease->activateLeaseFromTemp(prefix.first, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage, &id);

    addIATA(iaid, renew, {prefix.first});

    PacketInfo pkt;
    pkt.Layer5.push_back(renew);
    
    double before = net->prefixLease->getActiveLeases().at(prefix.first).leaseStart;

    expectPacket(Variable::Dhcpv6::Type::reply);
    server->handleDhcpPacket(pkt, iface, false, localAddress);

    double after = net->prefixLease->getActiveLeases().at(prefix.first).leaseStart;
    ASSERT_GT(after, before);
}

// Renew: IA_TA lease (temp address) gets renewed successfully
TEST_F(Dhcpv6ServerTest, Renew_ValidIA_TA_Renew_Ignored) 
{
    Dhcpv6Header renew = makePacket(Variable::Dhcpv6::Type::renew);
    ByteString iaid = generateIAID();

    ByteString ip = net->pool->allocateTempIP(nullptr, true);
    net->lease->activateLeaseFromTemp(ip, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage, nullptr);

    PacketInfo pkt;
    pkt.Layer5.push_back(renew);

    double before = net->lease->getActiveLeases().at(ip).leaseStart;

    expectNoPacket();
    server->handleDhcpPacket(pkt, iface, false, localAddress);
    
    double after = net->lease->getActiveLeases().at(ip).leaseStart;
    ASSERT_EQ(after, before);
}

*/
#pragma endregion
#pragma region Rebind
/*

// Rebind
TEST_F(Dhcpv6ServerTest, Rebind_ValidLease_Renews)
{
    Dhcpv6Header rebind = makePacket(Variable::Dhcpv6::Type::rebind, false);
    ByteString iaid = generateIAID();

    ByteString id = getClientDUID() + iaid;
    ByteString ip = net->pool->allocateTempIP(&id, true);
    id += ip;
    net->lease->activateLeaseFromTemp(ip, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage, &id);

    addIANA(iaid, rebind, {ip});

    PacketInfo pkt;
    pkt.Layer5.push_back(rebind);

    double before = net->lease->getActiveLeases().at(ip).leaseStart;

    expectPacket(Variable::Dhcpv6::Type::reply);
    server->handleDhcpPacket(pkt, iface, false, localAddress);

    double after = net->lease->getActiveLeases().at(ip).leaseStart;
    ASSERT_GT(after, before);
}

// Rebind: expired lease should not be renewed
TEST_F(Dhcpv6ServerTest, Rebind_ExpiredLease_Renews)
{
    Dhcpv6Header rebind = makePacket(Variable::Dhcpv6::Type::rebind, false);
    ByteString iaid = generateIAID();

    ByteString id = getClientDUID() + iaid;
    ByteString ip = net->pool->allocateTempIP(&id, true);
    id += ip;
    net->lease->activateLeaseFromTemp(ip, 1, 0.5, 0.8, &id);

    // Manually expire
    auto& lease = const_cast<LeaseManager::Lease&>(net->lease->getActiveLeases().at(ip));
    lease.leaseStart -= 2;

    addIANA(iaid, rebind, {ip});

    PacketInfo pkt;
    pkt.Layer5.push_back(rebind);
    expectNoPacket();

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

TEST_F(Dhcpv6ServerTest, Rebind_MissingDUID_Ignored)
{
    Dhcpv6Header rebind;
    rebind.type = Variable::Dhcpv6::Type::rebind;
    rebind.transactionID = makeTransID();
    ByteString iaid = generateIAID();
    ByteString ip = net->pool->allocateTempIP(nullptr);

    addIANA(iaid, rebind, {ip});

    PacketInfo pkt;
    pkt.Layer5.push_back(rebind);
    expectNoPacket();

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

TEST_F(Dhcpv6ServerTest, Rebind_AuthInvalid_Ignored) 
{
    Dhcpv6Header rebind = makePacket(Variable::Dhcpv6::Type::rebind, false);
    ByteString iaid = generateIAID();
    ByteString ip = net->pool->allocateTempIP(nullptr);

    addIANA(iaid, rebind, {ip});

    enableDelayedAuth("test", 1);
    ByteString badAuth = ByteString("\x00\x01\x00\x01\x00", 5);
    badAuth += Functions::numToByte(1000, 8);
    badAuth += ByteString(16, '\xff');
    rebind.options.push_back({
        Variable::Dhcpv6::Options::auth,
        Functions::numToByte(badAuth.size(), 2),
        badAuth
    });

    PacketInfo pkt;
    pkt.Layer5.push_back(rebind);
    expectNoPacket();

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

*/
#pragma endregion
#pragma region Release
/*

// Release: Valid IA_NA lease should be released and held temporarily
TEST_F(Dhcpv6ServerTest, Release_ValidLease_TriggersHold)
{
    Dhcpv6Header release = makePacket(Variable::Dhcpv6::Type::release);
    ByteString iaid = generateIAID();

    ByteString id = getClientDUID() + iaid;
    ByteString ip = net->pool->allocateTempIP(&id, true);
    id += ip;
    net->lease->activateLeaseFromTemp(ip, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage, &id);

    addIANA(iaid, release, {ip});

    PacketInfo pkt;
    pkt.Layer5.push_back(release);

    expectPacket(Variable::Dhcpv6::Type::reply);
    server->handleDhcpPacket(pkt, iface, false, localAddress);

    ASSERT_TRUE(net->pool->isTemporarilyOffered(ip));
    ASSERT_TRUE(net->lease->getActiveLeases().find(ip) == net->lease->getActiveLeases().end());
}

// Release: Valid IA_PD prefix should should be held temporarily
TEST_F(Dhcpv6ServerTest, Release_ValidPrefix_TriggersHold)
{
    Dhcpv6Header release = makePacket(Variable::Dhcpv6::Type::release);
    ByteString iaid = generateIAID();

    ByteString id = getClientDUID() + iaid;
    auto prefix = net->prefixPool->allocateTempPrefix(id, net->config->defaultSubnetPrefix, true);
    id += prefix.first;
    net->prefixLease->activateLeaseFromTemp(id, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage);

    addIAPD(iaid, release, {prefix});

    PacketInfo pkt;
    pkt.Layer5.push_back(release);

    expectPacket(Variable::Dhcpv6::Type::reply);
    server->handleDhcpPacket(pkt, iface, false, localAddress);

    ASSERT_TRUE(net->prefixPool->isTemporarilyOffered(prefix.first));
}

// Release: Unknown address should be ignored
TEST_F(Dhcpv6ServerTest, Release_InvalidLease_Ignored)
{
    Dhcpv6Header release = makePacket(Variable::Dhcpv6::Type::release);
    ByteString iaid = generateIAID();
    
    ByteString badIP = ByteString("\x20\x01\x0D\xB8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff", 16);

    addIANA(iaid, release, {badIP});

    PacketInfo pkt;
    pkt.Layer5.push_back(release);
    expectNoPacket();

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

TEST_F(Dhcpv6ServerTest, Release_MissingDUID_Ignored)
{
    Dhcpv6Header release;
    release.type = Variable::Dhcpv6::Type::release;
    release.transactionID = makeTransID();
    addServerID(release);

    ByteString iaid = generateIAID();

    ByteString id = getClientDUID() + iaid;
    ByteString ip = net->pool->allocateTempIP(&id, true);
    id += ip;
    net->lease->activateLeaseFromTemp(ip, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage, &id);

    addIANA(iaid, release, {ip});

    PacketInfo pkt;
    pkt.Layer5.push_back(release);

    expectPacket(Variable::Dhcpv6::Type::reply);
    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

TEST_F(Dhcpv6ServerTest, Release_AuthInvalid_Ignored)
{
    Dhcpv6Header release = makePacket(Variable::Dhcpv6::Type::release);
    ByteString iaid = generateIAID();
    ByteString ip = net->pool->allocateTempIP(nullptr);

    addIANA(iaid, release, {ip});

    enableDelayedAuth("test", 1);
    ByteString badAuth = ByteString("\x00\x01\x00\x01\x00", 5);
    badAuth += Functions::numToByte(1000, 8);
    badAuth += ByteString(16, '\xff');
    release.options.push_back({
        Variable::Dhcpv6::Options::auth,
        Functions::numToByte(badAuth.size(), 2),
        badAuth
    });

    PacketInfo pkt;
    pkt.Layer5.push_back(release);
    expectNoPacket();

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

*/
#pragma endregion
#pragma region Decline
/*

// Decline: Validate IA_NA lease should be marked in decline hold
TEST_F(Dhcpv6ServerTest, Decline_ValidLease_TriggersDeclineHold) 
{
    Dhcpv6Header decline = makePacket(Variable::Dhcpv6::Type::decline);
    ByteString iaid = generateIAID();

    ByteString id = getClientDUID() + iaid;
    ByteString ip = net->pool->allocateTempIP(&id, true);
    id += ip;
    net->lease->activateLeaseFromTemp(ip, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage, &id);

    addIANA(iaid, decline, {ip});

    PacketInfo pkt;
    pkt.Layer5.push_back(decline);

    expectPacket(Variable::Dhcpv6::Type::reply);
    server->handleDhcpPacket(pkt, iface, false, localAddress);

    ASSERT_TRUE(getActiveTimers()[Dhcp::TimerType::DECLINE_HOLD].size() == 1);
}

// Decline: Valid IA_PD prefix should be marked in decline hold
TEST_F(Dhcpv6ServerTest, Decline_ValidPrefix_TriggersDeclineHold)
{
    Dhcpv6Header decline = makePacket(Variable::Dhcpv6::Type::decline);
    ByteString iaid = generateIAID();

    ByteString id = getClientDUID() + iaid;
    auto prefix = net->prefixPool->allocatePrefix(id, net->config->defaultSubnetPrefix, true);
    id += prefix.first;
    net->prefixLease->activateLeaseFromTemp(prefix.first, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage);

    addIAPD(iaid, decline, {prefix});

    PacketInfo pkt;
    pkt.Layer5.push_back(decline);

    expectPacket(Variable::Dhcpv6::Type::reply);
    server->handleDhcpPacket(pkt, iface, false, localAddress);

    ASSERT_TRUE(getActiveTimers()[Dhcp::TimerType::DECLINE_HOLD].size() == 1);
}

// Decline: Missing client DUID should be ignored
TEST_F(Dhcpv6ServerTest, Decline_MissingDUID_Ignored)
{
    Dhcpv6Header decline;
    decline.type = Variable::Dhcpv6::Type::decline;
    decline.transactionID = makeTransID();
    addServerID(decline);
    ByteString iaid = generateIAID();

    ByteString id = getClientDUID() + iaid;
    ByteString ip = net->pool->allocateTempIP(&id, true);
    id += ip;
    net->lease->activateLeaseFromTemp(ip, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage, &id);

    PacketInfo pkt;
    pkt.Layer5.push_back(decline);
    expectNoPacket();
    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Decline: Invalid authentication should be ignored
TEST_F(Dhcpv6ServerTest, Decline_AuthInvalid_Ignored)
{
    Dhcpv6Header decline = makePacket(Variable::Dhcpv6::Type::decline);
    ByteString iaid = generateIAID();

    ByteString id = getClientDUID() + iaid;
    ByteString ip = net->pool->allocateTempIP(&id, true);
    id += ip;
    net->lease->activateLeaseFromTemp(ip, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage, &id);

    addIANA(iaid, decline, {ip});

    enableDelayedAuth("test", 1);
    ByteString badAuth = ByteString("\x00\x01\x00\x01\x00", 5);
    badAuth += Functions::numToByte(1000, 8);
    badAuth += ByteString(16, '\xff');
    decline.options.push_back({
        Variable::Dhcpv6::Options::auth,
        Functions::numToByte(badAuth.size(), 2),
        badAuth
    });

    PacketInfo pkt;
    pkt.Layer5.push_back(decline);
    expectNoPacket();
    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

*/
#pragma endregion
#pragma region Confirm
/*

// Confirm: Valid address on-link should return success
TEST_F(Dhcpv6ServerTest, Confirm_ValidAddress_OnLink_Success)
{
    Dhcpv6Header confirm = makePacket(Variable::Dhcpv6::Type::confirm, false);
    ByteString iaid = generateIAID();

    ByteString id = getClientDUID() + iaid;
    ByteString ip = net->pool->allocateTempIP(&id, true);
    id += ip;
    net->lease->activateLeaseFromTemp(ip, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage, &id);

    addIANA(iaid, confirm, {ip});

    PacketInfo pkt;
    pkt.Layer5.push_back(confirm);
    
    expectPacket(Variable::Dhcpv6::Type::reply, 1, [](const Dhcpv6Header& reply) -> bool {
        for (const auto& opt : reply.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::statusCode &&
                opt.value.substr(0, 2) == Variable::Dhcpv6::Status::success)
                return true;
        }
        return false;
    });

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

*/
#pragma endregion
#pragma region Confirm
/*

// Confirm: Invalid address off-link should return NotOnLink
TEST_F(Dhcpv6ServerTest, Confirm_InvalidAddress_OffLink_Failure)
{
    Dhcpv6Header confirm = makePacket(Variable::Dhcpv6::Type::confirm, false);
    ByteString iaid = generateIAID();

    // Add unrelated IP
    ByteString offLinkIP = ByteString("\xfd\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01", 16);
    addIANA(iaid, confirm, {offLinkIP});

    PacketInfo pkt;
    pkt.Layer5.push_back(confirm);

    expectPacket(Variable::Dhcpv6::Type::reply, 1, [](const Dhcpv6Header& reply) -> bool {
        for (const auto& opt : reply.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::statusCode && 
                opt.value.substr(0, 2) == Variable::Dhcpv6::Status::notOnLink)
                return true;
        }
        return false;
    });

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Confirm: valid prefix on-link should return success
TEST_F(Dhcpv6ServerTest, Confirm_ValidPrefix_OnLink_Success)
{
    Dhcpv6Header confirm = makePacket(Variable::Dhcpv6::Type::confirm, false);
    ByteString iaid = generateIAID();

    ByteString id = getClientDUID() + iaid;
    auto prefix = net->prefixPool->allocateTempPrefix(id, net->config->defaultSubnetPrefix, true);
    id += prefix.first;
    net->prefixLease->activateLeaseFromTemp(prefix.first, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage);

    addIAPD(iaid, confirm, {prefix});

    PacketInfo pkt;
    pkt.Layer5.push_back(confirm);

    expectPacket(Variable::Dhcpv6::Type::reply, 1, [](const Dhcpv6Header& reply) -> bool {
        for (const auto& opt : reply.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::statusCode && 
                opt.value.substr(0, 2) == Variable::Dhcpv6::Status::success)
                return true;
        }
        return false;
    });

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Confirm: Invalidate prefix off-link should return NotOnLink
TEST_F(Dhcpv6ServerTest, Confirm_InvalidPrefix_OffLink_Failure)
{
    Dhcpv6Header confirm = makePacket(Variable::Dhcpv6::Type::confirm, false);
    ByteString iaid = generateIAID();

    ByteString badPrefix = ByteString("\xde\xad\xbe\xef\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00");
    addIAPD(iaid, confirm, {{badPrefix, 58}});

    PacketInfo pkt;
    pkt.Layer5.push_back(confirm);

    expectPacket(Variable::Dhcpv6::Type::reply, 1, [](const Dhcpv6Header& reply) -> bool {
        for (const auto& opt : reply.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::statusCode &&
                opt.value.substr(0, 2) == Variable::Dhcpv6::Status::notOnLink)
                return true;
        }
        return false;
    });

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Confirm missing client DUID should be ignored
TEST_F(Dhcpv6ServerTest, Confirm_MissingDUID_Ignored)
{
    Dhcpv6Header confirm;
    confirm.type = Variable::Dhcpv6::Type::confirm;
    confirm.transactionID = makeTransID();
    ByteString iaid = generateIAID();

    ByteString id = getClientDUID() + iaid;
    auto prefix = net->prefixPool->allocateTempPrefix(id, net->config->defaultSubnetPrefix, true);
    id += prefix.first;
    net->prefixLease->activateLeaseFromTemp(prefix.first, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage);

    addIANA(iaid, confirm, {prefix.first});

    PacketInfo pkt;
    pkt.Layer5.push_back(confirm);
    expectNoPacket();
    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Confirm: Invalid authentication should be ignored
TEST_F(Dhcpv6ServerTest, Confirm_AuthInvalid_Ignored)
{
    Dhcpv6Header confirm = makePacket(Variable::Dhcpv6::Type::confirm, false);
    ByteString iaid = generateIAID();

    ByteString id = getClientDUID() + iaid;
    auto prefix = net->prefixPool->allocateTempPrefix(id, net->config->defaultSubnetPrefix, true);
    id += prefix.first;
    net->prefixLease->activateLeaseFromTemp(prefix.first, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage);
    
    enableDelayedAuth("test", 1);
    ByteString badAuth = ByteString("\x00\x01\x00\x01\x00", 5);
    badAuth += Functions::numToByte(1000, 8);
    badAuth += ByteString(16, '\xff');
    confirm.options.push_back({
        Variable::Dhcpv6::Options::auth,
        Functions::numToByte(badAuth.size(), 2),
        badAuth
    });
    
    PacketInfo pkt;
    pkt.Layer5.push_back(confirm);
    expectNoPacket();
    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Confirm: Clients that exceed rate limit should be denied
TEST_F(Dhcpv6ServerTest, Confirm_RateLimited_Denied)
{
    Dhcpv6Header confirm = makePacket(Variable::Dhcpv6::Type::confirm, false);
    ByteString iaid = generateIAID();

    ByteString id = getClientDUID() + iaid;
    auto prefix = net->prefixPool->allocateTempPrefix(id, net->config->defaultSubnetPrefix, true);
    id += prefix.first;
    net->prefixLease->activateLeaseFromTemp(prefix.first, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage);

    PacketInfo pkt;
    pkt.Layer5.push_back(confirm);

    // Simulate 5 rapid confirms to hit rate limit
    for (int i = 0; i < 5; ++i)
    {
        expectPacket(Variable::Dhcpv6::Type::confirm);
        server->handleDhcpPacket(pkt, iface, false, localAddress);
    }

    expectNoPacket();
    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

*/
#pragma endregion
#pragma region InformationRequest
/*

// Information-Request: With valid ORO, server responds with options
TEST_F(Dhcpv6ServerTest, InfoRequest_ValidORO_RespondsWithOptions) 
{
    Dhcpv6Header infoReq = makePacket(Variable::Dhcpv6::Type::informationRequest, false);

    // Request DNS and NTP
    ByteString oro = Variable::Dhcpv6::Options::dnsServer + Variable::Dhcpv6::Options::ntpServer;
    infoReq.options.push_back({
        Variable::Dhcpv6::Options::optionRequest,
        Functions::numToByte(oro.size(), 2),
        oro
    });

    PacketInfo pkt;
    pkt.Layer5.push_back(infoReq);

    expectPacket(Variable::Dhcpv6::Type::reply, 1, [](const Dhcpv6Header& reply) -> bool {
        bool hasDNS = false, hasNTP = false;
        for (const auto& opt : reply.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::dnsServer) hasDNS = true;
            if (opt.option == Variable::Dhcpv6::Options::ntpServer) hasNTP = true;
        }
        return hasDNS && hasNTP;
    });

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Information-Request: Without ORO, returns empty or minimal reply
TEST_F(Dhcpv6ServerTest, InfoRequest_MissingORO_RespondsEmpty)
{
    Dhcpv6Header infoReq = makePacket(Variable::Dhcpv6::Type::informationRequest, false);

    PacketInfo pkt;
    pkt.Layer5.push_back(infoReq);

    expectPacket(Variable::Dhcpv6::Type::reply, 1, [](const Dhcpv6Header& reply) -> bool {
        for (const auto& opt : reply.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::dnsServer || opt.option == Variable::Dhcpv6::Options::ntpServer)
                return false;
        }
        return true;
    });

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

*/
#pragma endregion
#pragma region RelayForward
/*

// Relay-Forward: With valid relay message, handles inner packet
TEST_F(Dhcpv6ServerTest, RelayForward_WithRelayMsg_HandlesInnerPacket)
{
    Dhcpv6Header solicit = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString iaid = generateIAID();
    addIANA(iaid, solicit);

    auto innerPacket = solicit.encapsulate();
    Dhcpv6RelayHeader relay;
    relay.msgType = Variable::Dhcpv6::Type::relayForward;
    relay.hopCount = 0;
    relay.linkAddress = ByteString(16, '\x00');
    relay.peerAddress = ByteString(16, '\x01');
    
    ByteString relayMsg;
    if (innerPacket.has_value())
    {
        relayMsg = innerPacket.value();
    }

    relay.options.push_back({
        Variable::Dhcpv6::Options::relayMsg,
        Functions::numToByte(relayMsg.size(), 2),
        relayMsg
    });

    PacketInfo pkt;
    pkt.Layer5.push_back(relay);

    expectPacket(Variable::Dhcpv6::Type::relayReply);
    server->handleDhcpPacket(pkt, iface, false, localAddress);

    expectPacket(Variable::Dhcpv6::Type::relayReply);
    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Replay-Forward: If relay-message is missing or malformed, packet is ignored.
TEST_F(Dhcpv6ServerTest, RelayForward_InvalidRelayMsg_Ignored)
{
    Dhcpv6RelayHeader relay;
    relay.msgType = Variable::Dhcpv6::Type::relayForward;
    relay.hopCount = 0;
    relay.linkAddress = ByteString(16, '\x00');
    relay.peerAddress = ByteString(16, '\x01');

    // Missing relay message option
    PacketInfo pkt;
    pkt.Layer5.push_back(relay);

    expectPacket(Variable::Dhcpv6::Type::relayReply, 0);
    expectNoPacket();
    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

*/
#pragma endregion
#pragma region
/*

// Relay-Reply: Sends encapsulated reply using matching relay-forward context
TEST_F(Dhcpv6ServerTest, RelayReply_SendsEncapsulatedResponse)
{
    Dhcpv6Header request = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString iaid = generateIAID();
    addIANA(iaid, request);

    auto relayValue = request.encapsulate();
    ByteString relayMsg;
    if (relayValue.has_value())
    {
        relayMsg = relayValue.value();
    }
    Dhcpv6RelayHeader relay;
    relay.msgType = Variable::Dhcpv6::Type::relayForward;
    relay.hopCount = 0;
    relay.linkAddress = ByteString(16, '\x00');
    relay.peerAddress = ByteString(16, '\x01');
    relay.options.push_back({
        Variable::Dhcpv6::Options::relayMsg,
        Functions::numToByte(relayMsg.size(), 2),
        relayMsg
    });

    PacketInfo pkt;
    pkt.Layer5.push_back(relay);

    expectPacket(Variable::Dhcpv6::Type::relayReply);
    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Relay-Reply: Unknown or unmatched transaction ID should be ignored
TEST_F(Dhcpv6ServerTest, RelayReply_UnknownTransactionID_Ignored)
{
    Dhcpv6Header reply;
    reply.type = Variable::Dhcpv6::Type::relayReply;
    reply.transactionID = makeTransID();

    // Simulate no inner context
    PacketInfo pkt;
    pkt.Layer5.push_back(reply);

    expectPacket(Variable::Dhcpv6::Type::relayReply, 0);
    expectNoPacket();
    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

TEST_F(Dhcpv6ServerTest, RelayReply_InterfaceResolution_Success)
{

}

TEST_F(Dhcpv6ServerTest, RelayReply_InterfaceNotFound_Ignored) 
{

}

*/
#pragma endregion
#pragma region Authentication
/*

// Auth: Valid MAC passes verification
TEST_F(Dhcpv6ServerTest, Auth_ValidMAC_Passes)
{
    Dhcpv6Header solicit = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString iaid = generateIAID();
    addIANA(iaid, solicit);

    // Enable authentication and sign the packet
    ByteString secret = "authKey";
    enableDelayedAuth(secret, 1);
    ByteString duid = getClientDUID();

    buildAuthOption(solicit, duid);

    PacketInfo pkt;
    pkt.Layer5.push_back(solicit);
    expectPacket(Variable::Dhcpv6::Type::advertise);

    server->handleDhcpPacket(pkt, iface, true, localAddress);
}

// Auth: Invalid MAC fails verification
TEST_F(Dhcpv6ServerTest, Auth_InvalidMAC_Fails)
{
    Dhcpv6Header solicit = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString iaid = generateIAID();
    addIANA(iaid, solicit);

    enableDelayedAuth("authKey", 1);

    // Add broken MAC
    ByteString badAuth = ByteString("\x00\x01\x00\x01\x00", 5);
    badAuth += Functions::numToByte(1, 8);
    badAuth += ByteString(16, '\xAA');

    solicit.options.push_back({
        Variable::Dhcpv6::Options::auth,
        Functions::numToByte(badAuth.size(), 2),
        badAuth
    });

    PacketInfo pkt;
    pkt.Layer5.push_back(solicit);
    expectNoPacket();

    server->handleDhcpPacket(pkt, iface, true, localAddress);
}

// Auth: Invalid protocol or algorithm fails
TEST_F(Dhcpv6ServerTest, Auth_InvalidProtocolOrAlgo_Fails)
{
    Dhcpv6Header solicit = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString iaid = generateIAID();
    addIANA(iaid, solicit);

    enableDelayedAuth("authkey", 1);

    // Use invalid protocol and algo
    ByteString badAuth = ByteString("\x00\xFF\x00\xFF\x00", 5);
    badAuth += Functions::numToByte(1, 8);
    badAuth += ByteString(16, '\xBB');

    solicit.options.push_back({
        Variable::Dhcpv6::Options::auth,
        Functions::numToByte(badAuth.size(), 2),
        badAuth
    });

    PacketInfo pkt;
    pkt.Layer5.push_back(solicit);
    expectNoPacket();

    server->handleDhcpPacket(pkt, iface, true, localAddress);
}

// Auth: Reusing an older reply counter should fail verification
TEST_F(Dhcpv6ServerTest, Auth_ReplayCounterTooLow_Fails)
{
    Dhcpv6Header solicit = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString iaid = generateIAID();
    addIANA(iaid, solicit);

    // Enable auth and generate a valid signed packet
    ByteString secret = "authKey";
    enableDelayedAuth(secret, 1);
    ByteString duid = getClientDUID();

    // First valid packet
    Dhcpv6Header first = solicit;
    PacketInfo pkt;
    buildAuthOption(solicit, duid);
    pkt.Layer5.push_back(first);
    expectPacket(Variable::Dhcpv6::Type::advertise);
    server->handleDhcpPacket(pkt, iface, true, localAddress);

    // Now reuse the same reply counter, which is too low
    PacketInfo pkt2;
    pkt2.Layer5.push_back(solicit);
    expectNoPacket();
    server->handleDhcpPacket(pkt2, iface, false, localAddress);
}

// Auth: Valid increasing relay counter should pass and update internal coutner
TEST_F(Dhcpv6ServerTest, Auth_ReplayCounterAccepted_UpdatesCounter)
{
    Dhcpv6Header solicit = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString iaid = generateIAID();
    addIANA(iaid, solicit);

    // Enable authentication
    ByteString secret = "authKey";
    enableDelayedAuth(secret, 1);
    ByteString duid = getClientDUID();

    // First packet
    Dhcpv6Header first = solicit;
    buildAuthOption(solicit, duid);
    PacketInfo pkt1;
    pkt1.Layer5.push_back(first);
    expectPacket(Variable::Dhcpv6::Type::advertise);
    server->handleDhcpPacket(pkt1, iface, true, localAddress);

    // Generate another packet, which should have a higher replay counter
    Dhcpv6Header second = solicit;
    buildAuthOption(second, duid);
    PacketInfo pkt2;
    pkt2.Layer5.push_back(second);
    expectPacket(Variable::Dhcpv6::Type::advertise);
    server->handleDhcpPacket(pkt2, iface, true, localAddress);
}

// Auth: buildAuthenticationOption() should return a valid authentication option
TEST_F(Dhcpv6ServerTest, Auth_BuildAuthOption_ProducesValidOption)
{
    enableDelayedAuth("testKey", 1);

    Dhcpv6Header auth;
    buildAuthOption(auth, getClientDUID());

    ASSERT_EQ(auth.options.begin()->option, Variable::Dhcpv6::Options::auth);
    ASSERT_GE(auth.options.begin()->length.size(), 2);
    ASSERT_GE(auth.options.begin()->value.size(), 25);

    // Protocol = 0x0001 (delayed), Algorithm = 0x0001 (HMAC-MD5), RDM = 0x00
    EXPECT_EQ(auth.options.begin()->value[0], 0x00);
    EXPECT_EQ(auth.options.begin()->value[1], 0x01);
    EXPECT_EQ(auth.options.begin()->value[2], 0x00);
    EXPECT_EQ(auth.options.begin()->value[3], 0x01);
    EXPECT_EQ(auth.options.begin()->value[4], 0x00);

    // MAC is the last 16 bytes
    ByteString mac = std::string(auth.options.begin()->value.end() - 16, auth.options.begin()->value.end());
    EXPECT_EQ(mac.size(), 16);
}

// Auth: When authentication is enabled, server replies should include the auth option.
TEST_F(Dhcpv6ServerTest, Auth_ReplyIncludesAuthOption)
{
    Dhcpv6Header request = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString iaid = generateIAID();
    addIANA(iaid, request);

    // Enable authentication and signe request
    enableDelayedAuth("testKey", 1);
    buildAuthOption(request, getClientDUID());

    PacketInfo pkt;
    pkt.Layer5.push_back(request);

    expectPacket(Variable::Dhcpv6::Type::advertise, 1, [](const Dhcpv6Header& reply) -> bool {
        for (const auto& opt : reply.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::auth)
                return true;
        }
        return false;
    });

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

*/
#pragma endregion
#pragma region Reconfigure
/*

TEST_F(Dhcpv6ServerTest, Reconfigure_SendsReconfigureWithRKAP)
{
    // Enable RKAP
    enableRKAP("rkap-key", 10);

    // Simulte sending Recofnig for client
    ByteString duid = getClientDUID();

    // Expect Reconfigure message to be sent
    expectPacket(Variable::Dhcpv6::Type::reconfigure, 1, [](const Dhcpv6Header& reconfig) -> bool {
        for (const auto& opt : reconfig.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::auth &&
                opt.value.substr(0, 2) == ByteString("\x00\x03", 2))
            {
                return true;
            }
        }
        return false;
    });

    sendReconfigure(duid, Dhcpv6::ReconfigReason::RENEW, false);
}

TEST_F(Dhcpv6ServerTest, ReconfigureAccept_SolicitNotAccepted)
{
    Dhcpv6Header solicit = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString iaid = generateIAID();

    addIANA(iaid, solicit);
    solicit.options.emplace_back(
        Variable::Dhcpv6::Options::reconfAccept,
        Functions::numToByte(0, 2),
        ByteString("")
    );

    PacketInfo packet;
    packet.Layer5.push_back(solicit);
    expectPacket(Variable::Dhcpv6::Type::advertise);
    server->handleDhcpPacket(packet, iface, true, localAddress);

    ASSERT_EQ(getClientAccepts().size(), 0);
}

TEST_F(Dhcpv6ServerTest, ReconfigureAccept_StoresClientAccept)
{
    Dhcpv6Header request = makePacket(Variable::Dhcpv6::Type::request);
    ByteString iaid = generateIAID();

    // Extract clientID
    ByteString id = getClientDUID() + iaid;
    ByteString offeredIP = net->pool->allocateTempIP(&id, true);

    addIANA(iaid, request, {offeredIP});
    request.options.emplace_back(
        Variable::Dhcpv6::Options::reconfAccept,
        Functions::numToByte(0, 2),
        ByteString("")
    );

    PacketInfo pkt;
    pkt.Layer5.push_back(request);

    ASSERT_EQ(getClientAccepts().size(), 1);
}

TEST_F(Dhcpv6ServerTest, Reconfigure_WithoutAuth_Ignored)
{
    Dhcpv6Header reconfig;
    reconfig.type = Variable::Dhcpv6::Type::reconfigure;
    reconfig.transactionID = makeTransID();
    addClientID(getClientDUID(), reconfig);
    addServerID(reconfig);

    PacketInfo pkt;
    pkt.Layer5.push_back(reconfig);

    expectNoPacket();
    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

TEST_F(Dhcpv6ServerTest, Reconfigure_InvalidTimestamp_Ignored)
{
    enableRKAP("rkap-key", 5);

    Dhcpv6Header reconfig;
    reconfig.type = Variable::Dhcpv6::Type::reconfigure;
    reconfig.transactionID = makeTransID();
    addClientID(getClientDUID(), reconfig);
    addServerID(reconfig);

    // Insert invalid Protocol 3 auth with old timestamp
    ByteString auth;
    auth += ByteString("\x00\x03\x00\x01\x00", 5); // Protocol 3, SHA1, RDM = 0
    auth += Functions::numToByte(1, 8);
    auth += ByteString(16, 0x00);
    
    reconfig.options.emplace_back(
        Variable::Dhcpv6::Options::auth,
        Functions::numToByte(auth.size(), 2),
        auth
    );

    PacketInfo pkt;
    pkt.Layer5.push_back(reconfig);

    expectNoPacket();
    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

TEST_F(Dhcpv6ServerTest, Reconfigure_ExpiredKey_Ignored)
{
    // Insert expired key
    enableRKAP("expired-key", 10, 61);

    Dhcpv6Header reconfig;
    reconfig.type = Variable::Dhcpv6::Type::reconfigure;
    reconfig.transactionID = makeTransID();
    addClientID(getClientDUID(), reconfig);
    addServerID(reconfig);

    // Insert matching Protocol 3 auth
    ByteString auth;
    auth += ByteString("\x00\x03\x00\x01\x00", 5);
    auth += Functions::numToByte(secondsSinceEpoch(), 8);
    auth += ByteString(16, 0x00);

    reconfig.options.emplace_back(
        Variable::Dhcpv6::Options::auth,
        Functions::numToByte(auth.size(), 2),
        auth
    );

    PacketInfo pkt;
    pkt.Layer5.push_back(reconfig);

    expectNoPacket();
    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

TEST_F(Dhcpv6ServerTest, Reconfigure_RelayEncapsulation_SentProperly)
{
    // Assume relay headers stored for client
}

*/
#pragma endregion
#pragma region Timer
/*

// Timer: After offer timeout, temporary leases should expire and be removed
TEST_F(Dhcpv6ServerTest, Timer_OfferTimeout_ExpiresLease)
{
    ByteString iaid = generateIAID();
    ByteString id = getClientDUID() + iaid;
    ByteString tempIP = net->pool->allocateTempIP(&id, true);

    // Schedule offer timeout
    server->scheduleTimeout(Dhcp::TimerType::IP_OFFER_TIMEOUT, id + tempIP, tempIP, net->config->getNetworkID(), 1);

    // Confirm it exists
    ASSERT_TRUE(net->pool->isTemporarilyOffered(tempIP));

    // Fast Forwart
    std::this_thread::sleep_for(std::chrono::seconds(2));

    ASSERT_FALSE(net->pool->isTemporarilyOffered(tempIP));
}

// Timer: Client request timeout should expire a lease if no confirmation is received
TEST_F(Dhcpv6ServerTest, Timer_ClientRequestTimeout_ExpiresLease)
{
    ByteString iaid = generateIAID();
    ByteString id = getClientDUID() + iaid;
    ByteString tempIP = net->pool->allocateTempIP(&id, true);

    // Schedule client request timeout
    server->scheduleTimeout(Dhcp::TimerType::CLIENT_REQUEST_TIMEOUT, id + tempIP, tempIP, net->config->getNetworkID(), 1);

    ASSERT_TRUE(net->pool->isTemporarilyOffered(tempIP));

    // Wait for timer to expire
    std::this_thread::sleep_for(std::chrono::seconds(2));

    ASSERT_FALSE(net->pool->isTemporarilyOffered(tempIP));
}

// Timer: Declined addresses should be held temporarily and unavailable for reuse
TEST_F(Dhcpv6ServerTest, Timer_DeclineHold_PreventsReuse)
{
    ByteString iaid = generateIAID();
    ByteString id = getClientDUID() + iaid;
    ByteString ip = net->pool->allocateTempIP(&id, true);
    id += ip;
    net->lease->activateLeaseFromTemp(ip, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage, &id);

    // Trigger decline hold
    server->scheduleTimeout(Dhcp::TimerType::DECLINE_HOLD, id, ip, net->config->getNetworkID(), 1);

    ASSERT_TRUE(net->pool->isTemporarilyOffered(ip));

    // Wait for timer to expire
    std::this_thread::sleep_for(std::chrono::seconds(2));

    ASSERT_FALSE(net->pool->isTemporarilyOffered(ip));
}

// Timer: Release address should be held temporarily before being reused
TEST_F(Dhcpv6ServerTest, Timer_ReleaseHold_PreventsReuse)
{
    ByteString iaid = generateIAID();
    ByteString id = getClientDUID() + iaid;
    ByteString ip = net->pool->allocateTempIP(&id, true);
    id += ip;
    net->lease->activateLeaseFromTemp(ip, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage, &id);

    // Trigger release hold
    server->scheduleTimeout(Dhcp::TimerType::RELEASE_HOLD, id, ip, net->config->getNetworkID(), 1);

    ASSERT_TRUE(net->pool->isTemporarilyOffered(ip));

    // Wait for timer to expire
    std::this_thread::sleep_for(std::chrono::seconds(2));

    ASSERT_FALSE(net->pool->isTemporarilyOffered(ip));
}

// Timer: All timer types should work in parallel without interfering
TEST_F(Dhcpv6ServerTest, Timer_AllTimers_WorkInParallel)
{
    ByteString baseClient = getClientDUID();
    ByteString client1 = baseClient;
    ByteString client2 = baseClient;
    client1[0] = '1'; // Make client ids different;

    ByteString iaid1 = generateIAID();
    ByteString iaid2 = generateIAID();

    ByteString id1 = client1 + iaid1;
    ByteString id2 = client2 + iaid2;

    ByteString ip1 = net->pool->allocateTempIP(&id1, true);
    ByteString ip2 = net->pool->allocateTempIP(&id2, true);

    id1 += ip1;
    id2 += ip2;

    net->lease->activateLeaseFromTemp(ip1, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage, &id1);
    net->lease->activateLeaseFromTemp(ip2, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage, &id2);

    // Schedule different timers for different clients
    server->scheduleTimeout(Dhcp::TimerType::IP_OFFER_TIMEOUT, id1, ip1, net->config->getNetworkID(), 1);
    server->scheduleTimeout(Dhcp::TimerType::RELEASE_HOLD, id2, ip2, net->config->getNetworkID(), 1);

    // Ensure both are active
    ASSERT_EQ(getActiveTimers()[Dhcp::TimerType::IP_OFFER_TIMEOUT].size(), 1);
    ASSERT_EQ(getActiveTimers()[Dhcp::TimerType::RELEASE_HOLD].size(), 1);

    // Run all timers (simulate passage of time)
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Verify they were cleared after execution
    ASSERT_EQ(getActiveTimers()[Dhcp::TimerType::IP_OFFER_TIMEOUT].size(), 0);
    ASSERT_EQ(getActiveTimers()[Dhcp::TimerType::RELEASE_HOLD].size(), 0);
}

// Timer: Cancel timeout should remove the correct timer
TEST_F(Dhcpv6ServerTest, Timer_CancelTimeout_StopsCorrectly)
{
    ByteString iaid = generateIAID();
    ByteString id = getClientDUID() + iaid;
    ByteString ip = net->pool->allocateTempIP(&id, true);
    id += ip;
    net->lease->activateLeaseFromTemp(ip, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage, &id);

    // Schedule a timer
    server->scheduleTimeout(Dhcp::TimerType::CLIENT_REQUEST_TIMEOUT, id, ip, net->config->getNetworkID(), 1);

    ASSERT_EQ(getActiveTimers()[Dhcp::TimerType::CLIENT_REQUEST_TIMEOUT].size(), 10);

    // Cancel the timer
    server->cancelTimeout(Dhcp::TimerType::CLIENT_REQUEST_TIMEOUT, id, ip);

    // Should not be empty
    ASSERT_EQ(getActiveTimers()[Dhcp::TimerType::CLIENT_REQUEST_TIMEOUT].size(), 0);
}

*/
#pragma endregion
#pragma region Status
/*

// Status: Success code is stored in per-client state
TEST_F(Dhcpv6ServerTest, StatusCode_Success_StoredPerClient)
{
    Dhcpv6Header request = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString iaid = generateIAID();
    addIANA(iaid, request);

    PacketInfo pkt;
    pkt.Layer5.push_back(request);
    expectPacket(Variable::Dhcpv6::Type::advertise, 1, [&](const Dhcpv6Header& pkt) {
        for (const auto& opt : pkt.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::statusCode &&
                opt.value.substr(0, 2) == Variable::Dhcpv6::Status::success)
                return true;
        }
        return false;
    });

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Status: NotOnLink code appears in Confirm if address is off-link
TEST_F(Dhcpv6ServerTest, StatusCode_NotOnLink_HandledCorrectly)
{
    Dhcpv6Header confirm = makePacket(Variable::Dhcpv6::Type::confirm, false);
    ByteString iaid = generateIAID();

    ByteString badIP = ByteString("\xfd\xff\xff\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01", 16);
    addIANA(iaid, confirm, {badIP});

    PacketInfo pkt;
    pkt.Layer5.push_back(confirm);

    expectPacket(Variable::Dhcpv6::Type::reply, 1, [](const Dhcpv6Header& reply) -> bool {
        for (const auto& opt : reply.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::statusCode &&
                opt.value.substr(0, 2) == Variable::Dhcpv6::Status::notOnLink)
                return true;
        }
        return false;
    });

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Status: noAddrsAvail should return if address pool is exausted
TEST_F(Dhcpv6ServerTest, StatusCode_NoAddrsAvail_ReturnedProperly)
{
    getNetworks().begin()->second->pool->allExcluded = true;

    Dhcpv6Header solicit = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString iaid = generateIAID();
    addIANA(iaid, solicit);

    PacketInfo pkt;
    pkt.Layer5.push_back(solicit);
    expectPacket(Variable::Dhcpv6::Type::advertise, 1, [](const Dhcpv6Header& pkt) -> bool {
        for (const auto& opt : pkt.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::statusCode &&
                opt.value.substr(0, 2) == Variable::Dhcpv6::Status::noAddrsAvail)
                return true;
        }
        return false;
    });

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Status: noBinding should appear on RENEW/REBIND when lease is unknown
TEST_F(Dhcpv6ServerTest, StatusCode_NoBinding_ReturnedProperly)
{
    Dhcpv6Header renew = makePacket(Variable::Dhcpv6::Type::renew);
    ByteString iaid = generateIAID();
    ByteString fakeIP = ByteString("\x20\x01\x0d\xb8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01", 16);

    addIANA(iaid, renew, {fakeIP});
    PacketInfo pkt;
    pkt.Layer5.push_back(renew);

    expectPacket(Variable::Dhcpv6::Type::reply, 1, [](const Dhcpv6Header& reply) -> bool {
        for (const auto& opt : reply.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::statusCode &&
                opt.value.substr(0, 2) == Variable::Dhcpv6::Status::noBinding)
                return true;
        }
        return false;
    });

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Status: noPrefixAvail should be returned if no prefix can be assigned
TEST_F(Dhcpv6ServerTest, StatusCode_NoPrefixAvail_ReturnedProperly)
{
    getNetworks().begin()->second->prefixPool->allExcluded = true;

    Dhcpv6Header solicit = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString iaid = generateIAID();
    addIAPD(iaid, solicit);

    PacketInfo pkt;
    pkt.Layer5.push_back(solicit);
    
    expectPacket(Variable::Dhcpv6::Type::advertise, 1, [](const Dhcpv6Header& pkt) -> bool {
        for (const auto& opt : pkt.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::statusCode &&
                opt.value.substr(0, 2) == Variable::Dhcpv6::Status::noPrefixAvail)
                return true;
        }
        return false;
    });

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Status: Missing status code shoule be handled gracefully
TEST_F(Dhcpv6ServerTest, StatusCode_Missing_Ignored)
{
    // Prepare a reply packet without status code
    Dhcpv6Header reply;
    reply.type = Variable::Dhcpv6::Type::reply;
    reply.transactionID = makeTransID();
    addClientID(getClientDUID(), reply);
    addServerID(reply);

    PacketInfo pkt;
    pkt.Layer5.push_back(reply);

    // Should not enqueue a response, but must not crash
    expectNoPacket();
    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Status: Validates correct status code returned on Confirm success
TEST_F(Dhcpv6ServerTest, StatusCode_ValidatesOnConfirm)
{
    Dhcpv6Header confirm = makePacket(Variable::Dhcpv6::Type::confirm, false);
    ByteString iaid = generateIAID();

    // Lease and IP
    ByteString id = getClientDUID() + iaid;
    ByteString ip = net->pool->allocateTempIP(&id, true);
    id += ip;
    net->lease->activateLeaseFromTemp(ip, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage, &id);

    addIANA(iaid, confirm, {ip});

    PacketInfo pkt;
    pkt.Layer5.push_back(confirm);

    // Expect reply with "success" status code
    expectPacket(Variable::Dhcpv6::Type::reply, 1, [](const Dhcpv6Header& pkt) -> bool {
        for (const auto& opt : pkt.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::statusCode &&
                opt.value.substr(0, 2) == Variable::Dhcpv6::Status::success)
                return true;
        }
        return false;
    });

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Status: Per-Client status codes are stored correctly
TEST_F(Dhcpv6ServerTest, StatusCode_StoredInClientMap)
{
    Dhcpv6Header confirm = makePacket(Variable::Dhcpv6::Type::confirm, false);
    ByteString iaid = generateIAID();

    ByteString id = getClientDUID() + iaid;
    ByteString ip = net->pool->allocateTempIP(&id, true);
    id += ip;
    net->lease->activateLeaseFromTemp(ip, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage, &id);

    addIANA(iaid, confirm, {ip});

    PacketInfo pkt;
    server->handleDhcpPacket(pkt, iface, false, localAddress);

    // Verify last status for the client was stored
    // TODO
}

*/
#pragma endregion
#pragma region ServerID
/*

// DUID: Server should extract DUID from client ID option correctly
TEST_F(Dhcpv6ServerTest, DUID_ExtractedCorrectly)
{
    Dhcpv6Header header;
    ByteString duid = getClientDUID();

    addClientID(duid, header);
    ByteString extracted = extractDUID(header);

    ASSERT_EQ(extracted, duid);
}

// DUID: Missing client id should result in empty DUID
TEST_F(Dhcpv6ServerTest, DUID_Missing_ReturnsEmpty)
{
    Dhcpv6Header header;

    auto extracted = extractDUID(header);
    ASSERT_EQ(extracted.size(), 0);
}

// ServerID: ServerID in packet should patch local DUID
TEST_F(Dhcpv6ServerTest, ServerID_Valid_Matches)
{
    Dhcpv6Header header;
    addServerID(header);

    ASSERT_TRUE(validateDUID(header));
}

// ServerID: Invalid server ID in packet should be rejected
TEST_F(Dhcpv6ServerTest, ServerID_Invalid_Rejected)
{
    Dhcpv6Header header;

    ByteString fakeID = ByteString("\x00\02\xde\xad\xbe\xef", 6);
    header.options.push_back({
        Variable::Dhcpv6::Options::serverID,
        Functions::numToByte(fakeID.size(), 2),
        fakeID
    });

    ASSERT_FALSE(validateDUID(header));
}

// DUID: Server generates DUID of tyep LL (link-layer)
TEST_F(Dhcpv6ServerTest, GenerateUniqueIdentifier_ReturnsDUID_LL)
{
    ByteString duid = getDuid();

    // DUID-LL starts with 00:03
    ASSERT_GE(duid.size(), 4);
    ASSERT_EQ(duid[0], 0x00);
    ASSERT_EQ(duid[1], 0x03);
}

*/
#pragma endregion
#pragma region Internal
/*

// Internal: extractIA_NA should parse IA_NA options into IA structure correctly
TEST_F(Dhcpv6ServerTest, ExtractIA_NA_ParsesCorrectly)
{
    Dhcpv6Header header;
    ByteString iaid = generateIAID();
    addIANA(iaid, header);
    
    auto result = extractIANA(header);
    ASSERT_EQ(result.size(), 1);
    ASSERT_EQ(result[0].iaid, iaid);
}

// Internal: ExtractIA_PD should parse IA_PD options into IA structure correctly
TEST_F(Dhcpv6ServerTest, ExtractIA_PD_ParsesCorrectly)
{
    Dhcpv6Header header;
    ByteString iaid = generateIAID();
    addIAPD(iaid, header);

    auto result = extractIAPD(header);
    ASSERT_EQ(result.size(), 1);
    ASSERT_EQ(result[0].iaid, iaid);
}

// Internal: extractIA_TA should parse IA_TA options into IA structure correctly
TEST_F(Dhcpv6ServerTest, ExtractIA_TA_ParsesCorrectly)
{
    Dhcpv6Header header;
    ByteString iaid = generateIAID();
    addIATA(iaid, header);

    auto result = extractIATA(header);
    ASSERT_EQ(result.size(), 1);
    ASSERT_EQ(result[0].iaid, iaid);
}

// Internal: extractORO should parse Option Request Option (ORO) into list of requested codes
TEST_F(Dhcpv6ServerTest, ExtractORO_ParsesCorrectly)
{
    Dhcpv6Header header;
    ByteString oro = Variable::Dhcpv6::Options::dnsServer + Variable::Dhcpv6::Options::dnsServer;

    header.options.push_back({
        Variable::Dhcpv6::Options::optionRequest,
        Functions::numToByte(oro.size(), 2),
        oro
    });

    auto result = extractORO(header);
    ASSERT_EQ(result.size(), 2);
    ASSERT_EQ(result[0], Variable::Dhcpv6::Options::dnsServer);
    ASSERT_EQ(result[1], Variable::Dhcpv6::Options::ntpServer);
}

// Internal: buildResponse should include IA_NA with status code if provided
TEST_F(Dhcpv6ServerTest, BuildResponse_IA_NA_WithStatus)
{
    ByteString iaid = generateIAID();

    Dhcpv6::IANABlock block;
    ByteString id = getClientDUID() + block.iaid;
    ByteString ip = block.network->lease->allocateIP(
        block.network->config->leaseTime.load(std::memory_order_relaxed),
        block.network->config->t1Percentage.load(std::memory_order_relaxed), 
        block.network->config->t2Percentage.load(std::memory_order_relaxed),
        &id, true);
    block.t1 = net->config->t1Percentage;
    block.t2 = net->config->t2Percentage;
    block.iaid = iaid;
    block.addresses.push_back(ip);
    block.network = net;

    ByteString status = Variable::Dhcpv6::Status::success;

    Dhcpv6Header reply = buildResponse(Variable::Dhcpv6::Type::reply, generateDhcpTransid(), getDuid(), {block}, {}, {});

    bool foundStatus = false;
    for (const auto& opt : reply.options)
    {
        if (opt.option == Variable::Dhcpv6::Options::IA_NA &&
            opt.value.find(status) != std::string::npos)
        {
            foundStatus = true;
            break;
        }
    }
    ASSERT_TRUE(foundStatus);
}

// Internal: buildResponse should include IA_PD wih status code if provided
TEST_F(Dhcpv6ServerTest, BuildResponse_IA_PD_WithStatus)
{
    ByteString iaid = generateIAID();

    Dhcpv6::IAPDBlock block;
    ByteString id = getClientDUID() + block.iaid;
    std::pair<ByteString, uint8_t> prefix = {ByteString("\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 16), 64};
    block.t1 = net->config->t1Percentage;
    block.t2 = net->config->t2Percentage;
    block.iaid = iaid;
    block.prefixes.push_back(prefix);
    block.network = net;

    ByteString status = Variable::Dhcpv6::Status::noPrefixAvail;

    Dhcpv6Header reply = buildResponse(Variable::Dhcpv6::Type::reply, generateDhcpTransid(), getDuid(), {}, {block}, {});

    bool foundStatus = false;
    for (const auto& opt : reply.options)
    {
        if (opt.option == Variable::Dhcpv6::Options::IA_NA &&
            opt.value.find(status) != std::string::npos)
        {
            foundStatus = true;
            break;
        }
    }
    ASSERT_TRUE(foundStatus);
}

// Internal: buildResponse should include IA_TA with status code if provided
TEST_F(Dhcpv6ServerTest, BuildResponse_IA_TA_WithStatus) 
{
    ByteString iaid = generateIAID();

    Dhcpv6::IANABlock block;
    ByteString id = getClientDUID() + block.iaid;
    ByteString ip = block.network->lease->allocateIP(
        block.network->config->leaseTime.load(std::memory_order_relaxed),
        block.network->config->t1Percentage.load(std::memory_order_relaxed), 
        block.network->config->t2Percentage.load(std::memory_order_relaxed),
        &id, true);
    block.iaid = iaid;
    block.addresses.push_back(ip);
    block.network = net;

    ByteString status = Variable::Dhcpv6::Status::success;

    Dhcpv6Header reply = buildResponse(Variable::Dhcpv6::Type::reply, generateDhcpTransid(), getDuid(), {}, {}, {block});

    bool foundStatus = false;
    for (const auto& opt : reply.options)
    {
        if (opt.option == Variable::Dhcpv6::Options::IA_NA &&
            opt.value.find(status) != std::string::npos)
        {
            foundStatus = true;
            break;
        }
    }
    ASSERT_TRUE(foundStatus);
}

// Internal: buildResponse should always include server ID option
TEST_F(Dhcpv6ServerTest, BuildResponse_IncludesServerID)
{
    Dhcpv6Header reply = buildResponse(Variable::Dhcpv6::Type::solicit, generateDhcpTransid(), getDuid(), {}, {}, {});

    bool found = false;
    for (const auto& opt : reply.options)
    {
        if (opt.option == Variable::Dhcpv6::Options::serverID)
        {
            found = true;
            break;
        }
    }

    ASSERT_TRUE(found);
}

// Internal: buildResponse should include auth option if enabled
TEST_F(Dhcpv6ServerTest, BuildResponse_IncludesAuth_WhenEnabled)
{
    enableDelayedAuth("auth-key", 1);

    Dhcpv6Header reply = buildResponse(Variable::Dhcpv6::Type::solicit, generateDhcpTransid(), getDuid(), {}, {}, {});

    bool found = false;
    for (const auto& opt : reply.options)
    {
        if (opt.option == Variable::Dhcpv6::Options::auth)
        {
            found = true;
            break;
        }
    }

    ASSERT_TRUE(found);
}

// Internal: buildOptions should include DNS options if requested via ORO
TEST_F(Dhcpv6ServerTest, BuildOptions_DNSIncluded_WhenRequested)
{
    std::vector<ByteString> oro = {Variable::Dhcpv6::Options::dnsServer};
    auto options = buildOptions(net->config, oro);

    bool foundDNS = false;
    for (const auto& opt : options)
    {
        if (opt.option == Variable::Dhcpv6::Options::dnsServer)
        {
            foundDNS = true;
            break;
        }
    }

    ASSERT_TRUE(foundDNS);
}

// Internal: buildOptions should include Domain Search List if requested
TEST_F(Dhcpv6ServerTest, BuildOptions_DomainIncluded_WhenRequested)
{
    std::vector<ByteString> oro = {Variable::Dhcpv6::Options::domainSearch};
    auto options = buildOptions(net->config, oro);

    bool foundDomain = false;
    for (const auto& opt : options)
    {
        if (opt.option == Variable::Dhcpv6::Options::domainSearch)
        {
            foundDomain = true;
            break;
        }
    }

    ASSERT_TRUE(foundDomain);
}

// Internal: buildOptions should include NTP srever if requested
TEST_F(Dhcpv6ServerTest, BuildOptions_NTPIncluded_WhenRequested)
{
    std::vector<ByteString> oro = {Variable::Dhcpv6::Options::ntpServer};
    auto options = buildOptions(net->config, oro);

    bool foundNTP = false;
    for (const auto& opt : options)
    {
        if (opt.option == Variable::Dhcpv6::Options::ntpServer)
        {
            foundNTP = true;
            break;
        }
    }

    ASSERT_TRUE(foundNTP);
}

*/
#pragma endregion
#pragma region Scenario
/*

// Scenario: Solicit followed by Request should result in confirmed lease
TEST_F(Dhcpv6ServerTest, Scenario_SolicitThenRequest_LeaseConfirmed)
{
    ByteString iaid = generateIAID();
    Dhcpv6Header solicit = makePacket(Variable::Dhcpv6::Type::solicit, false);
    addIANA(iaid, solicit);
    solicit.options.push_back({
        Variable::Dhcpv6::Options::rapidCommit,
        Functions::numToByte(0, 2),
        {}
    });

    PacketInfo pkt1;
    pkt1.Layer5.push_back(solicit);
    expectPacket(Variable::Dhcpv6::Type::reply);
    server->handleDhcpPacket(pkt1, iface, true, localAddress);

    Dhcpv6Header request = makePacket(Variable::Dhcpv6::Type::request);
    ByteString id = getClientDUID() + iaid;
    ByteString offered = net->pool->allocateTempIP(&id, true);
    addIANA(iaid, request, {offered});

    PacketInfo pkt2;
    pkt2.Layer5.push_back(request);
    expectPacket(Variable::Dhcpv6::Type::reply);
    server->handleDhcpPacket(pkt2, iface, false, localAddress);

    ASSERT_EQ(net->lease->getActiveLeases().size(), 1);
}

// Scenario: Request without prior Solicit still allocates lease
TEST_F(Dhcpv6ServerTest, Scenario_RequestWithoutSolicit_StillAllocates)
{
    ByteString iaid = generateIAID();
    Dhcpv6Header request = makePacket(Variable::Dhcpv6::Type::request);
    ByteString id = getClientDUID() + iaid;
    ByteString ip = net->pool->allocateTempIP(&id, true);

    addIANA(iaid, request, {ip});

    PacketInfo pkt;
    pkt.Layer5.push_back(request);
    expectPacket(Variable::Dhcpv6::Type::reply);
    server->handleDhcpPacket(pkt, iface, false, localAddress);

    ASSERT_EQ(net->lease->getActiveLeases().size(), 1);
}

// Scenario: Solicit with unavailable address should return NoAddrsAvail
TEST_F(Dhcpv6ServerTest, Scenario_SolicitWithUnavailableAddress_NoAddrsAvail)
{
    ByteString iaid = generateIAID();
    Dhcpv6Header solicit = makePacket(Variable::Dhcpv6::Type::solicit, false);
    addIANA(iaid, solicit);

    getNetworks().begin()->second->pool->allExcluded = true;

    PacketInfo pkt;
    pkt.Layer5.push_back(solicit);

    expectPacket(Variable::Dhcpv6::Type::advertise, 1, [](const Dhcpv6Header& reply) {
        for (const auto& opt : reply.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::statusCode &&
                opt.value.substr(0, 2) == Variable::Dhcpv6::Status::noAddrsAvail)
                return true;
        }
        return false;
    });

    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Scenario: Simultaneous clients should get different IPs
TEST_F(Dhcpv6ServerTest, Scenario_SimultaneousClients_GetDifferentIPs)
{
    ByteString iaid1 = generateIAID();
    ByteString iaid2 = generateIAID();

    Dhcpv6Header solicit1 = makePacket(Variable::Dhcpv6::Type::solicit, false);
    solicit1.options[0].value[0] = '1'; // Different DUID
    addIANA(iaid1, solicit1);

    Dhcpv6Header solicit2 = makePacket(Variable::Dhcpv6::Type::solicit, false);
    solicit2.options[0].value[0] = '2'; // Different DUID
    addIANA(iaid2, solicit2);

    PacketInfo pkt1;
    pkt1.Layer5.push_back(solicit1);
    PacketInfo pkt2;
    pkt2.Layer5.push_back(solicit2);

    expectPacket(Variable::Dhcpv6::Type::advertise, 2);
    server->handleDhcpPacket(pkt1, iface, true, localAddress);
    server->handleDhcpPacket(pkt2, iface, true, localAddress.substr(0, 15) + ByteString("\x02", 1));
}

// Scenario: Releasing an address followed by new Solicit reuses the address
TEST_F(Dhcpv6ServerTest, Scenario_ReleaseThenSolicit_ReusesAddress)
{
    ByteString iaid = generateIAID();
    ByteString id = getClientDUID() + iaid;
    ByteString ip = net->pool->allocateTempIP(&id, true);
    id += ip;
    net->lease->activateLeaseFromTemp(ip, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage, &id);

    Dhcpv6Header release = makePacket(Variable::Dhcpv6::Type::release);
    addIANA(iaid, release, {ip});
    PacketInfo releasePkt;
    releasePkt.Layer5.push_back(release);
    expectPacket(Variable::Dhcpv6::Type::reply);
    server->handleDhcpPacket(releasePkt, iface, false, localAddress);

    Dhcpv6Header solicit = makePacket(Variable::Dhcpv6::Type::solicit, false);
    addIANA(iaid, solicit, {ip});
    PacketInfo solicitPkt;
    solicitPkt.Layer5.push_back(solicit);

    expectPacket(Variable::Dhcpv6::Type::advertise, 1, [&](const Dhcpv6Header& reply) {
        for (const auto& opt : reply.options)
        {
            if (opt.option == Variable::Dhcpv6::Options::IA_NA &&
                opt.value.find(ip) != std::string::npos)
                return true;
        }
        return false;
    });

    server->handleDhcpPacket(solicitPkt, iface, false, localAddress);
}

// Scenario: Declined address followed by Request fails due to decline hold
TEST_F(Dhcpv6ServerTest, Scenario_DeclineThenRequest_FailsDueToHold)
{
    ByteString iaid = generateIAID();
    ByteString id = getClientDUID() + iaid;
    ByteString ip = net->pool->allocateTempIP(&id, true);
    id += ip;
    net->lease->activateLeaseFromTemp(ip, net->config->leaseTime, net->config->t1Percentage, net->config->t2Percentage, &id);

    server->scheduleTimeout(Dhcp::TimerType::DECLINE_HOLD, id, ip, net->config->getNetworkID(), 1);

    Dhcpv6Header request = makePacket(Variable::Dhcpv6::Type::request);
    addIANA(iaid, request, {ip});
    PacketInfo pkt;
    pkt.Layer5.push_back(request);

    expectNoPacket();
    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Scenario: Renewing lease before it expires should succeed
TEST_F(Dhcpv6ServerTest, Scenario_RenewBeforeTimeout_Succeeds)
{
    ByteString iaid = generateIAID();
    ByteString id = getClientDUID() + iaid;
    ByteString ip = net->pool->allocateTempIP(&id, true);
    id += ip;
    net->lease->activateLeaseFromTemp(ip, 30, 0.5, 0.8, &id);

    Dhcpv6Header renew = makePacket(Variable::Dhcpv6::Type::renew);
    addIANA(iaid, renew, {ip});
    PacketInfo pkt;
    pkt.Layer5.push_back(renew);

    expectPacket(Variable::Dhcpv6::Type::reply);
    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

*/
#pragma endregion
#pragma region EdgeCases
/*

// Edge: Malformed option should be ignored gracefully without crashing the server
TEST_F(Dhcpv6ServerTest, Edge_MalformedOption_IgnoredGracefully)
{
    Dhcpv6Header packet = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString malformed = ByteString("\x00\x01\x00", 3); // Too short for a valid option

    packet.options.push_back({
        ByteString("\xff\xff", 2), // Unknown/malformed option
        Functions::numToByte(malformed.size(), 2),
        malformed
    });

    PacketInfo pkt;
    pkt.Layer5.push_back(packet);

    expectPacket(Variable::Dhcpv6::Type::advertise, 1);
    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Edge: Packet with invalid transaction ID should be ignored
TEST_F(Dhcpv6ServerTest, Edge_InvalidTransactionID_Ignored)
{
    Dhcpv6Header packet = makePacket(Variable::Dhcpv6::Type::solicit, false);
    packet.transactionID = ByteString("\x01\x02"); // Invalid: only 2 bytes

    PacketInfo pkt;
    pkt.Layer5.push_back(packet);

    expectNoPacket();
    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Edge: Duplicate options (e.g. multiple client IDs) should be handled once
TEST_F(Dhcpv6ServerTest, Edge_DuplicateOptions_HandledOnce)
{
    Dhcpv6Header packet = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString duid = getClientDUID();
    ByteString iaid = generateIAID();

    addClientID(duid, packet); // Add valid one
    addClientID(duid, packet); // Duplicate

    addIANA(iaid, packet);

    PacketInfo pkt;
    pkt.Layer5.push_back(packet);

    expectPacket(Variable::Dhcpv6::Type::advertise, 1);
    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Edge: Two clients with same IAID should not affect each other's lease
TEST_F(Dhcpv6ServerTest, Edge_TwoClientsSameIAID_Isolated)
{
    ByteString iaid = generateIAID();

    // First client
    Dhcpv6Header solicit1 = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString duid1 = getClientDUID();
    addClientID(duid1, solicit1);
    addIANA(iaid, solicit1);

    // Second client
    Dhcpv6Header solicit2 = makePacket(Variable::Dhcpv6::Type::solicit, false);
    ByteString duid2 = duid1;
    duid2[0] ^= 0xFF; // Make DUID different
    addClientID(duid2, solicit2);
    addIANA(iaid, solicit2);

    PacketInfo pkt1, pkt2;
    pkt1.Layer5.push_back(solicit1);
    pkt2.Layer5.push_back(solicit2);

    expectPacket(Variable::Dhcpv6::Type::advertise, 2);
    server->handleDhcpPacket(pkt1, iface, true, localAddress);
    server->handleDhcpPacket(pkt2, iface, true, localAddress.substr(0, 15) + ByteString("\x02", 2));
}

// Edge: Packet with no options should not crash the server
TEST_F(Dhcpv6ServerTest, Edge_EmptyOptionList_NoCrash)
{
    Dhcpv6Header empty = makePacket(Variable::Dhcpv6::Type::solicit, false);
    empty.options.clear();

    PacketInfo pkt;
    pkt.Layer5.push_back(empty);

    expectNoPacket();
    server->handleDhcpPacket(pkt, iface, false, localAddress);
}

// Edge: Relay message with invalid inner encapsulation should be ignored gracefully
TEST_F(Dhcpv6ServerTest, Edge_InvalidRelayEncapsulation_Ignored)
{
    Dhcpv6RelayHeader relay;
    relay.msgType = Variable::Dhcpv6::Type::relayForward;
    relay.hopCount = 0;
    relay.linkAddress = ByteString(16, '\x00');
    relay.peerAddress = ByteString(16, '\x01');

    // Corrupted relay-msg content (not a valid DHCPv6 message)
    ByteString invalidInner = ByteString("\xFF\xFF\xFF", 3);
    relay.options.push_back({
        Variable::Dhcpv6::Options::relayMsg,
        Functions::numToByte(invalidInner.size(), 2),
        invalidInner
    });

    PacketInfo pkt;
    pkt.Layer5.push_back(relay);

    expectNoPacket();
    server->handleDhcpPacket(pkt, iface, false, localAddress);
}
*/
