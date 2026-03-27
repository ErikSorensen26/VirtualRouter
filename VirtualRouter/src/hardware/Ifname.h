/**
 * @file Ifname.h
 * @brief Helpers to convert between interface names and kernel interface indices.
 */

#ifndef IFNAME_H
#define IFNAME_H

#include <string>

namespace hardware
{

/**
 * @brief Converts a network interface name to its kernel interface index.
 *
 * Wrapper around @c if_nametoindex(3).
 *
 * @param ifname  Null-terminated interface name (e.g. "eth0").
 * @return Kernel interface index on success, or @c 0 if the name is not found.
 */
[[maybe_unused]] unsigned int ifnametoindex(const char* ifname);

/**
 * @brief Converts a kernel interface index to its interface name.
 *
 * Wrapper around @c if_indextoname(3).
 *
 * @param ifIndex  Kernel interface index.
 * @return Interface name string on success, or an empty string if the index is unknown.
 */
[[maybe_unused]] std::string indextoifname(unsigned int ifIndex);

} // namespace hardware

#endif // IFNAME_H

