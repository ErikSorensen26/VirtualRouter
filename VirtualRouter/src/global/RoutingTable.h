// RoutingTable.h

#ifndef ROUTING_TABLE_H
#define ROUTING_TABLE_H

#include <vector>
#include <mutex>
#include <PacketStructure.h>
#include <chrono>
#include <Functions.h>
#include <Logger.h>
#include <set>
#include <IPAddress.hpp>

enum class AddressFamily: uint8_t;

/**
 * @class RoutingTable
 * @brief Singleton class managing various routing-related tables and operations.
 *
 * The RoutingTable class provides a centralized management system for different routing tables,
 * including Routing Entries, Forwarding Information Base (FIB)
 * Routing Information Base (RIB), Policy-Based Routing (PBR) tables, Multicast tables, Access Control Lists (ACL),
 * and Enhanced Interior Gateway Routing Protocol (EIGRP) tables.
 *
 * It ensures thread-safe access and manipulation of these tables using mutexes and follows the Singleton design pattern
 * to guarantee a single instance throughout the application lifecycle.
 */
class RoutingTable 
{
public:

    /**
     * @brief Private constructor for the RoutingTable class.
     *
     * Initializes the RoutingTable instance. The constructor is private to enforce the Singleton pattern.
     */
    RoutingTable() = default;

    /**
     * @brief Default destructor for the RoutingTable class.
     */
    virtual ~RoutingTable() = default;

    /**
     * @struct Eigrp
     * @brief Represents an entry in the Enhanced Interior Gateway Routing Protocol (EIGRP) table.
     */
    struct Eigrp {
        Eigrp(uint32_t outInterface) 
          : interface(outInterface),
            age(std::chrono::steady_clock::now()) {}

        enum class RouteType { INTERNAL, EXTERNAL, SUMMARY, CONNECTED, STATIC, WITHDRAW };

        RouteType routeType{};              ///< Type of the route.

        IPAddress network;                  ///< Network address.
        IPAddress nextHop;                  ///< Next hop IP address.
        IPAddress originRouter;           ///< Originating router.
        uint8_t successor[16];              ///< Successor route.
        uint8_t feasibleSuccessor[16];      ///< Feasible successor route.
        uint8_t routeSource[16];            ///< Source of the route.
        uint8_t activeOrPassive[16];        ///< Active or passive state.
        uint8_t flags{};                    ///< Flags associated with the route.

        uint32_t interface{};               ///< Out interface.
        uint32_t metric{};                  ///< Metric value.
        uint32_t feasibleDistance{};        ///< Feasible distance.
        uint32_t reportedDistance{};        ///< Reported distance.
        uint32_t adminDistance{};           ///< Administrative distance.
        uint32_t sequenceNumber{};          ///< Sequence number.
        uint32_t routeTag{};                ///< Route tag.
        uint32_t bandwidth{};               ///< Bandwidth.
        uint32_t delay{};                   ///< Delay.
        uint32_t originAS{};                ///< Origin Autonomous System.
        uint32_t extendedMetric{};          ///< Extended metric.
        uint32_t extendedId{};              ///< Extended ID.

        uint16_t holdTime{};                ///< Hold time for the route.
        uint16_t updateTimer{};             ///< Update timer value.
        uint16_t retransmitInterval{};      ///< Retransmit interval.
        uint16_t mtu{};                     ///< Maximum Transmission Unit.

        uint8_t hopCount{};                 ///< Hop count.
        uint8_t load{};                     ///< Load.
        uint8_t reliability{};              ///< Reliability.
        uint8_t mask{};                     ///< Subnet mask.

        bool stuckInActive{};               ///< Stuck in active state.
        std::chrono::steady_clock::time_point age; ///< Age of the EIGRP entry.
        std::vector<IPAddress> nextHopsVector{};       ///< List of next hop IP addresses.
        std::set<IPAddress> nextHopsSet{};   ///< List of next hop IP addresses.
        bool isIPv6{false};                     ///< Flag indicating if the route is IPv6.
    };

    /**
     * @brief Prints the Enhanced Interior Gateway Routing Protocol (EIGRP) table to the console.
     */
    void printEigrpTable();

    /**
     * @brief Updates the EIGRP table with a given route, applying variance for IPv6.
     *
     * @param route The EIGRP route to update.
     * @param variance The variance factor to apply.
     * @param af The address family (IPv4 or IPv6).
     * @param as The Autonomous system
     */
    void updateEigrpWithVarianceIPv6(Eigrp* route, uint8_t variance, AddressFamily af, uint32_t as);

