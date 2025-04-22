// SyslogHeader.hpp

#ifndef SYSLOG_HEADER_HPP
#define SYSLOG_HEADER_HPP

#include <ByteString.hpp>
#include <optional>

/**
 * @struct SyslogHeader
 * @brief Represents a Syslog packet header.
 */
struct SyslogHeader {
    ByteString PRI{};      ///< Priority value.
    ByteString message{};  ///< Syslog message content.

    std::optional<ByteString> encapsulate()
    {
        return std::nullopt;
    }
    bool decapsulate(const ByteString syslogHeader)
    {
        return false;
    }
};

#endif // SYSLOG_HEADER_HPP
