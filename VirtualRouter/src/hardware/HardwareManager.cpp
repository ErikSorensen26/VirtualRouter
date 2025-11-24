#include <HardwareManager.h>
#include <Configs.h>
#include <InterfaceType.hpp>
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
#include <HeaderHelpers.hpp>
#include <Interface.h>
#include <Ifname.h>

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

    // Load interface configurations from JSON data
    if (!configJson.is_object() || !configJson.contains("Interface") || !configJson["Interface"].is_object())
        return;

    nlohmann::json& interfaces = configJson["Interface"];

    for (auto& [key, value] : interfaces.items())
    {
        InterfaceType type = getInterfaceType(key);
        if (type == InterfaceType::UNDEFINED) continue;

        if (!value.is_array()) continue;
        for (const auto& obj : value)
        {
            std::string nic{0};
            if (obj.is_string())
                nic = obj.get<std::string>();
            else if (obj.contains("nic") && obj["nic"].is_string())
                nic = obj["nic"].get<std::string>();
            else
                continue;

            uint32_t index = ifnametoindex(nic.c_str());

            physicalInterfaces[type].push_back(index);
            if (ensureInterface(nic.c_str()))
                continue;

            if (allowDummies && createDummy(nic.c_str()))
            {
                if (!ensureInterface(nic.c_str()))
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

    nlThreadRunning.store(true);
    nlThread = std::thread(&HardwareManager::netlinkMonitorThread, this);
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

uint32_t HardwareManager::getInterface(InterfaceType type, int index)
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

bool HardwareManager::ensureInterface(const char* ifname)
{
    if (strlen(ifname) >= IFNAMSIZ) return false;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return false;

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    std::snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);

    bool exists = false; 
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0)
    {
        auto info = extractHwInfo(sock, ifr);
        if (info.has_value())
        {
            exists = true;
            info->index = ifnametoindex(ifname);
            info->ifname = ifname;
            hwInfo[info->index] = *info;
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

bool HardwareManager::createDummy(const char* ifname)
{
    std::string cmd = "ip link add " + std::string(ifname) + " type dummy";
    int ret = system(cmd.c_str());
    return (ret == 0);
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

void HardwareManager::registerInterface(const HwIfaceInfo* info, Interface* iface)
{
    bringUp(info->ifname);
    registeredInterfaces[info->index].push_back(iface);
}

void HardwareManager::unregisterInterface(const HwIfaceInfo* info, Interface* iface)
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
