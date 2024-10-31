
Ip Routing

Connected──┐
static─────┤
OSPF───────┤───> Rib ───> Fib
RIP────────┤
BGP────────┘

Rib

prefix-in ───> Private ───> policy update ───> update calculate ───> current  










/home
  └── user
    ├── documents
    │   └── project
    └── downloads

 main
  └── Terminal ── Console ── Config
      └── CommandProcessing  └── Interface
                                 ├── Ingress
                                 │   └── Que
                                 ├── Process
                                 │   ├── Decapsulation
                                 │   │   └── PacketStructure
                                 │   ├── RoutingTable
                                 │   └── Encapsulation
                                 │       └── PacketStructure
                                 └── Egress


                    ┌─────────────< Logincal-Int <──────────────┐
        ┌────────> BridgePort ──> MPLS ────────> IPv4/ipv6 ───> Decapsulate
  In-Interface      ├──> Input ───┘├──> Input ───┘├──> Input ───┘└─> LocalIn
                   ↓├──> Forward  ↓├──> Forward  ↓├──> Forward        └─> RouterProcess
  Out-Interface     ├──< Output <─┐├──< Output <─┐├──< Output <─┐          └─> LocalOut
        └────> Encalsulation <─── BridgePort <── MPLS <──────── IPv4/ipv6 <─────┘  ↑│w
                    └─────────────> Logincal-Int ───────────────────────────────────┘


                    ┌───────────────────────────────────────────< Logincal-Int <──────────────────────────────────────────────┐
        ┌────────> BridgePort ────────────────────> MPLS ─────────────────────────────────> IPv4/ipv6 ─────────────────────> Decapsulate
  In-Interface      │   ┌─> Pre-Routing ──┐  Input ──┘└─> Decision            ┌─ Decapsulate │└─> Pre-Routing <─ Decrypt      │└─> LocalIn
                    └─> DTS-NAT ───> Decision ──┤          │└─> Pop-Label ──> Tunnel ────────┘     └──> Decision  └─> Policy ─┘     └─> RouterProcess
                              ┌── Firewall <── Forward     └─> Switch-Label                     Forward <──┘└─> Input ───┘               └─> LocalOut
                    ┌─ Ip <─ SRC-NAT <─ Output                  │                                └─> Post-Routing <── Output                   │  │
                    │  └─> Post-Routing └─> Decision ┐┌─────────┘                                  ┌──┘   Encryption  │                        │  │
  Out-Interface     ├───────┘                        │├──Push-Label <──────── Decision ──────┐┌─> Policy <─┘└─> Decision ─────┐                │ ↑│
        └────────> Encalsulation <───────────────── BridgePort <─────────────────────────── MPLS <────────────────────────── IPv4/ipv6 <───────┘  │
                    └───────────────────────────────────────────> Logincal-Int ───────────────────────────────────────────────────────────────────┘

PreRouting ──> Hotspot-In ──> Raw-Routing ──> Connection-Tracking ──> Mangle-Pre-Routing ──> DTS-NAT
Input ──> Mangle-Input ──> Filter-Input ──> HTP-Global ──> Simple-Queues
Forward ──> Decision ──> TTL-1 ──> Mangle Forward ──> Filter-Forward ──> Accounting
Output ──> Decision ──> Raw-Output ──> Connection-Tracking ──> Mangle-Output ──> Filter-Output ──> Router-Adjustment
Post-Routing ──> Mangle-Post-Routing ──> SRC-NAT ──> Hotspot-Out ──> HTB-Global ──> Simple-Queues




Here's an overview of how these networking tables are filled, which ones are filled at the same time, and when they are filled:

Routing Table

How Filled:
Static Routes: Manually configured by network administrators.
Dynamic Routes: Populated and updated automatically by routing protocols (e.g., OSPF, BGP, EIGRP, RIP).
When Filled:
Startup: Initial static routes are loaded.
Runtime: Dynamic routes are continuously updated as routing information is exchanged.
Filled At The Same Time: The Forwarding Information Base (FIB) is updated in sync with the routing table.

Forwarding Information Base (FIB)

