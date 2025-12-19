// Ospf.h

#ifndef OSPF_H
#define OSPF_H

#include <OspfTypes.hpp>

namespace OSPF 
{
struct OspfProcess
{
public:
    OspfConfigs& getConfigs() { return cfgs; }

private:
    OspfConfigs cfgs;
};
}

#endif // OSPF_H
