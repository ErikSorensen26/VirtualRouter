
#ifndef ICMPV6_HEADER_HPP
#define ICMPV6_HEADER_HPP

#include <HeaderHelpers.hpp>
#include <TLVOptions.hpp>
#include <vector>

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
        { writeU32(raw->reserved, val); }
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

#endif // ICMPV6_HEADER_HPP
