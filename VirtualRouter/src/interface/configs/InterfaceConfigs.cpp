// InterfaceConfigs.cpp

#include <IPAddress.h>
#include <TimeManager.h>

#include "InterfaceConfigs.h"
#include "hardware/HardwareManager.h"
#include "eigrp/core/Eigrp.h"

//ADD LOCK FREE VECTOR

InterfaceConfigs::InterfaceConfigs(TimeManager& timeManager, InterfaceType type, float id, const HwIfaceInfo& info)
  : id(id),
    interfaceType(type),
    key(calculateInterfaceKey(type, id)),
    hwInfo(info),
    ipv6(timeManager)
{
    macAddress.store(hwInfo.mac, std::memory_order_relaxed);
}

InterfaceConfigs::~InterfaceConfigs()
{
    eigrp.eigrpIfaceConfigs.clear();
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
void InterfaceConfigs::IPv4State::setPrimaryAddress(IPv4Prefix prefix)
{
    address.store(prefix.addr, std::memory_order_release);
    mask.store(prefix.prefixLength, std::memory_order_release);
}

void InterfaceConfigs::IPv4State::addSecondaryAddress(IPv4Prefix prefix)
{
    std::lock_guard<std::mutex> lk(ipMutex);
    if (std::find_if(secondary.begin(), secondary.end(),
            [&](const IPv4Prefix& p) {
                return p == prefix;
            }) != secondary.end()) return;
    secondary.push_back(prefix);
}

void InterfaceConfigs::IPv4State::removePrimaryAddress()
{
    address.store(0, std::memory_order_release);
    mask.store(0, std::memory_order_release);
}

void InterfaceConfigs::IPv4State::removeSecondaryAddress(IPv4Prefix prefix)
{
    std::lock_guard<std::mutex> lock(ipMutex);
    secondary.erase(std::remove_if(secondary.begin(), secondary.end(), [&](const IPv4Prefix& p) { return p == prefix; }), secondary.end());
}

IPv4Prefix InterfaceConfigs::IPv4State::getPrimaryPrefix() const
{
    return IPv4Prefix{address.load(std::memory_order_relaxed), mask.load(std::memory_order_relaxed)};
}

std::optional<IPv4Prefix> InterfaceConfigs::IPv4State::getSecondaryPrefix()
{
    std::lock_guard<std::mutex> lock(ipMutex);
    if (secondary.empty()) return std::nullopt;
    return secondary.front();
}

uint8_t* InterfaceConfigs::IPv4State::getPrimaryAddress(uint8_t* out) const
{
    writeU32(out, address.load(std::memory_order_relaxed));
    return out;
}

uint8_t* InterfaceConfigs::IPv4State::getSecondaryAddress(uint8_t* out) const
{
    std::lock_guard<std::mutex> lock(ipMutex);
    if (secondary.empty()) return nullptr;
    writeU32(out, secondary.front().addr);
    return out;
}

IPv4Address InterfaceConfigs::IPv4State::getPrimaryAddress() const
{
    return address.load(std::memory_order_relaxed);
}

std::optional<IPv4Address> InterfaceConfigs::IPv4State::getSecondaryAddress() const
{
    std::lock_guard<std::mutex> lock(ipMutex);
    if (secondary.empty()) return std::nullopt;
    return IPv4Address{secondary.front().addr};
}

bool InterfaceConfigs::IPv4State::hasPrimaryAddress() const
{
    return address.load(std::memory_order_relaxed) == 0;
}

bool InterfaceConfigs::IPv4State::hasPrimaryAddress(IPv4Prefix prefix) const
{
    return address.load(std::memory_order_relaxed) == prefix.addr && mask.load(std::memory_order_relaxed) == prefix.prefixLength;
}

bool InterfaceConfigs::IPv4State::hasPrimaryAddress(const uint8_t* addr, uint8_t len) const
{
    return address.load(std::memory_order_relaxed) == readU32(addr) && mask.load(std::memory_order_relaxed) == len;
}

bool InterfaceConfigs::IPv4State::hasSecondaryAddress(IPv4Prefix prefix) const
{
    std::lock_guard<std::mutex> lock(ipMutex);
    return std::find_if(secondary.begin(), secondary.end(),
        [&](const IPv4Prefix& p) { return p == prefix; }) != secondary.end();
}

bool InterfaceConfigs::IPv4State::hasSecondaryAddress(const uint8_t* addr, uint8_t len) const
{
    uint32_t ip = readU32(addr);
    std::lock_guard<std::mutex> lock(ipMutex);
    return std::find_if(secondary.begin(), secondary.end(),
        [&](const IPv4Prefix& p) { return p.addr == ip && p.prefixLength == len; }) != secondary.end();
}

uint8_t InterfaceConfigs::IPv4State::getPrimaryPrefix(uint8_t* out) const
{
    writeU32(out, address.load(std::memory_order_relaxed));
    return mask.load(std::memory_order_relaxed);
}

std::optional<uint8_t> InterfaceConfigs::IPv4State::getSecondaryPrefix(uint8_t* out) const
{
    std::lock_guard<std::mutex> lock(ipMutex);
    if (secondary.empty()) return std::nullopt;
    writeU32(out, secondary.front().addr);
    return secondary.front().prefixLength;
}

uint8_t InterfaceConfigs::IPv4State::getPrimaryMask() const
{
    return mask.load(std::memory_order_relaxed);
}

std::optional<uint8_t> InterfaceConfigs::IPv4State::getSecondaryMask() const
{
    std::lock_guard<std::mutex> lock(ipMutex);
    if (secondary.empty()) return std::nullopt;
    return secondary.front().prefixLength;
}

std::vector<IPv4Address> InterfaceConfigs::IPv4State::getSecondaryList() const
{
    std::vector<IPv4Address> ips;
    std::lock_guard<std::mutex> lock(ipMutex);
    for (const auto& ip : secondary)
        ips.push_back(IPv4Address{ip.addr});
    return ips;
}

std::vector<IPv4Prefix> InterfaceConfigs::IPv4State::getSecondaryPrefixList(bool maintainAddress) const
{
    std::lock_guard<std::mutex> lock(ipMutex);
    if (maintainAddress) return secondary;

    std::vector<IPv4Prefix> ips = secondary;
    for (auto& ip : ips)
        ip.addPrefixLen(ip.prefixLength);
    return ips;
}

std::unordered_set<IPv4Address> InterfaceConfigs::IPv4State::getSecondarySet() const
{
    std::unordered_set<IPv4Address> set;
    std::lock_guard<std::mutex> lock(ipMutex);
    for (const auto& ip : secondary)
        set.insert(IPv4Address{ip.addr});
    return set;
}

std::unordered_set<IPv4Prefix> InterfaceConfigs::IPv4State::getSecondaryPrefixSet(bool maintainAddress) const
{
    std::unordered_set<IPv4Prefix> set;
    std::lock_guard<std::mutex> lock(ipMutex);
    for (const auto& ip : secondary)
        set.insert({ip.addr, ip.prefixLength, maintainAddress});
    return set;
}

bool InterfaceConfigs::IPv4State::comparePrimaryAddress(const uint8_t* ip)
{
    return address.load(std::memory_order_relaxed) == readU32(ip);
}

bool InterfaceConfigs::IPv4State::comparePrimaryAddress(IPv4Address ip)
{
    return address.load(std::memory_order_relaxed) == ip.addr;
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

bool InterfaceConfigs::IPv6State::hasRoutableAddress()
{
    std::unique_lock<std::shared_mutex> lock(ipMutex);
    return !(uniqueLocalAddresses.empty() && globalAddresses.empty());
}

InterfaceConfigs::IPv6State::IPv6Address* InterfaceConfigs::IPv6State::addAddress(const IPv6Prefix& ip, bool local)
{
    if (local)
    {
        // Only one local-address can exist
        if (linkLocalAddress)
            removeLocalAddress();

        std::unique_lock<std::shared_mutex> lock(ipMutex);

        linkLocalAddress = new IPv6Address();
        linkLocalAddress->addr = ::IPv6Address(ip.addr);
        linkLocalAddress->length = ip.prefixLength;
        linkLocalAddress->tentative = true;
        linkLocalAddress->valid = false;
        return linkLocalAddress;
    }

    IPv6Address* address = new IPv6Address();
    address->addr = ::IPv6Address(ip.addr);
    address->length = ip.prefixLength;
    address->tentative = true;
    address->valid = false;
    {
        std::unique_lock<std::shared_mutex> lock(ipMutex);
        globalAddresses.push_back(address);
    }
    return address;
}

InterfaceConfigs::IPv6State::IPv6Address* InterfaceConfigs::IPv6State::addUniqueLocalAddress(const IPv6Prefix& ip)
{
    IPv6Address* address = new IPv6Address();
    address->addr = ::IPv6Address(ip.addr);
    address->length = ip.prefixLength;
    address->tentative = true;
    address->valid = false;
    {
        std::unique_lock<std::shared_mutex> lock(ipMutex);
        uniqueLocalAddresses.push_back(address);
    }
    return uniqueLocalAddresses.back();
}

InterfaceConfigs::IPv6State::IPv6Address* InterfaceConfigs::IPv6State::addGlobalAddress(const IPv6Prefix& ip)
{
    IPv6Address* address = new IPv6Address();
    address->addr = ::IPv6Address(ip.addr);
    address->length = ip.prefixLength;
    address->tentative = true;
    address->valid = false;
    {
        std::unique_lock<std::shared_mutex> lock(ipMutex);
        globalAddresses.push_back(address);
    }
    return globalAddresses.back();
}

void InterfaceConfigs::IPv6State::removeLocalAddress()
{
    std::unique_lock<std::shared_mutex> lock(ipMutex);
    if (linkLocalAddress)
    {
        delete linkLocalAddress;
        linkLocalAddress = nullptr;
    }
}

void InterfaceConfigs::IPv6State::removeAddress(const IPv6Prefix& prefix)
{
    std::unique_lock<std::shared_mutex> lock(ipMutex);
    auto& list = (((prefix.addr >> 112) & 0xFFFF) == 0xFC00) ? uniqueLocalAddresses : globalAddresses;
    std::erase_if(list, [&](const IPv6Address* addr) {
        bool match = prefix.addr == addr->addr.addr && prefix.prefixLength == addr->length;
        if (match) delete addr;
        return match;
    });
}

void InterfaceConfigs::IPv6State::removeAllAddresses()
{
    std::unique_lock<std::shared_mutex> lock(ipMutex);
    for (auto& addr : globalAddresses)
        delete addr;
    for (auto& addr : uniqueLocalAddresses)
        delete addr;
    if (linkLocalAddress)
    {
        delete linkLocalAddress;
        linkLocalAddress = nullptr;
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
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    for (auto& address : globalAddresses)
        address->validateAddress(false);
}

void InterfaceConfigs::IPv6State::validateLinkLocalAddress()
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    linkLocalAddress->validateAddress(true);
}

uint8_t* InterfaceConfigs::IPv6State::getLocalAddress(uint8_t* out) const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    if (linkLocalAddress)
    {
        writeU128(out, linkLocalAddress->addr.addr);
        return out;
    }
    return nullptr;
}

uint8_t* InterfaceConfigs::IPv6State::getGlobalUnicast(uint8_t* out) const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    if (!globalAddresses.empty())
    {
        writeU128(out, globalAddresses.front()->addr.addr);
        return out;
    }
    return nullptr;
}

