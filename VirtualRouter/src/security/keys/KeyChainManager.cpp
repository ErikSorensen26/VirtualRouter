// KeyChainManager.cpp

#include "KeyChain.h"
#include "KeyChainManager.h"

namespace security::authentication
{
KeyChainManager::~KeyChainManager()
{
    for (auto& kc : chains)
        delete kc;
}

bool KeyChainManager::validate(const uint8_t* hmac, uint8_t* computed, uint32_t keyId, const uint8_t* data, size_t size, const HmacType& type) const
{
    for (const auto& key : chains)
    {
        if (key->validate(hmac, computed, keyId, data, size, type))
            return true;
    }
    return false;
}

KeyChain* KeyChainManager::create(const std::string& name)
{
    if (auto it = std::find_if(chains.begin(), chains.end(),
        [&](const KeyChain* chain) { return chain->name == name; }); it != chains.end())
        return *it;

    KeyChain* kc = new KeyChain(name);
    chains.push_back(kc);
    return kc;
}

KeyChain* KeyChainManager::lookup(uint32_t id) const noexcept
{
    if (auto it = std::find_if(chains.begin(), chains.end(),
        [&](const KeyChain* chain) { return chain->chainID == id; }); it != chains.end())
        return *it;
    return nullptr;
}

KeyChain* KeyChainManager::lookup(const std::string& name) const noexcept
{
    if (auto it = std::find_if(chains.begin(), chains.end(),
        [&](const KeyChain* chain) { return chain->name == name; }); it != chains.end())
        return *it;
    return nullptr;
}

void KeyChainManager::remove(uint32_t id)
{
    auto it = std::find_if(chains.begin(), chains.end(),
        [&](const KeyChain* key) { return key->chainID == id; });
    if (it == chains.end()) return;

    delete *it;
    chains.erase(it);
}

void KeyChainManager::remove(const std::string& name)
{
    auto it = std::find_if(chains.begin(), chains.end(),
        [&](const KeyChain* key) { return key->name == name; });
    if (it == chains.end()) return;

    delete *it;
    chains.erase(it);
}
} // namespace security::authentication
