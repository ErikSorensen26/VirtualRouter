#pragma once

#include <vector>
#include <ByteString.hpp>
#include <mutex>
#include <PacketStructure.h>
#include <optional>
#include <chrono>
#include <map>
#include <Functions.h>
#include <Logger.h>

enum class AddressFamily 
{
    IPv4,
    IPv6
};

class RoutingTable {
public:

    struct RoutingEntry {
        ByteString destination, mask, nextHop, outInterface, source;
        int metric, admDist;
        std::chrono::system_clock::time_point age;
    };

    void printRoutingTable();

    struct Fib {
        ByteString destination, nextHop, outInt, mac;
        int preference;
    };

    void printFibTable();

    struct Arp {
        ByteString ipAddress, mac, interface, type;
        std::chrono::system_clock::time_point age;
    };

    void printArpTable();
    void updateArp(const ArpHeader& recievedArp);
    void updateArp(const ByteString ip, ByteString mac, ByteString interfaceAddress);
    std::optional<RoutingTable::Arp> ArpLookup(const ByteString& ipAddress);

    struct NDP {
        ByteString ipAddress, macAddress, interface, state;
        std::chrono::system_clock::time_point age;
    };

    void printNdpTable();

    struct MAC {
        ByteString mac, interface, vlanID, type;
        std::chrono::system_clock::time_point age;
    };

    void printMacTable();

    struct Rib {
        ByteString destination, mask, nextHop, outInterface, source;
        int metric, admDist;
        std::vector<ByteString> tags;
        std::chrono::system_clock::time_point age;
    };

    void printRibTable();

    struct Prb {
        ByteString sourceIp, destination, sourcePort, destPort, protocol, nextHop, outInterface, matchCriteria;
        int DSCP;
        std::chrono::system_clock::time_point age;
    };

    void printPrbTable();

    struct Multicast {
        ByteString group, sourceIp, inInterface, RPF, protocol;
        int routeMetric;
        std::vector<ByteString> outInterface;
        std::chrono::system_clock::time_point age;
    };

    void printMulticastTable();

    struct ACL {
        ByteString sourceIp, destIp, protocol, sourcePortRange, destPortRange, logString, action;
        int ruleNum, icmoCode, DSCP;
        std::chrono::system_clock::time_point age;
    };

    void printAclTable();

    struct Eigrp {
        ByteString network{}, nextHop{}, interface{}, successor{}, feasibleSuccessor{}, routeSource{}, routeType{}, activeOrPassive{}, originRouter{}, flags{};
        unsigned int metric{}, feasibleDistance{}, reportedDistance{}, adminDistance{}, holdTime{}, stuckInaActive{}, updateTimer{}, retransmitInterval{}, sequenceNumber{}, routeTag{};
        unsigned int hopCount{}, bandwidth{}, load{}, delay{}, reliability{}, mtu{}, mask{}, originAS{}, extendedMetric{}, extendedId{};
        std::chrono::system_clock::time_point age;
        std::vector<ByteString> nextHops{};
        bool isIPv6{false};
    };

    void printEigrpTable();
    void updateEigrpWithVarianceIPv6(const Eigrp& route, double variance, AddressFamily af);
    void addEigrp(const Eigrp route, AddressFamily af);
    void updateEigrp(const Eigrp route, AddressFamily af);
    void removeEigrp(const ByteString& network, int mask, AddressFamily af);
    std::vector<Eigrp> getAllEigrpRoutes(AddressFamily af);
    std::vector<RoutingTable::Eigrp> getAllConnectedEigrpRoutes(AddressFamily af);
    std::optional<RoutingTable::Eigrp> getEigrpRoute(const ByteString& destination, const int mask, AddressFamily af);
    void updateEigrpWithVariance(const Eigrp& route, double variance, AddressFamily af);

    // Static method to access the singleton instance
    static RoutingTable& getInstance()
    {
        static RoutingTable instance;
        return instance;
    }

    ByteString getNextHop(const ByteString& destination, int mask);

    // Delete copy constructor and assignment operator
    RoutingTable(const RoutingTable&) = delete;
    RoutingTable& operator=(const RoutingTable&) = delete;

    std::mutex tableMutex;

private:
    RoutingTable() = default;
    ~RoutingTable() = default;

    std::map<ByteString, RoutingEntry> routingTable;
    std::map<ByteString, Fib> fib;
    std::map<ByteString, Arp> arp;
    std::map<ByteString, NDP> ndp;
    std::map<ByteString, MAC> mac;
    std::map<ByteString, Rib> rib;
    std::map<ByteString, Prb> prb;
    std::map<ByteString, Multicast> multicast;
    std::map<ByteString, ACL> acl;
    std::map<ByteString, Eigrp> eigrp;
    std::map<ByteString, Eigrp> eigrpIPv6;
};
