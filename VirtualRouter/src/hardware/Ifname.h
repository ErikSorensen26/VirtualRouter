// Ifname.h

#ifndef IFNAME_H
#define IFNAME_H

#include <string>

namespace hardware
{

[[maybe_unused]] unsigned int ifnametoindex(const char* ifname);
[[maybe_unused]] std::string indextoifname(unsigned int ifIndex);

} // namespace hardware

#endif // IFNAME_H

