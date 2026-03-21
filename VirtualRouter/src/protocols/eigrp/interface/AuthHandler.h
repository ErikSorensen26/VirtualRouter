// AuthHandler.h

#ifndef EIGRP_AUTH_HANDLER_H
#define EIGRP_AUTH_HANDLER_H

#include <cstdint>
#include "configs/registry/router/EigrpInterfaceRegistry.h"

class PacketBuilder;
class Global;
struct TLV16Option;
namespace Authentication
{
class KeyChainManager;
}

namespace EIGRP
{
enum class AuthType : uint16_t;
class EigrpInterface;

class AuthHandler
{
public:

    AuthHandler(Config::EigrpInterfaceRegistry& iface, Authentication::KeyChainManager& keyMgr);

    uint16_t buildAuthTLV(uint8_t* out);

    bool validateAuth(const uint8_t* packetStart, size_t size, const TLV16Option* authOpt);

    static bool appendAuthHMAC(Global& global, uint8_t* packetStart, size_t size);

private:
    Config::EigrpInterfaceRegistry& configs;
    Authentication::KeyChainManager& keyMgr;
};
}

#endif // EIGRP_AUTH_HANDLER_H
