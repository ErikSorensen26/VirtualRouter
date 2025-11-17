// VirtualRouter.h

#ifndef VIRTUAL_ROUTER_H
#define VIRTUAL_ROUTER_H

#include <string>
#include <RoutingTable.hpp>
#include <set>
#include <shared_mutex>
#include <AddressFamily.hpp>
#include <Eigrp.h>

class Interface;
namespace Eigrp
{
    struct EigrpAutonomousSystem;
    struct EigrpNamed;
}

enum class InterfaceType: uint8_t;
class Global;

class VirtualRouter
{
public:
    friend class EigrpTest;

    VirtualRouter(Global& global, const std::string& name)
        : global(global)
    {
        instanceName = name;
        enabledAddressFamilies.insert(AddressFamily::IPv4);
    }
    ~VirtualRouter();

    RoutingTable routingTable; ///< VRF RoutingTable
    std::set<AddressFamily> enabledAddressFamilies;

    // Interface management
    Interface* addInterface(Interface* interface, uint32_t key);
    Interface* getInterface(uint32_t key);
    std::unordered_map<uint32_t, Interface*> getinterfaceList();
    bool removeInterface(uint32_t key);

    // Eigrp Autonomous Systems
    Eigrp::EigrpAutonomousSystem* addEigrpAutonomousSystem(uint32_t id);
    Eigrp::EigrpAutonomousSystem* getEigrpAutonomousSystem(uint32_t id);
    bool removeEigrpAutonomousSystem(uint32_t id);

    // Eigrp Named Systems
    Eigrp::EigrpNamed* addEigrpNamed(const std::string& name);
    Eigrp::EigrpNamed* getEigrpNamed(const std::string& name);
    bool removeEigrpNamed(const std::string& name);

    // Interfaces
    std::shared_mutex interfaceMutex; ///< Interface list mutex.
    std::unordered_map<uint32_t, Interface*> interfaceList; ///< Interface list.

    // Eigrp
    std::shared_mutex eigrpAutonomousSystemMutex; ///< Eigrp list mutex.
    std::map<uint32_t, Eigrp::EigrpAutonomousSystem*> eigrpList; ///< Eigrp Autonomous System list.
    std::shared_mutex eigrpNamedMutex; ///< Named eigrp mutex.
    std::map<std::string, Eigrp::EigrpNamed*> namedEigrpList; ///< Eigrp Named groups.

    std::string instanceName;

    Global& global;
};

#endif // VIRTUAL_ROUTER_H
