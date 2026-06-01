// EigrpConfigManager

#include <algorithm>
#include <AddressFamily.hpp>
#include <VirtualRouter.h>
#include <EnumBitMap.hpp>

#include "Eigrp.h"
#include "eigrp/interface/EigrpInterface.h"
#include "interface/configs/InterfaceType.hpp"
#include "configs/FieldAccessor.hpp"

namespace routing::eigrp
{
static config::EigrpRegistry& resolveEigrpRegistry(Eigrp& base)
{
    auto& vrf = *base.routingInstance;
    if (base.getAF() == types::AddressFamily::IPv4)
        return vrf.getConfigs().reg.get<config::Vrf::ROUTER_EIGRP_V4>().emplaceBack(static_cast<uint16_t>(base.getAS()));
    return vrf.getConfigs().reg.get<config::Vrf::ROUTER_EIGRP_V6>().emplaceBack(static_cast<uint16_t>(base.getAS()));
}

EigrpConfig::EigrpConfig(Eigrp& base)
    : base(base),
      configs(resolveEigrpRegistry(base))
{
    configs.reg.context().set(&base);
}

void EigrpConfig::addNetworkRange(const types::IPv4Prefix& newNetwork)
{
    if (base.getAF() != types::AddressFamily::IPv4) return;

    types::IPAddress ip(newNetwork.addr);
    uint8_t prefLen = newNetwork.prefixLength;

    bool added = false;
    configs.reg.get<config::Eigrp::NETWORK>().withWrite([&](auto& v) -> bool {
        for (const auto& [a, p] : v)
            if (a == ip && p.value == prefLen) return false;
        v.emplace_back(ip, config::IgnoreCompare<uint8_t>{prefLen});
        added = true;
        return true;
    });

    if (added)
        base.getIfaceMgr().refreshInterfaceList();
}

void EigrpConfig::delNetworkRange(const types::IPv4Prefix& delNetwork)
{
    if (base.getAF() != types::AddressFamily::IPv4) return;

    types::IPAddress ip(delNetwork.addr);
    uint8_t prefLen = delNetwork.prefixLength;

    bool removed = false;
    configs.reg.get<config::Eigrp::NETWORK>().withWrite([&](auto& v) -> bool {
        auto it = std::find_if(v.begin(), v.end(), [&](const auto& t) {
            return std::get<0>(t) == ip && std::get<1>(t).value == prefLen;
        });
        if (it != v.end()) {
            v.erase(it);
            removed = true;
            return true;
        }
        return false;
    });

    if (removed)
        base.getIfaceMgr().refreshInterfaceList();
}

bool EigrpConfig::isInNetworkRange(types::IPv4Address testIp) const
{
    bool found = false;
    configs.reg.get<config::Eigrp::NETWORK>().withRead([&](const auto& v) {
        for (const auto& [ipAddr, prefLenW] : v) {
            if (!ipAddr.isIPv4()) continue;
            uint32_t addr = ipAddr.v4();
            uint8_t prefLen = prefLenW.value;
            uint32_t mask = (prefLen == 0) ? 0u : (~0u << (32 - prefLen));
            if ((testIp.addr & mask) == (addr & mask)) {
                found = true;
                return;
            }
        }
    });
    return found;
}

size_t EigrpConfig::getNetworkSize() const
{
    size_t size{};
    configs.reg.get<config::Eigrp::NETWORK>().withRead([&size](auto& v) { size = v.size(); });
    return size;
}

void EigrpConfig::clearNetworks()
{
    configs.reg.get<config::Eigrp::NETWORK>().withWrite([](auto& v) -> bool {
        v.clear();
        return true;
    });
    base.getIfaceMgr().refreshInterfaceList();
}

void EigrpConfig::enableStub(bool isStub, bool advertiseConnected, bool advertiseStatic, bool advertiseSummary, bool advertiseRedistributed)
{
    if (!isStub) {
        configs.reg.get<config::Eigrp::STUB>().unset();
        return;
    }
    types::EnumBitMap<config::eigrp::Stub> bm;
    bm.reset();
    if (advertiseConnected)     bm.set(config::eigrp::Stub::CONNECTED);
    if (advertiseStatic)        bm.set(config::eigrp::Stub::STATIC);
    if (advertiseSummary)       bm.set(config::eigrp::Stub::SUMMARY);
    if (advertiseRedistributed) bm.set(config::eigrp::Stub::REDISTRIBUTED);
    configs.reg.get<config::Eigrp::STUB>().set(bm.raw());
}

void EigrpConfig::setPassiveInterface(interface::InterfaceKey key, bool add)
{
    configs.reg.get<config::Eigrp::PASSIVE_INTERFACES>().withWrite([&](std::vector<interface::InterfaceKey>& v) -> bool {
        if (add) {
            if (std::find(v.begin(), v.end(), key) == v.end()) {
                v.push_back(key);
                return true;
            }
        } else {
            v.erase(std::remove(v.begin(), v.end(), key), v.end());
            return true;
        }
        return false;
    });

    auto* eigrpIface = base.getIfaceMgr().getInterface(key);
    if (eigrpIface)
        eigrpIface->setPassiveMode(add);
}

void EigrpConfig::enableUnicastPeer(const types::IPAddress& neighborIp, interface::InterfaceKey key)
{
    configs.reg.get<config::Eigrp::NEIGHBOR>().withWrite([&](std::vector<std::tuple<types::IPAddress, interface::InterfaceKey>>& v) -> bool {
        for (const auto& [ip, k] : v)
            if (ip == neighborIp && k == key) return false;
        v.emplace_back(neighborIp, key);
        return true;
    });

    auto* iface = base.getIfaceMgr().getInterface(key);
    if (iface)
        iface->getNTable().createNeighbor(neighborIp, Neighbor::Version::UNKNOWN, true);
}

void EigrpConfig::disableUnicastPeer(const types::IPAddress& neighborIp, interface::InterfaceKey key)
{
    configs.reg.get<config::Eigrp::NEIGHBOR>().withWrite([&](std::vector<std::tuple<types::IPAddress, interface::InterfaceKey>>& v) -> bool {
        v.erase(std::remove_if(v.begin(), v.end(), [&](const auto& t) {
            return std::get<0>(t) == neighborIp && std::get<1>(t) == key;
        }), v.end());
        return true;
    });

    auto* iface = base.getIfaceMgr().getInterface(key);
    if (iface)
        iface->getNTable().deleteNeighbor(neighborIp, true);
}

bool EigrpConfig::isPassive(interface::InterfaceKey key) const
{
    bool found = false;
    configs.reg.get<config::Eigrp::PASSIVE_INTERFACES>().withRead([&](const std::vector<interface::InterfaceKey>& v) {
        found = std::find(v.begin(), v.end(), key) != v.end();
    });
    return found;
}

std::unordered_set<types::IPAddress> EigrpConfig::getUnicastNeighbors(interface::InterfaceKey key) const
{
    std::unordered_set<types::IPAddress> result;
    configs.reg.get<config::Eigrp::NEIGHBOR>().withRead([&](const std::vector<std::tuple<types::IPAddress, interface::InterfaceKey>>& v) {
        for (const auto& [ip, ifaceKey] : v)
            if (ifaceKey == key)
                result.insert(ip);
    });
    return result;
}

StubConfig EigrpConfig::getStubConfig() const
{
    StubConfig s;
    auto stubField = configs.reg.get<config::Eigrp::STUB>();
    s.isStub = stubField.hasValue();
    if (s.isStub) {
        types::EnumBitMap<config::eigrp::Stub> bm(stubField.load());
        s.advertiseConnected     = bm.test(config::eigrp::Stub::CONNECTED);
        s.advertiseStatic        = bm.test(config::eigrp::Stub::STATIC);
        s.advertiseSummary       = bm.test(config::eigrp::Stub::SUMMARY);
        s.advertiseRedistributed = bm.test(config::eigrp::Stub::REDISTRIBUTED);
        s.receiveOnly            = bm.test(config::eigrp::Stub::RECEIVE_ONLY);
    }
    auto leakMap = configs.reg.get<config::Eigrp::STUB_LEAK_MAP>();
    s.advertiseLeakMap     = leakMap.hasValue();
    return s;
}

KValue EigrpConfig::getKValues() const
{
    auto& reg = configs;
    return KValue(
        reg.reg.get<config::Eigrp::WEIGHT_K1>().load(),
        reg.reg.get<config::Eigrp::WEIGHT_K2>().load(),
        reg.reg.get<config::Eigrp::WEIGHT_K3>().load(),
        reg.reg.get<config::Eigrp::WEIGHT_K4>().load(),
        reg.reg.get<config::Eigrp::WEIGHT_K5>().load()
    );
}
} // namespace routing
