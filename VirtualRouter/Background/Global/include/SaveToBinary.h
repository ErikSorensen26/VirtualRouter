#pragma once

#include "RoutingTable.h"
//#include <shared_mutex>
//#include <mutex>
//#include <fstream>
//#include <iostream>
//#include <vector>
//#include <string>
//
//class Save {
//public:
//    static Save& getInstance();
//
//    void addRoutingTable(const RoutingTable& rt);
//    RoutingTable getRoutingTable();
//
//private:
//    Save();
//    Save(const Save&) = delete;
//    Save& operator=(const Save&) = delete;
//
//    std::string filePath = "../data.bin";
//    RoutingTable routingTable;
//    std::shared_mutex mutex;
//
//    template <typename T>
//    void serialize(std::ofstream& ofs, const T& value);
//
//    template <typename T>
//    void deserialize(std::ifstream& ifs, T& value);
//
//    void serializeString(std::ofstream& ofs, const std::string& str);
//    void deserializeString(std::ifstream& ifs, std::string& str);
//
//    template <typename T>
//    void serializeVector(std::ofstream& ofs, const std::vector<T>& vec);
//
//    template <typename T>
//    void deserializeVector(std::ifstream& ifs, std::vector<T>& vec);
//
//    template <typename T>
//    void serializeItem(std::ofstream& ofs, const T& item);
//
//    template <typename T>
//    void deserializeItem(std::ifstream& ifs, T& item);
//
//    // Serialization/Deserialization specializations for RoutingTable entries
//    void serializeItem(std::ofstream& ofs, const RoutingTable::RoutingEntry& item);
//    void deserializeItem(std::ifstream& ifs, RoutingTable::RoutingEntry& item);
//
//    void serializeItem(std::ofstream& ofs, const RoutingTable::Fib& item);
//    void deserializeItem(std::ifstream& ifs, RoutingTable::Fib& item);
//
//    void serializeItem(std::ofstream& ofs, const RoutingTable::Arp& item);
//    void deserializeItem(std::ifstream& ifs, RoutingTable::Arp& item);
//
//    void serializeItem(std::ofstream& ofs, const RoutingTable::NDP& item);
//    void deserializeItem(std::ifstream& ifs, RoutingTable::NDP& item);
//
//    void serializeItem(std::ofstream& ofs, const RoutingTable::MAC& item);
//    void deserializeItem(std::ifstream& ifs, RoutingTable::MAC& item);
//
//    void serializeItem(std::ofstream& ofs, const RoutingTable::Rib& item);
//    void deserializeItem(std::ifstream& ifs, RoutingTable::Rib& item);
//
//    void serializeItem(std::ofstream& ofs, const RoutingTable::Prb& item);
//    void deserializeItem(std::ifstream& ifs, RoutingTable::Prb& item);
//
//    void serializeItem(std::ofstream& ofs, const RoutingTable::Multicast& item);
//    void deserializeItem(std::ifstream& ifs, RoutingTable::Multicast& item);
//
//    void serializeItem(std::ofstream& ofs, const RoutingTable::ACL& item);
//    void deserializeItem(std::ifstream& ifs, RoutingTable::ACL& item);
//
//    void serializeItem(std::ofstream& ofs, const RoutingTable::Eigrp& item);
//    void deserializeItem(std::ifstream& ifs, RoutingTable::Eigrp& item);
//
//    void save();
//    void load();
//};
