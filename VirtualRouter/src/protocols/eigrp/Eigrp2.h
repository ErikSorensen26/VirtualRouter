// Eigrp.h

#ifndef EIGRP_H
#define EIGRP_H

#include <vector>
#include <PacketStructure.h>
#include <Functions.h>
#include <mutex>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <atomic>
#include <shared_mutex>
#include <condition_variable>
#include <RoutingTable.h>
#include <TimeManager.h>
#include <unordered_map>
#include <unordered_set>
#include <PacketBuilder.hpp>
#include <tuple>

#define MAX_RETRANSMISSIONS 16
#define PACKET_TIMEOUT_MS 5000

/**
 * @file Eigrp.h
 * @brief Header file for the EIGRP (Enhanced Interior Gateway Routing Protocol) implementation.
 */

class Interface;
class VirtualRouter;
class Internal_EigrpTest;
enum class InterfaceType : uint8_t;


class InterfaceConfigs;

namespace Protocol 
{
    class Eigrp;
    class EigrpInterface;
    class TopologyTable;

    /**
     * @struct EigrpAutonomousSystems
     * @brief Manages multiple Autonomous Systems within the EIGRP process.
     */
    struct EigrpAutonomousSystem
    {
        Eigrp* ipv4 = nullptr;
        Eigrp* ipv6 = nullptr;
        bool ipv4Named = false;
        bool ipv6Named = false;
    };

    /**
     * @struct EigrpNamed
     * @brief Represents an instance of the EIGRP process.
     */
    struct EigrpNamed
    {
        Eigrp* ipv4 = nullptr;
        Eigrp* ipv6 = nullptr;
    };

    /**
     * @struct EigrpInterfaceInstance
     * @brief Represents EIGRP interfaces for IPv4 and IPv6.
     */
    struct EigrpInterfaceInstance
    {
        EigrpInterface* IPv4; ///< Pointer to the IPv4 EIGRP interface.
        EigrpInterface* IPv6; ///< Pointer to the IPv6 EIGRP interface.
    };

    /**
     * @class ClassicEigrp
     * @brief Represents a Classic EIGRP process.
     *
     * The ClassicEigrp class inherits from the base Eigrp class and implements
     * Classic-specific functionalities such as auto-summarization.
     */
    class ClassicEigrp : public Eigrp
    {
    public:
        /**
         * @brief Constructs a ClassicEigrp instance.
         *
         * Initializes the ClassicEigrp process with the specified AS number and
         * address family, enabling Classic-specific features like auto-summarization.
         *
         * @param as Autonomous System number.
         * @param af Address family.
         */
        ClassicEigrp(uint32_t& as, AddressFamily af, VirtualRouter* vrf) : Eigrp(as, af, vrf) {}

        /**
         * @brief Initializes the Classic EIGRP process.
         *
         * Sets up Classic-specific settings such as enabling auto-summarization,
         * configuring K-values, and preparing the routing table for Classic operations.
         */
        void initializeEigrp() override;

        /**
         * @brief Shuts down the Classic EIGRP process gracefully.
         *
         * Disables auto-summarization, removes all routes associated with Classic EIGRP,
         * and cleans up resources specific to Classic operations.
         */
        void shutdown() override;
    };

    /**
     * @class NamedEigrp
     * @brief Represents a Named EIGRP process.
     *
     * The NamedEigrp class inherits from the base Eigrp class and implements
     * Named-specific functionalities, allowing for multiple named EIGRP processes
     * within the same routing domain.
     */
    class NamedEigrp : public Eigrp
    {
    private:
        std::string processName; ///< Name of the Named EIGRP process.

    public:
        /**
         * @brief Constructs a NamedEigrp instance.
         *
         * Initializes the NamedEigrp process with the specified AS number, address
         * family, and a unique process name, enabling Named-specific features and
         * multiple concurrent EIGRP instances.
         *
         * @param as Autonomous System number.
         * @param af Address family.
         * @param name Name of the EIGRP process.
         * @param vrf Pointer to the routing instance being used.
         * @param multicast Indicates if this instance routes multicast routes.
         */
        NamedEigrp(uint32_t& as, AddressFamily af, const std::string& name, VirtualRouter* vrf, bool multicast);

        /**
         * @brief Initializes the Named EIGRP process.
         *
         * Sets up Named-specific settings such as disabling auto-summarization,
         * configuring unique Router IDs, and preparing the routing table for Named operations.
         */
        void initializeEigrp() override;

        /**
         * @brief Shuts down the Named EIGRP process gracefully.
         *
         * Removes all routes associated with the Named EIGRP process, disables features
         * specific to Named operations, and cleans up allocated resources.
         */
        void shutdown() override;

        /**
         * @brief Configures an interface with specific EIGRP settings.
         *
         * Applies EIGRP configurations to a specified network interface, enabling
         * or modifying EIGRP operations on that interface based on the provided settings.
         *
         * @param interfaceName Name of the interface.
         * @param configs Interface configuration settings.
         */
        void configureInterface(uint32_t interfaceId, const EigrpConfigs::InterfaceConfigs& configs);
    };

}


#endif // EIGRP_H
