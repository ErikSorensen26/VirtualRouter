// IPv6Header.hpp

#ifndef IPV6_HEADER_HPP
#define IPV6_HEADER_HPP

#include <ByteString.hpp>
#include <optional>
#include <Functions.h>

/**
 * @struct IPv6Header
 * @brief Represents an IPv6 header.
 */
struct IPv6Header
{
    ByteString version{};           ///< Version field.
    ByteString trafficClass{};      ///< Traffic Class field.
    ByteString flowLabel{};         ///< Flow Label field.
    ByteString payloadLength{};     ///< Payload Length field.
    ByteString protocol{};          ///< Next Header field.
    ByteString hopLimit{};          ///< Hop Limit field.
    ByteString sourceAddress{};     ///< Source IPv6 address.
    ByteString destinationAddress{};///< Destination IPv6 address.

    const std::optional<ByteString> encapsulate() const
    {
        ByteString ipv6String;
        if (version.size() != 1 || trafficClass.size() != 2 || flowLabel.size() != 5 || payloadLength.size() != 2 ||
            protocol.size() != 1 || hopLimit.size() != 1 || sourceAddress.size() != 16 || 
            destinationAddress.size() != 16) return std::nullopt;

        ipv6String.reserve(40);
        ipv6String += Functions::hexToByte(version + trafficClass + flowLabel);
        ipv6String += payloadLength;
        ipv6String += protocol;
        ipv6String += hopLimit;
        ipv6String += sourceAddress;
        ipv6String += destinationAddress;

        return ipv6String;
    }
    bool decapsulate(const ByteString ipv6Header)
    {
        if (ipv6Header.size() != 40) return false;

        ByteString ipv6Temp = Functions::byteToHex(ipv6Header.substr(0, 4));
        version = ipv6Temp.substr(0, 1);
        trafficClass = ipv6Temp.substr(1, 2);
        flowLabel = ipv6Temp.substr(3, 5);
        payloadLength = ipv6Header.substr(4, 2);
        protocol = ipv6Header.substr(6, 1);
        hopLimit = ipv6Header.substr(7, 1);
        sourceAddress = ipv6Header.substr(8, 16);
        destinationAddress = ipv6Header.substr(24, 16);
        return true;
    }
};

#endif // IPV6_HEADER_HPP
