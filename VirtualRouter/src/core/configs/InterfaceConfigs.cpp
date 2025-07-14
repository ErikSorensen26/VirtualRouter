#include "InterfaceConfigs.h"
#include <Eigrp.h>

uint8_t* InterfaceConfigs::getMac(uint8_t* mac)
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    std::memcpy(mac, macAddress, 6);
    return mac;
}

uint64_t InterfaceConfigs::getMac()
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    return readU48(macAddress);
}

void InterfaceConfigs::setMac(const uint8_t* mac)
{
    std::unique_lock<std::shared_mutex> lock(ipMutex);
    std::memcpy(macAddress, mac, 6);
}

InterfaceConfigs::IPv6State::IPv6Address* InterfaceConfigs::IPv6State::addAddress(const uint8_t* ip, bool local, uint8_t prefix)
{
    if (local)
    {
        // Only one local-address can exist
        if (!linkLocalAddress->valid)
        {
            std::memcpy(linkLocalAddress->ip, ip, 16);
            linkLocalAddress->prefix = prefix;
            linkLocalAddress->tentative = true;
            linkLocalAddress->valid = false;
            return linkLocalAddress;
        }
        else
        {
            std::cerr << "Error: Link-Local address already assigned";
        }
    }
    else
    {
        IPv6Address* address = new IPv6Address();
        std::memcpy(address->ip, ip, 16);
        address->prefix = prefix;
        address->tentative = true;
        address->valid = false;
        globalAddresses.push_back(address);
        return globalAddresses.back();
    }
    return nullptr;
}

InterfaceConfigs::~InterfaceConfigs()
{
    for (auto& [_, eigrp] : eigrp.eigrpInterfaceConfigList)
    {
        delete eigrp;
    }
    eigrp.eigrpInterfaceConfigList.clear();
}

InterfaceConfigs::IPv6State::IPv6Address* InterfaceConfigs::IPv6State::addUniqueLocalAddress(const uint8_t* ip, uint8_t prefixLen)
{
    IPv6Address* address = new IPv6Address();
    std::memcpy(address->ip, ip, 16);
    address->prefix = prefixLen;
    address->tentative = true;
    address->valid = false;
    uniqueLocalAddresses.push_back(address);
    return uniqueLocalAddresses.back();
}

void InterfaceConfigs::IPv6State::removeAddress(const uint8_t* ip, bool local)
{
    if (local)
    {
        auto ipv6 = linkLocalAddress;
        linkLocalAddress = new IPv6Address();
        delete ipv6;
    }
    else
    {
        auto& addressList = (ip[0] == 0xfc && ip[1] == 0x00)
            ? uniqueLocalAddresses
            : globalAddresses;
        
        std::erase_if(addressList, [&](const IPv6Address* addr) {
            return std::memcmp(addr->ip, ip, 16);
        });
    }
}

void InterfaceConfigs::IPv6State::IPv6Address::validateAddress(bool local)
{
    if (local)
    {
        tentative = false;
        valid = true;
    }
    else
    {
        globalTentative = false;
        globalValid = true;
    }
}

void InterfaceConfigs::IPv6State::validateGlobalAddresses()
{
    for (auto& address : globalAddresses)
    {
        address->validateAddress(false);
    }
}

void InterfaceConfigs::IPv6State::validateLinkLocalAddress()
{
    linkLocalAddress->validateAddress(true);
}

uint8_t* InterfaceConfigs::IPv6State::getLocalAddress(uint8_t* out)
{
    std::shared_lock lock(ipMutex);
    if (linkLocalAddress)
    {
        std::memcpy(out, linkLocalAddress->ip, 16);
        return out;
    }
    return nullptr;
}

__uint128_t InterfaceConfigs::IPv6State::getLocalAddress()
{
    std::shared_lock lock(ipMutex);
    if (linkLocalAddress)
    {
        return readU128(linkLocalAddress->ip);
    }
    return 0;
}

uint8_t InterfaceConfigs::IPv6State::getGlobalUnicastPair(uint8_t* out)
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    if (!globalAddresses.empty())
    {
        auto* address = globalAddresses.front();
        std::memcpy(out, address->ip, 16);
        return address->prefix;
    }
    return {};
}

