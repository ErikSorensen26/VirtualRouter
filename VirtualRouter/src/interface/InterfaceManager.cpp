// InterfaceManager.cpp

#include "InterfaceManager.h"
#include "Interface.h"

namespace interface
{

Interface* InterfaceManager::add(Interface* iface, interface::InterfaceKey key)
{
    std::lock_guard lock(mutex);
    auto [it, inserted] = interfaces.try_emplace(key, iface);
    return inserted ? it->second : nullptr;
}

Interface* InterfaceManager::get(interface::InterfaceKey key) const
{
    std::lock_guard lock(mutex);
    auto it = interfaces.find(key);
    return it != interfaces.end() ? it->second : nullptr;
}

bool InterfaceManager::remove(interface::InterfaceKey key)
{
    {
        std::lock_guard lock(mutex);
        auto it = interfaces.find(key);
        if (it == interfaces.end())
            return false;
        interfaces.erase(it);
    }
    return true;
}

bool InterfaceManager::empty() const noexcept
{
    std::lock_guard lock(mutex);
    return interfaces.empty();
}

std::unordered_map<interface::InterfaceKey, Interface*> InterfaceManager::snapshot() const
{
    std::lock_guard lock(mutex);
    return interfaces;
}

InterfaceManager::StateEventMgr::Id InterfaceManager::subscribe(StateChange event, void* ctx, StateEventMgr::Callback cb)
{
    return stateEventMgr.registerCallback(event, ctx, cb);
}

InterfaceManager::IPv4EventMgr::Id InterfaceManager::subscribe(IPv4Event event, void* ctx, IPv4EventMgr::Callback cb)
{
    return ipv4EventMgr.registerCallback(event, ctx, cb);
}

InterfaceManager::IPv6EventMgr::Id InterfaceManager::subscribe(IPv6Event event, void* ctx, IPv6EventMgr::Callback cb)
{
    return ipv6EventMgr.registerCallback(event, ctx, cb);
}

void InterfaceManager::unsubscribe(StateEventMgr::Id id)
{
    stateEventMgr.unregister(id);
}

void InterfaceManager::unsubscribe(IPv4EventMgr::Id id)
{
    ipv4EventMgr.unregister(id);
}

void InterfaceManager::unsubscribe(IPv6EventMgr::Id id)
{
    ipv6EventMgr.unregister(id);
}

void InterfaceManager::notify(StateChange event, Interface& iface)
{
    stateEventMgr.run(event, iface);
}

void InterfaceManager::notify(IPv4Event event, Interface& iface, types::IPv4Prefix addr)
{
    ipv4EventMgr.run(event, iface, addr);
}

void InterfaceManager::notify(IPv6Event event, Interface& iface, types::IPv6Prefix addr)
{
    ipv6EventMgr.run(event, iface, addr);
}
} // namespace interface
