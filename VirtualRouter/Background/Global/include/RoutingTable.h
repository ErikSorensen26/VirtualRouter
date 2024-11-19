#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <algorithm>
#include <mutex>
#include <PacketStructure.h>
#include <optional>
#include <chrono>
#include <map>
#include <Functions.h>
#include <Logger.h>

class RoutingTable {
public:

    struct RoutingEntry {
        std::string destination, mask, nextHop, outInterface, source;
        int metric, admDist;
        std::chrono::system_clock::time_point age;
    };

    void printRoutingTable();

    struct Fib {
        std::string destination, nextHop, outInt, mac;
        int preference;
    };

    void printFibTable();

    struct Arp {
        std::string ipAddress, mac, interface, type;
        std::chrono::system_clock::time_point age;
    };

    void printArpTable();
    void UpdateArp(const arpHeader& recievedArp);
    void UpdateArp(const string ip, string mac, string interfaceAddress);
    std::optional<RoutingTable::Arp> ArpLookup(const std::string& ipAddress);

    struct NDP {
        std::string ipAddress, macAddress, interface, state;
        std::chrono::system_clock::time_point age;
    };

    void printNdpTable();

    struct MAC {
        std::string mac, interface, vlanID, type;
        std::chrono::system_clock::time_point age;
    };

    void printMacTable();

    struct Rib {
        std::string destination, mask, nextHop, outInterface, source;
        int metric, admDist;
        std::vector<std::string> tags;
        std::chrono::system_clock::time_point age;
    };

    void printRibTable();

    struct Prb {
        std::string sourceIp, destination, sourcePort, destPort, protocol, nextHop, outInterface, matchCriteria;
        int DSCP;
        std::chrono::system_clock::time_point age;
    };

    void printPrbTable();

    struct Multicast {
        std::string group, sourceIp, inInterface, RPF, protocol;
        int routeMetric;
        std::vector<std::string> outInterface;
        std::chrono::system_clock::time_point age;
    };

    void printMulticastTable();

    struct ACL {
        std::string sourceIp, destIp, protocol, sourcePortRange, destPortRange, logString, action;
        int ruleNum, icmoCode, DSCP;
        std::chrono::system_clock::time_point age;
    };

    void printAclTable();

    struct Eigrp {
        std::string network{}, nextHop{}, interface{}, successor{}, feasibleSuccessor{}, routeSource{}, routeType{}, activeOrPassive{};
        unsigned int metric{}, feasibleDistance{}, reportedDistance{}, adminDistance{}, holdTime{}, stuckInActive{}, updateTimer{}, retransmitInterval{}, sequenceNumber{}, routeTag{};
        unsigned int hopCount{}, bandwidth{}, load{}, delay{}, reliability{}, mtu{}, mask{};
        std::chrono::system_clock::time_point age;
        vector<std::string> nextHops{};
    };

    void printEigrpTable();
    void AddEigrp(const Eigrp route);
    void UpdateEigrp(const Eigrp route);
    void RemoveEigrp(const std::string& network, int mask);
    std::vector<Eigrp> GetAllEigrpRoutes();
    std::optional<RoutingTable::Eigrp> GetEigrpRoute(const std::string& destination, const int mask);
    void UpdateEigrpWithVaraence(const Eigrp& route, double variance);

    // Static method to access the singleton instance
    static RoutingTable& getInstance()
    {
        static RoutingTable instance;
        return instance;
    }

    std::string GetNextHop(const std::string& destination, int mask);

    // Delete copy constructor and assignment operator
    RoutingTable(const RoutingTable&) = delete;
    RoutingTable& operator=(const RoutingTable&) = delete;

    std::mutex tableMutex;

private:
    RoutingTable() = default;
    ~RoutingTable() = default;

    std::map<std::string, RoutingEntry> routingTable;
    std::map<std::string, Fib> fib;
    std::map<std::string, Arp> arp;
    std::map<std::string, NDP> ndp;
    std::map<std::string, MAC> mac;
    std::map<std::string, Rib> rib;
    std::map<std::string, Prb> prb;
    std::map<std::string, Multicast> multicast;
    std::map<std::string, ACL> acl;
    std::map<std::string, Eigrp> eigrp;

    Variable variable;
};