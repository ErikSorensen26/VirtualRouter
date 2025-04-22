// AhHeader.hpp

#ifndef AH_HEADER_HPP
#define AH_HEADER_HPP

#include <ByteString.hpp>
#include <optional>

/**
 * @struct AhHeader
 * @brief Represents an AH (Authentication Header) header.
 */
struct AhHeader
{
    ByteString next{};      ///< Next Header field.
    ByteString length{};    ///< Payload Length field.
    ByteString reserved{};  ///< Reserved field.
    ByteString spi{};       ///< Security Parameters Index (SPI).
    ByteString sequence{};  ///< Sequence Number.
    ByteString icv{};        ///< Integrity Check Value (ICV).

    const std::optional<ByteString> encapsulate() const
    {
        ByteString ahString;
        if (next.size() != 1 || length.size() != 1 || reserved.size() != 2 || spi.size() != 4 || sequence.size() != 8) return std::nullopt;

        ahString.reserve(12);
        ahString += next;
        ahString += length;
        ahString += reserved;
        ahString += spi;
        ahString += sequence;
        ahString += icv;

        return ahString;
    }
    bool decapsulate(const ByteString ahHeader)
    {
        if (ahHeader.size() < 12)
        {
            next = ahHeader.substr(0, 1);
            length = ahHeader.substr(1, 1);
            reserved = ahHeader.substr(2, 2);
            spi = ahHeader.substr(4, 4);
            sequence = ahHeader.substr(8, 4);
            icv = ahHeader.substr(12);
        }

        return true;
    }
};

#endif //AH_HEADER_HPP