    /**
     * @brief Adds a new EIGRP route to the table.
     *
     * @param route The EIGRP route to add.
     * @param af The address family (IPv4 or IPv6).
     * @param as The Autonomous system
     */
    bool addEigrp(Eigrp* route, AddressFamily af, uint32_t as);

    /**
     * @brief Updates a EIGRP route to the table.
     *
     * @param route The EIGRP route to add.
     * @param af The address family (IPv4 or IPv6).
     * @param as The Autonomous system
     */
    bool updateEigrp(Eigrp* route, AddressFamily af, uint32_t as);

    /**
     * @brief Removes an EIGRP route from the table.
     *
     * @param network The network address of the route to remove.
     * @param mask The subnet mask of the route to remove.
     * @param af The address family (IPv4 or IPv6).
     * @param as The Autonomous system
     */
    void removeEigrp(const uint8_t* network, uint8_t mask, AddressFamily af, uint32_t as);

    /**
     * @brief Removes all EIGRP routes from the table
     *
     * Removes all EIGRP routes from the RIB and FIB
     * for a specific autonomous system.
     *
     * @param af The address family (IPv4 or IPv6).
     * @param as The Autonomous system
     */
    void removeAllEigrp(AddressFamily af, uint32_t as);
    void removeEigrpWithOutInterface(AddressFamily af, uint32_t as, uint32_t out);

    /**
     * @brief Retrieves all EIGRP routes for a specific address family.
     *
     * @param af The address family (IPv4 or IPv6).
     * @return std::vector<Eigrp> A vector of EIGRP routes.
     * @param as The Autonomous system
     */
    std::vector<RoutingTable::Eigrp*> getAllEigrpRoutes(AddressFamily af, uint32_t as);

    /**
     * @brief Retrieves a specific EIGRP route based on destination and mask.
     *
     * @param destination The destination network address.
     * @param mask The subnet mask.
     * @param af The address family (IPv4 or IPv6).
     * @param as The Autonomous system
     * @return std::optional<RoutingTable::Eigrp> The EIGRP route if found, otherwise std::nullopt.
     */
    std::vector<RoutingTable::Eigrp*> getAllConnectedEigrpRoutes(AddressFamily af, uint32_t as);

    /**
     * @brief Retrieves a specific EIGRP route based on destination and mask.
     *
     * @param destination The destination network address.
     * @param mask The subnet mask.
     * @param af The address family (IPv4 or IPv6).
     * @param as The Autonomous system
     * @return std::optional<RoutingTable::Eigrp> The EIGRP route if found, otherwise std::nullopt.
     */
    RoutingTable::Eigrp* getEigrpRoute(const uint8_t* destination, const uint8_t mask, AddressFamily af, uint32_t as);

    /**
     * @brief Updates the EIGRP table with a given route, applying variance.
     *
     * @param route The EIGRP route to update.
     * @param variance The variance factor to apply.
     * @param af The address family (IPv4 or IPv6).
     * @param as The Autonomous system
     */
    void updateEigrpWithVariance(Eigrp* route, uint8_t variance, AddressFamily af, uint32_t as);

    /**
     * @brief Determines the next hop for a given destination and mask.
     *
     * @param destination The destination network address.
     * @param mask The subnet mask for the destination.
     * @return ByteString The next hop IP address.
     */
    bool getNextHop(uint8_t* out, const uint8_t* destination, uint8_t mask, uint32_t as, AddressFamily af);

    /**
     * @brief Deleted copy constructor to prevent copying of the singleton instance.
     */
    RoutingTable(const RoutingTable&) = delete;

    /**
     * @brief Deleted assignment operator to prevent assignment of the singleton instance.
     */
    RoutingTable& operator=(const RoutingTable&) = delete;

    std::mutex tableMutex; ///< Mutex to ensure thread-safe access to the routing tables.

    void clear()
    {
        eigrp.clear();
        eigrpIPv6.clear();
    }

private:

    std::map<uint32_t, std::map<IPPrefix, Eigrp*>> eigrp;      ///< EIGRP routing table for IPv4.
    std::map<uint32_t, std::map<IPPrefix, Eigrp*>> eigrpIPv6;  ///< EIGRP routing table for IPv6.
};

#endif // ROUTING_TABLE_H
