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

class RoutingTable {
public:

    struct RoutingEntry {
        std::string destination, mask, nextHop, outInterface, source;
        int metric, admDist;
        std::chrono::system_clock::time_point age;
    };

    struct Fib {
        std::string destination, nextHop, outInt, mac;
        int preference;
    };

    struct Arp {
        std::string ipAddress, mac, interface, type;
        std::chrono::system_clock::time_point age;
    };

    void UpdateArp(const arpHeader& recievedArp);
    void UpdateArp(const string ip, string mac, string interfaceAddress);
    std::optional<RoutingTable::Arp> ArpLookup(const std::string& ipAddress);

    struct NDP {
        std::string ipAddress, macAddress, interface, state;
        std::chrono::system_clock::time_point age;
    };

    struct MAC {
        std::string mac, interface, vlanID, type;
        std::chrono::system_clock::time_point age;
    };

    struct Rib {
        std::string destination, mask, nextHop, outInterface, source;
        int metric, admDist;
        std::vector<std::string> tags;
        std::chrono::system_clock::time_point age;
    };

    struct Prb {
        std::string sourceIp, destination, sourcePort, destPort, protocol, nextHop, outInterface, matchCriteria;
        int DSCP;
        std::chrono::system_clock::time_point age;
    };

    struct Multicast {
        std::string group, sourceIp, inInterface, RPF, protocol;
        int routeMetric;
        std::vector<std::string> outInterface;
        std::chrono::system_clock::time_point age;
    };

    struct ACL {
        std::string sourceIp, destIp, protocol, sourcePortRange, destPortRange, logString, action;
        int ruleNum, icmoCode, DSCP;
        std::chrono::system_clock::time_point age;
    };

    struct Eigrp {
        std::string network{}, nextHop{}, interface{}, successor{}, feasibleSuccessor{}, routeSource{}, routeType{}, activeOrPassive{};
        int metric{}, feasibleDistance{}, reportedDistance{}, adminDistance{}, holdTime{}, stuckInActive{}, updateTimer{}, retransmitInterval{}, sequenceNumber{}, routeTag{};
        int hopCount{}, bandwidth{}, load{}, delay{}, reliability{}, mtu{}, mask{};
        std::chrono::system_clock::time_point age;
    };

    void UpdateEigrp(const Eigrp& route);
    void RemoveEigrp(const std::string& network, int mask);
    std::vector<Eigrp> GetAllEigrpRoutes();

    // Static method to access the singleton instance
    static RoutingTable& getInstance() {
        static RoutingTable instance;
        return instance;
    }

    // Delete copy constructor and assignment operator
    RoutingTable(const RoutingTable&) = delete;
    RoutingTable& operator=(const RoutingTable&) = delete;


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

    std::mutex tableMutex;

private:
    RoutingTable() = default;
    ~RoutingTable() = default;

    Variable variable;

    Functions* function = Functions::getInstance();
};



