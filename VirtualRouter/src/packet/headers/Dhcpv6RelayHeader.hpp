/**
 * @file Dhcpv6RelayHeader.hpp
 * @brief DHCPv6 Relay-Forward/Relay-Reply (RFC 8415 sec. 8) wire-format header.
 */

// Dhcpv6RelayHeader.hpp

#ifndef DHCPV6_RELAY_HEADER_HPP
#define DHCPV6_RELAY_HEADER_HPP

#include <span>
#include "packet/HeaderHelpers.hpp"

namespace packet
{
/**
 * @struct Dhcpv6RelayHeaderRaw
 * @brief Represents a raw DHCPv6 relay agent header.
 * @ingroup PACKET_HEADERS
 */
#pragma pack(push, 1)
struct Dhcpv6RelayHeaderRaw
{
    uint8_t msgType;            ///< DHCPV6_TYPE_RELAY_FORWARD or DHCPV6_TYPE_RELAY_REPLY.
    uint8_t hopCount;           ///< Number of relay agents that have relayed this message.
    uint8_t linkAddress[16];    ///< Used by the server to identify the client's link.
    uint8_t peerAddress[16];    ///< Address of the client or relay agent from which this message was received.
};
#pragma pack(pop)

/**
 * @struct Dhcpv6RelayHeader
 * @brief Represents a DHCPv6 relay agent header for relay agent interactions.
 * @ingroup PACKET_HEADERS
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

} // namespace packet

#endif // DHCPV6_RELAY_HEADER_HPP