How Filled: Derived from the routing table, optimized for quick lookup.
When Filled:
Startup: Initial static routes are loaded.
Runtime: Updated in real-time as the routing table changes.
Filled At The Same Time: Routing table updates trigger FIB updates.

Address Resolution Protocol (ARP) Table

How Filled:
On-Demand: Devices send ARP requests when they need to resolve an IP address to a MAC address.
Static Entries: Manually configured by network administrators.
When Filled:
On-Demand: As needed when devices communicate on the local network.
Startup: Pre-configured static ARP entries are loaded.

Neighbor Discovery Protocol (NDP) Table (for IPv6)

How Filled:
On-Demand: Devices send Neighbor Solicitation messages to resolve IPv6 addresses to MAC addresses.
When Filled:
On-Demand: As needed when devices communicate on the local network.
Startup: Pre-configured static NDP entries are loaded.

MAC Address Table

How Filled:
Dynamic Learning: Switches learn MAC addresses from incoming frames and associate them with the receiving port.
Static Entries: Manually configured by network administrators.
When Filled:
Runtime: Continuously updated as frames are received.
Startup: Pre-configured static MAC entries are loaded.

Routing Information Base (RIB)

How Filled:
Dynamic Routes: Populated by routing protocols (e.g., OSPF, BGP).
When Filled:
Runtime: Continuously updated as routing information is exchanged.
Filled At The Same Time: Routing table updates based on RIB information.

Policy-Based Routing (PBR) Table

How Filled:
Manual Configuration: Network administrators define routing policies.
When Filled:
Startup: Policies are loaded.
Runtime: Policies can be dynamically applied or updated.

Multicast Routing Table

How Filled:
Dynamic Learning: Populated by multicast routing protocols (e.g., PIM) and group membership reports (e.g., IGMP, MLD).
When Filled:
Runtime: Continuously updated as multicast group memberships change and routing information is exchanged.

Access Control Lists (ACLs)

How Filled:
Manual Configuration: Network administrators define ACL rules.
When Filled:
Startup: ACL rules are loaded.
Runtime: Rules can be dynamically applied or updated.a

Frame 7212: 77 bytes on wire (616 bits), 77 bytes captured (616 bits) on interface -, id 0
Ethernet II, Src: 04:5a:7d:ff:00:03 (04:5a:7d:ff:00:03), Dst: IPv4mcast_0a (01:00:5e:00:00:0a)
    Destination: IPv4mcast_0a (01:00:5e:00:00:0a)
    Source: 04:5a:7d:ff:00:03 (04:5a:7d:ff:00:03)
    Type: IPv4 (0x0800)
Internet Protocol Version 4, Src: 48.25.2.22, Dst: 8.0.1.224
    0100 .... = Version: 4
    .... 0101 = Header Length: 20 bytes (5)
    Differentiated Services Field: 0x00 (DSCP: CS0, ECN: Not-ECT)
    Total Length: 63
    Identification: 0x0000 (0)
    000. .... = Flags: 0x0
    ...0 0000 0000 0000 = Fragment Offset: 0
    Time to Live: 64
    Protocol: EIGRP (88)
    Header Checksum: 0x3459 incorrect, should be 0x3e59(may be caused by "IP checksum offload"?)
        [Expert Info (Error/Checksum): Bad checksum [should be 0x3e59]]
    [Header checksum status: Bad]
    [Calculated Checksum: 0x3e59]
    Source Address: 48.25.2.22
    Destination Address: 8.0.1.224
Cisco EIGRP
    Version: 0
    Opcode: Unknown (0)
    Checksum: 0x0a02 incorrect, should be 0x0002
        [Expert Info (Warning/Checksum): Bad Checksum [should be 0x0002]]
            [Bad Checksum [should be 0x0002]]
            [Severity level: Warning]
            [Group: Checksum]
    [Checksum Status: Bad]
    Flags: 0x05eecb00
    Sequence: 0
    Acknowledge: 0
    Virtual Router ID: 0 (Address-Family)
    Autonomous System: 0
