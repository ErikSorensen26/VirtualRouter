// OspfInterfaceManager.h

#ifndef OSPF_INTERFACE_MANAGER_H
#define OSPF_INTERFACE_MANAGER_H

#include <OspfInterfaceId.hpp>
#include <IPAddress.hpp>
#include <map>
#include <shared_mutex>

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

    OspfInterface& createInterface(Interface& interface, OspfInterfaceId& id);
    void refreshInterfaceList();

    void deactivateAll();

    OspfInterface* getInterface(const OspfInterfaceId& id);
    OspfInterface* getInterfaceByAddress(const IPAddress& addr);
    std::vector<IPAddress> getReachableInterfaces(uint32_t area);
    bool isInterfaceReachable(uint32_t area, uint32_t id);

    // Lists
    std::map<OspfInterfaceId, OspfInterface> ospfInterfaceList;
    mutable std::shared_mutex interfaceMutex;

private:
    OspfProcess& process;
};
}

#endif // OSPF_INTERFACE_MANAGER_H
