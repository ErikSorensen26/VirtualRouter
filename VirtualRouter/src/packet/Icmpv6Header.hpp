// Icmpv6Header.hpp

#ifndef ICMPV6_HEADER_HPP
#define ICMPV6_HEADER_HPP

#include <ByteString.hpp>
#include <optional>
#include <vector>
#include <Functions.h>

/**
 * @struct IcmpV6Header
 * @brief Represents an ICMPv6 header.
 */
struct IcmpV6Header
{
    ByteString type{};             ///< ICMPv6 type.
    ByteString code{};             ///< ICMPv6 code.
    ByteString checksum{};         ///< ICMPv6 checksum.
    ByteString reserved{};         ///< Reserved field.
    ByteString payload{};          ///< ICMPv6 payload.

    /**
     * @struct Option
     * @brief Represents an ICMPv6 option.
     */
    struct Option
    {
        ByteString option{};   ///< Option type.
        ByteString length{};   ///< Option length.
        ByteString value{};    ///< Option value.
    };

    std::vector<Option> options{}; ///< Vector of ICMPv6 options.

    const std::optional<ByteString> encapsulate() const
    {
        ByteString icmpv6String;
        if (type.size() != 1 || code.size() != 1 || reserved.size() != 4) return std::nullopt;

        icmpv6String.reserve(8);
        icmpv6String += type;
        icmpv6String += code;
        icmpv6String += ByteString(2, 0x00);
        icmpv6String += reserved;
        icmpv6String += payload;
        for (const auto& opt : options)
        {
            icmpv6String += opt.option;
            icmpv6String += opt.length;
            icmpv6String += opt.value;
        }

        return icmpv6String;
    }
    bool decapsulate(const ByteString& icmpV6Header)
    {
        size_t payloadSize = 0;
        switch(icmpV6Header[0].value)
        {
            case 0x82: payloadSize = 16;
                break;
            case 0x83: payloadSize = 16;
                break;
            case 0x84: payloadSize = 16;
                break;
            case 0x85: payloadSize = 0;
                break;
            case 0x86: payloadSize = 4;
                break;
            case 0x87: payloadSize = 16;
                break;
            case 0x88: payloadSize = 16;
                break;
            case 0x89: payloadSize = 32;
                break;
        }

        size_t icmpv6Start = 0;
        size_t icmpv6End = 0;
        if (icmpV6Header.size() < 8 + payloadSize) return false;

        type = icmpV6Header.substr(0, 1);
        code = icmpV6Header.substr(1, 1);
        checksum = icmpV6Header.substr(2, 2);
        reserved = icmpV6Header.substr(4, 4);
        payload = icmpV6Header.substr(8, payloadSize);
        icmpv6Start = 8 + payloadSize;
        icmpv6End = icmpV6Header.size();
        
        while (icmpv6Start != icmpv6End)
        {
            IcmpV6Header::Option option;
            if (!validateSize(icmpv6Start, 2, icmpV6Header)) return false;
            option.option = icmpV6Header.substr(icmpv6Start, 1);
            icmpv6Start += 1;
            option.length = icmpV6Header.substr(icmpv6Start, 1);
            icmpv6Start += 1;
            size_t icmpv6ADD = static_cast<size_t>((Functions::byteToNum(option.length) * 8) - 2);
            if (!validateSize(icmpv6Start, icmpv6ADD, icmpV6Header)) return false;
            option.value = icmpV6Header.substr(icmpv6Start, icmpv6ADD);
            icmpv6Start += icmpv6ADD;
            options.push_back(option);
        }
        return true;
    }
};

#endif // ICMPV6_HEADER_HPP
