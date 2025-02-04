// MockInterface.hpp

#ifndef MOCK_INTERFACE_HPP
#define MOCK_INTERFACE_HPP

#include <gmock/gmock.h>
#include <Interface.h>
#include <PacketStructure.h>
#include <ByteString.hpp>

class MockInterface : public Interface
{
public:
    // Constructor forwarding to base class constructor
    MockInterface(InterfaceType interfaceType = InterfaceType::UNDEFINED,
                  std::string outInterface = "lo",
                  size_t inQueSiz = 100,
                  size_t outQueSiz = 100,
                  std::string mac = "010203040506",
                  uint8_t interfaceId = 0,
                  bool debug = false)
        : Interface(interfaceType, outInterface, inQueSiz, outQueSiz, mac, interfaceId, debug) {}

    // Destructor
    ~MockInterface() override 
    {
        Interface::cleanupInterface();
    }

    // Mocking virtual methods
    MOCK_METHOD(void, setIPv4, (ByteString ip, uint8_t subnet), (override));
    MOCK_METHOD(void, setIPv6, (ByteString ip, uint8_t subnet, bool eui64), (override));
    MOCK_METHOD(void, Shutdown, (bool shut), (override));
    MOCK_METHOD(void, enqueuePacket, (PacketInfo& packetInfo, ByteString mac), (override));
    MOCK_METHOD(void, startThreads, (), (override));

    void enableIPs() {
        EXPECT_CALL(*this, setIPv4).Times(::testing::AnyNumber()).WillRepeatedly(::testing::Invoke([this](ByteString ip, uint8_t subnet) {
            if (!Get()) return;
            std::lock_guard<std::shared_mutex> lock(Get()->ipMutex);
            Get()->ipv4.ipAddress = ip; 
            Get()->ipv4.mask = subnet;
        }));
        EXPECT_CALL(*this, setIPv6).Times(::testing::AnyNumber()).WillRepeatedly(::testing::Invoke([this](ByteString ip, uint8_t subnet, bool eui64) {
            if (!Get()) return;
            std::lock_guard<std::shared_mutex> lock(Get()->ipMutex);
            Get()->ipv6.ipAddress = ip; 
            Get()->ipv6.mask = subnet;
        }));
    }
    void enableShutdown()
    {
        EXPECT_CALL(*this, Shutdown).Times(::testing::AnyNumber()).WillRepeatedly(::testing::Invoke([this](bool shut) {
            Interface::Shutdown(shut);
        }));
    }
    void blockEnqueues()
    {
        EXPECT_CALL(*this, enqueuePacket).Times(::testing::AnyNumber());
    }
};


#endif // MOCK_INTERFACE_HPP
