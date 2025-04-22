// VlanHeader.hpp

#ifndef VLAN_HEADER_HPP
#define VLAN_HEADER_HPP

#include <ByteString.hpp>
#include <optional>

/**
 * @struct VlanHeader
 * @brief Represents a VLAN (Virtual LAN) header.
 */
struct VlanHeader
{
    ByteString priority{}; ///< VLAN Priority field.
    ByteString dei{};      ///< Drop Eligible Indicator (DEI) field.
    ByteString id{};       ///< VLAN Identifier (VID) field.
    ByteString type{};     ///< EtherType field.

    const std::optional<ByteString> encapsulate() const
    {
        return std::nullopt;
    }
    bool decapsulate(const ByteString vlanHeader)
    {
        return false;
    }
};

#endif // VLAN_HEADER_HPP
