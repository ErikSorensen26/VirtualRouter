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
    Interface* iface;
    {
        std::lock_guard lock(mutex);
        auto it = interfaces.find(key);
        if (it == interfaces.end())
            return false;
        iface = it->second;
        interfaces.erase(it);
    }
    notify(StateChange::IF_DOWN, *iface);
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

InterfaceManager::IPEventMgr::Id InterfaceManager::subscribe(IPEvent event, void* ctx, IPEventMgr::Callback cb)
{
    return ipEventMgr.registerCallback(event, ctx, cb);
}

void InterfaceManager::unsubscribe(StateEventMgr::Id id)
{
    stateEventMgr.unregister(id);
}

void InterfaceManager::unsubscribe(IPEventMgr::Id id)
{
    ipEventMgr.unregister(id);
}

void InterfaceManager::notify(StateChange event, Interface& iface)
{
    stateEventMgr.run(event, iface);
}

void InterfaceManager::notify(IPEvent event, Interface& iface, const types::IPPrefix& addr)
{
    ipEventMgr.run(event, iface, addr);
}
} // namespace interface
