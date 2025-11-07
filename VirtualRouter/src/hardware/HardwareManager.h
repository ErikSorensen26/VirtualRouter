// HardwareManager.h

#ifndef HARDWARE_MANAGER
#define HARDWARE_MANAGER

#include <vector>
#include <string>
#include <json.hpp>
#include <map>

class IFileSystem;
enum class InterfaceType : uint8_t;

struct HwIfaceInfo
{
    std::string iface;
    uint64_t mac;
    uint64_t bandwidth;
};

class HardwareManager
{
public:
    HardwareManager(const std::string& hwConfigFile, IFileSystem& fileSystem, bool enableDummies = false);

    std::string getInterface(InterfaceType type, int index);
    bool bringUp(const std::string& ifname);
    bool bringDown(const std::string& ifname);

    const HwIfaceInfo* getHwInfo(const std::string& iface) const;
    const std::map<InterfaceType, std::vector<std::string>>& getPhysicalInterfaces() { return physicalInterfaces; }
    const std::vector<std::string>& getPhysicalInterfaces(InterfaceType type) { return physicalInterfaces[type]; }

private:
    std::optional<HwIfaceInfo> extractHwInfo(int sock, struct ifreq& ifr);
    bool ensureInterface(const std::string& ifname, InterfaceType type);
    bool createDummy(const std::string& ifname);

    std::map<InterfaceType, std::vector<std::string>> physicalInterfaces;
    std::map<std::string, HwIfaceInfo> hwInfo;

    nlohmann::json configJson;
    bool allowDummies;
};

#endif // HARDWARE_MANAGER
