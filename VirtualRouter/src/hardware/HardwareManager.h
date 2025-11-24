// HardwareManager.h

#ifndef HARDWARE_MANAGER
#define HARDWARE_MANAGER

#include <vector>
#include <string>
#include <json.hpp>
#include <map>
#include <thread>
#include <atomic>

class IFileSystem;
class Interface;
enum class InterfaceType : uint8_t;

struct HwIfaceInfo
{
    uint32_t index;
    std::string ifname;
    uint64_t mac;
    uint64_t bandwidth;
};

class HardwareManager
{
public:
    using StateCallback = std::function<void(bool carrier)>;
    HardwareManager(const std::string& hwConfigFile, IFileSystem& fileSystem, bool enableDummies = false);
    ~HardwareManager();

    uint32_t getInterface(InterfaceType type, int index);

    void registerInterface(const HwIfaceInfo* info, Interface* iface);
    void unregisterInterface(const HwIfaceInfo* info, Interface* iface);

    const HwIfaceInfo* getHwInfo(uint32_t index) const;
    const std::map<InterfaceType, std::vector<uint32_t>>& getPhysicalInterfaces() { return physicalInterfaces; }
    const std::vector<uint32_t>& getPhysicalInterfaces(InterfaceType type) { return physicalInterfaces[type]; }

private:
    std::optional<HwIfaceInfo> extractHwInfo(int sock, struct ifreq& ifr);

    bool bringUp(const std::string& ifname);
    bool bringDown(const std::string& ifname);

    void netlinkMonitorThread();
    void onLinkEvent(uint32_t index, bool carrierUp);

    bool ensureInterface(const char* ifname);
    bool createDummy(const char* ifname);

    std::unordered_map<uint32_t, std::vector<Interface*>> registeredInterfaces;
    std::map<InterfaceType, std::vector<uint32_t>> physicalInterfaces;
    std::map<uint32_t, HwIfaceInfo> hwInfo;

    nlohmann::json configJson;
    bool allowDummies;

    int nlSock = -1;
    std::thread nlThread;
    std::atomic<bool> nlThreadRunning{false};
};

#endif // HARDWARE_MANAGER
