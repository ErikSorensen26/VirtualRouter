// AuthHandler.h

#ifndef EIGRP_AUTH_HANDLER_H
#define EIGRP_AUTH_HANDLER_H

#include <cstdint>
#include <string>
#include "configs/registry/router/EigrpInterfaceRegistry.h"

namespace core { class Global; }
namespace processing { class PacketBuilder; }
namespace security { namespace authentication { class KeyChainManager; } }
namespace packet { struct TLV16Option; }

namespace routing::eigrp
{
enum class AuthType : uint16_t;
class EigrpInterface;

class AuthHandler
{
public:

    AuthHandler(config::EigrpInterfaceRegistry& iface, security::authentication::KeyChainManager& keyMgr);

    uint16_t buildAuthTLV(uint8_t* out);

    bool validateAuth(const uint8_t* packetStart, size_t size, const packet::TLV16Option* authOpt);

    static bool appendAuthHMAC(core::Global& global, const std::string& chainName, uint8_t* packetStart, size_t size);

private:
    config::EigrpInterfaceRegistry& configs;
    security::authentication::KeyChainManager& keyMgr;
};
} // namespace routing

#endif // EIGRP_AUTH_HANDLER_H

