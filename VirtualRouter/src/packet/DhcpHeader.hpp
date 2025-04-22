// DhcpHeader.hpp

#ifndef DHCP_HEADER_HPP
#define DHCP_HEADER_HPP

#include <ByteString.hpp>
#include <optional>
#include <vector>
#include <Functions.h>

/**
 * @struct DhcpHeader
 * @brief Represents a DHCP (Dynamic Host Configuration Protocol) header.
 */
struct DhcpHeader
{
    ByteString boot{};                         ///< Boot Message Type.
    ByteString hardwareType{};                 ///< Hardware Type.
    ByteString hardwareAddressLength{};        ///< Hardware Address Length.
    ByteString hops{};                          ///< Hops.
    ByteString transID{};                       ///< Transaction ID.
    ByteString secondsElapsed{};                ///< Seconds Elapsed.
    ByteString clientIP{};                      ///< Client IP Address.
    ByteString yourClientIP{};                  ///< 'Your' (Client) IP Address.
    ByteString nextServerIP{};                  ///< Next Server IP Address.
    ByteString relayAgentIP{};                  ///< Relay Agent IP Address.
    ByteString clientMacAddress{};              ///< Client MAC Address.
    ByteString clientHardwareAddressPadding{};  ///< Padding for Client Hardware Address.
    ByteString serverHostName{};                ///< Server Hostname.
    ByteString bootFile{};                      ///< Boot File Name.
    ByteString magicCookie{};                   ///< Magic Cookie.
    ByteString padding{};                       ///< Padding after DHCP options.
    ByteString end{};                           ///< DHCP Option End Marker.

    /**
     * @struct BootpFlags
     * @brief Represents BOOTP flags.
     */
    struct BootpFlags
    {
        ByteString broadcast{};   ///< Broadcast flag.
        ByteString reserved{};    ///< Reserved bits.
    } bootpFlags;

    /**
     * @struct Option
     * @brief Represents a DHCP option.
     */
    struct Option
    {
        ByteString option{};   ///< Option code.
        ByteString length{};   ///< Option length.
        ByteString value{};    ///< Option value.
    };

    std::vector<Option> options{}; ///< Vector of DHCP options.

    const std::optional<ByteString> encapsulate() const
    {
        ByteString dhcpString;
        if (boot.size() != 1 || hardwareType.size() != 1 || hardwareAddressLength.size() != 1 || hops.size() != 1 || transID.size() != 4 ||
            secondsElapsed.size() != 2 || bootpFlags.broadcast.size() != 1 || bootpFlags.reserved.size() != 15 || clientIP.size() != 4 ||
            yourClientIP.size() != 4 || nextServerIP.size() != 4 || relayAgentIP.size() != 4 || clientMacAddress.size() != 6 || clientHardwareAddressPadding.size() != 10 ||
            serverHostName.size() != 64 || bootFile.size() != 128 || magicCookie.size() != 4 || end.size() != 1) return std::nullopt;

        dhcpString.reserve(240);
        dhcpString += boot;
        dhcpString += hardwareType;
        dhcpString += hardwareAddressLength;
        dhcpString += hops;
        dhcpString += transID;
        dhcpString += secondsElapsed;
        dhcpString += Functions::binToByte(bootpFlags.broadcast + bootpFlags.reserved);
        dhcpString += clientIP;
        dhcpString += yourClientIP;
        dhcpString += nextServerIP;
        dhcpString += relayAgentIP;
        dhcpString += clientMacAddress;
        dhcpString += clientHardwareAddressPadding;
        dhcpString += serverHostName;
        dhcpString += bootFile;
        dhcpString += magicCookie;
        for (DhcpHeader::Option opt : options)
        {
            dhcpString += opt.option;
            dhcpString += opt.length;
            dhcpString += opt.value;
        }
        dhcpString += end;
        dhcpString += padding;

        return dhcpString;
    }
    bool decapsulate(const ByteString dhcpHeaders)
    {
        size_t dhcpStart;
        ByteString dhcpHeader;
        if (dhcpHeaders.size() < 240) return false;

        dhcpHeader = dhcpHeaders;
        boot = dhcpHeader.substr(0, 1);
        hardwareType = dhcpHeader.substr(1, 1);
        hardwareAddressLength = dhcpHeader.substr(2, 1);
        hops = dhcpHeader.substr(3, 1);
        transID = dhcpHeader.substr(4, 4);
        secondsElapsed = dhcpHeader.substr(8, 2);
        ByteString flags = Functions::byteToBin(dhcpHeader.substr(10, 2));
        bootpFlags.broadcast = flags.substr(0, 1);
        bootpFlags.reserved = flags.substr(1);
        clientIP = dhcpHeader.substr(12, 4);
        yourClientIP = dhcpHeader.substr(16, 4);
        nextServerIP = dhcpHeader.substr(20, 4);
        relayAgentIP = dhcpHeader.substr(24, 4);
        clientMacAddress = dhcpHeader.substr(28, 6);
        clientHardwareAddressPadding = dhcpHeader.substr(34, 10);
        serverHostName = dhcpHeader.substr(44, 64);
        bootFile = dhcpHeader.substr(108, 128);
        magicCookie = dhcpHeader.substr(236, 4);
        dhcpStart = 240;

        size_t dhcpEnd{};
        size_t dhcpLength = dhcpHeader.size();
        for (size_t i = dhcpLength - 1; i >= 0; --i)
        {
            if (static_cast<unsigned char>(dhcpHeader[i]) == 0xff)
            {
                dhcpEnd = i;
                break;
            }
        }

        while (dhcpStart != dhcpEnd)
        {
            DhcpHeader::Option option;
            option.option = dhcpHeader.substr(dhcpStart, 1);
            dhcpStart += 1;
            option.length = dhcpHeader.substr(dhcpStart, 1);
            dhcpStart += 1;
            option.value = dhcpHeader.substr(dhcpStart, static_cast<size_t>(Functions::byteToNum(option.length)));
            size_t dhcpADD = static_cast<size_t>(Functions::byteToNum(option.length));
            dhcpStart += dhcpADD;
            options.push_back(option);
        }

        end = dhcpHeader.substr(dhcpStart, 1);
        padding = dhcpHeader.substr(dhcpStart + 1);
        return true;
    }
};

#endif // DHCP_HEADER_HPP
