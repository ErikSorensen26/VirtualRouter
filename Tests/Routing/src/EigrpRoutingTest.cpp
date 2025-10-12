#include <gtest/gtest.h>
#include <Eigrp.h>
#include <Gns3Environment.hpp>
#include <TopologyGenerator.h>
#include <ListenerManager.hpp>
#include <PacketSniffer.hpp>
#include <CommandProcessor.h>
#include <VirtualRouter.h>
#include <Logger.h>


class Routing_EigrpTest : public ::testing::Test
{
protected:

    static Environment* gns3;
    static Topology::TopologyGenerator* gen;
    static Global* global;
    ListenerManager listener = ListenerManager();
    CliSession* session;

    static void SetUpTestSuite()
    {
        Logger::getInstance().initialize(true);
        global = new Global({}, true);
        global->routingEnabled = true;
        global->testingMode = true;
        ASSERT_TRUE(gns3->start());
        Topology::Settings::Ring eigrpTop;
        eigrpTop.nodeCount = 4;
        gns3 = new Environment(*global);
        gen = new Topology::TopologyGenerator(*gns3->gns3);
        gen->generate(eigrpTop);
        gen->configVirtualSession(gns3->session);
        gen->setUpSniffer(gns3->session);
    }
    
    void SetUp() override
    {
        session = gns3->session;
    }
    
    void TearDown() override
    {
    }

    static void TearDownTestSuite()
    {
        delete gen;
        delete gns3;
    }

    void sendCommandToEachInterface(const std::string& command)
    {
        for (const auto& node : gen->topology->nodeList)
        {
            if (node->port != 0)
            {
                for (const auto& [interface, _] : node->interfaceIPv6s)
                {
                    gen->sendCommand("interface " + interface + "\r\n" + command, node->port);
                }
            }
        }
    }
};

Environment* Routing_EigrpTest::gns3 = nullptr;
Global* Routing_EigrpTest::global = nullptr;
Topology::TopologyGenerator* Routing_EigrpTest::gen = nullptr;

TEST_F(Routing_EigrpTest, TestingHellos)
{
    auto sniffer = gen->getSniffers().front();
    listener.addListener(sniffer->createListenCondition<EigrpHeader>(
        [](const EigrpHeader& eigrp) -> bool
        {
            return eigrp.opcode == Variable::Eigrp::Type::hello &&
            Functions::byteToNum(eigrp.ack) == 0;
        },
        1
    ), 10000);

    session->handleInput("router eigrp 1\n");
    EXPECT_TRUE(session->commandProcessor->currentVrf->getEigrpAutonomousSystem(1)->ipv4->eigrpInterfaceList.empty());
    session->handleInput("network 0.0.0.0 255.255.255.255\n");
    EXPECT_FALSE(session->commandProcessor->currentVrf->getEigrpAutonomousSystem(1)->ipv4->eigrpInterfaceList.empty());

    EXPECT_TRUE(listener.waitAll());
    
    session->handleInput("no router eigrp 1\n");
}

TEST_F(Routing_EigrpTest, IPv4Adjacency_ClassicMode)
{
    listener.addListener([&]() -> bool {
        auto* as = session->commandProcessor->currentVrf->getEigrpAutonomousSystem(1);
        if (as && as->ipv4)
        {
            std::lock_guard<std::mutex> lock(as->ipv4->neighborMutex);
            if (as->ipv4->allNeighbors.size() != 2) return false;
            for (const auto& neighbor : as->ipv4->allNeighbors)
            {
                if (neighbor.second->neighborState.load(std::memory_order_relaxed) != EigrpConfigs::NeighborState::LOADING &&
                    neighbor.second->neighborState.load(std::memory_order_relaxed) != EigrpConfigs::NeighborState::ESTABLISHED)
                {
                    return false;
                }
            }
            return true;
        }
        return false;
    }, 1000000);

    session->handleInput("router eigrp 1\n");
    EXPECT_TRUE(session->commandProcessor->currentVrf->getEigrpAutonomousSystem(1)->ipv4->eigrpInterfaceList.empty());
    session->handleInput("network 0.0.0.0 255.255.255.255\n");
    EXPECT_FALSE(session->commandProcessor->currentVrf->getEigrpAutonomousSystem(1)->ipv4->eigrpInterfaceList.empty());
    gen->sendCommandAll("configure terminal\r\nrouter eigrp 1\r\nnetwork 0.0.0.0 255.255.255.255");

    bool eigrpLoaded = listener.waitAll();

    session->handleInput("no router eigrp 1\n");

    gen->sendCommandAll("no router eigrp 1\r\n");
    EXPECT_TRUE(eigrpLoaded);
}

