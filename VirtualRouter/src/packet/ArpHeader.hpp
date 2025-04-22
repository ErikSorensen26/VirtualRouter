// ArpHeader.hpp

#ifndef ARP_HEADER_HPP
#define ARP_HEADER_HPP

#include <ByteString.hpp>
#include <optional>

/**
 * @struct ArpHeader
 * @brief Represents an ARP (Address Resolution Protocol) header.
 */
struct ArpHeader
{
    ByteString hardwareType{};          ///< Hardware type (e.g., Ethernet).
    ByteString protocolType{};          ///< Protocol type (e.g., IPv4).
    ByteString hardwareSize{};          ///< Hardware address length.
    ByteString protocolSize{};          ///< Protocol address length.
    ByteString opcode{};                ///< ARP operation code.
    ByteString senderHardwareAddress{}; ///< Sender's hardware address.
    ByteString senderIpAddress{};        ///< Sender's IP address.
    ByteString targetHardwareAddress{}; ///< Target's hardware address.
    ByteString targetIpAddress{};        ///< Target's IP address.

    const std::optional<ByteString> encapsulate() const
    {
        ByteString arpString;
        if (hardwareType.size() != 2 || protocolType.size() != 2 || hardwareSize.size() != 1 || protocolSize.size() != 1 ||
            opcode.size() != 2 || senderHardwareAddress.size() != 6 || senderIpAddress.size() != 4 ||
            senderHardwareAddress.size() != 6 || senderIpAddress.size() != 4) return std::nullopt;

        arpString.reserve(28);
        arpString += hardwareType;
        arpString += protocolType;
        arpString += hardwareSize;
        arpString += protocolSize;
        arpString += opcode;
        arpString += senderHardwareAddress;
        arpString += senderIpAddress;
        arpString += targetHardwareAddress;
        arpString += targetIpAddress;

        return arpString;
    }
    bool decapsulate(const ByteString arpHeader)
    {
        if (arpHeader.size() == 28)
        {
            hardwareType = arpHeader.substr(0, 2);
            protocolType = arpHeader.substr(2, 2);
            hardwareSize = arpHeader.substr(4, 1);
            protocolSize = arpHeader.substr(5, 1);
            opcode = arpHeader.substr(6, 2);
            senderHardwareAddress = arpHeader.substr(8, 6);
            senderIpAddress = arpHeader.substr(14, 4);
            targetHardwareAddress = arpHeader.substr(18, 6);
            targetIpAddress = arpHeader.substr(24, 4);
        }
        else return false;
        return true;
    }
};

#endif // ARP_HEADER_HPP
