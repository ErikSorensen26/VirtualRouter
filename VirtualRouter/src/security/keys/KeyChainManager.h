// KeyChainManager.h

#ifndef KEY_CHAIN_MANAGER_H
#define KEY_CHAIN_MANAGER_H

#include <string>
#include <vector>
#include <cstdint>

namespace Authentication
{
class KeyChain;
enum class HmacType : int;

class KeyChainManager
{
public:
    KeyChainManager() = default;
    ~KeyChainManager();

    bool validate(const uint8_t* hmac, uint8_t* computed, uint32_t keyId, const uint8_t* data, size_t size, const Authentication::HmacType& type) const;
    KeyChain* create(const std::string& name);

    KeyChain* lookup(uint32_t id) const noexcept;
    KeyChain* lookup(const std::string& name) const noexcept;

    void remove(uint32_t id);
    void remove(const std::string& name);

private:

    std::vector<KeyChain*> chains;
};
}

#endif // KEY_CHAIN_MANAGER_H
