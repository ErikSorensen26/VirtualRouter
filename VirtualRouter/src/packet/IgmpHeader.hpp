// IgmpHeader.hpp

#ifndef IGMP_HEADER_HPP
#define IGMP_HEADER_HPP

#include <ByteString.hpp>
#include <optional>
#include <Functions.h>

/**
 * @struct IgmpHeader
 * @brief Represents an IGMP (Internet Group Management Protocol) header.
 */
struct IgmpHeader
{
    ByteString type{};             ///< IGMP type.
    ByteString maxRestTime{};      ///< Max Resp Time.
    ByteString checksum{};         ///< IGMP checksum.
    ByteString multicastAddress{}; ///< Multicast address.

    /**
     * @struct V3
     * @brief Represents IGMPv3-specific fields.
     */
    struct V3
    {
        ByteString supress{};        ///< Suppress flag.
        ByteString qrv{};            ///< Querier's Robustness Variable.
        ByteString qqic{};           ///< Querier's Query Interval Code.
        ByteString numSrc{};         ///< Number of Sources.
    } v3;

    const std::optional<ByteString> encapsulate() const
    {
        ByteString igmpString;
        if (type.size() != 1 || maxRestTime.size() != 1 || checksum.size() != 2 || multicastAddress.size() != 4) return std::nullopt;

        igmpString.reserve(8);
        igmpString += type;
        igmpString += maxRestTime;
        igmpString += ByteString(2, 0x00);
        igmpString += multicastAddress;
        if (v3.supress.size() == 1 && v3.qrv.size() == 3 && v3.qqic.size() == 1 && v3.numSrc.size() == 2)
        {
            // NEEDS FURTHER IMPLEMENTATION
            igmpString.reserve(12);
            igmpString += Functions::binToByte(v3.supress + v3.qrv) + v3.qqic + v3.numSrc;
        }

        return igmpString;
    }
    bool decapsulate(const ByteString igmpHeader)
    {
        if (igmpHeader.size() < 8) return false;

        type = igmpHeader.substr(0, 1);
        maxRestTime = igmpHeader.substr(1, 1);
        checksum = igmpHeader.substr(2, 2);
        multicastAddress = igmpHeader.substr(4, 4);

        if (igmpHeader.size() == 12) 
        {
            v3.supress = (Functions::byteToBin(igmpHeader.substr(8, 1))).substr(5, 1);
            v3.qrv = (Functions::byteToBin(igmpHeader.substr(8, 1))).substr(6, 3);
            v3.qqic = igmpHeader.substr(9, 1);
            v3.numSrc = igmpHeader.substr(10, 2);
        }
        return true;
    }
};

#endif // IGMP_HEADER_HPP
