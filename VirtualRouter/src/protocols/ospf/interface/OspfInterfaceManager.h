// OspfInterfaceManager.h

#ifndef OSPF_INTERFACE_MANAGER_H
#define OSPF_INTERFACE_MANAGER_H

#include <OspfInterfaceId.hpp>
#include <IPAddress.hpp>
#include <map>

class Interface;
struct IPAddress;

namespace OSPF
{
struct OspfInterfaceId;
class InterfaceConfigs;
class OspfProcess;
class OspfInterface;

class InterfaceManager
{
public:
    InterfaceManager(OspfProcess& process);
    ~InterfaceManager();

    OspfInterface& createInterface(Interface& interface, const OspfInterfaceId& id);
    void removeInterface(const OspfInterfaceId& id);
    void refreshInterfaceList();

    void deactivateAll();
    void syncNeighbors();

    OspfInterface* getInterface(const OspfInterfaceId& id);
    OspfInterface* getInterfaceByAddress(const IPAddress& addr);
    std::vector<IPAddress> getReachableInterfaces(uint32_t area);
    bool isInterfaceReachable(uint32_t area, uint32_t id);

    // Lists
    std::map<OspfInterfaceId, OspfInterface> ospfInterfaceList;

private:
    OspfProcess& process;
};
}

#endif // OSPF_INTERFACE_MANAGER_H
