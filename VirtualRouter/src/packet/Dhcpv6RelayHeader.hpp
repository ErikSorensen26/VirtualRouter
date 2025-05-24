// Dhcpv6RelayHeader.hpp

#ifndef DHCPV6_RELAY_HEADER_HPP
#define DHCPV6_RELAY_HEADER_HPP

#include <ByteString.hpp>
#include <optional>
#include <vector>
#include <Functions.h>

/**
 * @brief DHCPv6 Relay header for relay agent interactions.
 */
struct Dhcpv6RelayHeader
{
    ByteString msgType;
    ByteString hopCount;
    ByteString linkAddress;
    ByteString peerAddress;

    /**
     * @struct option
     * @brief represents DHCPv6 option
     */
    struct Option
    {
        ByteString option{};   ///< Option code.
        ByteString length{};   ///< Option length.
        ByteString value{};    ///< Option value.
    };

    std::vector<Option> options{};  ///< Vector of DHCPv6 Options.
                                    ///
    const std::optional<ByteString> encapsulate() const
    {
        ByteString dhcpString;
        if (msgType.size() != 1 && hopCount.size() != 1 && linkAddress.size() != 16 && peerAddress.size() != 16) return std::nullopt;

        dhcpString.reserve(34);
        dhcpString += msgType;
        dhcpString += hopCount;

        for (Dhcpv6RelayHeader::Option opt : options)
        {
            dhcpString += opt.option;
            dhcpString += opt.length;
            dhcpString += opt.value;
        }

        return dhcpString;
    }
    bool decapsulate(const ByteString dhcpHeaders)
    {
        size_t dhcpStart;
        ByteString dhcpHeader;
        if (dhcpHeaders.size() < 34) return false;

        dhcpHeader = dhcpHeaders;
        msgType = dhcpHeader.substr(0, 1);
        hopCount = dhcpHeader.substr(1, 1);
        linkAddress = dhcpHeader.substr(2, 16);
        peerAddress = dhcpHeader.substr(18, 16);

        dhcpStart = 34;

        size_t dhcpEnd{};
        size_t dhcpLength = dhcpHeader.size();
        for (size_t i = dhcpLength - 1; i >= 0; --i)
        {
            if (dhcpHeader[i] == 0xff)
            {
                dhcpEnd = i;
                break;
            }
        }

        while (dhcpStart != dhcpEnd)
        {
            Dhcpv6RelayHeader::Option option;
            option.option = dhcpHeader.substr(dhcpStart, 2);
            dhcpStart += 2;
            option.length = dhcpHeader.substr(dhcpStart, 2);
            dhcpStart += 2;
            size_t length = Functions::byteToNum(option.length);
            option.value = dhcpHeader.substr(dhcpStart, static_cast<size_t>(length));
            size_t dhcpADD = static_cast<size_t>(length);
            dhcpStart += dhcpADD;
            options.push_back(option);
        }

        return true;
    }
};

#endif // DHCPV6_RELAY_HEADER_HPP
