// InterfaceTable.h

#ifndef INTERFACE_TABLE_H
#define INTERFACE_TABLE_H

#include <unordered_map>
#include <shared_mutex>

namespace EigrpConfigs
{
struct InterfaceConfigs;
}

class Interface;
struct IPAddress;

namespace Protocol
{
class EigrpInterface;

class InterfaceTable
{
public:

    /**
     * @brief Updates the list of EIGRP interfaces manually for multicast.
     * 
     * @param interface Pointer to the interface you want added.
     * @return The newly made EigrpInterface.
     */
    EigrpInterface* createInterface(Interface* interface);
    
    /**
     * @brief Updates the list of EIGRP interfaces based on address matching.
     *
     * Scans the network interfaces, matches them against configured EIGRP networks,
     * and updates the internal list of active EIGRP interfaces accordingly.
     */
    void refreshInterfaceList();

    /**
     * @brief Enables a specific unicast neighbor on a specific interface.
     *
     * Will attempt to add the unciast interface if able to, if able to it will
     * disable multicast on the interface removing all of the multicast neighobrs.
     * Will then store the unicast neighbor for the future.
     *
     * @param neighborIp IP of the static neighbor.
     * @param interfaceType The type of interface this neighbor is being added to.
     * @param id The interface ID of the interface that this neighbor is being added to.
     */
    void enableUnicastPeer(const IPAddress& neighborIp, uint32_t key);

    /**
     * @brief Disables a unicast neighbor to the specified interface.
     *
     * Will remove the unicast neighbor if the nieghbor is currently present
     * on the specified interface.
     *
     * @param neighborIp IP of the static neighbor.
     * @param interfaceType The type of interface this neighbor is being added to.
     * @param id The interface ID of the interface that this neighbor is being added to.
     */
    void disableUnicastPeer(const IPAddress& neighborIp, uint32_t key);

    void deactivateAll();

    // Lists
    std::unordered_map<uint32_t, EigrpInterface*> eigrpInterfaceList; ///< Map of EIGRP interfaces by identifier.
    std::unordered_map<uint32_t, EigrpConfigs::InterfaceConfigs*> eigrpInterfaceConfigList; ///< Map of EIGRP interface config by identifier.
    std::shared_mutex interfaceMutex;
};
}

#endif // EIGRP_INTERFACE_MANAGER_H
