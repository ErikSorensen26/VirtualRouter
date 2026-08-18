/**
 * @file Dhcpv6Header.hpp
 * @brief DHCPv6 (RFC 8415) wire-format header, option codes, and TLV option parsing.
 */

// Dhcpv6Header.hpp

#ifndef DHCPV6_HEADER_HPP
#define DHCPV6_HEADER_HPP

#include <vector>
#include <ByteUtils.hpp>
#include "packet/HeaderHelpers.hpp"
#include "packet/TlvOptions.hpp"

#define DHCPV6_TYPE_SOLICIT                 0x01U ///< DHCPv6 Solicit message type
#define DHCPV6_TYPE_ADVERTISE               0x02U ///< DHCPv6 Advertise message type
#define DHCPV6_TYPE_REQUEST                 0x03U ///< DHCPv6 Request message type
#define DHCPV6_TYPE_CONFIRM                 0x04U ///< DHCPv6 Confirm message type
#define DHCPV6_TYPE_RENEW                   0x05U ///< DHCPv6 Renew message type
#define DHCPV6_TYPE_REBIND                  0x06U ///< DHCPv6 Rebind message type
#define DHCPV6_TYPE_REPLY                   0x07U ///< DHCPv6 Reply message type
#define DHCPV6_TYPE_RELEASE                 0x08U ///< DHCPv6 Release message type
#define DHCPV6_TYPE_DECLINE                 0x09U ///< DHCPv6 Decline message type
#define DHCPV6_TYPE_RECONFIGURE             0x0AU ///< DHCPv6 Reconfigure message type
#define DHCPV6_TYPE_INFORMATION_REQUEST     0x0BU ///< DHCPv6 Information Request message type
#define DHCPV6_TYPE_RELAY_FORWARD           0x0CU ///< DHCPv6 Relay Forward message type
#define DHCPV6_TYPE_RELAY_REPLY             0x0DU ///< DHCPv6 Relay Reply message type

#define DHCPV6_OPTION_CLIENT_ID             0x0001U ///< DHCPv6 Option for Client ID (1)
#define DHCPV6_OPTION_SERVER_ID             0x0002U ///< DHCPv6 Option for Server ID (2)
#define DHCPV6_OPTION_IA_NA                 0x0003U ///< DHCPv6 Option for Identity Association of Non-Temporary Address (3)
#define DHCPV6_OPTION_IA_TA                 0x0004U ///< DHCPv6 Option for Identity Association of Temporary Address (4)
#define DHCPV6_OPTION_IAADDR                0x0005U ///< DHCPv6 Option for Identity Association Address (5)
#define DHCPV6_OPTION_OPTION_REQUEST        0x0006U ///< DHCPv6 Option for Option Request (6)
#define DHCPV6_OPTION_PREFERENCE            0x0007U ///< DHCPv6 Option for Preference (7)
#define DHCPV6_OPTION_ELAPSED_TIME          0x0008U ///< DHCPv6 Option for Elapsed Time (8)
#define DHCPV6_OPTION_RELAY_MSG             0x0009U ///< DHCPv6 Option for Relay Message (9)
#define DHCPV6_OPTION_AUTHENTICATION        0x000BU ///< DHCPv6 Option for Authentication (11)
#define DHCPV6_OPTION_UNICAST               0x000CU ///< DHCPv6 Option for Unicast (12)
#define DHCPV6_OPTION_STATUS_CODE           0x000DU ///< DHCPv6 Option for Status Code (13)
#define DHCPV6_OPTION_RAPID_COMMIT          0x000EU ///< DHCPv6 Option for Rapid Commit (14)
#define DHCPV6_OPTION_VENDOR_OPTS           0x0010U ///< DHCPv6 Option for Vendor Options (16)
#define DHCPV6_OPTION_VENDOR_CLASS_ID       0x0011U ///< DHCPv6 Option for Vendor Class ID (17)
#define DHCPV6_OPTION_INTERFACE_ID          0x0012U ///< DHCPv6 Option for interface::Interface ID (18)
#define DHCPV6_OPTION_RECONFIG_MESSAGE      0x0013U ///< DHCPv6 Option for Reconfigure Message (19)
#define DHCPV6_OPTION_RECONFIG_ACCEPT       0x0014U ///< DHCPv6 Option for Reconfigure Accept (20)
#define DHCPV6_OPTION_DNS_SERVERS           0x0017U ///< DHCPv6 Option for DNS Servers (23)
#define DHCPV6_OPTION_DOMAIN_SEARCH         0x0018U ///< DHCPv6 Option for Domain Search (24)
#define DHCPV6_OPTION_IA_PD                 0x0019U ///< DHCPv6 Option for Identity Association of Prefix Delegation (25)
#define DHCPV6_OPTION_IA_PREFIX             0x001AU ///< DHCPv6 Option for Identity Association Prefix (26)
#define DHCPV6_OPTION_INFO_REFRESH_TIME     0x0020U ///< DHCPv6 Option for Info Refresh Time (32)
#define DHCPV6_OPTION_REMOTE_ID             0x0025U ///< DHCPv6 Option for Remote ID (37)
#define DHCPV6_OPTION_FQDN                  0x0027U ///< DHCPv6 Option for Fully Qualified Domain Name (39)
#define DHCPV6_OPTION_NTP_SERVERS           0x0038U ///< DHCPv6 Option for NTP Servers (56)
#define DHCPV6_OPTION_SOL_MAX_RT            0x0052U ///< DHCPv6 Option for Solicit Maximum Retransmission Time (82)
#define DHCPV6_OPTION_INF_MAX_RT            0x0053U ///< DHCPv6 Option for Information Maximum Retransmission Time (83)
#define DHCPV6_OPTION_LEASE_QUERY           0x002CU ///< DHCPv6 Option for Lease Query (44)
#define DHCPV6_OPTION_LEASE_CLIENT_DATA     0x002DU ///< DHCPv6 Option for Lease Client Data (45)
#define DHCPV6_OPTION_LEASE_QUERY_DATA      0x002EU ///< DHCPv6 Option for Lease Query Data (46)