uint8_t* InterfaceConfigs::IPv6State::getGlobalUnicast(uint8_t* out)
{
    std::shared_lock lock(ipMutex);
    if (!globalAddresses.empty())
    {
        std::memcpy(out, globalAddresses.front()->ip, 16);
        return out;
    }
    return nullptr;
}

uint8_t InterfaceConfigs::IPv6State::getGlobalUnicastMask()
{
    std::shared_lock lock(ipMutex);
    if (!globalAddresses.empty())
    {
        return globalAddresses.front()->prefix;
    }
    return {};
}

__uint128_t InterfaceConfigs::IPv6State::getGlobalUnicast()
{
    std::shared_lock lock(ipMutex);
    if (!globalAddresses.empty())
    {
        return readU128(globalAddresses.front()->ip);
    }
    return 0;
}

uint8_t InterfaceConfigs::IPv6State::getLocalUnicastPair(uint8_t* out)
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    if (!globalAddresses.empty())
    {
        auto* address = uniqueLocalAddresses.front();
        std::memcpy(out, address->ip, 16);
        return address->prefix;
    }
    return {};
}

uint8_t* InterfaceConfigs::IPv6State::getLocalUnicast(uint8_t* out)
{
    std::shared_lock lock(ipMutex);
    if (!uniqueLocalAddresses.empty())
    {
        std::memcpy(out, uniqueLocalAddresses.front()->ip, 16);
        return out;
    }
    return nullptr;
}

uint8_t InterfaceConfigs::IPv6State::getLocalUnicastMask()
{
    std::shared_lock lock(ipMutex);
    if (!uniqueLocalAddresses.empty())
    {
        return uniqueLocalAddresses.front()->prefix;
    }
    return {};
}

__uint128_t InterfaceConfigs::IPv6State::getLocalUnicast()
{
    std::shared_lock lock(ipMutex);
    if (!uniqueLocalAddresses.empty())
    {
        return readU128(uniqueLocalAddresses.front()->ip);
    }
    return 0;
}

std::vector<IPAddress> InterfaceConfigs::IPv6State::getGlobalList()
{
    std::shared_lock lock(ipMutex);
    std::vector<IPAddress> ips;
    for (const auto* ip : globalAddresses)
    {
        ips.emplace_back();
        std::copy(ip->ip, ip->ip + 16, ips.back().raw);
        ips.back().isV6 = true;
    }
    return ips;
}

std::vector<IPAddress> InterfaceConfigs::IPv6State::getLocalList()
{
    std::shared_lock lock(ipMutex);
    std::vector<IPAddress> ips;
    for (const auto* ip : uniqueLocalAddresses)
    {
        ips.emplace_back();
        std::copy(ip->ip, ip->ip + 16, ips.back().raw);
        ips.back().isV6 = true;
    }
    return ips;
}

InterfaceConfigs::IPv6State::IPv6State(TimeManager& time) : timeManager(time) {}

InterfaceConfigs::IPv6State::~IPv6State()
{
    for (auto* addr : globalAddresses)
    {
        if (addr->preferredLifetime != 0) { timeManager.cancelTimer(addr->preferedExpirationId);}
        if (addr->expirationId != 0) { timeManager.cancelTimer(addr->expirationId); } delete addr;
    }
    for (auto* addr : uniqueLocalAddresses)
    {
        if (addr->preferredLifetime != 0) { timeManager.cancelTimer(addr->preferedExpirationId); }
        if (addr->expirationId != 0) { timeManager.cancelTimer(addr->expirationId); } delete addr;
    }

    if (linkLocalAddress->preferredLifetime != 0) { timeManager.cancelTimer(linkLocalAddress->preferedExpirationId); }
    if (linkLocalAddress->expirationId != 0) { timeManager.cancelTimer(linkLocalAddress->expirationId); } delete linkLocalAddress;
}

bool InterfaceConfigs::hasAddress(const uint8_t* address)
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    if (std::memcmp(ipv6.getLocalAddress(), address, 16) == 0) return true;
    for (auto ip : ipv6.getGlobalList()) { if (ip == address) return true; }
    for (auto ip : ipv6.getLocalList()) { if (ip == address) return true; }
    return false;
}
