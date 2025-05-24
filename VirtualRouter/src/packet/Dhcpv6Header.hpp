// Dhcpv6Header.hpp

#ifndef DHCPV6_HEADER_HPP
#define DHCPV6_HEADER_HPP

#include <ByteString.hpp>
#include <optional>
#include <vector>
#include <Functions.h>

/**
 * @struct Dhcpv6Header,
 * @brief Represents a DHCPv6 (Dynamic Host Configuration Protocol) header.
 */
struct Dhcpv6Header
{
    ByteString type{};          ///< DHCPv6 message type.
    ByteString transactionID{}; ///< DHCPv6 Transaction ID.
    
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

    const std::optional<ByteString> encapsulate() const
    {
        ByteString dhcpString;
        if (type.size() != 1 && transactionID.size() != 3) return std::nullopt;

        dhcpString.reserve(4);
        dhcpString += type;
        dhcpString += transactionID;

        for (Dhcpv6Header::Option opt : options)
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
        if (dhcpHeaders.size() < 4) return false;

        dhcpHeader = dhcpHeaders;
        type = dhcpHeader.substr(0, 1);
        transactionID = dhcpHeader.substr(1, 3);

        dhcpStart = 4;

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
            Dhcpv6Header::Option option;
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

#endif // DHCPV6_HEADER_HPP
