// HardwareManager.h

#ifndef HARDWARE_MANAGER
#define HARDWARE_MANAGER

#include <vector>
#include <string>
#include <json.hpp>
#include <map>

class IFileSystem;
enum class InterfaceType : uint8_t;

class HardwareManager
{
public:
    HardwareManager(const std::string& hwConfigFile, IFileSystem& fileSystem, bool enableDummies = false);

    std::string getInterface(InterfaceType type, int index);
    bool bringUp(const std::string& ifname);
    bool bringDown(const std::string& ifname);

    std::string getMac(const std::string& ifname);
    const std::vector<std::string> getMacs(InterfaceType type);
    const std::map<InterfaceType, std::vector<std::string>>& getPhysicalInterfaces() { return physicalInterfaces; }
    const std::vector<std::string>& getPhysicalInterfaces(InterfaceType type) { return physicalInterfaces[type]; }

private:
    bool ensureInterface(const std::string& ifname, InterfaceType type);
    bool createDummy(const std::string& ifname);

    std::map<InterfaceType, std::vector<std::string>> physicalInterfaces;
    std::map<std::string, std::string> macs;
    std::map<InterfaceType, std::vector<std::string>> ifaceToMac;

    nlohmann::json configJson;
    bool allowDummies;
};

#endif // HARDWARE_MANAGER
