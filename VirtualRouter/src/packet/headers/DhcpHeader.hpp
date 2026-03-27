/**
 * @file DhcpHeader.hpp
 */

// DhcpHeader.hpp

#ifndef DHCP_HEADER_HPP
#define DHCP_HEADER_HPP

#include <vector>
#include <ByteUtils.hpp>
#include "packet/HeaderHelpers.hpp"
#include "packet/TlvOptions.hpp"

#define DHCP_TYPE_DISCOVER                  0x01U ///< DHCP Discover message type
#define DHCP_TYPE_OFFER                     0x02U ///< DHCP Offer message type
#define DHCP_TYPE_REQUEST                   0x03U ///< DHCP Request message type
#define DHCP_TYPE_DECLINE                   0x04U ///< DHCP Decline message type
#define DHCP_TYPE_ACK                       0x05U ///< DHCP Acknowledgment message type
#define DHCP_TYPE_NAK                       0x06U ///< DHCP Negative Acknowledgment message type
#define DHCP_TYPE_RELEASE                   0x07U ///< DHCP Release message type
#define DHCP_TYPE_INFORM                    0x08U ///< DHCP Inform message type
#define DHCP_TYPE_FORCE_RENEW               0x09U ///< DHCP Force Renew message type
#define DHCP_TYPE_LEASE_QUERY               0x0AU ///< DHCP Lease Query message type
#define DHCP_TYPE_LEASE_UNASSIGNED          0x0BU ///< DHCP Lease Unassigned type
#define DHCP_TYPE_LEASE_UNKNOWN             0x0CU ///< DHCP Lease Unknown type
#define DHCP_TYPE_LEASE_ACTIVE              0x0DU ///< DHCP Lease Active type

#define DHCP_OPTION_MASK                    0x01U ///< DHCP Option for Subnet Mask
#define DHCP_OPTION_BROADCAST               0x1CU ///< DHCP Option for Broadcast Message
#define DHCP_OPTION_ROUTER                  0x03U ///< DHCP Option for Router
#define DHCP_OPTION_DOMAIN_NAME             0x0FU ///< DHCP Option for Domain Name
#define DHCP_OPTION_DOMAIN_SERVER           0x06U ///< DHCP Option for Domain Server
#define DHCP_OPTION_DOMAIN_SEARCH           0x77U ///< DHCP Option for Domain Search
#define DHCP_OPTION_NETBIOS_SERVER          0x2CU ///< DHCP Option for NETBIOS Server
#define DHCP_OPTION_MTU                     0x1AU ///< DHCP Option for MTU
#define DHCP_OPTION_CLASSLESS_STATIC_ROUTE  0x79U ///< DHCP Option for Classless Static Route
#define DHCP_OPTION_NTP                     0x2AU ///< DHCP Option for NTP
#define DHCP_OPTION_OVERLOAD                0x34U ///< DHCP Option for Overload
#define DHCP_OPTION_TYPE                    0x35U ///< DHCP Option for Type
#define DHCP_OPTION_HOSTNAME                0x0CU ///< DHCP Option for Hostname
#define DHCP_OPTION_CLIENT_ID               0x3DU ///< DHCP Option for Client ID
#define DHCP_OPTION_SERVER_IDENTIFIER       0x36U ///< DHCP Option for Server Identifier
#define DHCP_OPTION_LEASE_TIME              0x33U ///< DHCP Option for Lease Time
#define DHCP_OPTION_RENEWAL_TIME            0x3AU ///< DHCP Option for Renewal Time
#define DHCP_OPTION_REBINDING_TIME          0x3BU ///< DHCP Option for Rebinding Time
#define DHCP_OPTION_REQUEST_IP              0x32U ///< DHCP Option for Request IP
#define DHCP_OPTION_REQUEST_LIST            0x37U ///< DHCP Option for Request List
#define DHCP_OPTION_MAX_SIZE                0x39U ///< DHCP Option for Max Size
#define DHCP_OPTION_TFTP_SERVER_NAME        0x42U ///< DHCP Option for TFTP Server Name
#define DHCP_OPTION_BOOT_FILE               0x43U ///< DHCP Option for Boot File
#define DHCP_OPTION_STATIC_ROUTE            0x21U ///< DHCP Option for Static Route
#define DHCP_OPTION_VENDOR_SPECIFIC         0x2BU ///< DHCP Option for Vendor Specific
#define DHCP_OPTION_VENDOR_CLASS_ID         0x3CU ///< DHCP Option for Vendor Class ID
#define DHCP_OPTION_AUTHENTICATION          0x5AU ///< DHCP Option for Authentication
#define DHCP_OPTION_RAPID_COMMIT            0x50U ///< DHCP Option for Rapid Commit
#define DHCP_OPTION_RELAY_AGENT_INFO        0x52U ///< DHCP Option for Relay Agent Info
#define DHCP_OPTION_TIMESTAMP               0x5BU ///< DHCP Option for Timestamp
#define DHCP_OPTION_TFTP_SERVERS            0x96U ///< DHCP Option for TFTP Servers
#define DHCP_OPTION_END                     0xFFU ///< DHCP Option End Marker

#define DHCP_MAGIC_COOKIE 0x63825363U //< DHCP Magic Cookie

