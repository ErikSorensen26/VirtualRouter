// BgpNeighbor.h

#ifndef BGP_NEIGHBOR_H
#define BGP_NEIGHBOR_H

#include "configs/registry/router/BgpRegistry.h"

namespace BGP
{
class BgpProcess;
class Session;

class Neighbor
{
public:
    Neighbor(const IPAddress& ipAddress, BgpProcess& proc);
    ~Neighbor();

    const IPAddress neighborAddress;

    uint32_t rid = 0;

    Session* session = nullptr;
    BgpProcess& getProcess() { return process; }
    const BgpProcess& getProcess() const { return process; }

    Config::BgpNeighborSessionRegistry& getConfigs() { return configs.get(); }
    const Config::BgpNeighborSessionRegistry& getConfigs() const { return configs.get(); }
    
private:

    BgpProcess& process;

    Config::Reference<Config::BgpNeighborSessionRegistry> configs;
};
}

#endif // BGP_NEIGHBOR_H
