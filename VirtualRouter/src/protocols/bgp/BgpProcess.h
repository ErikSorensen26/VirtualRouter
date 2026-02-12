// BgpProcess

#ifndef BGP_PROCESS_H
#define BGP_PROCESS_H

#include <cstdint>
#include <vector>
#include <Registry.hpp>
#include <BgpRegistry.h>

namespace BGP
{
class BgpNeighbor;

class BgpProcess
{
public:
    BgpProcess(uint32_t as);

private:
    std::vector<BgpNeighbor> neighbors;
    const uint32_t listenerId;

    Config::Reference<Config::BgpRegistry> configs;
};
}

#endif // BGP_PROCESS_H
