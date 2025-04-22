// Icmpv6Header

#ifndef ICMP_HEADER_HPP
#define ICMP_HEADER_HPP

#include <ByteString.hpp>
#include <optional>

/**
 * @struct IcmpHeader
 * @brief Represents an ICMP (Internet Control Message Protocol) header.
 */
struct IcmpHeader
{
    ByteString type{};             ///< ICMP type.
    ByteString code{};             ///< ICMP code.
    ByteString checksum{};         ///< ICMP checksum.
    ByteString identifier{};       ///< Identifier.
    ByteString sequenceNumber{};   ///< Sequence number.

    const std::optional<ByteString> encapsulate() const
    {
        ByteString icmpString;
        if (type.size() != 1 || code.size() != 1 || checksum.size() != 2 || identifier.size() != 2 || sequenceNumber.size() != 2) return std::nullopt;

        icmpString.reserve(8);
        icmpString += type;
        icmpString += code;
        icmpString += ByteString(2, 0x00);
        icmpString += identifier;
        icmpString += sequenceNumber;

        return icmpString;
    }
    bool decapsulate(const ByteString& icmpHeader)
    {
        if (icmpHeader.size() != 8) return false;

        type = icmpHeader.substr(0, 1);
        code = icmpHeader.substr(1, 1);
        checksum = icmpHeader.substr(2, 2);
        identifier = icmpHeader.substr(4, 2);
        sequenceNumber = icmpHeader.substr(6, 2);

        return true;
    }
};

#endif // ICMP_HEADER_HPP