[Malformed Packet: EIGRP]
    [Expert Info (Error/Malformed): Malformed Packet (Exception occurred)]
        [Malformed Packet (Exception occurred)]
        [Severity level: Error]
        [Group: Malformed]



















#ifndef ROUTINGTABLE_H
#define ROUTINGTABLE_H

#include <string>
#include <vector>

class RoutingTable {
public:
    struct RoutingEntry {
        std::string destination, mask, nextHop, outInterface, source;
        int metric, age, admDist;

        RoutingEntry() = default;
        RoutingEntry(const RoutingEntry& other) = default;
        RoutingEntry& operator=(const RoutingEntry& other) = default;
    };

    struct Fib {
        std::string destination, nextHop, outInt, mac;
        int preference;

        Fib() = default;
        Fib(const Fib& other) = default;
        Fib& operator=(const Fib& other) = default;
    };

    struct Arp {
        std::string ipAddress, mac, interface, type;
        int age;

        Arp() = default;
        Arp(const Arp& other) = default;
        Arp& operator=(const Arp& other) = default;
    };

    struct NDP {
        std::string ipAddress, macAddress, interface, state;
        int age;

        NDP() = default;
        NDP(const NDP& other) = default;
        NDP& operator=(const NDP& other) = default;
    };

    struct MAC {
        std::string mac, interface, vlanID, type;
        int age;

        MAC() = default;
        MAC(const MAC& other) = default;
        MAC& operator=(const MAC& other) = default;
    };

    struct Rib {
        std::string destination, mask, nextHop, outInterface, source;
        int metric, age, admDist;
        std::vector<std::string> tags;

        Rib() = default;
        Rib(const Rib& other) = default;
        Rib& operator=(const Rib& other) = default;
    };

    struct Prb {
        std::string sourceIp, destination, sourcePort, destPort, protocol, nextHop, outInterface, matchCriteria;
        int DSCP;

        Prb() = default;
        Prb(const Prb& other) = default;
        Prb& operator=(const Prb& other) = default;
    };

    struct Multicast {
        std::string group, sourceIp, inInterface, RPF, protocol;
        int age, routeMetric;
        std::vector<std::string> outInterface;

        Multicast() = default;
        Multicast(const Multicast& other) = default;
        Multicast& operator=(const Multicast& other) = default;
    };

    struct ACL {
        std::string sourceIp, destIp, protocol, sourcePortRange, destPortRange, logString;
        bool action;
        int ruleNum, icmoCode, age, DSCP;

        ACL() = default;
        ACL(const ACL& other) = default;
        ACL& operator=(const ACL& other) = default;
    };

    std::vector<RoutingEntry> routingTable;
    std::vector<Fib> fib;
    std::vector<Arp> arp;
    std::vector<NDP> ndp;
    std::vector<MAC> mac;
    std::vector<Rib> rib;
    std::vector<Prb> prb;
    std::vector<Multicast> multicast;
    std::vector<ACL> acl;

    RoutingTable() = default;
    RoutingTable(const RoutingTable& other) = default;
    RoutingTable& operator=(const RoutingTable& other) = default;
    ~RoutingTable() = default;
};

#endif // ROUTINGTABLE_H


#ifndef SINGLETON_H
#define SINGLETON_H

#include "RoutingTable.h"
#include <shared_mutex>
#include <fstream>
#include <iostream>
#include <vector>

class Singleton {
public:
    static Singleton& getInstance() {
        static Singleton instance;
        return instance;
    }

    void addRoutingTable(const RoutingTable& rt) {
        std::lock_guard<std::shared_mutex> writeLock(mutex);
        routingTable = rt;
        save();
    }

    RoutingTable getRoutingTable() {
        std::shared_lock<std::shared_mutex> readLock(mutex);
        return routingTable;
    }

private:
    Singleton() {
        load();
    }

    // Delete copy constructor and assignment operator to enforce singleton
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;

    std::string filePath = "../data.bin";
    RoutingTable routingTable;
    std::shared_mutex mutex;

