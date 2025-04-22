// UdpHeader.hpp

#ifndef UDP_HEADER_HPP
#define UDP_HEADER_HPP

#include <ByteString.hpp>
#include <optional>

/**
 * @struct UdpHeader
 * @brief Represents a UDP (User Datagram Protocol) header.
 */
struct UdpHeader
{
    ByteString sourcePort{};        ///< Source port number.
    ByteString destinationPort{};   ///< Destination port number.
    ByteString length{};            ///< Length of UDP header and payload.
    ByteString checksum{};          ///< Checksum.

    const std::optional<ByteString> encapsulate() const
    {
        ByteString udpString;
        if (sourcePort.size() != 2 || destinationPort.size() != 2 || length.size() != 2 || checksum.size() != 2) return std::nullopt;

        udpString.reserve(8);
        udpString += sourcePort;
        udpString += destinationPort;
        udpString += length;
        udpString += ByteString(2, 0x00);

        return udpString;
    }
    bool decapsulate(const ByteString& udpHeader)
    {
        if (udpHeader.size() != 8) return false;

        sourcePort = udpHeader.substr(0, 2);
        destinationPort = udpHeader.substr(2, 2);
        length = udpHeader.substr(4, 2);
        checksum = udpHeader.substr(6, 2);

        return true;
    }
};

#endif // UDP_HEADER_HPP