//void printRoutingTable(const RoutingTable& rt) {
//    std::cout << "RoutingTable - RoutingEntry:\n";
//    for (const auto& entry : rt.routingTable) {
//        std::cout << "Destination: " << entry.destination
//                  << ", Mask: " << entry.mask
//                  << ", NextHop: " << entry.nextHop
//                  << ", OutInterface: " << entry.outInterface
//                  << ", Source: " << entry.source
//                  << ", Metric: " << entry.metric
//                  << ", Age: " << entry.age
//                  << ", AdmDist: " << entry.admDist
//                  << "\n";
//    }
//
//    std::cout << "RoutingTable - Fib:\n";
//    for (const auto& entry : rt.fib) {
//        std::cout << "Destination: " << entry.destination
//                  << ", NextHop: " << entry.nextHop
//                  << ", OutInt: " << entry.outInt
//                  << ", MAC: " << entry.mac
//                  << ", Preference: " << entry.preference
//                  << "\n";
//    }
//
//    std::cout << "RoutingTable - Arp:\n";
//    for (const auto& entry : rt.arp) {
//        std::cout << "IP Address: " << entry.ipAddress
//                  << ", MAC: " << entry.mac
//                  << ", Interface: " << entry.interface
//                  << ", Type: " << entry.type
//                  << ", Age: " << entry.age
//                  << "\n";
//    }
//
//    std::cout << "RoutingTable - NDP:\n";
//    for (const auto& entry : rt.ndp) {
//        std::cout << "IP Address: " << entry.ipAddress
//                  << ", MAC Address: " << entry.macAddress
//                  << ", Interface: " << entry.interface
//                  << ", State: " << entry.state
//                  << ", Age: " << entry.age
//                  << "\n";
//    }
//
//    std::cout << "RoutingTable - MAC:\n";
//    for (const auto& entry : rt.mac) {
//        std::cout << "MAC: " << entry.mac
//                  << ", Interface: " << entry.interface
//                  << ", VLAN ID: " << entry.vlanID
//                  << ", Type: " << entry.type
//                  << ", Age: " << entry.age
//                  << "\n";
//    }
//
//    std::cout << "RoutingTable - Rib:\n";
//    for (const auto& entry : rt.rib) {
//        std::cout << "Destination: " << entry.destination
//                  << ", Mask: " << entry.mask
//                  << ", NextHop: " << entry.nextHop
//                  << ", OutInterface: " << entry.outInterface
//                  << ", Source: " << entry.source
//                  << ", Metric: " << entry.metric
//                  << ", Age: " << entry.age
//                  << ", AdmDist: " << entry.admDist
//                  << ", Tags: ";
//        for (const auto& tag : entry.tags) {
//            std::cout << tag << " ";
//        }
//        std::cout << "\n";
//    }
//
//    std::cout << "RoutingTable - Prb:\n";
//    for (const auto& entry : rt.prb) {
//        std::cout << "Source IP: " << entry.sourceIp
//                  << ", Destination: " << entry.destination
//                  << ", Source Port: " << entry.sourcePort
//                  << ", Dest Port: " << entry.destPort
//                  << ", Protocol: " << entry.protocol
//                  << ", NextHop: " << entry.nextHop
//                  << ", OutInterface: " << entry.outInterface
//                  << ", Match Criteria: " << entry.matchCriteria
//                  << ", DSCP: " << entry.DSCP
//                  << "\n";
//    }
//
//    std::cout << "RoutingTable - Multicast:\n";
//    for (const auto& entry : rt.multicast) {
//        std::cout << "Group: " << entry.group
//                  << ", Source IP: " << entry.sourceIp
//                  << ", InInterface: " << entry.inInterface
//                  << ", RPF: " << entry.RPF
//                  << ", Protocol: " << entry.protocol
//                  << ", Age: " << entry.age
//                  << ", Route Metric: " << entry.routeMetric
//                  << ", OutInterfaces: ";
//        for (const auto& outInterface : entry.outInterface) {
//            std::cout << outInterface << " ";
//        }
//        std::cout << "\n";
//    }
//
//    std::cout << "RoutingTable - ACL:\n";
//    for (const auto& entry : rt.acl) {
//        std::cout << "Source IP: " << entry.sourceIp
//                  << ", Dest IP: " << entry.destIp
//                  << ", Protocol: " << entry.protocol
//                  << ", Source Port Range: " << entry.sourcePortRange
//                  << ", Dest Port Range: " << entry.destPortRange
//                  << ", Log String: " << entry.logString
//                  << ", Action: " << (entry.action ? "Allow" : "Deny")
//                  << ", Rule Number: " << entry.ruleNum
//                  << ", ICMP Code: " << entry.icmoCode
//                  << ", Age: " << entry.age
//                  << ", DSCP: " << entry.DSCP
//                  << "\n";
//    }
//}