#include "InterfaceConfigs.h"
#include <Eigrp.h>

//ADD LOCK FREE VECTOR

InterfaceConfigs::InterfaceConfigs(TimeManager& timeManager, InterfaceType type, float id, const uint8_t* mac)
  : id(id),
    interfaceType(type),
    key(calculateInterfaceKey(type, id)),
    ipv6(timeManager)
{
    macAddress.store(readU48(mac), std::memory_order_relaxed);
}

InterfaceConfigs::~InterfaceConfigs()
{
    for (auto& [_, cfg] : eigrp.eigrpInterfaceConfigList)
        delete cfg;
    eigrp.eigrpInterfaceConfigList.clear();
}

uint8_t* InterfaceConfigs::getMac(uint8_t* mac)
{
    writeU48(mac, macAddress.load(std::memory_order_relaxed));
    return mac;
}

uint64_t InterfaceConfigs::getMac()
{
    return macAddress.load(std::memory_order_relaxed);
}

void InterfaceConfigs::setMac(const uint8_t* mac)
{
    macAddress.store(readU48(mac), std::memory_order_relaxed);
}

//IPV4
uint8_t* InterfaceConfigs::IPv4State::getAddress(uint8_t* out)
{
    writeU32(out, address.load(std::memory_order_relaxed));
    return out;
}

uint32_t InterfaceConfigs::IPv4State::getAddress()
{
    return address.load(std::memory_order_relaxed);
}

void InterfaceConfigs::IPv4State::setAddress(uint32_t newAddress, uint8_t newMask)
{
    address.store(newAddress, std::memory_order_release);
    mask.store(newMask, std::memory_order_relaxed);
}

bool InterfaceConfigs::IPv4State::compareAddress(const uint8_t* ip)
{
    return address.load(std::memory_order_relaxed) == readU32(ip);
}

bool InterfaceConfigs::IPv4State::compareAddress(uint32_t ip)
{
    return address.load(std::memory_order_release) == ip;
}

uint8_t InterfaceConfigs::IPv4State::getMask()
{
    return mask.load(std::memory_order_relaxed);
}

// IPV6
InterfaceConfigs::IPv6State::IPv6State(TimeManager& time) : timeManager(time) {}

InterfaceConfigs::IPv6State::~IPv6State()
{
    for (auto& addr : globalAddresses)
    {
        cancelTimers(*addr);
        delete addr;
    }
    for (auto& addr : uniqueLocalAddresses)
    {
        cancelTimers(*addr);
        delete addr;
    }
    if (linkLocalAddress)
    {
        cancelTimers(*linkLocalAddress);
        delete linkLocalAddress;
    }
}

void InterfaceConfigs::IPv6State::cancelTimers(IPv6Address& addr)
{
    if (addr.preferredLifetime) timeManager.cancelTimer(addr.preferedExpirationId);
    if (addr.expirationId)      timeManager.cancelTimer(addr.expirationId);
}

InterfaceConfigs::IPv6State::IPv6Address* InterfaceConfigs::IPv6State::addAddress(const uint8_t* ip, bool local, uint8_t prefix)
{
    if (local)
    {
        // Only one local-address can exist
        if (linkLocalAddress)
            removeLocalAddress();

        linkLocalAddress = new IPv6Address();
        std::memcpy(linkLocalAddress->ip, ip, 16);
        linkLocalAddress->ipInt = readU128(ip);
        linkLocalAddress->prefix = prefix;
        linkLocalAddress->tentative = true;
        linkLocalAddress->valid = false;
        return linkLocalAddress;
    }

    IPv6Address* address = new IPv6Address();
    std::memcpy(address->ip, ip, 16);
    address->prefix = prefix;
    address->tentative = true;
    address->valid = false;
    globalAddresses.push_back(address);
    return address;
}

InterfaceConfigs::IPv6State::IPv6Address* InterfaceConfigs::IPv6State::addUniqueLocalAddress(const uint8_t* ip, uint8_t prefixLen)
{
    IPv6Address* address = new IPv6Address();
    std::memcpy(address->ip, ip, 16);
    address->ipInt = readU128(ip);
    address->prefix = prefixLen;
    address->tentative = true;
    address->valid = false;
    uniqueLocalAddresses.push_back(address);
    return uniqueLocalAddresses.back();
}

InterfaceConfigs::IPv6State::IPv6Address* InterfaceConfigs::IPv6State::addGlobalAddress(const uint8_t* ip, uint8_t prefixLen)
{
    IPv6Address* address = new IPv6Address();
    std::memcpy(address->ip, ip, 16);
    address->ipInt = readU128(ip);
    address->prefix = prefixLen;
    address->tentative = true;
    address->valid = false;
    globalAddresses.push_back(address);
    return globalAddresses.back();
}

void InterfaceConfigs::IPv6State::removeLocalAddress()
{
    delete linkLocalAddress;
    linkLocalAddress = nullptr;
}

void InterfaceConfigs::IPv6State::removeAddress(const uint8_t* ip)
{
    auto& list = (ip[0] == 0xfc && ip[1] == 0x00) ? uniqueLocalAddresses : globalAddresses;
    std::erase_if(list, [&](const IPv6Address* addr) {
        bool match = std::memcmp(addr->ip, ip, 16) == 0;
        if (match) delete addr;
        return match;
    });
}