#define DHCP_TIMER_MIN_LEASE_TIME 3600
#define DHCP_TIMER_MAX_LEASE_TIME 86400

inline constexpr uint8_t DHCP_CLIENT_HARDWARE_ADDRESS_PADDING[10] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; ///< Padding for Client Hardware Address.
inline constexpr uint8_t DHCP_SERVER_HOSTNAME[64] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; ///< Server Name.
inline constexpr uint8_t DHCP_BOOT_FILE[128] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; ///< Bootfile Name.
inline constexpr uint8_t DHCP_END_PADDING[25] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; ///< Padding after DHCP options.

namespace packet
{
/**
 * @struct DhcpHeaderRaw
 * @brief Represents a raw DHCP header
 * @ingroup PACKET_HEADERS
 */
#pragma pack(push, 1)
struct DhcpHeaderRaw
{
    uint8_t opcode;         ///< Boot Message Type.
    uint8_t hType;          ///< Hardware Type.
    uint8_t hLen;           ///< Hardware Address Length.
    uint8_t hops;           ///< Hops.
    uint8_t xId[4];         ///< Transaction ID.
    uint8_t secs[2];        ///< Seconds Elapsed.
    uint8_t flags[2];       ///< Flags
    uint8_t ciaddr[4];      ///< Client IP Address.
    uint8_t yiaddr[4];      ///< 'Your' (Client) IP Address.
    uint8_t siaddr[4];      ///< Next Server IP Address.
    uint8_t giaddr[4];      ///< Relay Agent IP Address.
    uint8_t chaddr[16];     ///< Client MAC Address + padding.
    uint8_t serverName[64]; ///< Server Hostname.
    uint8_t file[128];      ///< Boot File Name.
    uint8_t magicCookie[4]; ///< Magic Cookie.
};
#pragma pack(pop)


/**
 * @struct DhcpHeader
 * @brief Represents a DHCP (Dynamic Host Configuration Protocol) header.
 * @ingroup PACKET_HEADERS
 */
struct DhcpHeader
{
    DEFINE_PACKET_HEADER(DhcpHeaderRaw);

    uint8_t getOpcode() const { return raw->opcode; }
    uint8_t getHType() const { return raw->hType; }
    uint8_t getHLen() const { return raw->hLen; }
    uint8_t getHops() const { return raw->hops; }
    const uint8_t* getXid() const { return raw->xId; }
    uint16_t getSecs() const { return utils::readU16(raw->secs); }
    uint16_t getFlags() const { return utils::readU16(raw->flags); }
    const uint8_t* getClientIP() const { return raw->ciaddr; }
    uint32_t getClientIPInt() const { return utils::readU32(raw->ciaddr); }
    const uint8_t* getYourIP() const { return raw->yiaddr; }
    const uint8_t* getNextServerIP() const { return raw->siaddr; }
    const uint8_t* getRelayAgentIP() const { return raw->giaddr; }
    const uint8_t* getClientMac() const { return raw->chaddr; }
    const uint8_t* getServerName() const { return raw->serverName; }
    const uint8_t* getBootFile() const { return raw->file; }
    uint32_t getMagicCookie() const { return utils::readU32(raw->magicCookie); }

    void setOpcode(uint8_t val)
        { raw->opcode = val; }
    void setHType(uint8_t val)
        { raw->hType = val; }
    void setHLen(uint8_t val)
        { raw->hLen = val; }
    void setHops(uint8_t val)
        { raw->hops = val; }
    void setXid(uint32_t val)
        { utils::writeU32(raw->xId, val); }
    void setXid(const uint8_t* val)
        { std::memcpy(raw->xId, val, 4); }
    void setSecs(uint16_t val)
        { utils::writeU16(raw->secs, val); }
    void setFlags(const uint8_t* val)
        { std::memcpy(raw->flags, val, 2); }
    void setClientIp(const uint8_t* val)
        { std::memcpy(raw->ciaddr, val, 4); }
    void setYourIp(const uint8_t* val)
        { std::memcpy(raw->yiaddr, val, 4); }
    void setNextServerIP(const uint8_t* val)
        { std::memcpy(raw->siaddr, val, 4); }
    void setRelayAgentIp(const uint8_t* val)
        { std::memcpy(raw->giaddr, val, 4); }
    void setClientMac(const uint8_t* val)
        { std::memcpy(raw->chaddr, val, getHLen()); }
    void setServerName(const uint8_t* val)
        { std::memcpy(raw->serverName, val, 64); }
    void setBootFile(const uint8_t* val)
        { std::memcpy(raw->file, val, 128); }
    void setMagicCookie(uint32_t val)
        { utils::writeU32(raw->magicCookie, val); }
};

inline bool parseDhcpOptions(const uint8_t* data, size_t size, std::vector<TLV8Option>& outOptions, bool overload = false)
{
    size_t offset = 0;
    while (offset + 2 <= size)
    {
        if (overload && data[offset] == 0x00) return true; // end of options in overload
        const uint8_t type = data[offset];
        const uint8_t length = data[offset + 1];

        if (offset + 2 + length > size) return false;

        uint8_t* value = const_cast<uint8_t*>(data) + offset + 2;
        outOptions.emplace_back(type, length, value, length);
        
        offset += 2 + length;
    }
    return offset == size;
}

} // namespace packet

#endif // DHCP_HEADER_HPP

