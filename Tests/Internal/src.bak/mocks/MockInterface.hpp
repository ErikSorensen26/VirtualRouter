// MockInterface.hpp

#ifndef MOCK_INTERFACE_HPP
#define MOCK_INTERFACE_HPP

#include <gmock/gmock.h>
#include <Interface.h>
#include <InterfaceType.hpp>
#include <PacketStructure.h>
#include <HardwareManager.h>
#include <Global.h>
#include <Ifname.h>

static HwIfaceInfo defaultHwInfo = { ifnametoindex("lo"), "lo", 0x010203040506, 1000000000 };

class MockInterface : public Interface
{
public:
    // Constructor forwarding to base class constructor
    MockInterface(Global& global,
                  InterfaceType interfaceType = InterfaceType::GIGABIT_ETHERNET,
                  const HwIfaceInfo& hwInfo = defaultHwInfo,
                  float interfaceId = 0,
                  VirtualRouter* vrf = nullptr,
                  bool debug = false)
        : Interface({interfaceType, interfaceId, vrf ? *vrf : *global.getRoutingInstance("default"), hwInfo, debug})
    {}

    // Destructor
    ~MockInterface() override 
    {
        Interface::cleanupInterface();
        Interface::stopThreads();
    }

    // Mocking virtual methods
    MOCK_METHOD(void, setIPv4, (uint32_t ip, uint8_t subnet), (override));
    MOCK_METHOD(void, setIPv6, (const uint8_t* ip, bool local, uint8_t subnet, bool eui64), (override));
    MOCK_METHOD(void, shutdown, (bool shut), (override));
    MOCK_METHOD(void, enqueuePacket, (PacketBuilder& packetInfo, const uint8_t* mac), (override));

    void enableIPs() {
        EXPECT_CALL(*this, setIPv4).Times(::testing::AnyNumber()).WillRepeatedly(::testing::Invoke([this](uint32_t ip, uint8_t subnet) {
            if (shutdownFlag.load(std::memory_order_relaxed)) return;
            std::lock_guard<std::shared_mutex> lock(configs.ipMutex);
            configs.ipv4.setAddress(ip, subnet);
        }));
        EXPECT_CALL(*this, setIPv6).Times(::testing::AnyNumber()).WillRepeatedly(::testing::Invoke([this](const uint8_t* ip, bool local, uint8_t subnet, bool eui64) {
            if (shutdownFlag.load(std::memory_order_relaxed)) return;
            std::lock_guard<std::shared_mutex> lock(configs.ipMutex);
            configs.ipv6.addAddress(ip, local, subnet);
        }));
    }
    void enableShutdown()
    {
        EXPECT_CALL(*this, shutdown).Times(::testing::AnyNumber()).WillRepeatedly(::testing::Invoke([this](bool shut) {
            shutdownFlag.store(shut, std::memory_order_release);
            if (shut) 
            {
                stateChange(StateChange::SHUTDOWN);
                stateChangeV6(StateChange::SHUTDOWN);
                stopThreads();
            }
            else if (!shut) 
            {
                startThreads();
                stateChange(StateChange::INITIATE);
                stateChangeV6(StateChange::INITIATE);
            }
        }));
    }
    void blockEnqueues()
    {
        if (blocked) return;
        blocked = true;
        EXPECT_CALL(*this, enqueuePacket).Times(::testing::AnyNumber()).WillRepeatedly(::testing::Invoke([this](PacketBuilder& packetInfo, const uint8_t* mac) {
            return;
        }));
    }

    bool blocked = false;
};


#endif // MOCK_INTERFACE_HPP
