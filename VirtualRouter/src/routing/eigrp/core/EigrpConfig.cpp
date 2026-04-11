// EigrpConfigManager

#include <algorithm>
#include <AddressFamily.hpp>
#include <VirtualRouter.h>

#include "Eigrp.h"
#include "eigrp/interface/EigrpInterface.h"
#include "interface/configs/InterfaceType.hpp"

namespace routing::eigrp
{
EigrpConfig::EigrpConfig(Eigrp& base)
    : base(base),
    configs(base.routingInstance->getRegistry().create<config::EigrpRegistry>())
{
    configs->context().set(&base);
}

void EigrpConfig::addNetworkRange(const types::IPv4Prefix& newNetwork)
{
    if (base.getAF() != types::AddressFamily::IPv4) return;

    uint32_t addr = newNetwork.addr;
    uint32_t wildcard = (newNetwork.prefixLength == 0) ? 0xFFFFFFFF
                      : (newNetwork.prefixLength >= 32) ? 0u
                      : (~0u >> newNetwork.prefixLength);

    bool added = false;
    configs.get<config::Eigrp::NETWORK>().withWrite([&](std::vector<std::tuple<uint32_t, uint32_t>>& v) {
        for (const auto& [a, w] : v)
            if (a == addr && w == wildcard) return;
        v.emplace_back(addr, wildcard);
        added = true;
    });

    if (added)
        base.getIfaceMgr().refreshInterfaceList();
}

void EigrpConfig::delNetworkRange(const types::IPv4Prefix& delNetwork)
{
    if (base.getAF() != types::AddressFamily::IPv4) return;

    uint32_t addr = delNetwork.addr;
    uint32_t wildcard = (delNetwork.prefixLength == 0) ? 0xFFFFFFFF
                      : (delNetwork.prefixLength >= 32) ? 0u
                      : (~0u >> delNetwork.prefixLength);

    bool removed = false;
    configs.get<config::Eigrp::NETWORK>().withWrite([&](std::vector<std::tuple<uint32_t, uint32_t>>& v) {
        auto it = std::find_if(v.begin(), v.end(), [&](const auto& t) {
            return std::get<0>(t) == addr && std::get<1>(t) == wildcard;
        });
        if (it != v.end()) {
            v.erase(it);
            removed = true;
        }
    });

    if (removed)
        base.getIfaceMgr().refreshInterfaceList();
}

bool EigrpConfig::isInNetworkRange(types::IPv4Address testIp) const
{
    bool found = false;
    configs.get<config::Eigrp::NETWORK>().withRead([&](const std::vector<std::tuple<uint32_t, uint32_t>>& v) {
        for (const auto& [addr, wildcard] : v) {
            if ((testIp.addr & ~wildcard) == (addr & ~wildcard)) {
                found = true;
                return;
            }
        }
    });
    return found;
}

void EigrpConfig::clearNetworks()
{
    configs.get<config::Eigrp::NETWORK>().withWrite([](std::vector<std::tuple<uint32_t, uint32_t>>& v) {
        v.clear();
    });
    base.getIfaceMgr().refreshInterfaceList();
}

void EigrpConfig::enableStub(bool isStub, bool advertiseConnected, bool advertiseStatic, bool advertiseSummary, bool advertiseRedistributed)
{
    auto& reg = configs.get();
    reg.get<config::Eigrp::STUB>().set(isStub);
    reg.get<config::Eigrp::STUB_CONNECTED>().set(advertiseConnected);
    reg.get<config::Eigrp::STUB_STATIC>().set(advertiseStatic);
    reg.get<config::Eigrp::STUB_SUMMARY>().set(advertiseSummary);
    reg.get<config::Eigrp::STUB_REDISTRIBUTED>().set(advertiseRedistributed);
}

void EigrpConfig::setPassiveInterface(interface::InterfaceKey key, bool add)
{
    configs.get<config::Eigrp::PASSIVE_INTERFACES>().withWrite([&](std::vector<interface::InterfaceKey>& v) {
        if (add) {
            if (std::find(v.begin(), v.end(), key) == v.end())
                v.push_back(key);
        } else {
            v.erase(std::remove(v.begin(), v.end(), key), v.end());
        }
    });

    auto* eigrpIface = base.getIfaceMgr().getInterface(key);
    if (eigrpIface)
        eigrpIface->setPassiveMode(add);
}

void EigrpConfig::enableUnicastPeer(const types::IPAddress& neighborIp, interface::InterfaceKey key)
{
    configs.get<config::Eigrp::NEIGHBOR>().withWrite([&](std::vector<std::tuple<types::IPAddress, interface::InterfaceKey>>& v) {
        for (const auto& [ip, k] : v)
            if (ip == neighborIp && k == key) return;
        v.emplace_back(neighborIp, key);
    });

    auto* iface = base.getIfaceMgr().getInterface(key);
    if (iface)
        iface->getNTable().createNeighbor(neighborIp, Neighbor::Version::UNKNOWN, true);
}

void EigrpConfig::disableUnicastPeer(const types::IPAddress& neighborIp, interface::InterfaceKey key)
{
    configs.get<config::Eigrp::NEIGHBOR>().withWrite([&](std::vector<std::tuple<types::IPAddress, interface::InterfaceKey>>& v) {
        v.erase(std::remove_if(v.begin(), v.end(), [&](const auto& t) {
            return std::get<0>(t) == neighborIp && std::get<1>(t) == key;
        }), v.end());
    });

    auto* iface = base.getIfaceMgr().getInterface(key);
    if (iface)
        iface->getNTable().deleteNeighbor(neighborIp, true);
}

bool EigrpConfig::isPassive(interface::InterfaceKey key) const
{
    bool found = false;
    configs.get<config::Eigrp::PASSIVE_INTERFACES>().withRead([&](const std::vector<interface::InterfaceKey>& v) {
        found = std::find(v.begin(), v.end(), key) != v.end();
    });
    return found;
}

std::unordered_set<types::IPAddress> EigrpConfig::getUnicastNeighbors(interface::InterfaceKey key) const
{
    std::unordered_set<types::IPAddress> result;
    configs.get<config::Eigrp::NEIGHBOR>().withRead([&](const std::vector<std::tuple<types::IPAddress, interface::InterfaceKey>>& v) {
        for (const auto& [ip, ifaceKey] : v)
            if (ifaceKey == key)
                result.insert(ip);
    });
    return result;
}

StubConfig EigrpConfig::getStubConfig() const
{
    StubConfig s;
    auto& reg = configs.get();
    s.isStub               = reg.get<config::Eigrp::STUB>().load();
    s.advertiseConnected   = reg.get<config::Eigrp::STUB_CONNECTED>().load();
    s.advertiseStatic      = reg.get<config::Eigrp::STUB_STATIC>().load();
    s.advertiseSummary     = reg.get<config::Eigrp::STUB_SUMMARY>().load();
    s.advertiseRedistributed = reg.get<config::Eigrp::STUB_REDISTRIBUTED>().load();
    s.receiveOnly          = reg.get<config::Eigrp::STUB_RECEIVE_ONLY>().load();
    auto& leakMap = reg.get<config::Eigrp::STUB_LEAK_MAP>();
    s.advertiseLeakMap     = leakMap.hasValue();
    return s;
}

KValue EigrpConfig::getKValues() const
{
    auto& reg = configs.get();
    return KValue(
        reg.get<config::Eigrp::WEIGHT_K1>().load(),
        reg.get<config::Eigrp::WEIGHT_K2>().load(),
        reg.get<config::Eigrp::WEIGHT_K3>().load(),
        reg.get<config::Eigrp::WEIGHT_k4>().load(),
        reg.get<config::Eigrp::WEIGHT_k5>().load()
    );
}
} // namespace routing
