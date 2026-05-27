// MockInterface.hpp

#ifndef MOCK_INTERFACE_HPP
#define MOCK_INTERFACE_HPP

#include <gmock/gmock.h>
#include <interface/Interface.h>
#include <interface/configs/InterfaceType.hpp>
#include <hardware/Ifname.h>
#include <packet/PacketStructure.h>
#include <hardware/HardwareManager.h>
#include <dhcp/dhcpv4/DhcpClient.h>
#include <VirtualRouter.h>
#include <Global.h>

static hardware::HwIfaceInfo defaultHwInfo = { hardware::ifnametoindex("lo"), "lo", 0x010203040506, 1000000000 };

namespace interface
{
class MockInterface : public Interface
{
public:
    // Constructor forwarding to base class constructor
    MockInterface(core::Global& global,
                  InterfaceType interfaceType = InterfaceType::GIGABIT_ETHERNET,
                  const hardware::HwIfaceInfo& hwInfo = defaultHwInfo,
                  float interfaceId = 0,
                  core::VirtualRouter* vrf = nullptr,
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
    MOCK_METHOD(bool, setIPv4, (types::IPv4Prefix ip, bool secondary), (override));
    MOCK_METHOD(bool, setIPv6, (const types::IPv6Prefix& addr, bool eui64), (override));
    MOCK_METHOD(void, shutdown, (bool shut), (override));
    MOCK_METHOD(void, enqueuePacket, (processing::PacketBuilder& packetInfo, uint64_t mac), (override));

    void enableIPs() {
        EXPECT_CALL(*this, setIPv4).Times(::testing::AnyNumber()).WillRepeatedly(::testing::Invoke([this](types::IPv4Prefix ip, bool secondary) -> bool {
            if (shutdownFlag.load(std::memory_order_relaxed)) return false;
            if (secondary)
                configs.ipv4.addSecondaryAddress(ip);
            else
                configs.ipv4.setPrimaryAddress(ip);
        }));
        EXPECT_CALL(*this, setIPv6).Times(::testing::AnyNumber()).WillRepeatedly(::testing::Invoke([this](const types::IPv6Prefix& addr, bool eui64) -> bool {
            if (shutdownFlag.load(std::memory_order_relaxed)) return false;
            InterfaceConfigs::IPv6State::IPv6Address* ipv6 = nullptr;
            types::IPv6Address ip{addr};

            if (ip.isLocalLink())
                ipv6 = configs.ipv6.addAddress(addr, true);
            else if (ip.isLocalUnicast())
                ipv6 = configs.ipv6.addUniqueLocalAddress(addr);
            else if (ip.isGlobalUnicast())
                ipv6 = configs.ipv6.addAddress(addr, false);
            return false; // invalid
        }));
    }

    void enableShutdown()
    {
        EXPECT_CALL(*this, shutdown).Times(::testing::AnyNumber()).WillRepeatedly(::testing::Invoke([this](bool shut) {
            if (shutdownFlag.load(std::memory_order_relaxed) == shut)
                return;
            shutdownFlag.store(shut, std::memory_order_release);
            if (shut) 
            {
                if (dhcp) dhcp->shutdown();
                arp.shutdown();
                if (getVRF()->getGlobal().isIPv6UnicastRouting())
                {
                    // DHCPV6
                    ndp.shutdown();
                }

                getVRF()->getInterfaceManager().notify(StateChange::IF_DOWN, *this);
            }
            else if (!shut) 
            {
                if (dhcp) dhcp->initiate();
                arp.refresh();
                if (getVRF()->getGlobal().isIPv6UnicastRouting())
                {
                    // DHCPV6
                    ndp.refresh();
                }

                getVRF()->getInterfaceManager().notify(StateChange::IF_READY, *this);
            }
        }));
    }

    void blockEnqueues()
    {
        if (blocked) return;
        blocked = true;
        EXPECT_CALL(*this, enqueuePacket).Times(::testing::AnyNumber()).WillRepeatedly(::testing::Invoke([](processing::PacketBuilder&, uint64_t) {
            return;
        }));
    }

    bool blocked = false;
};
}

#endif // MOCK_INTERFACE_HPP