#define DHCPV6_OPTION_QUERY_BY_ADDR         0x0001U ///< DHCPv6 Option for Query By Address (1)
#define DHCPV6_OPTION_QUERY_BY_CLIENT_ID    0x0002U ///< DHCPv6 Option for Query By Client ID (2)

#define DHCPV6_STATUS_SUCCESS       0x0000U ///< Status code for Success
#define DHCPV6_STATUS_UNSPEC_FAIL   0x0001U ///< Status code for Unspecified Failure
#define DHCPV6_STATUS_NO_ADDRESS    0x0002U ///< Status code for No Address Available
#define DHCPV6_STATUS_NO_BINDING    0x0003U ///< Status code for No Binding Available
#define DHCPV6_STATUS_NOT_ON_LINK   0x0004U ///< Status code for Not On Link
#define DHCPV6_STATUS_USE_MULTICAST 0x0005U ///< Status code for Use Multicast
#define DHCPV6_STATUS_NO_PREFIX     0x0006U ///< Status code for No Prefix Available

#define DHCPV6_TIMER_SOL_MAX_DELAY      1 ///< Max delay of first Solicit
#define DHCPV6_TIMER_SOL_TIMEOUT        1 ///< Initial Solicit Timeout
#define DHCPV6_TIMER_SOL_MAX_RT      3600 ///< Max Solicit Timeout Value
#define DHCPV6_TIMER_REQ_TIMEOUT        1 ///< Initial Request Timeout
#define DHCPV6_TIMER_REQ_MAX_TIMEOUT   30 ///< Max Request Timeout
#define DHCPV6_TIMER_REQ_MAX_RC        10 ///< Max Request Retry Attempts
#define DHCPV6_TIMER_CNF_MAX_DELAY      1 ///< Max Delay of first Confirm
#define DHCPV6_TIMER_CNF_TIMEOUT        1 ///< Initial Confirm Timeout
#define DHCPV6_TIMER_CNF_MAX_RT         4 ///< Max Confirm Timeout
#define DHCPV6_TIMER_CNF_MAX_RD        10 ///< Max Confirm Duration
#define DHCPV6_TIMER_REN_TIMEOUT       10 ///< Initial Renew Timeout
#define DHCPV6_TIMER_REN_MAX_RT       600 ///< Max Renew Timeout
#define DHCPV6_TIMER_REB_TIMEOUT       10 ///< Initial Rebind Timeout
#define DHCPV6_TIMER_REB_MAX_RT       600 ///< Max Rebing Timeout

inline constexpr uint8_t DHCPV6_CLIENT_TO_SERVER[16] = { 0xFF, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02 }; ///< Site-local multicast address for client-to-relay/server sends (ff05::1:2).
inline constexpr uint8_t DHCPV6_RELAY_TO_SERVER[16] = { 0xFF, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03 }; ///< All_DHCP_Servers site-local multicast address (ff05::1:3).
inline constexpr uint8_t DHCPV6_SERVER_TO_ALL[16] = { 0xFF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 }; ///< All_DHCP_Relay_Agents_and_Servers link-local multicast address (ff02::1:1).

namespace packet
{

/**
 * @struct Dhcpv6HeaderRaw
 * @brief Represents a raw DHCPv6 header.
 * @ingroup PACKET_HEADERS
 */
#pragma pack(push, 1)
struct Dhcpv6HeaderRaw
{
    uint8_t type;         ///< Message type (DHCPV6_TYPE_*).
    uint8_t transId[3];   ///< Client/server transaction ID; correlates a message exchange.
};
#pragma pack(pop)

/**
 * @struct Dhcpv6Header
 * @brief Represents a DHCPv6 (Dynamic Host Configuration Protocol v6) header.
 * @ingroup PACKET_HEADERS
 */
struct Dhcpv6Header
{
    DEFINE_PACKET_HEADER(Dhcpv6HeaderRaw);

    uint8_t getType() const
        { return raw->type; }
    const uint8_t* getTransId() const
        { return raw->transId; }

    void setType(uint8_t val)
        { raw->type = val; }
    void setTransId(const uint8_t* val)
        { std::memcpy(raw->transId, val, 3); }
};

/**
 * @brief Parses a buffer of DHCPv6 TLV options (2-byte code, 2-byte length) into @p outOptions.
 *
 * @param data Option buffer to parse.
 * @param size Length of @p data in bytes.
 * @param[out] outOptions Parsed options, appended in encounter order.
 * @return True if the buffer was fully consumed with valid options, false on a
 * truncated or malformed option.
 */
inline bool parseDhcpv6Options(const uint8_t* data, size_t size, std::vector<TLV16Option>& outOptions)
{
    size_t offset = 0;
    while (offset + 4 <= size)
    {
        uint16_t code = utils::read<uint16_t>(data + offset);
        uint16_t length = utils::read<uint16_t>(data + offset + 2);

        if (offset + 4 + length > size) return false;

        uint8_t* value = const_cast<uint8_t*>(data) + offset + 4;
        outOptions.emplace_back(code, length, value, length);

        offset += 4 + length;
    }
    return offset == size;
}

} // namespace packet

#endif // DHCPV6_HEADER_HPP