uint8_t* InterfaceConfigs::IPv6State::getLocalUnicast(uint8_t* out) const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    if (!uniqueLocalAddresses.empty())
    {
        writeU128(out, uniqueLocalAddresses.front()->addr.addr);
        return out;
    }
    return nullptr;
}

IPv6Address InterfaceConfigs::IPv6State::getLocalAddress() const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    if (linkLocalAddress)
        return linkLocalAddress->addr;
    return {};
}

IPv6Address InterfaceConfigs::IPv6State::getGlobalUnicast() const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    return globalAddresses.empty() ? ::IPv6Address{} : globalAddresses.front()->addr;
}

IPv6Address InterfaceConfigs::IPv6State::getLocalUnicast() const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    return uniqueLocalAddresses.empty() ? ::IPv6Address{} : uniqueLocalAddresses.front()->addr;
}

IPv6Prefix InterfaceConfigs::IPv6State::getLocalPrefix() const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    if (!linkLocalAddress) return {};
    return IPv6Prefix(linkLocalAddress->addr.addr, linkLocalAddress->length, true);
}

IPv6Prefix InterfaceConfigs::IPv6State::getGlobalUnicastPrefix() const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    if (globalAddresses.empty()) return {};
    return IPv6Prefix(globalAddresses.front()->addr.addr, globalAddresses.front()->length, true);
}

