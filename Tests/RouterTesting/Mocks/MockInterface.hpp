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
    ~MockInterface() override = default;

    // Mocking virtual methods
    MOCK_METHOD(void, setIPv4, (ByteString ip, uint8_t subnet), (override));
    MOCK_METHOD(void, setIPv6, (ByteString ip, uint8_t subnet, bool eui64), (override));
    MOCK_METHOD(void, Shutdown, (bool shut), (override));
    MOCK_METHOD(void, enqueuePacket, (PacketInfo& packetInfo, ByteString mac), (override));
    MOCK_METHOD(void, startThreads, (), (override));
};


#endif // MOCK_INTERFACE_HPP
