// Dhcpv6Header.hpp

#ifndef DHCPV6_HEADER_HPP
#define DHCPV6_HEADER_HPP

#include <HeaderHelpers.hpp>
#include <TLVOptions.hpp>
#include <vector>

/**
 * @struct Dhcpv6HeaderRaw
 * @brief Represents a raw DHCPv6 header.
 */
#pragma pack(push, 1)
struct Dhcpv6HeaderRaw
{
    uint8_t type;
    uint8_t transId[3];
};
#pragma pack(pop)

/**
 * @struct Dhcpv6Header,
 * @brief Represents a DHCPv6 (Dynamic Host Configuration Protocol) header.
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

inline bool parseDhcpv6Options(const uint8_t* data, size_t size, std::vector<TLV16Option>& outOptions)
{
    size_t offset = 0;
    while (offset + 4 <= size)
    {
        uint16_t code = readU16(data + offset);
        uint16_t length = readU16(data + offset + 2);

        if (offset + 4 + length > size) return false;

        const uint8_t* value = data + offset + 4;
        outOptions.emplace_back(code, length, value, length);

        offset += 4 + length;
    }
    return offset == size;
}

#endif // DHCPV6_HEADER_HPP