    template <typename T>
    void serialize(std::ofstream& ofs, const T& value) {
        ofs.write(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    template <typename T>
    void deserialize(std::ifstream& ifs, T& value) {
        ifs.read(reinterpret_cast<char*>(&value), sizeof(T));
    }

    void serializeString(std::ofstream& ofs, const std::string& str) {
        size_t size = str.size();
        serialize(ofs, size);
        ofs.write(str.data(), size);
    }

    void deserializeString(std::ifstream& ifs, std::string& str) {
        size_t size;
        deserialize(ifs, size);
        str.resize(size);
        ifs.read(&str[0], size);
    }

    template <typename T>
    void serializeVector(std::ofstream& ofs, const std::vector<T>& vec) {
        size_t size = vec.size();
        serialize(ofs, size);
        for (const auto& item : vec) {
            serializeItem(ofs, item);
        }
    }

    template <typename T>
    void deserializeVector(std::ifstream& ifs, std::vector<T>& vec) {
        size_t size;
        deserialize(ifs, size);
        vec.resize(size);
        for (auto& item : vec) {
            deserializeItem(ifs, item);
        }
    }

    template <typename T>
    void serializeItem(std::ofstream& ofs, const T& item) {
        // Specialization needed for complex types
    }

    template <typename T>
    void deserializeItem(std::ifstream& ifs, T& item) {
        // Specialization needed for complex types
    }

    // Serialization/Deserialization specializations for RoutingTable entries
    void serializeItem(std::ofstream& ofs, const RoutingTable::RoutingEntry& item) {
        serializeString(ofs, item.destination);
        serializeString(ofs, item.mask);
        serializeString(ofs, item.nextHop);
        serializeString(ofs, item.outInterface);
        serializeString(ofs, item.source);
        serialize(ofs, item.metric);
        serialize(ofs, item.age);
        serialize(ofs, item.admDist);
    }

    void deserializeItem(std::ifstream& ifs, RoutingTable::RoutingEntry& item) {
        deserializeString(ifs, item.destination);
        deserializeString(ifs, item.mask);
        deserializeString(ifs, item.nextHop);
        deserializeString(ifs, item.outInterface);
        deserializeString(ifs, item.source);
        deserialize(ifs, item.metric);
        deserialize(ifs, item.age);
        deserialize(ifs, item.admDist);
    }

    void serializeItem(std::ofstream& ofs, const RoutingTable::Fib& item) {
        serializeString(ofs, item.destination);
        serializeString(ofs, item.nextHop);
        serializeString(ofs, item.outInt);
        serializeString(ofs, item.mac);
        serialize(ofs, item.preference);
    }

    void deserializeItem(std::ifstream& ifs, RoutingTable::Fib& item) {
        deserializeString(ifs, item.destination);
        deserializeString(ifs, item.nextHop);
        deserializeString(ifs, item.outInt);
        deserializeString(ifs, item.mac);
        deserialize(ifs, item.preference);
    }

    void serializeItem(std::ofstream& ofs, const RoutingTable::Arp& item) {
        serializeString(ofs, item.ipAddress);
        serializeString(ofs, item.mac);
        serializeString(ofs, item.interface);
        serializeString(ofs, item.type);
        serialize(ofs, item.age);
    }

    void deserializeItem(std::ifstream& ifs, RoutingTable::Arp& item) {
        deserializeString(ifs, item.ipAddress);
        deserializeString(ifs, item.mac);
        deserializeString(ifs, item.interface);
        deserializeString(ifs, item.type);
        deserialize(ifs, item.age);
    }

    void serializeItem(std::ofstream& ofs, const RoutingTable::NDP& item) {
        serializeString(ofs, item.ipAddress);
        serializeString(ofs, item.macAddress);
        serializeString(ofs, item.interface);
        serializeString(ofs, item.state);
        serialize(ofs, item.age);
    }

    void deserializeItem(std::ifstream& ifs, RoutingTable::NDP& item) {
        deserializeString(ifs, item.ipAddress);
        deserializeString(ifs, item.macAddress);
        deserializeString(ifs, item.interface);
        deserializeString(ifs, item.state);
        deserialize(ifs, item.age);
    }

    void serializeItem(std::ofstream& ofs, const RoutingTable::MAC& item) {
        serializeString(ofs, item.mac);
        serializeString(ofs, item.interface);
        serializeString(ofs, item.vlanID);
        serializeString(ofs, item.type);
        serialize(ofs, item.age);
    }

    void deserializeItem(std::ifstream& ifs, RoutingTable::MAC& item) {
        deserializeString(ifs, item.mac);
        deserializeString(ifs, item.interface);
        deserializeString(ifs, item.vlanID);
        deserializeString(ifs, item.type);
        deserialize(ifs, item.age);
    }

    void serializeItem(std::ofstream& ofs, const RoutingTable::Rib& item) {
        serializeString(ofs, item.destination);
        serializeString(ofs, item.mask);
        serializeString(ofs, item.nextHop);
        serializeString(ofs, item.outInterface);
        serializeString(ofs, item.source);
        serialize(ofs, item.metric);
        serialize(ofs, item.age);
        serialize(ofs, item.admDist);
        serializeVector(ofs, item.tags);
    }

    void deserializeItem(std::ifstream& ifs, RoutingTable::Rib& item) {
        deserializeString(ifs, item.destination);
        deserializeString(ifs, item.mask);
        deserializeString(ifs, item.nextHop);
        deserializeString(ifs, item.outInterface);
        deserializeString(ifs, item.source);
        deserialize(ifs, item.metric);
        deserialize(ifs, item.age);
        deserialize(ifs, item.admDist);
        deserializeVector(ifs, item.tags);
    }

    void serializeItem(std::ofstream& ofs, const RoutingTable::Prb& item) {
        serializeString(ofs, item.sourceIp);
        serializeString(ofs, item.destination);
        serializeString(ofs, item.sourcePort);
        serializeString(ofs, item.destPort);
        serializeString(ofs, item.protocol);
        serializeString(ofs, item.nextHop);
        serializeString(ofs, item.outInterface);
        serializeString(ofs, item.matchCriteria);
        serialize(ofs, item.DSCP);
    }

    void deserializeItem(std::ifstream& ifs, RoutingTable::Prb& item) {
        deserializeString(ifs, item.sourceIp);
        deserializeString(ifs, item.destination);
        deserializeString(ifs, item.sourcePort);
        deserializeString(ifs, item.destPort);
        deserializeString(ifs, item.protocol);
        deserializeString(ifs, item.nextHop);
        deserializeString(ifs, item.outInterface);
        deserializeString(ifs, item.matchCriteria);
        deserialize(ifs, item.DSCP);
    }

    void serializeItem(std::ofstream& ofs, const RoutingTable::Multicast& item) {
        serializeString(ofs, item.group);
        serializeString(ofs, item.sourceIp);
        serializeString(ofs, item.inInterface);
        serializeString(ofs, item.RPF);
        serializeString(ofs, item.protocol);
        serialize(ofs, item.age);
        serialize(ofs, item.routeMetric);
        serializeVector(ofs, item.outInterface);
    }

    void deserializeItem(std::ifstream& ifs, RoutingTable::Multicast& item) {
        deserializeString(ifs, item.group);
        deserializeString(ifs, item.sourceIp);
        deserializeString(ifs, item.inInterface);
        deserializeString(ifs, item.RPF);
        deserializeString(ifs, item.protocol);
        deserialize(ifs, item.age);
        deserialize(ifs, item.routeMetric);
        deserializeVector(ifs, item.outInterface);
    }

    void serializeItem(std::ofstream& ofs, const RoutingTable::ACL& item) {
        serializeString(ofs, item.sourceIp);
        serializeString(ofs, item.destIp);
        serializeString(ofs, item.protocol);
        serializeString(ofs, item.sourcePortRange);
        serializeString(ofs, item.destPortRange);
        serializeString(ofs, item.logString);
        serialize(ofs, item.action);
        serialize(ofs, item.ruleNum);
        serialize(ofs, item.icmoCode);
        serialize(ofs, item.age);
        serialize(ofs, item.DSCP);
    }

    void deserializeItem(std::ifstream& ifs, RoutingTable::ACL& item) {
        deserializeString(ifs, item.sourceIp);
        deserializeString(ifs, item.destIp);
        deserializeString(ifs, item.protocol);
        deserializeString(ifs, item.sourcePortRange);
        deserializeString(ifs, item.destPortRange);
        deserializeString(ifs, item.logString);
        deserialize(ifs, item.action);
        deserialize(ifs, item.ruleNum);
        deserialize(ifs, item.icmoCode);
        deserialize(ifs, item.age);
        deserialize(ifs, item.DSCP);
    }

    void save() {
        std::ofstream ofs(filePath, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            std::cerr << "Error opening file for writing.\n";
            return;
        }

        serializeVector(ofs, routingTable.routingTable);
        serializeVector(ofs, routingTable.fib);
        serializeVector(ofs, routingTable.arp);
        serializeVector(ofs, routingTable.ndp);
        serializeVector(ofs, routingTable.mac);
        serializeVector(ofs, routingTable.rib);
        serializeVector(ofs, routingTable.prb);
        serializeVector(ofs, routingTable.multicast);
        serializeVector(ofs, routingTable.acl);
    }

    void load() {
        std::ifstream ifs(filePath, std::ios::binary);
        if (!ifs) {
            std::cerr << "Error opening file for reading.\n";
            return;
        }

        try {
            deserializeVector(ifs, routingTable.routingTable);
            deserializeVector(ifs, routingTable.fib);
            deserializeVector(ifs, routingTable.arp);
            deserializeVector(ifs, routingTable.ndp);
            deserializeVector(ifs, routingTable.mac);
            deserializeVector(ifs, routingTable.rib);
            deserializeVector(ifs, routingTable.prb);
            deserializeVector(ifs, routingTable.multicast);
            deserializeVector(ifs, routingTable.acl);
        } catch (const std::exception& e) {
            std::cerr << "Exception during deserialization: " << e.what() << '\n';
        }
    }
};

#endif // SINGLETON_H





// routing table types
//class RoutingTable
//{
//public:
//    RoutingTable();
//
//    struct RoutingEntry
//    {
//        string destination{},
//            mask{}, nextHop{},
//            outInterface{},
//            source{};
//        int metric{}, age{}, admDist{};
//    }; vector<RoutingEntry> routingTable;
//
//    struct Fib
//    {
//        string destination{},
//            nextHop{}, outInt{},
//            mac{};
//        int preference{};
//    }; vector<Fib> fib;
//
//    struct Arp
//    {
//        string ipAddress{},
//            mac{}, interface{},
//            type{};
//        int age{};
//    }; vector<Arp> arp;
//
//    struct NDP
//    {
//        string ipAddress{},
//            macAddress{}, interface{},
//            state{};
//        int age{};
//    }; vector<NDP> ndp;
//
//    struct MAC
//    {
//        string mac{}, interface{},
//            vlanID{}, type{};
//        int age{};
//    }; vector<MAC> mac;
//
//    struct Rib
//    {
//        string destination{},
//            mask{}, nextHop{},
//            outInterface{},
//            source{};
//        int metric{}, age{}, admDist{};
//        vector<string> tags{};
//    }; vector<Rib> rib;
//
//    struct Prb
//    {
//        string sourceIp{}, destination{},
//            sourcePort{}, destPort{},
//            protocol{},
//            nextHop{}, outInterface{},
//            matchCriteria{};
//        int DSCP{};
//    }; vector<Prb> prb;
//
//    struct Multicast
//    {
//        string group{}, sourceIp{},
//            inInterface{},
//            RPF{}, protocol{};
//        int age{}, routeMetric{};
//        vector<string> outInterface{};
//    }; vector<Multicast> multicast{};
//
//    struct ACL
//    {
//        string sourceIp{}, destIp{},
//            protocol{}, sourcePortRange{},
//            destPortRange{}, logString{};
//        bool action{};
//        int ruleNum{}, icmoCode{}, age{}, DSCP{};
//    }; vector<ACL> acl{};
//
//};