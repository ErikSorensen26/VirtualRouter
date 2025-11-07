#include <HardwareManager.h>
#include <Configs.h>
#include <InterfaceType.hpp>
#include <fstream>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <unistd.h>
#include <cstring>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <HeaderHelpers.hpp>
#include <iostream>

HardwareManager::HardwareManager(const std::string& hwConfigFile, IFileSystem& fileSystem, bool enableDummies)
    : allowDummies(enableDummies)
{
    // Load JSON data
    if (fileSystem.fileExists(hwConfigFile))
    {
        std::string content;
        if (fileSystem.readFile(hwConfigFile, content))
        {
            try
            {
                configJson = json::parse(content);
            }
            catch (json::parse_error& e)
            {
                configJson = json::object();
            }
        }
    }
    else 
    {
        configJson = json::object();
    }


    if (configJson.is_object())
    {
        // Load interface configurations from JSON data
        if (configJson.contains("Interface") && configJson["Interface"].is_object())
        {
            nlohmann::json& interfaces = configJson["Interface"];

            for (auto& [key, value] : interfaces.items())
            {
                InterfaceType type = getInterfaceType(key);
                if (type == InterfaceType::UNDEFINED) continue;

                if (!value.is_array()) continue;
                for (const auto& obj : value)
                {
                    std::string nic;
                    if (obj.is_string())
                        nic = obj.get<std::string>();
                    else if (obj.contains("nic") && obj["nic"].is_string())
                        nic = obj["nic"].get<std::string>();

                    if (!nic.empty())
                    {
                        physicalInterfaces[type].push_back(nic);
                        if (!ensureInterface(nic, type))
                        {
                            if (allowDummies && createDummy(nic))
                            {
                                if (!ensureInterface(nic, type))
                                    throw std::runtime_error("Failed to create dummy interface " + nic);
                            }
                            else
                            {
                                throw std::runtime_error("Failed to ensure interface: " + nic);
                            }
                        }
                    }
                }
            }
        }
    }
}

std::string HardwareManager::getInterface(InterfaceType type, int index)
{
    auto it = physicalInterfaces.find(type);
    if (it == physicalInterfaces.end() || index < 0 || index >= (int)it->second.size())
        return {};
    return it->second[index];
}

const HwIfaceInfo* HardwareManager::getHwInfo(const std::string& iface) const
{
    if (auto it = hwInfo.find(iface); it != hwInfo.end())
        return &it->second;
    return nullptr;
}

std::optional<HwIfaceInfo> HardwareManager::extractHwInfo(int sock, struct ifreq& ifr)
{
    HwIfaceInfo info;

    if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0)
    {
        info.mac = readU48(reinterpret_cast<uint8_t*>(ifr.ifr_hwaddr.sa_data));
    }
    else return std::nullopt;

    struct ethtool_cmd edata {};
    edata.cmd = ETHTOOL_GSET;
    ifr.ifr_data = reinterpret_cast<char*>(&edata);

    if (ioctl(sock, SIOCETHTOOL, &ifr) == 0)
    {
        unsigned int mbps = ethtool_cmd_speed(&edata);
        unsigned int kbps = mbps * 1000;
        info.bandwidth = kbps;
    }
    else return std::nullopt;

    return info;
}

bool HardwareManager::ensureInterface(const std::string& ifname, InterfaceType type)
{
    if (ifname.size() >= IFNAMSIZ) return false;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return false;

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    std::snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname.c_str());

    bool exists = false; 
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0)
    {
        auto info = extractHwInfo(sock, ifr);
        if (info.has_value())
        {
            exists = true;
            hwInfo[ifname] = *info;
        }
    }
    else
    {
        if (errno == ENODEV)
            exists = false;
    }

    ::close(sock);
    return exists;
}

bool HardwareManager::createDummy(const std::string& ifname)
{
    std::string cmd = "ip link add " + ifname + " type dummy";
    int ret = system(cmd.c_str());
    return (ret == 0);
}

bool HardwareManager::bringUp(const std::string& ifname)
{
    if (ifname.size() >= IFNAMSIZ)
        return false;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return false;

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    std::snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname.c_str());

    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0)
    {
        close(sock);
        return false;
    }

    ifr.ifr_flags |= IFF_UP;

    bool ok = (ioctl(sock, SIOCSIFFLAGS, &ifr) == 0);
    ::close(sock);
    return ok;
}

bool HardwareManager::bringDown(const std::string& ifname)
{
    if (ifname.size() >= IFNAMSIZ)
        return false;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return false;

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    std::snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname.c_str());

    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0)
    {
        close(sock);
        return false;
    }

    ifr.ifr_flags &= ~IFF_UP; // clear UP

    bool ok = (ioctl(sock, SIOCSIFFLAGS, &ifr) == 0);
    close(sock);
    return ok;
}
