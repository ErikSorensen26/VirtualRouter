// EspHeader.hpp

#ifndef ESP_HEADER_HPP
#define ESP_HEADER_HPP

#include "packet/HeaderHelpers.hpp"

/**
 * @struct EspHeaderRaw
 */
#pragma pack(push, 1)
struct EspHeaderRaw
{
    uint32_t spi;
    uint32_t sequence;
};
#pragma pack(pop)

/**
 * @struct EspHeader
 * @brief Represents an ESP (Encapsulating Security Payload) header.
 */
struct EspHeader
{
    DEFINE_FIXED_HEADER(EspHeaderRaw);

    void setSpi() {}
    void setSequence() {}
};

#endif // ESP_HEADER_HPP
