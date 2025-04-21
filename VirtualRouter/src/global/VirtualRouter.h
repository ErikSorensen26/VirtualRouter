// VirtualRouter.h

#ifndef VIRTUAL_ROUTER_H
#define VIRTUAL_ROUTER_H

#include <string>
#include <map>
#include <RoutingTable.h>
#include <InterfacePairHash.hpp>
#include <shared_mutex>

class Interface;
namespace Protocol
{
    class EigrpAutonomousSystem;
    class EigrpNamed;
}

enum class InterfaceType;

class VirtualRouter
{
public:
    friend class EigrpTest;

    VirtualRouter(const std::string& name) : instanceName(name) {}
    ~VirtualRouter();

    RoutingTable routingTable; ///< VRF RoutingTable

    // Interface management
    Interface* addInterface(Interface* interface, InterfaceType type, float interfaceID);
    Interface* getInterface(InterfaceType type, float interfaceID);
    bool removeInterface(InterfaceType type, float interfaceId);

    // Eigrp Autonomous Systems
    Protocol::EigrpAutonomousSystem* addEigrpAutonomousSystem(uint32_t id);
    Protocol::EigrpAutonomousSystem* getEigrpAutonomousSystem(uint32_t id);
    bool removeEigrpAutonomousSystem(uint32_t id);

    // Eigrp Named Systems
    Protocol::EigrpNamed* addEigrpNamed(const std::string& name);
    Protocol::EigrpNamed* getEigrpNamed(const std::string& name);
    bool removeEigrpNamed(const std::string& name);

    // Interfaces
    std::shared_mutex interfaceMutex; ///< Interface list mutex.
    std::unordered_map<std::pair<InterfaceType, float>, Interface*, InterfacePairHash> interfaceList; ///< Interface list.

    // Eigrp
    std::shared_mutex eigrpAutonomousSystemMutex; ///< Eigrp list mutex.
    std::map<uint32_t, Protocol::EigrpAutonomousSystem*> eigrpList; ///< Eigrp Autonomous System list.
    std::shared_mutex eigrpNamedMutex; ///< Named eigrp mutex.
    std::map<std::string, Protocol::EigrpNamed*> namedEigrpList; ///< Eigrp Named groups.

    std::string instanceName;
};

#endif // VIRTUAL_ROUTER_H
