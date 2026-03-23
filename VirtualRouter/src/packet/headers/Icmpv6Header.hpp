
#ifndef ICMPV6_HEADER_HPP
#define ICMPV6_HEADER_HPP

#include <vector>
#include <ByteUtils.hpp>
#include "packet/HeaderHelpers.hpp"
#include "packet/TlvOptions.hpp"

#define ICMPV6_OPCODE_UNREACHABLE                       0x01    ///< Unreachable error code for ICMPv6
#define ICMPV6_OPCODE_PACKET_TOO_BIG                    0x02    ///< Packet too big error for ICMPv6
#define ICMPV6_OPCODE_TIME_EXCEEDED                     0x03    ///< Time exceeded error for ICMPv6
#define ICMPV6_OPCODE_PARAMETER_PROBLEM                 0x04    ///< Parameter problem for ICMPv6
#define ICMPV6_OPCODE_ECHO_REQUEST                      0x80    ///< Echo request for Ping (128)
#define ICMPV6_OPCODE_ECHO_REPLY                        0x81    ///< Echo reply for Ping (129)
#define ICMPV6_OPCODE_MLD_LISTEN_QUERY                  0x82    ///< MLD Listen Query (130)
#define ICMPV6_OPCODE_MLD_LISTEN_REPORT                 0x83    ///< MLD Listen Report (131)
#define ICMPV6_OPCODE_MLD_LISTEN_DONE                   0x84    ///< MLD Listen Done (132)
#define ICMPV6_OPCODE_MLD_LISTEN_REPORT_V2              0x8F    ///< MLD Listen Report V2 (143)
#define ICMPV6_OPCODE_NDP_ROUTE_SOLICITATION            0x85    ///< NDP Route Solicitation (133)
#define ICMPV6_OPCODE_NDP_ROUTE_ADVERTISEMENT           0x86    ///< NDP Route Solicitation (134)
#define ICMPV6_OPCODE_NDP_NEIGHBOR_SOLICITATION         0x87    ///< NDP Neighbor Solicitation (135)
#define ICMPV6_OPCODE_NDP_NEIGHBOR_ADVERTISEMENT        0x88    ///< NDP Neighbor Solicitation (136)
#define ICMPV6_OPCODE_NDP_REDIRECT_MESSAGE              0x89    ///< NDP Message Redirect (137)
#define ICMPV6_OPCODE_NODE_INFORMATION_QUERY            0x8B    ///< Node Information Query (139)
#define ICMPV6_OPCODE_NODE_INFORMATION_RESPONSE         0x8C    ///< Node Information Response (140)
#define ICMPV6_OPCODE_MULTICAST_ROUTER_ADVERTISEMENT    0x98    ///< Multicast Router Advertisement (152)
#define ICMPV6_OPCODE_MULTICAST_ROUTER_SOLICITATION     0x99    ///< Multicast Router Solicitation (153)
#define ICMPV6_OPCODE_MULTICAST_ROUTER_TERMINATION      0x9A    ///< Multicast Router Termination (154)
#define ICMPV6_OPCODE_EXTENDED_ECHO_REQUEST             0xA0    ///< Extended Echo Request (160)
#define ICMPV6_OPCODE_EXTENDED_ECHO_REPLY               0xA1    ///< Extended Echo Reply (161)

#define ICMPV6_OPTION_NDP_SOURCE        0x01    ///< Source Option for NDP
#define ICMPV6_OPTION_NDP_TARGET        0x02    ///< Target Option for NDP
#define ICMPV6_OPTION_NDP_PREFIX        0x03    ///< Prefix Information Option for NDP
#define ICMPV6_OPTION_NDP_REDIRECT      0x04    ///< Redirect Option for NDP
#define ICMPV6_OPTION_NDP_MTU           0x05    ///< MTU Option for NDP
#define ICMPV6_OPTION_NDP_NBMA          0x06    ///< NBMA Option for NDP
#define ICMPV6_OPTION_NDP_CGA           0x0B    ///< CGA Option for NDP
#define ICMPV6_OPTION_NDP_RSA           0x0C    ///< RSA Option for NDP
#define ICMPV6_OPTION_NDP_TIMESTAMP     0x0D    ///< Timestamp Option for NDP
#define ICMPV6_OPTION_NDP_NONCE         0x0E    ///< Nonce Option for NDP
#define ICMPV6_OPTION_NDP_TRUST_ANCHOR  0x0F    ///< Trust Anchor Option for NDP
#define ICMPV6_OPTION_NDP_CERTIFICATE   0x10    ///< Certification Option for NDP
#define ICMPV6_OPTION_NDP_ROUTE_INFO    0x18    ///< Route Information Option for NDP
#define ICMPV6_OPTION_NDP_DNS_SERVER    0x19    ///< DNS Server Option for NDP
#define ICMPV6_OPTION_NDP_DNS_SEARCH    0x1F    ///< DNS Search Option for NDP

inline constexpr __uint128_t ICMPV6_SOLICIT_MULTICAST = (__uint128_t{0xFF02000000000000} << 64) | 0x000000000001FF00000000ULL;
inline constexpr __uint128_t ICMPV6_ALL_ROUTERS = (__uint128_t{0xFF02000000000000} << 64) | 0x0000000000000002ULL;

namespace packet
{

#pragma pack(push, 1)
struct Icmpv6HeaderRaw
{
    uint8_t type;
    uint8_t code;
    uint8_t checksum[2];
    uint8_t reserved[4];
};
#pragma pack(pop)

struct Icmpv6Header
{
    DEFINE_PACKET_HEADER(Icmpv6HeaderRaw);

    uint8_t getType() const
        { return raw->type; }
    uint8_t getCode() const
        { return raw->code; }
    const uint8_t* getChecksum() const
        { return raw->checksum; }
    const uint8_t* getReserved() const
        { return raw->reserved; }

    void setType(uint8_t val)
        { raw->type = val; }
    void setCode(uint8_t val)
        { raw->code = val; }
    void setChecksum(uint8_t* val)
        { memcpy(raw->checksum, val, 2); }
    void setReserved(uint8_t* val)
        { memcpy(raw->reserved, val, 4); }
    void setReservedInt(uint32_t val)
        { utils::writeU32(raw->reserved, val); }
};

// Parses trailing data into ICMPv6 options
inline bool parseIcmpv6Options(const uint8_t* data, size_t size, std::vector<TLV8Option>& outOptions)
{
    size_t offset = 0;
    while (offset + 2 <= size)
    {
        uint8_t type = data[offset];
        uint8_t lenUnits = data[offset + 1];

        size_t fullLen = lenUnits * 8;
        if (lenUnits == 0 || offset + fullLen > size) return false;

        uint8_t* value = const_cast<uint8_t*>(data) + offset + 2;
        size_t valueSize = fullLen - 2;

        outOptions.emplace_back( type, lenUnits, value, valueSize );
        offset += fullLen;
    }
    return offset == size;
}

} // namespace packet

#endif // ICMPV6_HEADER_HPP

