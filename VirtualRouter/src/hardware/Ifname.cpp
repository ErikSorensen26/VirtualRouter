// Ifname.cpp

#include <net/if.h>
#include <stdexcept>
#include "Ifname.h"

unsigned int ifnametoindex(const char* ifname)
{
    unsigned int idx = if_nametoindex(ifname);
    return idx;
}

std::string indextoifname(unsigned int ifIndex)
{
    char nameBuf[IF_NAMESIZE];
    if (if_indextoname(ifIndex, nameBuf))
        return std::string(nameBuf);
    return {};
}
