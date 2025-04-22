// IPv4Header.hpp

#ifndef IPV4_HEADER_HPP
#define IPV4_HEADER_HPP

#include <ByteString.hpp>
#include <optional>
#include <Functions.h>

/**
 * @struct IPv4Header
 * @brief Represents an IPv4 header.
 */
struct IPv4Header
{
    ByteString version{};        ///< Version field.
    ByteString headerLength{};   ///< Header length field.
    ByteString serviceField{};   ///< Type of Service (ToS) field.
    ByteString totalLength{};    ///< Total length of the IP packet.
    ByteString identification{}; ///< Identification field.
    ByteString TTL{};            ///< Time-To-Live (TTL) field.
    ByteString protocol{};       ///< Protocol field.
    ByteString checksum{};       ///< Header checksum.
    ByteString sourceAddress{};  ///< Source IP address.
    ByteString destinationAddress{}; ///< Destination IP address.

    /**
     * @struct FragmentFlag
     * @brief Represents fragmentation flags and offset.
     */
    struct FragmentFlag
    {
        ByteString reserved{};        ///< Reserved flag.
        ByteString fragment{};        ///< More fragments flag.
        ByteString moreFragment{};    ///< Don't Fragment flag.
        ByteString fragmentOffset{};  ///< Fragment offset.
    } fragmentFlag;

    /**
     * @struct Options
     * @brief Represents IPv4 header options.
     */
    struct Options
    {
        /**
         * @struct Type
         * @brief Represents the type of IPv4 options.
         */
        struct Type
        {
            ByteString copy{};               ///< Copy flag.
            ByteString classControl{};       ///< Class-Control flag.
            ByteString routerAlert{};        ///< Router Alert flag.
        } type;

        ByteString length{};           ///< Length of the options field.
        ByteString routerAlert{};      ///< Router Alert option value.

    } options;

    const std::optional<ByteString> encapsulate() const
    {
        //Profiler::getInstance().notify("IPv4 encap start");
        if (version.size() != 1 || headerLength.size() != 1 || serviceField.size() != 1 || totalLength.size() != 2 ||
            identification.size() != 2 || TTL.size() != 1 || protocol.size() != 1 || checksum.size() != 2 || 
            sourceAddress.size() != 4 || destinationAddress.size() != 4) return std::nullopt;

        ByteString ipv4String;
        ipv4String.reserve(20);

        ipv4String.append(Functions::hexToByte(version + headerLength));
        ipv4String.append(serviceField);
        ipv4String.append(totalLength);
        ipv4String.append(identification);
        ipv4String.append(Functions::binToByte(fragmentFlag.reserved + fragmentFlag.fragment + fragmentFlag.moreFragment + fragmentFlag.fragmentOffset));
        ipv4String.append(TTL);
        ipv4String.append(protocol);
        ipv4String.append(ByteString(2, 0x00));
        ipv4String.append(sourceAddress);
        ipv4String.append(destinationAddress);
        ipv4String.append(Functions::binToByte(options.type.copy) + options.type.classControl + options.type.routerAlert);
        ipv4String.append(options.length);
        ipv4String.append(options.routerAlert);
        
        if (options.type.copy.size() == 1 && options.type.classControl.size() == 2 && 
            options.type.routerAlert.size() == 5 && options.length.size() == 1)
        {
            ByteString typeString;
            ipv4String.append(Functions::binToByte(options.type.copy + options.type.classControl + options.type.routerAlert));
            ipv4String.append(options.length);
            ipv4String.append(options.routerAlert);
        }

        //Profiler::getInstance().notify("IPv4 encap end");
        return ipv4String;
    }
    bool decapsulate(const ByteString ipv4Header)
    {
        //Profiler::getInstance().notify("IPv4 decap start");
        if (ipv4Header.size() < 20) return false;

        ByteString ipHeader = ipv4Header.substr(0, 1).toHex();
        version = ipHeader.substr(0, 1);
        headerLength = ipHeader.substr(1, 1);
        serviceField = ipv4Header.substr(1, 1);
        totalLength = ipv4Header.substr(2, 2);
        identification = ipv4Header.substr(4, 2);
        TTL = ipv4Header.substr(8, 1);
        protocol = ipv4Header.substr(9, 1);
        checksum = ipv4Header.substr(10, 2);
        sourceAddress = ipv4Header.substr(12, 4);
        destinationAddress = ipv4Header.substr(16, 4);

        ByteString fragmentFlags = Functions::byteToBin(ipv4Header.substr(6, 2));

        fragmentFlag.reserved = fragmentFlags.substr(0, 1);
        fragmentFlag.fragment = fragmentFlags.substr(1, 1);
        fragmentFlag.moreFragment = fragmentFlags.substr(2, 1);
        fragmentFlag.fragmentOffset = fragmentFlags.substr(3);

        if (ipv4Header.size() == 23)
        {
            ByteString type = Functions::byteToBin(ipv4Header.substr(20, 1));
            options.type.copy = type.substr(0, 1);
            options.type.classControl = type.substr(1, 2);
            options.type.routerAlert = type.substr(3, 5);
            options.length = ipv4Header.substr(21, 1);
            options.routerAlert = ipv4Header.substr(22, 1);
        }
        //Profiler::getInstance().notify("IPv4 decap end");
        return true;
    }
};

#endif // IPV4_HEADER_HPP
