// EspHeader.hpp

#ifndef ESP_HEADER_HPP
#define ESP_HEADER_HPP

#include <ByteString.hpp>
#include <optional>

/**
 * @struct EspHeader
 * @brief Represents an ESP (Encapsulating Security Payload) header.
 */
struct EspHeader
{
    ByteString spi{};       ///< Security Parameters Index (SPI).
    ByteString sequence{};  ///< Sequence Number.

    const std::optional<ByteString> encapsulate() const
    {
        ByteString espString;
        if (spi.size() != 4 || sequence.size() != 4) return std::nullopt;

        espString.reserve(8);
        espString += spi;
        espString += sequence;

        return espString;
    }
    bool decapsulate(const ByteString espHeader)
    {
        if (espHeader.size() != 8) return false;

        spi = espHeader.substr(0, 4);
        sequence = espHeader.substr(4, 4);

        return true;
    }
};

#endif // ESP_HEADER_HPP
