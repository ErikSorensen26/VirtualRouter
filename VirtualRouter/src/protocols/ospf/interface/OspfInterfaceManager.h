// OspfInterfaceManager.h

#ifndef OSPF_INTERFACE_MANAGER_H
#define OSPF_INTERFACE_MANAGER_H

#include <OspfInterfaceId.hpp>
#include <unordered_map>
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

    OspfInterface* createInterface(Interface* interface, OspfInterfaceId& id);
    void refreshInterfaceList();

    void deactivateAll();

    OspfInterface* getInterface(const OspfInterfaceId& id);

    // Lists
    std::map<OspfInterfaceId, OspfInterface> ospfInterfaceList;
    std::unordered_map<OspfInterfaceId, InterfaceConfigs*> ospfInterfaceConfigList;
    std::shared_mutex interfaceMutex;

private:
    OspfProcess& process;
};
}

#endif // OSPF_INTERFACE_MANAGER_H
