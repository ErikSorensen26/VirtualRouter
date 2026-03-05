// BgpNeighbor.h

#ifndef BGP_NEIGHBOR_H
#define BGP_NEIGHBOR_H

#include "configs/registry/router/BgpRegistry.h"

namespace BGP
{
class BgpProcess;

class Neighbor
{
public:
    Neighbor(const IPAddress& ipAddress, BgpProcess& proc);
    ~Neighbor();

    const IPAddress neighborAddress;

    uint32_t rid = 0;

    BgpProcess& getProcess() { return process; }
    Config::BgpNeighborSessionRegistry& getConfigs() { return configs.get(); }
    const BgpProcess& getProcess() const { return process; }
    
private:

    BgpProcess& process;

    Config::Reference<Config::BgpNeighborSessionRegistry> configs;
};
}

#endif // BGP_NEIGHBOR_H
