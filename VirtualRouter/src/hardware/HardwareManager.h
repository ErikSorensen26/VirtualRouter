// HardwareManager.h

#ifndef HARDWARE_MANAGER
#define HARDWARE_MANAGER

#include <vector>
#include <string>
#include <json.hpp>
#include <map>
#include <thread>
#include <atomic>

namespace interface { class Interface; enum class InterfaceType : uint8_t; }
namespace cli { class FileSystem; }

namespace hardware
{

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
    HardwareManager() = default;
    ~HardwareManager();

    void addHardware(const std::string& hwConfigFile, cli::FileSystem& fileSystem, bool enableDummies = false);

    uint32_t getInterface(interface::InterfaceType type, int index);

    void registerInterface(const HwIfaceInfo* info, interface::Interface* iface);
    void unregisterInterface(const HwIfaceInfo* info, interface::Interface* iface);

    const HwIfaceInfo* getHwInfo(uint32_t index) const;
    const std::map<interface::InterfaceType, std::vector<uint32_t>>& getPhysicalInterfaces() { return physicalInterfaces; }
    const std::vector<uint32_t>& getPhysicalInterfaces(interface::InterfaceType type) { return physicalInterfaces[type]; }

    std::optional<HwIfaceInfo> extractHwInfo(int sock, struct ifreq& ifr);

    bool bringUp(const std::string& ifname);
    bool bringDown(const std::string& ifname);

private:
    void netlinkMonitorThread();
    void onLinkEvent(uint32_t index, bool carrierUp);

    bool ensureInterface(const char* ifname);
    bool createDummy(const char* ifname);

    std::unordered_map<uint32_t, std::vector<interface::Interface*>> registeredInterfaces;
    std::map<interface::InterfaceType, std::vector<uint32_t>> physicalInterfaces;
    std::map<uint32_t, HwIfaceInfo> hwInfo;

    nlohmann::ordered_json configJson;
    bool allowDummies;

    int nlSock = -1;
    std::thread nlThread;
    std::atomic<bool> nlThreadRunning{false};
};

} // namespace hardware

#endif // HARDWARE_MANAGER