IPv6Prefix InterfaceConfigs::IPv6State::getLocalUnicastPrefix() const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    if (uniqueLocalAddresses.empty()) return {};
    return IPv6Prefix(uniqueLocalAddresses.front()->addr.addr, uniqueLocalAddresses.front()->length, true);
}

bool InterfaceConfigs::IPv6State::hasAddress(const uint8_t* addr)
{
    return hasAddress(readU128(addr));
}

bool InterfaceConfigs::IPv6State::hasAddress(::IPv6Address addr)
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    if (((addr.addr >> 118) & 0x3FF) == 0xb1111111010)
        return linkLocalAddress && linkLocalAddress->addr.addr == addr.addr;
    else if (((addr.addr >> 121) & 0x7F) == 0b1111110)
    {
        for (const auto& ip : uniqueLocalAddresses)
            if (ip->addr.addr == addr.addr) return true;
    }
    else if (((addr.addr >> 125) & 0x7) == 0b001)
    {
        for (const auto& ip : globalAddresses)
            if (ip->addr.addr == addr.addr) return true;
    }
    return false;
}

bool InterfaceConfigs::IPv6State::hasLocalAddress(const uint8_t* addr, uint8_t len) const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    return linkLocalAddress && linkLocalAddress->addr.addr == readU128(addr) && linkLocalAddress->length == len;
}

