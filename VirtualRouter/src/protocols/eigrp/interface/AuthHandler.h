// AuthHandler.h

#ifndef EIGRP_AUTH_HANDLER_H
#define EIGRP_AUTH_HANDLER_H

#include <string>
#include <cstdint>

class PacketBuilder;
struct TLV16Option;
namespace EigrpConfigs
{
enum class AuthType : uint8_t;
struct InterfaceConfigs;
}

namespace Eigrp
{
class EigrpInterface;

class AuthHandler
{
public:

    AuthHandler(EigrpConfigs::InterfaceConfigs& iface);

    void setKeyChain(uint8_t* keyId = nullptr, const std::string* key = nullptr, EigrpConfigs::AuthType* type = nullptr, bool enable = false);

    virtual uint8_t buildAuthTLV(uint8_t* out);

    bool validateAuth(const uint8_t* packetStart, TLV16Option& authOpt);

    static void appendAuthHMAC(uint8_t* packetStart);

private:
    EigrpConfigs::InterfaceConfigs& configs;
};
}

#endif // EIGRP_AUTH_HANDLER_H
