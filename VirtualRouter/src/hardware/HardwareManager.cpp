// HardwareManager.cpp

#include <stdexcept>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <unistd.h>
#include <cstring>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>

#include "HardwareManager.h"
#include "cli/runtime/Configs.h"
#include "interface/Interface.h"
#include "interface/configs/InterfaceType.hpp"
#include "hardware/Ifname.h"

namespace hardware
{
void HardwareManager::addHardware(const std::string& hwConfigFile, cli::FileSystem& fileSystem, bool enableDummies)
{
    allowDummies = enableDummies;
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

    // Load interface configurations from JSON data
    if (!configJson.is_object() || !configJson.contains("Interface") || !configJson["Interface"].is_object())
        return;

    nlohmann::ordered_json& interfaces = configJson["Interface"];

    for (auto& [key, value] : interfaces.items())
    {
        interface::InterfaceType type = interface::getInterfaceType(key);
        if (type == interface::InterfaceType::UNDEFINED) continue;

        if (!value.is_array()) continue;
        for (const auto& obj : value)
        {
            std::string nic{};
            if (obj.is_string())
                nic = obj.get<std::string>();
            else if (obj.contains("nic") && obj["nic"].is_string())
                nic = obj["nic"].get<std::string>();
            else
                continue;

            auto addInterface = [&](const char* iface) -> bool {
                if (!ensureInterface(iface)) return false;
                uint32_t index = ifnametoindex(iface);
                physicalInterfaces[type].push_back(index);
                return true;
            };

            if (addInterface(nic.c_str()))
            {
                //bringDown(nic.c_str());
                continue;
            }

            if (allowDummies && createDummy(nic.c_str()))
            {
                if (!addInterface(nic.c_str()))
                    throw std::runtime_error("Failed to create dummy interface " + nic);
            }
            else
            {
                throw std::runtime_error("Failed to ensure interface: " + nic);
            }
        }
    }

    nlSock = ::socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (nlSock < 0)
    {
        perror("socket(AF_NETLINK)");
        return;
    }

    sockaddr_nl addr = {};
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = RTMGRP_LINK;

    if (bind(nlSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        perror("bind(netlink)");
        ::close(nlSock);
        nlSock = -1;
        return;
    }

    if (!nlThread.joinable())
    {
        nlThreadRunning.store(true);
        nlThread = std::thread(&HardwareManager::netlinkMonitorThread, this);
    }
}

HardwareManager::~HardwareManager()
{
    nlThreadRunning.store(false);

    if (nlSock >= 0)
    {
        ::shutdown(nlSock, SHUT_RDWR);
        ::close(nlSock);
        nlSock = -1;
    }

    if (nlThread.joinable())
        nlThread.join();
}

uint32_t HardwareManager::getInterface(interface::InterfaceType type, int index)
{
    auto it = physicalInterfaces.find(type);
    if (it == physicalInterfaces.end() || index < 0 || index >= (int)it->second.size())
        return {};
    return it->second[static_cast<size_t>(index)];
}

const HwIfaceInfo* HardwareManager::getHwInfo(uint32_t index) const
{
    if (auto it = hwInfo.find(index); it != hwInfo.end())
        return &it->second;
    return nullptr;
}

std::optional<HwIfaceInfo> HardwareManager::extractHwInfo(int sock, struct ifreq& ifr)
{
    HwIfaceInfo info;

    info.ifname = ifr.ifr_name;
    uint32_t idx = ifnametoindex(ifr.ifr_name);
    if (idx == 0)
        return std::nullopt;
    info.index = idx;

    if (ioctl(sock, SIOCGIFHWADDR, &ifr) != 0)
        return std::nullopt;
    info.mac = utils::readU48(reinterpret_cast<uint8_t*>(ifr.ifr_hwaddr.sa_data));

    struct ethtool_cmd edata {};
    edata.cmd = ETHTOOL_GSET;
    ifr.ifr_data = reinterpret_cast<char*>(&edata);

    if (ioctl(sock, SIOCETHTOOL, &ifr) == 0)
    {
        unsigned int mbps = ethtool_cmd_speed(&edata);
        if (mbps != (unsigned int)-1)
            info.bandwidth = mbps * 1000;
        else
            return std::nullopt;
    }
    else
        return std::nullopt;

    return info;
}

bool HardwareManager::ensureInterface(const char* ifname)
{
    if (strlen(ifname) >= IFNAMSIZ)
        return false;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return false;

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';

    bool success = false; 
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0)
    {
        struct ifreq ifr_hw;
        memset(&ifr_hw, 0, sizeof(ifr_hw));
        strncpy(ifr_hw.ifr_name, ifname, IFNAMSIZ - 1);
        ifr_hw.ifr_name[IFNAMSIZ - 1] = '\0';

        auto info = extractHwInfo(sock, ifr_hw);

        HwIfaceInfo hw{};
        hw.ifname = ifname;
        uint32_t idx = ifnametoindex(ifname);
        if (idx != 0)
        {
            hw.index = idx;
            if (info.has_value())
                hw = *info;

            hwInfo[hw.index] = hw;
            success = true;
        }
    }
    ::close(sock);
    return success;
}

bool HardwareManager::createDummy(const char* ifname)
{
    if (strlen(ifname) >= IFNAMSIZ)
        return false;

    std::string cmd = "ip link add " + std::string(ifname) + " type dummy";
    int ret = system(cmd.c_str());
    if (ret != 0)
        return false;
    
    return ifnametoindex(ifname) != 0;
}

bool HardwareManager::bringUp(const std::string& ifname)
{
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

void HardwareManager::registerInterface(const HwIfaceInfo* info, interface::Interface* iface)
{
    registeredInterfaces[info->index].push_back(iface);
}

void HardwareManager::unregisterInterface(const HwIfaceInfo* info, interface::Interface* iface)
{
    auto it = registeredInterfaces.find(info->index);
    if (it == registeredInterfaces.end()) return;
    it->second.erase(std::remove(it->second.begin(), it->second.end(), iface), it->second.end());
    if (it->second.empty())
    {
        registeredInterfaces.erase(info->index);
        bringDown(info->ifname);
    }
}

void HardwareManager::onLinkEvent(uint32_t index, bool carrierUp)
{
    auto it = registeredInterfaces.find(index);
    if (it == registeredInterfaces.end()) return;
    for (auto& iface : it->second)
        iface->physicalShutdown(!carrierUp);
}

void HardwareManager::netlinkMonitorThread()
{
    constexpr size_t BUF_SIZE = 8192;
    char buffer[BUF_SIZE];

    while (nlThreadRunning.load())
    {
        ssize_t len = recv(nlSock, buffer, sizeof(buffer), 0);
        if (len <= 0)
            continue;
        
        nlmsghdr* nh;
        for (nh = reinterpret_cast<nlmsghdr*>(buffer); NLMSG_OK(nh, len); nh = NLMSG_NEXT(nh, len))
        {
            if (nh->nlmsg_type == NLMSG_DONE)
                break;
            
            if (nh->nlmsg_type != RTM_NEWLINK)
                continue;
            
            auto* ifi = reinterpret_cast<ifinfomsg*>(NLMSG_DATA(nh));

            bool carrierUp = (ifi->ifi_flags & IFF_LOWER_UP) != 0;
            uint32_t index = static_cast<unsigned int>(ifi->ifi_index);

            onLinkEvent(index, carrierUp);
        }
    }
}

} // namespace hardware
