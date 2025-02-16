// VirtualRouter.h

#ifndef VIRTUAL_ROUTER_H
#define VIRTUAL_ROUTER_H

#include <string>
#include <map>
#include <mutex>
#include <functional>
#include <RoutingTable.h>

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
    void forEachInterface(const std::function<void(InterfaceType, float, Interface*)>& func, const std::vector<InterfaceType>& types = {}, bool include = true);

    // Eigrp Autonomous Systems
    Protocol::EigrpAutonomousSystem* addEigrpAutonomousSystem(uint32_t id);
    Protocol::EigrpAutonomousSystem* getEigrpAutonomousSystem(uint32_t id);
    bool removeEigrpAutonomousSystem(uint32_t id);
    void forEachEigrpAutonomousSystem(const std::function<void(uint32_t, Protocol::EigrpAutonomousSystem*)>& func);

    // Eigrp Named Systems
    Protocol::EigrpNamed* addEigrpNamed(const std::string& name);
    Protocol::EigrpNamed* getEigrpNamed(const std::string& name);
    bool removeEigrpNamed(const std::string& name);
    void forEachEigrpNamed(const std::function<void(std::string, Protocol::EigrpNamed*)> func);

private:
    // Interfaces
    std::mutex interfaceMutex; ///< Interface list mutex.
    std::map<InterfaceType, std::map<float, Interface*>> interfaceList; ///< Interface list.

    // Eigrp
    std::mutex eigrpAutonomousSystemMutex; ///< Eigrp list mutex.
    std::map<uint32_t, Protocol::EigrpAutonomousSystem*> eigrpList; ///< Eigrp Autonomous System list.
    std::mutex eigrpNamedMutex; ///< Named eigrp mutex.
    std::map<std::string, Protocol::EigrpNamed*> namedEigrpList; ///< Eigrp Named groups.

    std::string instanceName;
};

#endif // VIRTUAL_ROUTER_H