bool InterfaceConfigs::IPv6State::hasLocalUnicast(const uint8_t* addr, uint8_t len) const
{
    return hasLocalUnicast(IPv6Prefix(readU128(addr), len, true));
}

bool InterfaceConfigs::IPv6State::hasGlobalUnicast(const uint8_t* addr, uint8_t len) const
{
    return hasGlobalUnicast(IPv6Prefix(readU128(addr), len, true));
}

bool InterfaceConfigs::IPv6State::hasLocalAddress(const IPv6Prefix& prefix) const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    return linkLocalAddress && linkLocalAddress->addr.addr == prefix.addr && linkLocalAddress->length == prefix.prefixLength;
}

bool InterfaceConfigs::IPv6State::hasLocalUnicast(const IPv6Prefix& prefix) const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    for (auto* ip : uniqueLocalAddresses)
        if (ip->addr.addr == prefix.addr && ip->length == prefix.prefixLength)
            return true;
    return false;
}

bool InterfaceConfigs::IPv6State::hasGlobalUnicast(const IPv6Prefix& prefix) const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    for (auto* ip : globalAddresses)
        if (ip->addr.addr == prefix.addr && ip->length == prefix.prefixLength)
            return true;
    return false;
}

uint8_t InterfaceConfigs::IPv6State::getLocalPrefix(uint8_t* out) const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    if (!linkLocalAddress) return 0;
    writeU128(out, linkLocalAddress->addr.addr);
    return linkLocalAddress->length;
}

uint8_t InterfaceConfigs::IPv6State::getGlobalUnicastPrefix(uint8_t* out) const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    if (globalAddresses.empty()) return 0;
    writeU128(out, globalAddresses.front()->addr.addr);
    return globalAddresses.front()->length;
}

uint8_t InterfaceConfigs::IPv6State::getLocalUnicastPrefix(uint8_t* out) const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    if (uniqueLocalAddresses.empty()) return 0;
    writeU128(out, uniqueLocalAddresses.front()->addr.addr);
    return uniqueLocalAddresses.front()->length;
}

uint8_t InterfaceConfigs::IPv6State::getLocalMask() const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    return linkLocalAddress ? linkLocalAddress->length : 0;
}

uint8_t InterfaceConfigs::IPv6State::getGlobalUnicastMask() const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    return globalAddresses.empty() ? 0 : globalAddresses.front()->length;
}

uint8_t InterfaceConfigs::IPv6State::getLocalUnicastMask() const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    return uniqueLocalAddresses.empty() ? 0 : uniqueLocalAddresses.front()->length;
}

std::vector<IPv6Address> InterfaceConfigs::IPv6State::getRoutableList() const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    std::vector<::IPv6Address> out;
    out.reserve(globalAddresses.size() + uniqueLocalAddresses.size());
    for (const auto* ip : globalAddresses)
        out.emplace_back(ip->addr);
    for (const auto* ip : uniqueLocalAddresses)
        out.emplace_back(ip->addr);
    return out;
}

std::vector<IPv6Address> InterfaceConfigs::IPv6State::getGlobalList() const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    std::vector<::IPv6Address> out;
    out.reserve(globalAddresses.size());
    for (const auto* ip : globalAddresses)
        out.emplace_back(ip->addr);
    return out;
}

