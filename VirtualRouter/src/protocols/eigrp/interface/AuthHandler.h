// AuthHandler.h

#ifndef EIGRP_AUTH_HANDLER_H
#define EIGRP_AUTH_HANDLER_H

#include <string>
#include <cstdint>

class PacketBuilder;
class Global;
struct TLV16Option;
namespace Authentication
{
class KeyChainManager;
}
namespace EigrpConfigs
{
enum class AuthType : uint16_t;
struct InterfaceConfigs;
}

namespace Eigrp
{
class EigrpInterface;

class AuthHandler
{
public:

    AuthHandler(EigrpConfigs::InterfaceConfigs& iface, Authentication::KeyChainManager& keyMgr);

    uint16_t buildAuthTLV(uint8_t* out);

    bool validateAuth(const uint8_t* packetStart, size_t size, const TLV16Option* authOpt);

    static bool appendAuthHMAC(Global& global, uint8_t* packetStart, size_t size);

private:
    EigrpConfigs::InterfaceConfigs& configs;
    Authentication::KeyChainManager& keyMgr;
};
}

#endif // EIGRP_AUTH_HANDLER_H
