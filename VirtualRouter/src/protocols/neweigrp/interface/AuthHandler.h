// EigrpInterfaceAuth

#ifndef EIGRP_INTERFACE_AUTH_H
#define EIGRP_INTERFACE_AUTH_H

#include <string>
#include <cstdint>

namespace EigrpConfigs
{
enum class AuthType : uint8_t;
}

namespace Protocol
{
class AuthHandler
{
public:
    
    void setKeyChain(uint8_t* keyId = nullptr, const std::string* key = nullptr, EigrpConfigs::AuthType* type = nullptr, bool enable = false);

    virtual uint8_t buildAuthTLV(uint8_t* out);

    static void appendAuthHMAC(uint8_t* packetStart);
};
}

#endif // EIGRP_INTERFACE_AUTH_H
