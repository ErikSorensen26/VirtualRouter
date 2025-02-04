// RoutingTable.h

#ifndef ROUTING_TABLE_H
#define ROUTING_TABLE_H

#include <vector>
#include <ByteString.hpp>
#include <mutex>
#include <PacketStructure.h>
#include <optional>
#include <chrono>
#include <map>
#include <unordered_set>
#include <Functions.h>
#include <Logger.h>

/**
 * @enum AddressFamily
 * @brief Enumerates the supported address families for routing.
 */
enum class AddressFamily 
{
    IPv4,   ///< IPv4 address family.
    IPv6    ///< IPv6 address family.
};

/**
 * @class RoutingTable
 * @brief Singleton class managing various routing-related tables and operations.
 *
 * The RoutingTable class provides a centralized management system for different routing tables,
 * including Routing Entries, Forwarding Information Base (FIB), ARP tables, NDP tables, MAC tables,
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
     * @struct RoutingEntry
     * @brief Represents an entry in the Routing Information Base (RIB).
     */
    struct RoutingEntry {
        ByteString destination;     ///< Destination network address.
        ByteString mask;            ///< Subnet mask for the destination.
        ByteString nextHop;          ///< Next hop IP address.
        ByteString outInterface;     ///< Outgoing interface name.
        ByteString source;           ///< Source of the routing entry.
        uint32_t metric;                  ///< Routing metric.
        uint8_t admDist;                 ///< Administrative distance.
        std::chrono::system_clock::time_point age; ///< Age of the routing entry.
    };

    /**
     * @brief Prints the Routing Information Base (RIB) to the console.
     */
    void printRoutingTable();

    /**
     * @struct Fib
     * @brief Represents an entry in the Forwarding Information Base (FIB).
     */
    struct Fib {
        ByteString destination;   ///< Destination network address.
        ByteString nextHop;       ///< Next hop IP address.
        ByteString outInt;        ///< Outgoing interface name.
        ByteString mac;           ///< MAC address associated with the next hop.
        uint8_t preference;           ///< Preference value for the route.
    };

    /**
     * @brief Prints the Forwarding Information Base (FIB) to the console.
     */
    void printFibTable();

    /**
     * @struct Arp
     * @brief Represents an entry in the Address Resolution Protocol (ARP) table.
     */
    struct Arp {
        ByteString ipAddress;      ///< IP address.
        ByteString mac;            ///< MAC address corresponding to the IP.
        ByteString interface;      ///< Interface name.
        ByteString type;           ///< Type of ARP entry.
        std::chrono::system_clock::time_point age; ///< Age of the ARP entry.
    };

    /**
     * @brief Prints the ARP table to the console.
     */
    void printArpTable();

    /**
     * @brief Updates the ARP table with a received ARP packet.
     *
     * @param recievedArp The received ARP header information.
     */
    virtual void updateArp(const ArpHeader& recievedArp);

    /**
     * @brief Updates the ARP table with provided IP and MAC addresses.
     *
     * @param ip The IP address to update.
     * @param mac The MAC address corresponding to the IP.
     * @param interfaceAddress The interface associated with the ARP entry.
     */
    void updateArp(const ByteString ip, ByteString mac, ByteString interfaceAddress);

    /**
     * @brief Looks up an ARP entry based on the IP address.
     *
     * @param ipAddress The IP address to look up.
     * @return std::optional<RoutingTable::Arp> The ARP entry if found, otherwise std::nullopt.
     */
    std::optional<RoutingTable::Arp> ArpLookup(const ByteString& ipAddress);

    /**
     * @struct NDP
     * @brief Represents an entry in the Neighbor Discovery Protocol (NDP) table.
     */
    struct NDP {
        ByteString ipAddress;      ///< IPv6 address.
        ByteString macAddress;     ///< MAC address corresponding to the IPv6 address.
        ByteString interface;      ///< Interface name.
        ByteString state;          ///< State of the NDP entry.
        std::chrono::system_clock::time_point age; ///< Age of the NDP entry.
    };

    /**
     * @brief Prints the NDP table to the console.
     */
    void printNdpTable();

    /**
     * @struct MAC
     * @brief Represents an entry in the MAC address table.
     */
    struct MAC {
        ByteString mac;            ///< MAC address.
        ByteString interface;      ///< Interface name.
        ByteString vlanID;         ///< VLAN ID.
        ByteString type;           ///< Type of MAC entry.
        std::chrono::system_clock::time_point age; ///< Age of the MAC entry.
    };

    /**
     * @brief Prints the MAC address table to the console.
     */
    void printMacTable();

    /**
     * @struct Rib
     * @brief Represents an entry in the Routing Information Base (RIB).
     */
    struct Rib {
        ByteString destination;   ///< Destination network address.
        ByteString mask;          ///< Subnet mask for the destination.
        ByteString nextHop;       ///< Next hop IP address.
        ByteString outInterface;  ///< Outgoing interface name.
        ByteString source;        ///< Source of the routing entry.
        uint32_t metric;               ///< Routing metric.
        uint8_t admDist;              ///< Administrative distance.
        std::vector<ByteString> tags; ///< Tags associated with the routing entry.
        std::chrono::system_clock::time_point age; ///< Age of the RIB entry.
    };

    /**
     * @brief Prints the Routing Information Base (RIB) to the console.
     */
    void printRibTable();

    /**
     * @brief Prints the Policy-Based Routing (PBR) table to the console.
     */
    struct Prb {
        ByteString sourceIp;          ///< Source IP address.
        ByteString destination;       ///< Destination IP address.
        ByteString sourcePort;        ///< Source port.
        ByteString destPort;          ///< Destination port.
        ByteString protocol;          ///< Protocol (e.g., TCP, UDP).
        ByteString nextHop;           ///< Next hop IP address.
        ByteString outInterface;      ///< Outgoing interface name.
        ByteString matchCriteria;     ///< Criteria for matching the route.
        uint8_t DSCP;                     ///< Differentiated Services Code Point value.
        std::chrono::system_clock::time_point age; ///< Age of the PBR entry.
    };

    /**
     * @struct Prb
     * @brief Represents an entry in the Policy-Based Routing (PBR) table.
     */
    void printPrbTable();

    /**
     * @struct Multicast
     * @brief Represents an entry in the Multicast routing table.
     */
    struct Multicast {
        ByteString group;            ///< Multicast group address.
        ByteString sourceIp;         ///< Source IP address.
        ByteString inInterface;      ///< Incoming interface name.
        ByteString RPF;              ///< Reverse Path Forwarding interface.
        ByteString protocol;         ///< Protocol used for multicast routing.
        uint32_t routeMetric;        ///< Metric for the multicast route.
        std::vector<ByteString> outInterface; ///< Outgoing interfaces for the multicast group.
        std::chrono::system_clock::time_point age; ///< Age of the multicast entry.
    };

    /**
     * @brief Prints the Multicast routing table to the console.
     */
    void printMulticastTable();

    /**
     * @struct ACL
     * @brief Represents an entry in the Access Control List (ACL).
     */
    struct ACL {
        ByteString sourceIp;             ///< Source IP address.
        ByteString destIp;               ///< Destination IP address.
        ByteString protocol;             ///< Protocol (e.g., TCP, UDP).
        ByteString sourcePortRange;      ///< Source port range.
        ByteString destPortRange;        ///< Destination port range.
        ByteString logString;            ///< Log string for matched traffic.
        ByteString action;               ///< Action to take (e.g., permit, deny).
        uint16_t ruleNum;                ///< Rule number in the ACL.
        uint8_t icmpCode;                ///< ICMP code for the ACL entry.
        uint8_t DSCP;                    ///< Differentiated Services Code Point value.
        std::chrono::system_clock::time_point age; ///< Age of the ACL entry.
    };

    /**
     * @brief Prints the Access Control List (ACL) table to the console.
     */
    void printAclTable();

    /**
     * @struct Eigrp
     * @brief Represents an entry in the Enhanced Interior Gateway Routing Protocol (EIGRP) table.
     */
    struct Eigrp {
        ByteString network{};               ///< Network address.
        ByteString nextHop{};               ///< Next hop IP address.
        ByteString interface{};             ///< Interface name.
        ByteString successor{};             ///< Successor route.
        ByteString feasibleSuccessor{};     ///< Feasible successor route.
        ByteString routeSource{};           ///< Source of the route.
        ByteString routeType{};             ///< Type of the route.
        ByteString activeOrPassive{};       ///< Active or passive state.
        ByteString originRouter{};          ///< Originating router.
        ByteString flags{};                 ///< Flags associated with the route.

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
        std::chrono::system_clock::time_point age; ///< Age of the EIGRP entry.
        std::vector<ByteString> nextHopsVector{};       ///< List of next hop IP addresses.
        std::unordered_set<ByteString> nextHopsSet{};   ///< List of next hop IP addresses.
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
    void addEigrp(Eigrp* route, AddressFamily af, uint32_t as);

    /**
     * @brief Removes an EIGRP route from the table.
     *
     * @param network The network address of the route to remove.
     * @param mask The subnet mask of the route to remove.
     * @param af The address family (IPv4 or IPv6).
     * @param as The Autonomous system
     */
    void removeEigrp(const ByteString& network, uint8_t mask, AddressFamily af, uint32_t as);

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
    std::optional<RoutingTable::Eigrp*> getEigrpRoute(const ByteString& destination, const uint8_t mask, AddressFamily af, uint32_t as);

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
    ByteString getNextHop(const ByteString& destination, uint8_t mask, uint32_t as);

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
        routingTable.clear();
        fib.clear();
        arp.clear();
        ndp.clear();
        mac.clear();
        rib.clear();
        prb.clear();
        multicast.clear();
        acl.clear();
        eigrp.clear();
        eigrpIPv6.clear();
    }

private:

    std::map<ByteString, RoutingEntry> routingTable;       ///< Routing Information Base (RIB).
    std::map<ByteString, Fib> fib;                         ///< Forwarding Information Base (FIB).
    std::map<ByteString, Arp> arp;                         ///< Address Resolution Protocol (ARP) table.
    std::map<ByteString, NDP> ndp;                         ///< Neighbor Discovery Protocol (NDP) table.
    std::map<ByteString, MAC> mac;                         ///< MAC address table.
    std::map<ByteString, Rib> rib;                         ///< Routing Information Base (RIB).
    std::map<ByteString, Prb> prb;                         ///< Policy-Based Routing (PBR) table.
    std::map<ByteString, Multicast> multicast;             ///< Multicast routing table.
    std::map<ByteString, ACL> acl;                         ///< Access Control List (ACL) table.
    std::map<uint32_t, std::map<ByteString, Eigrp*>> eigrp;      ///< EIGRP routing table for IPv4.
    std::map<uint32_t, std::map<ByteString, Eigrp*>> eigrpIPv6;  ///< EIGRP routing table for IPv6.
};

#endif // ROUTING_TABLE_H
