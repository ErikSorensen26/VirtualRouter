#include "InterfaceConfigs.h"
#include <Eigrp.h>

ByteString InterfaceConfigs::getMac()
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    return macAddress;
}

void InterfaceConfigs::setMac(const ByteString& mac)
{
    std::unique_lock<std::shared_mutex> lock(ipMutex);
    macAddress = mac;
}

InterfaceConfigs::IPv6State::IPv6Address* InterfaceConfigs::IPv6State::addAddress(const ByteString& ip, bool local, uint8_t prefix)
{
    if (local)
    {
        // Only one local-address can exist
        if (linkLocalAddress->ip.empty())
        {
            linkLocalAddress->ip = ip;
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
        address->ip = ip;
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

InterfaceConfigs::IPv6State::IPv6Address* InterfaceConfigs::IPv6State::addUniqueLocalAddress(const ByteString& ip, uint8_t prefixLen)
{
    IPv6Address* address = new IPv6Address();
    address->ip = ip;
    address->prefix = prefixLen;
    address->tentative = true;
    address->valid = false;
    uniqueLocalAddresses.push_back(address);
    return uniqueLocalAddresses.back();
}

void InterfaceConfigs::IPv6State::removeAddress(const ByteString& ip, bool local)
{
    if (local)
    {
        auto ipv6 = linkLocalAddress;
        linkLocalAddress = new IPv6Address();
        delete ipv6;
    }
    else
    {
        auto& addressList = (ip.substr(0, 2) == "\xfc\x00")
            ? uniqueLocalAddresses
            : globalAddresses;
        
        std::erase_if(addressList, [&](const IPv6Address* addr) {
            return addr->ip == ip;
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
    if (!linkLocalAddress->ip.empty())
    {
        linkLocalAddress->validateAddress(true);
    }
}

ByteString InterfaceConfigs::IPv6State::getLocalAddress()
{
    std::shared_lock lock(ipMutex);
    return linkLocalAddress ? linkLocalAddress->ip : "";
}

std::pair<ByteString, uint8_t> InterfaceConfigs::IPv6State::getGlobalUnicastPair()
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    if (!globalAddresses.empty())
    {
        auto* address = globalAddresses.front();
        return {address->ip, address->prefix};
    }
    return {};
}

ByteString InterfaceConfigs::IPv6State::getGlobalUnicast()
{
    std::shared_lock lock(ipMutex);
    if (!globalAddresses.empty())
    {
        return globalAddresses.front()->ip;
    }
    return {};
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

std::pair<ByteString, uint8_t> InterfaceConfigs::IPv6State::getLocalUnicastPair()
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    if (!globalAddresses.empty())
    {
        auto* address = uniqueLocalAddresses.front();
        return {address->ip, address->prefix};
    }
    return {};
}

ByteString InterfaceConfigs::IPv6State::getLocalUnicast()
{
    std::shared_lock lock(ipMutex);
    if (!uniqueLocalAddresses.empty())
    {
        return uniqueLocalAddresses.front()->ip;
    }
    return {};
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

std::vector<ByteString> InterfaceConfigs::IPv6State::getGlobalList() 
{
    std::shared_lock lock(ipMutex);
    std::vector<ByteString> ips;
    for (const auto* ip : globalAddresses)
    {
        ips.push_back(ip->ip);
    }
    return ips;
}

std::vector<ByteString> InterfaceConfigs::IPv6State::getLocalList()
{
    std::shared_lock lock(ipMutex);
    std::vector<ByteString> ips;
    for (const auto* ip : uniqueLocalAddresses)
    {
        ips.push_back(ip->ip);
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

bool InterfaceConfigs::hasAddress(const ByteString& address)
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    if (ipv6.getLocalAddress() == address) return true;
    for (auto ip : ipv6.getGlobalList()) { if (ip == address) return true; }
    for (auto ip : ipv6.getLocalList()) { if (ip == address) return true; }
    return false;
}
