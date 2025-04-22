// TlsHeader.hpp

#ifndef TLS_HEADER_HPP
#define TLS_HEADER_HPP

#include <ByteString.hpp>
#include <optional>

/**
 * @struct TlsHeader
 * @brief Represents a TLS (Transport Layer Security) header.
 */
struct TlsHeader
{
    ByteString type{};        ///< TLS Content Type.
    ByteString version{};     ///< TLS Version.
    ByteString length{};      ///< TLS Length.

    std::optional<ByteString> encapsulate()
    {
        return std::nullopt;
    }
    bool decapsulate(const ByteString Header)
    {
        return false;
    }
};

#endif // TLS_HEADER_HPP
