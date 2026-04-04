// InterfaceConfigs.cpp

#include <Global.h>
#include <VirtualRouter.h>
#include <ControlScheduler.h>

#include "InterfaceConfigs.h"
#include "hardware/HardwareManager.h"
#include "eigrp/core/Eigrp.h"
#include "interface/Interface.h"

namespace interface
{

//ADD LOCK FREE VECTOR

InterfaceConfigs::InterfaceConfigs(interface::Interface& iface, InterfaceType type, float id, const hardware::HwIfaceInfo& info)
  : id(id),
    interfaceType(type),
    key(type, id),
    hwInfo(info),
    ipv6(iface.getVRF()->getGlobal().timeManager),
    configs([&iface, type, id]() {
        interface::InterfaceKey key(type, id);
        auto* vrf = iface.getVRF();
        auto& interfaceList = vrf->getGlobalConfigs().get<config::Global::INTERFACE>();
        return vrf->getRegistry().emplaceBack(interfaceList, key);
    }()),
    macAddress(hwInfo.mac)
{
    syncMac();
    syncPrimaryIP();
    syncSecondaryIP();
    syncLocalLink();
    syncIPv6();
    syncDhcpv6();
}

InterfaceConfigs::~InterfaceConfigs()
{
    eigrp.eigrpIfaceConfigs.clear();
}

void InterfaceConfigs::syncMac()
{
    auto& macField = configs->get<config::Interface::MAC_ADDRESS>();
    if (macField.hasValue())
        macAddress.store(macField.load(), std::memory_order_release);
    else
        macAddress.store(hwInfo.mac, std::memory_order_release);
}

void InterfaceConfigs::syncPrimaryIP()
{
    // TODO: include dhcp, clientid, hostname
}

void InterfaceConfigs::syncSecondaryIP()
{
    // TODO
}

void InterfaceConfigs::syncLocalLink()
{
    // TODO
}

void InterfaceConfigs::syncIPv6()
{
    // TODO
}

void InterfaceConfigs::syncDhcpv6()
{
    // TODO
}

uint8_t* InterfaceConfigs::getMac(uint8_t* mac)
{
    utils::writeU48(mac, macAddress.load(std::memory_order_relaxed));
    return mac;
}

types::Mac InterfaceConfigs::getMac()
{
    return macAddress.load(std::memory_order_relaxed);
}

uint32_t InterfaceConfigs::getBandwidth()
{
    if (id != std::floor(id)) // Child interface
    {
        auto& bw = configs->get<config::Interface::BANDWIDTH_INHERITANCE>();
        if (bw.hasValue()) return bw.load();
    }
    return configs->get<config::Interface::BANDWIDTH>().load();
}

uint32_t InterfaceConfigs::getReceiveBandwidth()
{
    if (id != std::floor(id)) // Child interface
    {
        auto& bw = configs->get<config::Interface::BANDWIDTH_RECEIVE_INHERITANCE>();
        if (bw.hasValue()) return bw.load();
    }
    return configs->get<config::Interface::BANDWIDTH_RECEIVE>().load();
}

//IPV4
void InterfaceConfigs::IPv4State::setPrimaryAddress(types::IPv4Prefix prefix)
{
    address.store(prefix.addr, std::memory_order_release);
    mask.store(prefix.prefixLength, std::memory_order_release);
}

void InterfaceConfigs::IPv4State::addSecondaryAddress(types::IPv4Prefix prefix)
{
    std::lock_guard<std::mutex> lk(ipMutex);
    if (std::find_if(secondary.begin(), secondary.end(),
            [&](const types::IPv4Prefix& p) {
                return p == prefix;
            }) != secondary.end()) return;
    secondary.push_back(prefix);
}

void InterfaceConfigs::IPv4State::removePrimaryAddress()
{
    address.store(0, std::memory_order_release);
    mask.store(0, std::memory_order_release);
}

void InterfaceConfigs::IPv4State::removeSecondaryAddress(types::IPv4Prefix prefix)
{
    std::lock_guard<std::mutex> lock(ipMutex);
    secondary.erase(std::remove_if(secondary.begin(), secondary.end(), [&](const types::IPv4Prefix& p) { return p == prefix; }), secondary.end());
}

types::IPv4Prefix InterfaceConfigs::IPv4State::getPrimaryPrefix() const
{
    return types::IPv4Prefix{address.load(std::memory_order_relaxed), mask.load(std::memory_order_relaxed)};
}

std::optional<types::IPv4Prefix> InterfaceConfigs::IPv4State::getSecondaryPrefix()
{
    std::lock_guard<std::mutex> lock(ipMutex);
    if (secondary.empty()) return std::nullopt;
    return secondary.front();
}

uint8_t* InterfaceConfigs::IPv4State::getPrimaryAddress(uint8_t* out) const
{
    utils::writeU32(out, address.load(std::memory_order_relaxed));
    return out;
}

uint8_t* InterfaceConfigs::IPv4State::getSecondaryAddress(uint8_t* out) const
{
    std::lock_guard<std::mutex> lock(ipMutex);
    if (secondary.empty()) return nullptr;
    utils::writeU32(out, secondary.front().addr);
    return out;
}

types::IPv4Address InterfaceConfigs::IPv4State::getPrimaryAddress() const
{
    return address.load(std::memory_order_relaxed);
}

std::optional<types::IPv4Address> InterfaceConfigs::IPv4State::getSecondaryAddress() const
{
    std::lock_guard<std::mutex> lock(ipMutex);
    if (secondary.empty()) return std::nullopt;
    return types::IPv4Address{secondary.front().addr};
}

bool InterfaceConfigs::IPv4State::hasPrimaryAddress() const
{
    return address.load(std::memory_order_relaxed) == 0;
}

bool InterfaceConfigs::IPv4State::hasPrimaryAddress(types::IPv4Prefix prefix) const
{
    return address.load(std::memory_order_relaxed) == prefix.addr && mask.load(std::memory_order_relaxed) == prefix.prefixLength;
}

bool InterfaceConfigs::IPv4State::hasPrimaryAddress(const uint8_t* addr, uint8_t len) const
{
    return address.load(std::memory_order_relaxed) == utils::readU32(addr) && mask.load(std::memory_order_relaxed) == len;
}

bool InterfaceConfigs::IPv4State::hasSecondaryAddress(types::IPv4Prefix prefix) const
{
    std::lock_guard<std::mutex> lock(ipMutex);
    return std::find_if(secondary.begin(), secondary.end(),
        [&](const types::IPv4Prefix& p) { return p == prefix; }) != secondary.end();
}

bool InterfaceConfigs::IPv4State::hasSecondaryAddress(const uint8_t* addr, uint8_t len) const
{
    uint32_t ip = utils::readU32(addr);
    std::lock_guard<std::mutex> lock(ipMutex);
    return std::find_if(secondary.begin(), secondary.end(),
        [&](const types::IPv4Prefix& p) { return p.addr == ip && p.prefixLength == len; }) != secondary.end();
}

uint8_t InterfaceConfigs::IPv4State::getPrimaryPrefix(uint8_t* out) const
{
    utils::writeU32(out, address.load(std::memory_order_relaxed));
    return mask.load(std::memory_order_relaxed);
}

std::optional<uint8_t> InterfaceConfigs::IPv4State::getSecondaryPrefix(uint8_t* out) const
{
    std::lock_guard<std::mutex> lock(ipMutex);
    if (secondary.empty()) return std::nullopt;
    utils::writeU32(out, secondary.front().addr);
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

std::vector<types::IPv4Address> InterfaceConfigs::IPv4State::getSecondaryList() const
{
    std::vector<types::IPv4Address> ips;
    std::lock_guard<std::mutex> lock(ipMutex);
    for (const auto& ip : secondary)
        ips.push_back(types::IPv4Address{ip.addr});
    return ips;
}

std::vector<types::IPv4Prefix> InterfaceConfigs::IPv4State::getSecondaryPrefixList(bool maintainAddress) const
{
    std::lock_guard<std::mutex> lock(ipMutex);
    if (maintainAddress) return secondary;

    std::vector<types::IPv4Prefix> ips = secondary;
    for (auto& ip : ips)
        ip.addPrefixLen(ip.prefixLength);
    return ips;
}

std::unordered_set<types::IPv4Address> InterfaceConfigs::IPv4State::getSecondarySet() const
{
    std::unordered_set<types::IPv4Address> set;
    std::lock_guard<std::mutex> lock(ipMutex);
    for (const auto& ip : secondary)
        set.insert(types::IPv4Address{ip.addr});
    return set;
}

std::unordered_set<types::IPv4Prefix> InterfaceConfigs::IPv4State::getSecondaryPrefixSet(bool maintainAddress) const
{
    std::unordered_set<types::IPv4Prefix> set;
    std::lock_guard<std::mutex> lock(ipMutex);
    for (const auto& ip : secondary)
        set.insert({ip.addr, ip.prefixLength, maintainAddress});
    return set;
}

bool InterfaceConfigs::IPv4State::comparePrimaryAddress(const uint8_t* ip)
{
    return address.load(std::memory_order_relaxed) == utils::readU32(ip);
}

bool InterfaceConfigs::IPv4State::comparePrimaryAddress(types::IPv4Address ip)
{
    return address.load(std::memory_order_relaxed) == ip.addr;
}

bool InterfaceConfigs::IPv4State::comparePrimaryPrefix(types::IPv4Prefix prefix)
{
    return address.load(std::memory_order_relaxed) == prefix.addr &&
           mask.load(std::memory_order_relaxed) == prefix.prefixLength;
}

// IPV6
InterfaceConfigs::IPv6State::IPv6State(core::TimeManager& tmgr) : timeManager(tmgr) {}

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

void InterfaceConfigs::IPv6State::cancelTimers(IPv6State::IPv6Address& addr)
{
    if (addr.preferredLifetime) timeManager.cancelTimer(addr.preferedExpirationId);
    if (addr.expirationId)      timeManager.cancelTimer(addr.expirationId);
}

bool InterfaceConfigs::IPv6State::hasRoutableAddress()
{
    std::lock_guard lock(ipMutex);
    return !(uniqueLocalAddresses.empty() && globalAddresses.empty());
}

InterfaceConfigs::IPv6State::IPv6Address* InterfaceConfigs::IPv6State::addAddress(const types::IPv6Prefix& ip, bool local)
{
    if (local)
    {
        // Only one local-address can exist
        if (linkLocalAddress)
            removeLocalAddress();

        std::lock_guard lock(ipMutex);

        linkLocalAddress = new IPv6State::IPv6Address();
        linkLocalAddress->prefix = ip;
        linkLocalAddress->tentative = true;
        linkLocalAddress->valid = false;
        return linkLocalAddress;
    }

    IPv6State::IPv6Address* address = new IPv6State::IPv6Address();
    address->prefix = ip;
    address->tentative = true;
    address->valid = false;
    {
        std::lock_guard lock(ipMutex);
        globalAddresses.push_back(address);
    }
    return address;
}

InterfaceConfigs::IPv6State::IPv6Address* InterfaceConfigs::IPv6State::addUniqueLocalAddress(const types::IPv6Prefix& ip)
{
    IPv6State::IPv6Address* address = new IPv6State::IPv6Address();
    address->prefix = ip;
    address->tentative = true;
    address->valid = false;
    {
        std::lock_guard lock(ipMutex);
        uniqueLocalAddresses.push_back(address);
    }
    return uniqueLocalAddresses.back();
}

InterfaceConfigs::IPv6State::IPv6Address* InterfaceConfigs::IPv6State::addGlobalAddress(const types::IPv6Prefix& ip)
{
    IPv6State::IPv6Address* address = new IPv6State::IPv6Address();
    address->prefix = ip;
    address->tentative = true;
    address->valid = false;
    {
        std::lock_guard lock(ipMutex);
        globalAddresses.push_back(address);
    }
    return globalAddresses.back();
}

void InterfaceConfigs::IPv6State::removeLocalAddress()
{
    std::lock_guard lock(ipMutex);
    if (linkLocalAddress)
    {
        delete linkLocalAddress;
        linkLocalAddress = nullptr;
    }
}

void InterfaceConfigs::IPv6State::removeAddress(const types::IPv6Prefix& prefix)
{
    std::lock_guard lock(ipMutex);
    auto& list = (((prefix.addr >> 112) & 0xFFFF) == 0xFC00) ? uniqueLocalAddresses : globalAddresses;
    std::erase_if(list, [&](const IPv6State::IPv6Address* addr) {
        bool match = prefix.addr == addr->prefix.addr && prefix.prefixLength == addr->prefix.prefixLength;
        if (match) delete addr;
        return match;
    });
}

void InterfaceConfigs::IPv6State::removeAllAddresses()
{
    std::lock_guard lock(ipMutex);
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
    std::lock_guard lock(ipMutex);
    for (auto& address : globalAddresses)
        address->validateAddress(false);
}

void InterfaceConfigs::IPv6State::validateLinkLocalAddress()
{
    std::lock_guard lock(ipMutex);
    linkLocalAddress->validateAddress(true);
}

uint8_t* InterfaceConfigs::IPv6State::getLocalAddress(uint8_t* out) const
{
    std::lock_guard lock(ipMutex);
    if (linkLocalAddress)
    {
        utils::writeU128(out, linkLocalAddress->prefix.addr);
        return out;
    }
    return nullptr;
}

uint8_t* InterfaceConfigs::IPv6State::getGlobalUnicast(uint8_t* out) const
{
    std::lock_guard lock(ipMutex);
    if (!globalAddresses.empty())
    {
        utils::writeU128(out, globalAddresses.front()->prefix.addr);
        return out;
    }
    return nullptr;
}

uint8_t* InterfaceConfigs::IPv6State::getLocalUnicast(uint8_t* out) const
{
    std::lock_guard lock(ipMutex);
    if (!uniqueLocalAddresses.empty())
    {
        utils::writeU128(out, uniqueLocalAddresses.front()->prefix.addr);
        return out;
    }
    return nullptr;
}

types::IPv6Address InterfaceConfigs::IPv6State::getLocalAddress() const
{
    std::lock_guard lock(ipMutex);
    if (linkLocalAddress)
        return linkLocalAddress->prefix.addr;
    return {};
}

types::IPv6Address InterfaceConfigs::IPv6State::getGlobalUnicast() const
{
    std::lock_guard lock(ipMutex);
    return globalAddresses.empty() ? types::IPv6Address{} : types::IPv6Address{globalAddresses.front()->prefix.addr};
}

types::IPv6Address InterfaceConfigs::IPv6State::getLocalUnicast() const
{
    std::lock_guard lock(ipMutex);
    return uniqueLocalAddresses.empty() ? types::IPv6Address{} : types::IPv6Address{uniqueLocalAddresses.front()->prefix.addr};
}

types::IPv6Prefix InterfaceConfigs::IPv6State::getLocalPrefix() const
{
    std::lock_guard lock(ipMutex);
    if (!linkLocalAddress) return {};
    return linkLocalAddress->prefix;
}

types::IPv6Prefix InterfaceConfigs::IPv6State::getGlobalUnicastPrefix() const
{
    std::lock_guard lock(ipMutex);
    if (globalAddresses.empty()) return {};
    return globalAddresses.front()->prefix;
}

types::IPv6Prefix InterfaceConfigs::IPv6State::getLocalUnicastPrefix() const
{
    std::lock_guard lock(ipMutex);
    if (uniqueLocalAddresses.empty()) return {};
    return uniqueLocalAddresses.front()->prefix;
}

bool InterfaceConfigs::IPv6State::hasAddress(const uint8_t* addr)
{
    return hasAddress(utils::readU128(addr));
}

bool InterfaceConfigs::IPv6State::hasAddress(types::IPv6Address addr)
{
    std::lock_guard lock(ipMutex);
    if (addr.isLocalLink())
        return linkLocalAddress && linkLocalAddress->prefix.addr == addr.addr;
    else if (addr.isLocalUnicast())
    {
        for (const auto& ip : uniqueLocalAddresses)
            if (ip->prefix.addr == addr.addr) return true;
    }
    else if (addr.isGlobalUnicast())
    {
        for (const auto& ip : globalAddresses)
            if (ip->prefix.addr == addr.addr) return true;
    }
    return false;
}

bool InterfaceConfigs::IPv6State::hasLocalAddress(const uint8_t* addr, uint8_t len) const
{
    std::lock_guard lock(ipMutex);
    return linkLocalAddress && linkLocalAddress->prefix.addr == utils::readU128(addr) && linkLocalAddress->prefix.prefixLength == len;
}

bool InterfaceConfigs::IPv6State::hasLocalUnicast(const uint8_t* addr, uint8_t len) const
{
    return hasLocalUnicast(types::IPv6Prefix(utils::readU128(addr), len, true));
}

bool InterfaceConfigs::IPv6State::hasGlobalUnicast(const uint8_t* addr, uint8_t len) const
{
    return hasGlobalUnicast(types::IPv6Prefix(utils::readU128(addr), len, true));
}

bool InterfaceConfigs::IPv6State::hasLocalAddress(const types::IPv6Prefix& prefix) const
{
    std::lock_guard lock(ipMutex);
    return linkLocalAddress && linkLocalAddress->prefix == prefix;
}

bool InterfaceConfigs::IPv6State::hasLocalUnicast(const types::IPv6Prefix& prefix) const
{
    std::lock_guard lock(ipMutex);
    for (auto* ip : uniqueLocalAddresses)
        if (ip->prefix == prefix)
            return true;
    return false;
}

bool InterfaceConfigs::IPv6State::hasGlobalUnicast(const types::IPv6Prefix& prefix) const
{
    std::lock_guard lock(ipMutex);
    for (auto* ip : globalAddresses)
        if (ip->prefix == prefix)
            return true;
    return false;
}

uint8_t InterfaceConfigs::IPv6State::getLocalPrefix(uint8_t* out) const
{
    std::lock_guard lock(ipMutex);
    if (!linkLocalAddress) return 0;
    utils::writeU128(out, linkLocalAddress->prefix.addr);
    return linkLocalAddress->prefix.prefixLength;
}

uint8_t InterfaceConfigs::IPv6State::getGlobalUnicastPrefix(uint8_t* out) const
{
    std::lock_guard lock(ipMutex);
    if (globalAddresses.empty()) return 0;
    utils::writeU128(out, globalAddresses.front()->prefix.addr);
    return globalAddresses.front()->prefix.prefixLength;
}

uint8_t InterfaceConfigs::IPv6State::getLocalUnicastPrefix(uint8_t* out) const
{
    std::lock_guard lock(ipMutex);
    if (uniqueLocalAddresses.empty()) return 0;
    utils::writeU128(out, uniqueLocalAddresses.front()->prefix.addr);
    return uniqueLocalAddresses.front()->prefix.prefixLength;
}

uint8_t InterfaceConfigs::IPv6State::getLocalMask() const
{
    std::lock_guard lock(ipMutex);
    return linkLocalAddress ? linkLocalAddress->prefix.prefixLength : 0;
}

uint8_t InterfaceConfigs::IPv6State::getGlobalUnicastMask() const
{
    std::lock_guard lock(ipMutex);
    return globalAddresses.empty() ? 0 : globalAddresses.front()->prefix.prefixLength;
}

uint8_t InterfaceConfigs::IPv6State::getLocalUnicastMask() const
{
    std::lock_guard lock(ipMutex);
    return uniqueLocalAddresses.empty() ? 0 : uniqueLocalAddresses.front()->prefix.prefixLength;
}

std::vector<types::IPv6Address> InterfaceConfigs::IPv6State::getRoutableList() const
{
    std::lock_guard lock(ipMutex);
    std::vector<types::IPv6Address> out;
    out.reserve(globalAddresses.size() + uniqueLocalAddresses.size());
    for (const auto* ip : globalAddresses)
        out.emplace_back(ip->prefix.addr);
    for (const auto* ip : uniqueLocalAddresses)
        out.emplace_back(ip->prefix.addr);
    return out;
}

std::vector<types::IPv6Address> InterfaceConfigs::IPv6State::getGlobalList() const
{
    std::lock_guard lock(ipMutex);
    std::vector<types::IPv6Address> out;
    out.reserve(globalAddresses.size());
    for (const auto* ip : globalAddresses)
        out.emplace_back(ip->prefix.addr);
    return out;
}

std::vector<types::IPv6Address> InterfaceConfigs::IPv6State::getLocalList() const
{
    std::lock_guard lock(ipMutex);
    std::vector<types::IPv6Address> out;
    out.reserve(uniqueLocalAddresses.size());
    for (const auto* ip : uniqueLocalAddresses)
        out.emplace_back(ip->prefix.addr);
    return out;
}

std::vector<types::IPv6Prefix> InterfaceConfigs::IPv6State::getRoutablePrefixList(bool maintainAddress) const
{
    std::lock_guard lock(ipMutex);
    std::vector<types::IPv6Prefix> out;
    out.reserve(globalAddresses.size() + uniqueLocalAddresses.size());
    for (const auto* ip : globalAddresses)
        out.emplace_back(ip->prefix.addr, ip->prefix.prefixLength, maintainAddress);
    for (const auto* ip : uniqueLocalAddresses)
        out.emplace_back(ip->prefix.addr, ip->prefix.prefixLength, maintainAddress);
    return out;
}

std::vector<types::IPv6Prefix> InterfaceConfigs::IPv6State::getGlobalPrefixList(bool maintainAddress) const
{
    std::lock_guard lock(ipMutex);
    std::vector<types::IPv6Prefix> out;
    out.reserve(globalAddresses.size());
    for (const auto* ip : globalAddresses)
        out.emplace_back(ip->prefix.addr, ip->prefix.prefixLength, maintainAddress);
    return out;
}

std::vector<types::IPv6Prefix> InterfaceConfigs::IPv6State::getLocalPrefixList(bool maintainAddress) const
{
    std::lock_guard lock(ipMutex);
    std::vector<types::IPv6Prefix> out;
    out.reserve(uniqueLocalAddresses.size());
    for (const auto* ip : uniqueLocalAddresses)
        out.emplace_back(ip->prefix.addr, ip->prefix.prefixLength, maintainAddress);
    return out;
}

std::unordered_set<types::IPv6Address> InterfaceConfigs::IPv6State::getRoutableSet() const
{
    std::lock_guard lock(ipMutex);
    std::unordered_set<types::IPv6Address> out;
    for (const auto* ip : globalAddresses)
        out.emplace(ip->prefix.addr);
    for (const auto* ip : uniqueLocalAddresses)
        out.emplace(ip->prefix.addr);
    return out;
}

std::unordered_set<types::IPv6Address> InterfaceConfigs::IPv6State::getGlobalSet() const
{
    std::lock_guard lock(ipMutex);
    std::unordered_set<types::IPv6Address> out;
    for (const auto* ip : globalAddresses)
        out.emplace(ip->prefix.addr);
    return out;
}

std::unordered_set<types::IPv6Address> InterfaceConfigs::IPv6State::getUniqueSet() const
{
    std::lock_guard lock(ipMutex);
    std::unordered_set<types::IPv6Address> out;
    for (const auto* ip : uniqueLocalAddresses)
        out.emplace(ip->prefix.addr);
    return out;
}

std::unordered_set<types::IPv6Prefix> InterfaceConfigs::IPv6State::getRoutablePrefixSet(bool maintainAddress) const
{
    std::lock_guard lock(ipMutex);
    std::unordered_set<types::IPv6Prefix> out;
    for (const auto* ip : globalAddresses)
        out.emplace(ip->prefix.addr, ip->prefix.prefixLength, maintainAddress);
    for (const auto* ip : uniqueLocalAddresses)
        out.emplace(ip->prefix.addr, ip->prefix.prefixLength, maintainAddress);
    return out;
}

std::unordered_set<types::IPv6Prefix> InterfaceConfigs::IPv6State::getGlobalPrefixSet(bool maintainAddress) const
{
    std::lock_guard lock(ipMutex);
    std::unordered_set<types::IPv6Prefix> out;
    for (const auto* ip : globalAddresses)
        out.emplace(ip->prefix.addr, ip->prefix.prefixLength, maintainAddress);
    return out;
}

std::unordered_set<types::IPv6Prefix> InterfaceConfigs::IPv6State::getUniquePrefixSet(bool maintainAddress) const
{
    std::lock_guard lock(ipMutex);
    std::unordered_set<types::IPv6Prefix> out;
    for (const auto* ip : uniqueLocalAddresses)
        out.emplace(ip->prefix.addr, ip->prefix.prefixLength, maintainAddress);
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
    types::IPv6Prefix prefix(address, len, true);
    if (ipv6.hasLocalAddress(prefix)) return true;
    else if (ipv6.hasGlobalUnicast(prefix)) return true;
    else if (ipv6.hasLocalUnicast(prefix)) return true;
    return false;
}

} // namespace interface
