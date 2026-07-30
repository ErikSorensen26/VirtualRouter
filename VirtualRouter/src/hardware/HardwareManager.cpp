// HardwareManager.cpp

#include <stdexcept>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/eventfd.h>
#include <linux/if.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <cmath>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>

#include "HardwareManager.h"
#include "cli/session/Configs.h"
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
            configJson = utils::json::parse(content);
        }
    }

    // Load interface configurations from JSON data
    if (!configJson.isObject() || !configJson.contains("Interface") || !configJson["Interface"].isObject())
        return;

    utils::json::JsonNode interfaces = configJson["Interface"];

    for (auto& value : interfaces)
    {
        interface::InterfaceType type = interface::getInterfaceType(value.name);
        if (type == interface::InterfaceType::UNDEFINED) continue;

        if (!value.isArray()) continue;
        for (const auto& obj : value)
        {
            std::string nic{};
            if (obj.isString())
                nic = obj.asString();
            else if (obj.contains("nic") && obj["nic"].isString())
                nic = obj["nic"].asString();
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

    nlWakeFd = ::eventfd(0, EFD_CLOEXEC);
    if (nlWakeFd < 0)
    {
        perror("eventfd");
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

    if (nlWakeFd >= 0)
    {
        uint64_t one = 1;
        (void)::write(nlWakeFd, &one, sizeof(one));
    }

    if (nlThread.joinable())
        nlThread.join();

    if (nlWakeFd >= 0)
    {
        ::close(nlWakeFd);
        nlWakeFd = -1;
    }

    if (nlSock >= 0)
    {
        ::close(nlSock);
        nlSock = -1;
    }
}

const HwIfaceInfo* HardwareManager::getHwInfo(interface::InterfaceKey key) const
{
    auto [type, id] = key.decode();
    unsigned int baseId = static_cast<unsigned int>(std::floor(id));
    auto pit = physicalInterfaces.find(type);
    if (pit == physicalInterfaces.end() || baseId < 0 || baseId >= static_cast<size_t>(pit->second.size() - 1))
        return nullptr;
    if (auto hwit = hwInfo.find(baseId); hwit != hwInfo.end())
        return &hwit->second;
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

    int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (sock < 0)
        return false;

    struct
    {
        struct nlmsghdr nh;
        struct ifinfomsg ifi;
        char attrbuf[512];
    } req = {};

    req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(req.ifi));
    req.nh.nlmsg_type  = RTM_NEWLINK;
    req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
    req.ifi.ifi_family = AF_UNSPEC;

    // IFLA_IFNAME
    struct rtattr* rta = reinterpret_cast<struct rtattr*>(((char*)&req) + NLMSG_ALIGN(req.nh.nlmsg_len));
    rta->rta_type = IFLA_IFNAME;
    rta->rta_len = RTA_LENGTH(strlen(ifname) + 1);
    std::memcpy(RTA_DATA(rta), ifname, strlen(ifname) + 1);
    req.nh.nlmsg_len = NLMSG_ALIGN(req.nh.nlmsg_len) + RTA_ALIGN(rta->rta_len);

    // IFLA_LINKINFO
    struct rtattr* linkinfo = reinterpret_cast<struct rtattr*>(((char*)&req) + NLMSG_ALIGN(req.nh.nlmsg_len));
    linkinfo->rta_type = IFLA_LINKINFO;
    linkinfo->rta_len = RTA_LENGTH(0);

    // IFLA_INFO_KIND = "dummy"
    struct rtattr* kind = reinterpret_cast<struct rtattr*>(((char*)linkinfo) + RTA_ALIGN(linkinfo->rta_len));
    kind->rta_type = IFLA_INFO_KIND;
    kind->rta_len = RTA_LENGTH(strlen("dummy") + 1);
    std::memcpy(RTA_DATA(kind), "dummy", strlen("dummy") + 1);

    linkinfo->rta_len += RTA_ALIGN(kind->rta_len);
    req.nh.nlmsg_len += RTA_ALIGN(linkinfo->rta_len);

    if (send(sock, &req, req.nh.nlmsg_len, 0) < 0)
    {
        close(sock);
        return false;
    }

    // Read ACK
    char buf[4096];
    ssize_t n = recv(sock, buf, sizeof(buf), 0);
    close(sock);

    if (n < 0)
        return false;

    struct nlmsghdr* nh = reinterpret_cast<struct nlmsghdr*>(buf);
    if (nh->nlmsg_type == NLMSG_ERROR)
    {
        struct nlmsgerr* err = reinterpret_cast<struct nlmsgerr*>(NLMSG_DATA(nh));
        if (err->error != 0)
            return false;
    }

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
    return false;
    if (allowDummies)
        return false;

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
        pollfd fds[2] = {};
        fds[0].fd = nlSock;
        fds[0].events = POLLIN;
        fds[1].fd = nlWakeFd;
        fds[1].events = POLLIN;

        int ready = poll(fds, 2, -1);
        if (ready < 0)
        {
            if (errno == EINTR)
                continue;
            break;
        }

        if (fds[1].revents != 0)
            break;

        if ((fds[0].revents & POLLIN) == 0)
            continue;

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