std::vector<IPv6Address> InterfaceConfigs::IPv6State::getLocalList() const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    std::vector<::IPv6Address> out;
    out.reserve(uniqueLocalAddresses.size());
    for (const auto* ip : uniqueLocalAddresses)
        out.emplace_back(ip->addr);
    return out;
}

std::vector<IPv6Prefix> InterfaceConfigs::IPv6State::getRoutablePrefixList(bool maintainAddress) const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    std::vector<IPv6Prefix> out;
    out.reserve(globalAddresses.size() + uniqueLocalAddresses.size());
    for (const auto* ip : globalAddresses)
        out.emplace_back(ip->addr.addr, ip->length, maintainAddress);
    for (const auto* ip : uniqueLocalAddresses)
        out.emplace_back(ip->addr.addr, ip->length, maintainAddress);
    return out;
}

std::vector<IPv6Prefix> InterfaceConfigs::IPv6State::getGlobalPrefixList(bool maintainAddress) const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    std::vector<IPv6Prefix> out;
    out.reserve(globalAddresses.size());
    for (const auto* ip : globalAddresses)
        out.emplace_back(ip->addr.addr, ip->length, maintainAddress);
    return out;
}

std::vector<IPv6Prefix> InterfaceConfigs::IPv6State::getLocalPrefixList(bool maintainAddress) const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    std::vector<IPv6Prefix> out;
    out.reserve(uniqueLocalAddresses.size());
    for (const auto* ip : uniqueLocalAddresses)
        out.emplace_back(ip->addr.addr, ip->length, maintainAddress);
    return out;
}

std::unordered_set<IPv6Address> InterfaceConfigs::IPv6State::getRoutableSet() const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    std::unordered_set<::IPv6Address> out;
    for (const auto* ip : globalAddresses)
        out.emplace(ip->addr);
    for (const auto* ip : uniqueLocalAddresses)
        out.emplace(ip->addr);
    return out;
}

std::unordered_set<IPv6Address> InterfaceConfigs::IPv6State::getGlobalSet() const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    std::unordered_set<::IPv6Address> out;
    for (const auto* ip : globalAddresses)
        out.emplace(ip->addr);
    return out;
}

std::unordered_set<IPv6Address> InterfaceConfigs::IPv6State::getUniqueSet() const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    std::unordered_set<::IPv6Address> out;
    for (const auto* ip : uniqueLocalAddresses)
        out.emplace(ip->addr);
    return out;
}

std::unordered_set<IPv6Prefix> InterfaceConfigs::IPv6State::getRoutablePrefixSet(bool maintainAddress) const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    std::unordered_set<IPv6Prefix> out;
    for (const auto* ip : globalAddresses)
        out.emplace(ip->addr.addr, ip->length, maintainAddress);
    for (const auto* ip : uniqueLocalAddresses)
        out.emplace(ip->addr.addr, ip->length, maintainAddress);
    return out;
}

std::unordered_set<IPv6Prefix> InterfaceConfigs::IPv6State::getGlobalPrefixSet(bool maintainAddress) const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    std::unordered_set<IPv6Prefix> out;
    for (const auto* ip : globalAddresses)
        out.emplace(ip->addr.addr, ip->length, maintainAddress);
    return out;
}

std::unordered_set<IPv6Prefix> InterfaceConfigs::IPv6State::getUniquePrefixSet(bool maintainAddress) const
{
    std::shared_lock<std::shared_mutex> lock(ipMutex);
    std::unordered_set<IPv6Prefix> out;
    for (const auto* ip : uniqueLocalAddresses)
        out.emplace(ip->addr.addr, ip->length, maintainAddress);
    return out;
}

bool InterfaceConfigs::hasAddress(const uint8_t* address, uint8_t len)
{
    if (ipv4.hasPrimaryAddress(address, len)) return true;
    else if (ipv4.hasSecondaryAddress(address, len)) return true;
    else if (ipv6.hasLocalAddress(address, len)) return true;
    else if (ipv6.hasGlobalUnicast(address, len)) return true;
    else if (ipv6.hasLocalUnicast(address, len)) return true;
    return false;
}

bool InterfaceConfigs::hasAddress(__uint128_t address, uint8_t len)
{
    IPv6Prefix prefix(address, len, true);
    if (ipv6.hasLocalAddress(prefix)) return true;
    else if (ipv6.hasGlobalUnicast(prefix)) return true;
    else if (ipv6.hasLocalUnicast(prefix)) return true;
    return false;
}
