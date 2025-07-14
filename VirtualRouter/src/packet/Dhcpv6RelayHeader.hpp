// Dhcpv6RelayHeader.hpp

#ifndef DHCPV6_RELAY_HEADER_HPP
#define DHCPV6_RELAY_HEADER_HPP

#include <HeaderHelpers.hpp>
#include <span>

/*
 * @brief DHCPv6 raw relay header
 */
#pragma pack(push, 1)
struct Dhcpv6RelayHeaderRaw
{
    uint8_t msgType;
    uint8_t hopCount;
    uint8_t linkAddress[16];
    uint8_t peerAddress[16];
};
#pragma pack(pop)

/**
 * @brief DHCPv6 Relay header for relay agent interactions.
 */
struct Dhcpv6RelayHeader
{
    DEFINE_PACKET_HEADER(Dhcpv6RelayHeaderRaw);

    uint8_t getMsgType() { return raw->msgType; }
    uint8_t getHopCount() { return raw->hopCount; }
    const uint8_t* getLinkAddress() { return raw->linkAddress; }
    const uint8_t* getPeerAddress() { return raw->peerAddress; }

    void setMsgType(uint8_t val)
        { raw->msgType = val; }
    void setHopCount(uint8_t val)
        { raw->hopCount = val; }
    void setLinkAddress(uint8_t* val)
        { std::memcpy(raw->linkAddress, val, 16); }
    void setPeerAddress(uint8_t* val) 
        { std::memcpy(raw->peerAddress, val, 16); }
};

#endif // DHCPV6_RELAY_HEADER_HPP