void InterfaceConfigs::IPv6State::removeAddress(__uint128_t ip)
{
    auto& list = (ip >> 120 == 0xfc) ? uniqueLocalAddresses : globalAddresses;
    std::erase_if(list, [&](const IPv6Address* addr) {
        bool match = (addr->ipInt == ip);
        if (match) delete addr;
        return match;
    });
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
        address->validateAddress(false);
}

void InterfaceConfigs::IPv6State::validateLinkLocalAddress()
{
    linkLocalAddress->validateAddress(true);
}

uint8_t* InterfaceConfigs::IPv6State::getLocalAddress(uint8_t* out)
{
    if (linkLocalAddress)
    {
        writeU128(out, linkLocalAddress->ipInt);
        return out;
    }
    return nullptr;
}

uint8_t* InterfaceConfigs::IPv6State::getGlobalUnicast(uint8_t* out)
{
    if (!globalAddresses.empty())
    {
        writeU128(out, linkLocalAddress->ipInt);
        return out;
    }
    return nullptr;
}

uint8_t* InterfaceConfigs::IPv6State::getLocalUnicast(uint8_t* out)
{
    if (!uniqueLocalAddresses.empty())
    {
        writeU128(out, linkLocalAddress->ipInt);
        return out;
    }
    return nullptr;
}

__uint128_t InterfaceConfigs::IPv6State::getLocalAddress()
{
    if (linkLocalAddress)
    {
        return readU128(linkLocalAddress->ip);
    }
    return 0;
}

__uint128_t InterfaceConfigs::IPv6State::getGlobalUnicast()
{
    return globalAddresses.empty() ? 0 : readU128(globalAddresses.front()->ip);
}

__uint128_t InterfaceConfigs::IPv6State::getLocalUnicast()
{
    return uniqueLocalAddresses.empty() ? 0 : readU128(uniqueLocalAddresses.front()->ip);
}

bool InterfaceConfigs::IPv6State::hasLocalAddress(const uint8_t* addr)
{
    return linkLocalAddress && std::memcmp(linkLocalAddress->ip, addr, 16) == 0;
}

bool InterfaceConfigs::IPv6State::hasLocalUnicast(const uint8_t* addr)
{
    for (auto* ip : uniqueLocalAddresses)
        if (std::memcmp(ip->ip, addr, 16) == 0)
            return true;
    return false;
}

bool InterfaceConfigs::IPv6State::hasGlobalUnicast(const uint8_t* addr)
{
    for (auto* ip : globalAddresses)
        if (std::memcmp(ip->ip, addr, 16) == 0)
            return true;
    return false;
}

bool InterfaceConfigs::IPv6State::hasLocalAddress(__uint128_t addr)
{
    return linkLocalAddress && linkLocalAddress->ipInt == addr;
}

bool InterfaceConfigs::IPv6State::hasLocalUnicast(__uint128_t addr)
{
    for (auto* ip : uniqueLocalAddresses)
        if (ip->ipInt == addr)
            return true;
    return false;
}

bool InterfaceConfigs::IPv6State::hasGlobalUnicast(__uint128_t addr)
{
    for (auto* ip : globalAddresses)
        if (ip->ipInt == addr)
            return true;
    return false;
}

uint8_t InterfaceConfigs::IPv6State::getGlobalUnicastPair(uint8_t* out)
{
    if (globalAddresses.empty()) return 0;
    writeU128(out, globalAddresses.front()->ipInt);
    return globalAddresses.front()->prefix;
}

uint8_t InterfaceConfigs::IPv6State::getLocalUnicastPair(uint8_t* out)
{
    if (uniqueLocalAddresses.empty()) return 0;
    writeU128(out, uniqueLocalAddresses.front()->ipInt);
    return uniqueLocalAddresses.front()->prefix;
}

uint8_t InterfaceConfigs::IPv6State::getGlobalUnicastMask()
{
    return globalAddresses.empty() ? 0 : globalAddresses.front()->prefix;
}

uint8_t InterfaceConfigs::IPv6State::getLocalUnicastMask()
{
    return uniqueLocalAddresses.empty() ? 0 : uniqueLocalAddresses.front()->prefix;
}

std::vector<IPAddress> InterfaceConfigs::IPv6State::getGlobalList()
{
    std::vector<IPAddress> out;
    for (const auto* ip : globalAddresses)
    {
        out.emplace_back();
        std::copy(ip->ip, ip->ip + 16, out.back().raw);
        out.back().isV6 = true;
    }
    return out;
}

std::vector<IPAddress> InterfaceConfigs::IPv6State::getLocalList()
{
    std::vector<IPAddress> out;
    for (const auto* ip : uniqueLocalAddresses)
    {
        out.emplace_back();
        std::copy(ip->ip, ip->ip + 16, out.back().raw);
        out.back().isV6 = true;
    }
    return out;
}

bool InterfaceConfigs::hasAddress(const uint8_t* address)
{
    if (ipv6.hasLocalAddress(address)) return true;
    else if (ipv6.hasGlobalUnicast(address)) return true;
    else if (ipv6.hasLocalUnicast(address)) return true;
    return false;
}

bool InterfaceConfigs::hasAddress(__uint128_t address)
{
    if (ipv6.hasLocalAddress(address)) return true;
    else if (ipv6.hasGlobalUnicast(address)) return true;
    else if (ipv6.hasLocalUnicast(address)) return true;
    return false;
}