TEST_F(Routing_EigrpTest, IPv6Adjacency_ClassicMode)
{
    listener.addListener([&]() -> bool {
        auto* as = session->commandProcessor->currentVrf->getEigrpAutonomousSystem(1);
        if (as && as->ipv6)
        {
            std::lock_guard<std::mutex> lock(as->ipv6->neighborMutex);
            if (as->ipv6->allNeighbors.size() != 2) return false;
            for (const auto& neighbor : as->ipv6->allNeighbors)
            {
                if (neighbor.second->neighborState.load(std::memory_order_relaxed) != EigrpConfigs::NeighborState::LOADING &&
                    neighbor.second->neighborState.load(std::memory_order_relaxed) != EigrpConfigs::NeighborState::ESTABLISHED)
                {
                    return false;
                }
            }
            return true;
        }
        return false;
    }, 1000000);

    session->handleInput("ipv6 router eigrp 1\n");
    EXPECT_TRUE(session->commandProcessor->currentVrf->getEigrpAutonomousSystem(1)->ipv6->eigrpInterfaceList.empty());
    for (size_t i = 0; i < gen->topology->node->interfaceIPv6s.size(); i++)
    {
        session->handleInput("interface GigabitEthernet " + std::to_string(i) + "\n");
        session->handleInput("ipv6 eigrp 1\n");
    }

    gen->sendCommandAll("ipv6 router eigrp 1");
    sendCommandToEachInterface("ipv6 eigrp 1\r\n");

    bool eigrpLoaded = listener.waitAll();

    sendCommandToEachInterface("no ipv6 eigrp 1\r\n");
    gen->sendCommandAll("no ipv6 router eigrp 1");
    session->handleInput("no ipv6 router eigrp 1\n");
    for (size_t i = 0; i < gen->topology->node->interfaceIPv6s.size(); i++)
    {
        session->handleInput("interface GigabitEthernet " + std::to_string(i) + "\n");
        session->handleInput("no ipv6 eigrp 1\n");
    }

    EXPECT_TRUE(eigrpLoaded);
}

TEST_F(Routing_EigrpTest, IPv4Adjacency_NamedMode)
{
    listener.addListener([&]() -> bool {
        auto* as = session->commandProcessor->currentVrf->getEigrpAutonomousSystem(1);
        if (as && as->ipv4)
        {
            std::lock_guard<std::mutex> lock(as->ipv4->neighborMutex);
            if (as->ipv4->allNeighbors.size() != 2) return false;
            for (const auto& neighbor : as->ipv4->allNeighbors)
            {
                if (neighbor.second->neighborState.load(std::memory_order_relaxed) != EigrpConfigs::NeighborState::LOADING &&
                    neighbor.second->neighborState.load(std::memory_order_relaxed) != EigrpConfigs::NeighborState::ESTABLISHED)
                {
                    return false;
                }
            }
            return true;
        }
        return false;
    }, 1000000);

    session->handleInput("router eigrp test\n");
    session->handleInput("address-family ipv4 au 1\n");
    EXPECT_TRUE(session->commandProcessor->currentVrf->getEigrpAutonomousSystem(1)->ipv4->eigrpInterfaceList.empty());
    session->handleInput("network 0.0.0.0 255.255.255.255\n");
    EXPECT_FALSE(session->commandProcessor->currentVrf->getEigrpAutonomousSystem(1)->ipv4->eigrpInterfaceList.empty());
    gen->sendCommandAll("configure terminal\r\nrouter eigrp 1\r\nnetwork 0.0.0.0 255.255.255.255");

    bool eigrpLoaded = listener.waitAll();

    session->handleInput("no router eigrp 1\n");
    gen->sendCommandAll("no router eigrp test\r\n");

    EXPECT_TRUE(eigrpLoaded);
}

TEST_F(Routing_EigrpTest, IPv6Adjacency_NamedMode)
{
    listener.addListener([&]() -> bool {
        auto* as = session->commandProcessor->currentVrf->getEigrpAutonomousSystem(1);
        if (as && as->ipv6)
        {
            std::lock_guard<std::mutex> lock(as->ipv6->neighborMutex);
            if (as->ipv6->allNeighbors.size() != 2) return false;
            for (const auto& neighbor : as->ipv6->allNeighbors)
            {
                if (neighbor.second->neighborState.load(std::memory_order_relaxed) != EigrpConfigs::NeighborState::LOADING &&
                    neighbor.second->neighborState.load(std::memory_order_relaxed) != EigrpConfigs::NeighborState::ESTABLISHED)
                {
                    return false;
                }
            }
            return true;
        }
        return false;
    }, 1000000);

    session->handleInput("router eigrp test\n");
    session->handleInput("address-family ipv6 au 1\n");
    EXPECT_TRUE(session->commandProcessor->currentVrf->getEigrpAutonomousSystem(1)->ipv6->eigrpInterfaceList.empty());
    for (size_t i = 0; i < gen->topology->node->interfaceIPv6s.size(); i++)
    {
        session->handleInput("af-interface GigabitEthernet " + std::to_string(i) + "\nexit\n");
    }

    gen->sendCommandAll("ipv6 router eigrp 1");
    sendCommandToEachInterface("ipv6 eigrp 1\r\n");

    bool eigrpLoaded = listener.waitAll();

    sendCommandToEachInterface("no ipv6 eigrp 1\r\n");
    gen->sendCommandAll("no ipv6 router eigrp 1");
    session->handleInput("no ipv6 router eigrp 1\n");
    for (size_t i = 0; i < gen->topology->node->interfaceIPv6s.size(); i++)
    {
        session->handleInput("interface GigabitEthernet " + std::to_string(i) + "\n");
        session->handleInput("no ipv6 eigrp 1\n");
    }

    EXPECT_TRUE(eigrpLoaded);
}
