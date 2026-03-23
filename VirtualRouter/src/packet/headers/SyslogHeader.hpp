// SyslogHeader.hpp

#ifndef SYSLOG_HEADER_HPP
#define SYSLOG_HEADER_HPP

#include <span>

#include "packet/HeaderHelpers.hpp"

namespace packet
{

/**
 * @struct SyslogHeaderRaw
 * @brief Represents a raw Syslog header.
 */
#pragma pack(push, 0)
struct SyslogHeaderRaw
{
    uint8_t PRI;
};
#pragma pack(pop)

/**
 * @struct SyslogHeader
 * @brief Represents a Syslog packet header.
 */
struct SyslogHeader {
    DEFINE_PACKET_HEADER(SyslogHeaderRaw);
};

} // namespace packet

#endif // SYSLOG_HEADER_HPP

