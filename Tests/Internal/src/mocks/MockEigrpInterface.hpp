// MockEigrpInterface.hpp

#ifndef MOCK_EIGRP_INTERFACE_HPP
#define MOCK_EIGRP_INTERFACE_HPP

#include <gmock/gmock.h>
#include <Eigrp.h>
#include <MockInterface.hpp>

class MockEigrpInterface : public Protocol::EigrpInterface
{
public:
    MockEigrpInterface(Protocol::Eigrp& eigrpInstance, MockInterface* interfacePtr)
        : EigrpInterface(eigrpInstance, interfacePtr) {}

    MOCK_METHOD(void, sendHelloPacket, (EigrpConfigs::NeighborInfo* neighbor, bool unicast, bool update, uint32_t sequenceNumber), (override));
    MOCK_METHOD(void, startHoldTimer, (EigrpConfigs::NeighborInfo* neighbor, const ByteString& address, uint16_t holdTime), (override));
    MOCK_METHOD(void, sendUpdateToNeighbor, (EigrpConfigs::NeighborInfo* neighbor, const std::vector<RoutingTable::Eigrp*> &routes, EigrpConfigs::UpdateType updateType, bool restart, bool conditional, std::vector<ByteString> conditionalNeighbors), (override));
    MOCK_METHOD(void, sendQueryToNeighbor, (EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, RoutingTable::Eigrp* failedRoute), (override));
    MOCK_METHOD(void, sendReplyToNeighbor, (EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, RoutingTable::Eigrp* route), (override));
    MOCK_METHOD(void, stopHello, (), (override));

    void disableMock()
    {
        EXPECT_CALL(*this, sendHelloPacket(::testing::_, ::testing::_, ::testing::_, ::testing::_))
            .WillRepeatedly(::testing::Invoke([&](EigrpConfigs::NeighborInfo* neighbor, bool unicast, bool update, uint32_t sequenceNumber) {
                Protocol::EigrpInterface::sendHelloPacket(neighbor, unicast, update, sequenceNumber);
            }));
        EXPECT_CALL(*this, startHoldTimer(::testing::_, ::testing::_, ::testing::_))
            .WillRepeatedly(::testing::Invoke([&](EigrpConfigs::NeighborInfo* neighbor, const ByteString& address, uint16_t holdTime) {
                Protocol::EigrpInterface::startHoldTimer(neighbor, address, holdTime);
            }));
        EXPECT_CALL(*this, sendUpdateToNeighbor(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
            .WillRepeatedly(::testing::Invoke([&](EigrpConfigs::NeighborInfo* neighbor, const std::vector<RoutingTable::Eigrp*> &routes, EigrpConfigs::UpdateType updateType, bool restart, bool conditional, std::vector<ByteString> conditionalNeighbors) {
                Protocol::EigrpInterface::sendUpdateToNeighbor(neighbor, routes, updateType, restart, conditional, conditionalNeighbors);
            }));
        EXPECT_CALL(*this, sendQueryToNeighbor(::testing::_, ::testing::_, ::testing::_))
            .WillRepeatedly(::testing::Invoke([&](EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, RoutingTable::Eigrp* failedRoute) {
                Protocol::EigrpInterface::sendQueryToNeighbor(neighbor, neighborIp, failedRoute);
            }));
        EXPECT_CALL(*this, sendReplyToNeighbor(::testing::_, ::testing::_, ::testing::_))
            .WillRepeatedly(::testing::Invoke([&](EigrpConfigs::NeighborInfo* neighbor, const ByteString& neighborIp, RoutingTable::Eigrp* route) {
                Protocol::EigrpInterface::sendReplyToNeighbor(neighbor, neighborIp, route);
            }));
        EXPECT_CALL(*this, stopHello)
            .WillRepeatedly(::testing::Invoke([&]() {
                Protocol::EigrpInterface::stopHello();
            }));
    }
};

#endif // MOCK_EIGRP_INTERFACE_HPP
