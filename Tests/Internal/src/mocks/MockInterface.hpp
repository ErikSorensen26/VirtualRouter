// MockInterface.hpp

#ifndef MOCK_INTERFACE_HPP
#define MOCK_INTERFACE_HPP

#include <gmock/gmock.h>
#include <Interface.h>
#include <InterfaceType.hpp>
#include <PacketStructure.h>
#include <Global.h>

class MockInterface : public Interface
{
public:
    // Constructor forwarding to base class constructor
    MockInterface(Global& global,
                  InterfaceType interfaceType = InterfaceType::GIGABIT_ETHERNET,
                  std::string outInterface = "lo",
                  size_t inQueSiz = 100,
                  size_t outQueSiz = 100,
                  std::string mac = "010203040506",
                  float interfaceId = 0,
                  VirtualRouter* vrf = nullptr,
                  bool debug = false)
        : Interface({interfaceType, interfaceId, vrf ? *vrf : *global.getRoutingInstance("default"), outInterface.c_str(), reinterpret_cast<const uint8_t*>(mac.data()), debug}) {}

    // Destructor
    ~MockInterface() override 
    {
        Interface::stopThreads();
        Interface::cleanupInterface();
    }

    // Mocking virtual methods
    MOCK_METHOD(void, setIPv4, (uint32_t ip, uint8_t subnet), (override));
    MOCK_METHOD(void, setIPv6, (const uint8_t* ip, bool local, uint8_t subnet, bool eui64), (override));
    MOCK_METHOD(void, Shutdown, (bool shut), (override));
    MOCK_METHOD(void, enqueuePacket, (PacketBuilder& packetInfo, const uint8_t* mac), (override));
    MOCK_METHOD(void, startThreads, (), (override));

    void enableIPs() {
        EXPECT_CALL(*this, setIPv4).Times(::testing::AnyNumber()).WillRepeatedly(::testing::Invoke([this](uint32_t ip, uint8_t subnet) {
            if (shutdownFlag.load(std::memory_order_relaxed)) return;
            std::lock_guard<std::shared_mutex> lock(configs.ipMutex);
            configs.ipv4.setAddress(ip, subnet);
        }));
        EXPECT_CALL(*this, setIPv6).Times(::testing::AnyNumber()).WillRepeatedly(::testing::Invoke([this](const uint8_t* ip, bool local, uint8_t subnet, bool eui64) {
            if (shutdownFlag.load(std::memory_order_relaxed)) return;
            std::lock_guard<std::shared_mutex> lock(configs.ipMutex);
            configs.ipv6.addAddress(ip, true, subnet);
        }));
    }
    void enableShutdown()
    {
        EXPECT_CALL(*this, Shutdown).Times(::testing::AnyNumber()).WillRepeatedly(::testing::Invoke([this](bool shut) {
            shutdownFlag = shut;
            if (shut) 
            {
                stopThreads();
            }
            else if (!shut) 
            {
                startThreads();
            }
            stateChange(StateChange::SHUTDOWN);
            stateChangeV6(StateChange::SHUTDOWN);
        }));
    }
    void blockEnqueues()
    {
        EXPECT_CALL(*this, enqueuePacket).Times(::testing::AnyNumber());
    }
};


#endif // MOCK_INTERFACE_HPP
