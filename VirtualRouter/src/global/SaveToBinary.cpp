#include <SaveToBinary.h>
//
//// Static member initialization
//Save& Save::getInstance() {
//    static Save instance;
//    return instance;
//}
//
//Save::Save() {
//    load();
//}
//
//void Save::addRoutingTable(const RoutingTable& rt) {
//    std::unique_lock<std::shared_mutex> lock(mutex);
//    routingTable = rt;
//    save();
//}
//
//RoutingTable Save::getRoutingTable() {
//    std::shared_lock<std::shared_mutex> lock(mutex);
//    return routingTable;
//}
//
//template <typename T>
//void Save::serialize(std::ofstream& ofs, const T& value) {
//    ofs.write(reinterpret_cast<const char*>(&value), sizeof(T));
//}
//
//template <typename T>
//void Save::deserialize(std::ifstream& ifs, T& value) {
//    ifs.read(reinterpret_cast<char*>(&value), sizeof(T));
//}
//
//void Save::serializeString(std::ofstream& ofs, const std::string& str) {
//    int size = str.size();
//    serialize(ofs, size);
//    ofs.write(str.data(), static_cast<std::streamsize>(size));
//}
//
//void Save::deserializeString(std::ifstream& ifs, std::string& str) {
//    int size;
//    deserialize(ifs, size);
//    str.resize(size);
//    ifs.read(&str[0], static_cast<std::streamsize>(size));
//}
//
//template <typename T>
//void Save::serializeVector(std::ofstream& ofs, const std::vector<T>& vec) {
//    size_t size = vec.size();
//    serialize(ofs, size);
//    for (const auto& item : vec) {
//        serializeItem(ofs, item);
//    }
//}
//
//template <typename T>
//void Save::deserializeVector(std::ifstream& ifs, std::vector<T>& vec) {
//    size_t size;
//    deserialize(ifs, size);
//    vec.resize(size);
//    for (auto& item : vec) {
//        deserializeItem(ifs, item);
//    }
//}
//
//template <typename T>
//void Save::serializeItem(std::ofstream& ofs, const T& item) {
//    // Specialization needed for complex types
//}
//
//template <typename T>
//void Save::deserializeItem(std::ifstream& ifs, T& item) {
//    // Specialization needed for complex types
//}
//
//void Save::serializeItem(std::ofstream& ofs, const RoutingTable::RoutingEntry& item) {
//    serializeString(ofs, item.destination);
//    serializeString(ofs, item.mask);
//    serializeString(ofs, item.nextHop);
//    serializeString(ofs, item.outInterface);
//    serializeString(ofs, item.source);
//    serialize(ofs, item.metric);
//    serialize(ofs, item.age);
//    serialize(ofs, item.admDist);
//}
//
//void Save::deserializeItem(std::ifstream& ifs, RoutingTable::RoutingEntry& item) {
//    deserializeString(ifs, item.destination);
//    deserializeString(ifs, item.mask);
//    deserializeString(ifs, item.nextHop);
//    deserializeString(ifs, item.outInterface);
//    deserializeString(ifs, item.source);
//    deserialize(ifs, item.metric);
//    deserialize(ifs, item.age);
//    deserialize(ifs, item.admDist);
//}
//
//void Save::serializeItem(std::ofstream& ofs, const RoutingTable::Fib& item) {
//    serializeString(ofs, item.destination);
//    serializeString(ofs, item.nextHop);
//    serializeString(ofs, item.outInt);
//    serializeString(ofs, item.mac);
//    serialize(ofs, item.preference);
//}
//
//void Save::deserializeItem(std::ifstream& ifs, RoutingTable::Fib& item) {
//    deserializeString(ifs, item.destination);
//    deserializeString(ifs, item.nextHop);
//    deserializeString(ifs, item.outInt);
//    deserializeString(ifs, item.mac);
//    deserialize(ifs, item.preference);
//}
//
//void Save::serializeItem(std::ofstream& ofs, const RoutingTable::Arp& item) {
//    serializeString(ofs, item.ipAddress);
//    serializeString(ofs, item.mac);
//    serializeString(ofs, item.interface);
//    serializeString(ofs, item.type);
//    serialize(ofs, item.age);
//}
//
//void Save::deserializeItem(std::ifstream& ifs, RoutingTable::Arp& item) {
//    deserializeString(ifs, item.ipAddress);
//    deserializeString(ifs, item.mac);
//    deserializeString(ifs, item.interface);
//    deserializeString(ifs, item.type);
//    deserialize(ifs, item.age);
//}
//
//void Save::serializeItem(std::ofstream& ofs, const RoutingTable::NDP& item) {
//    serializeString(ofs, item.ipAddress);
//    serializeString(ofs, item.macAddress);
//    serializeString(ofs, item.interface);
//    serializeString(ofs, item.state);
//    serialize(ofs, item.age);
//}
//
//void Save::deserializeItem(std::ifstream& ifs, RoutingTable::NDP& item) {
//    deserializeString(ifs, item.ipAddress);
//    deserializeString(ifs, item.macAddress);
//    deserializeString(ifs, item.interface);
//    deserializeString(ifs, item.state);
//    deserialize(ifs, item.age);
//}
//
//void Save::serializeItem(std::ofstream& ofs, const RoutingTable::MAC& item) {
//    serializeString(ofs, item.mac);
//    serializeString(ofs, item.interface);
//    serializeString(ofs, item.vlanID);
//    serializeString(ofs, item.type);
//    serialize(ofs, item.age);
//}
//
//void Save::deserializeItem(std::ifstream& ifs, RoutingTable::MAC& item) {
//    deserializeString(ifs, item.mac);
//    deserializeString(ifs, item.interface);
//    deserializeString(ifs, item.vlanID);
//    deserializeString(ifs, item.type);
//    deserialize(ifs, item.age);
//}
//
//void Save::serializeItem(std::ofstream& ofs, const RoutingTable::Rib& item) {
//    serializeString(ofs, item.destination);
//    serializeString(ofs, item.mask);
//    serializeString(ofs, item.nextHop);
//    serializeString(ofs, item.outInterface);
//    serializeString(ofs, item.source);
//    serialize(ofs, item.metric);
//    serialize(ofs, item.age);
//    serialize(ofs, item.admDist);
//    serializeVector(ofs, item.tags);
//}
//
//void Save::deserializeItem(std::ifstream& ifs, RoutingTable::Rib& item) {
//    deserializeString(ifs, item.destination);
//    deserializeString(ifs, item.mask);
//    deserializeString(ifs, item.nextHop);
//    deserializeString(ifs, item.outInterface);
//    deserializeString(ifs, item.source);
//    deserialize(ifs, item.metric);
//    deserialize(ifs, item.age);
//    deserialize(ifs, item.admDist);
//    deserializeVector(ifs, item.tags);
//}
//
//void Save::serializeItem(std::ofstream& ofs, const RoutingTable::Prb& item) {
//    serializeString(ofs, item.sourceIp);
//    serializeString(ofs, item.destination);
//    serializeString(ofs, item.sourcePort);
//    serializeString(ofs, item.destPort);
//    serializeString(ofs, item.protocol);
//    serializeString(ofs, item.nextHop);
//    serializeString(ofs, item.outInterface);
//    serializeString(ofs, item.matchCriteria);
//    serialize(ofs, item.DSCP);
//}
//
//void Save::deserializeItem(std::ifstream& ifs, RoutingTable::Prb& item) {
//    deserializeString(ifs, item.sourceIp);
//    deserializeString(ifs, item.destination);
//    deserializeString(ifs, item.sourcePort);
//    deserializeString(ifs, item.destPort);
//    deserializeString(ifs, item.protocol);
//    deserializeString(ifs, item.nextHop);
//    deserializeString(ifs, item.outInterface);
//    deserializeString(ifs, item.matchCriteria);
//    deserialize(ifs, item.DSCP);
//}
//
//void Save::serializeItem(std::ofstream& ofs, const RoutingTable::Multicast& item) {
//    serializeString(ofs, item.group);
//    serializeString(ofs, item.sourceIp);
//    serializeString(ofs, item.inInterface);
//    serializeString(ofs, item.RPF);
//    serializeString(ofs, item.protocol);
//    serialize(ofs, item.age);
//    serialize(ofs, item.routeMetric);
//    serializeVector(ofs, item.outInterface);
//}
//
//void Save::deserializeItem(std::ifstream& ifs, RoutingTable::Multicast& item) {
//    deserializeString(ifs, item.group);
//    deserializeString(ifs, item.sourceIp);
//    deserializeString(ifs, item.inInterface);
//    deserializeString(ifs, item.RPF);
//    deserializeString(ifs, item.protocol);
//    deserialize(ifs, item.age);
//    deserialize(ifs, item.routeMetric);
//    deserializeVector(ifs, item.outInterface);
//}
//
//void Save::serializeItem(std::ofstream& ofs, const RoutingTable::ACL& item) {
//    serializeString(ofs, item.sourceIp);
//    serializeString(ofs, item.destIp);
//    serializeString(ofs, item.protocol);
//    serializeString(ofs, item.sourcePortRange);
//    serializeString(ofs, item.destPortRange);
//    serializeString(ofs, item.logString);
//    serializeString(ofs, item.action);
//    serialize(ofs, item.ruleNum);
//    serialize(ofs, item.icmoCode);
//    serialize(ofs, item.age);
//    serialize(ofs, item.DSCP);
//}
//
//void Save::deserializeItem(std::ifstream& ifs, RoutingTable::ACL& item) {
//    deserializeString(ifs, item.sourceIp);
//    deserializeString(ifs, item.destIp);
//    deserializeString(ifs, item.protocol);
//    deserializeString(ifs, item.sourcePortRange);
//    deserializeString(ifs, item.destPortRange);
//    deserializeString(ifs, item.logString);
//    deserializeString(ifs, item.action);
//    deserialize(ifs, item.ruleNum);
//    deserialize(ifs, item.icmoCode);
//    deserialize(ifs, item.age);
//    deserialize(ifs, item.DSCP);
//}
//
//void Save::serializeItem(std::ofstream& ofs, const RoutingTable::Eigrp& item) {
//    serializeString(ofs, item.network);
//    serializeString(ofs, item.mask);
//    serializeString(ofs, item.nextHop);
//    serializeString(ofs, item.interface);
//    serializeString(ofs, item.successor);
//    serializeString(ofs, item.feasibleSuccessor);
//    serializeString(ofs, item.routeSource);
//    serializeString(ofs, item.routeType);
//    serializeString(ofs, item.activeOrPassive);
//    serialize(ofs, item.metric);
//    serialize(ofs, item.feasibleDistance);
//    serialize(ofs, item.reportedDistance);
//    serialize(ofs, item.adminDistance);
//    serialize(ofs, item.holdTime);
//    serialize(ofs, item.stuckInActive);
//    serialize(ofs, item.updateTimer);
//    serialize(ofs, item.retransmitInterval);
//    serialize(ofs, item.sequenceNumber);
//    serialize(ofs, item.routeTag);
//    serialize(ofs, item.hopCount);
//    serialize(ofs, item.bandwidth);
//    serialize(ofs, item.load);
//    serialize(ofs, item.delay);
//    serialize(ofs, item.reliability);
//    serialize(ofs, item.mtu);
//}
//
//void Save::deserializeItem(std::ifstream& ifs, RoutingTable::Eigrp& item) {
//    deserializeString(ifs, item.network);
//    deserializeString(ifs, item.mask);
//    deserializeString(ifs, item.nextHop);
//    deserializeString(ifs, item.interface);
//    deserializeString(ifs, item.successor);
//    deserializeString(ifs, item.feasibleSuccessor);
//    deserializeString(ifs, item.routeSource);
//    deserializeString(ifs, item.routeType);
//    deserializeString(ifs, item.activeOrPassive);
//    deserialize(ifs, item.metric);
//    deserialize(ifs, item.feasibleDistance);
//    deserialize(ifs, item.reportedDistance);
//    deserialize(ifs, item.adminDistance);
//    deserialize(ifs, item.holdTime);
//    deserialize(ifs, item.stuckInActive);
//    deserialize(ifs, item.updateTimer);
//    deserialize(ifs, item.retransmitInterval);
//    deserialize(ifs, item.sequenceNumber);
//    deserialize(ifs, item.routeTag);
//    deserialize(ifs, item.hopCount);
//    deserialize(ifs, item.bandwidth);
//    deserialize(ifs, item.load);
//    deserialize(ifs, item.delay);
//    deserialize(ifs, item.reliability);
//    deserialize(ifs, item.mtu);
//}
//
//void Save::save() {
//    std::ofstream ofs(filePath, std::ios::binary);
//    if (!ofs) {
//        std::cerr << "Error opening file for writing: " << filePath << std::endl;
//        return;
//    }
//    serialize(ofs, routingTable);
//    ofs.close();
//}
//
//void Save::load() {
//    std::ifstream ifs(filePath, std::ios::binary);
//    if (!ifs) {
//        std::cerr << "Error opening file for reading: " << filePath << std::endl;
//        return;
//    }
//    deserialize(ifs, routingTable);
//    ifs.close();
//}
