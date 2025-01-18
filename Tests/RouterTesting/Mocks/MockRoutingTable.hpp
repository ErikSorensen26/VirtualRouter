// MockRoutingTable.hpp

#ifndef MOCK_ROUTING_TABLE_HPP
#define MOCK_ROUTING_TABLE_HPP

#include <gmock/gmock.h>
#include <RoutingTable.h>

struct ArpHeader;

class MockRoutingTable : public RoutingTable
{
public:
    MOCK_METHOD(void, updateArp, (const ArpHeader& receivedArp), (override));
};

#endif // MOCK_ROUTING_TABLE_HPP
