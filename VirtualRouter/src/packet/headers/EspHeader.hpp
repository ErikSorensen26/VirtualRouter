/**
 * @file EspHeader.hpp
 * @brief ESP (Encapsulating Security Payload) wire-format header.
 */

// EspHeader.hpp

#ifndef ESP_HEADER_HPP
#define ESP_HEADER_HPP

#include "packet/HeaderHelpers.hpp"

namespace packet
{

/**
 * @struct EspHeaderRaw
 * @brief Represents a raw ESP (Encapsulating Security Payload) header.
 * @ingroup PACKET_HEADERS
 */
#pragma pack(push, 1)
struct EspHeaderRaw
{
    uint32_t spi;      ///< Security Parameter Index; identifies the security association.
    uint32_t sequence; ///< Monotonically increasing anti-replay sequence number.
};
#pragma pack(pop)

/**
 * @struct EspHeader
 * @brief Represents an ESP (Encapsulating Security Payload) header.
 * @ingroup PACKET_HEADERS
 */
struct EspHeader
{
    DEFINE_FIXED_HEADER(EspHeaderRaw);

    void setSpi() {}
    void setSequence() {}
};

} // namespace packet

#endif // ESP_HEADER_HPP

