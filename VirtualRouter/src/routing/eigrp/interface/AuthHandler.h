/**
 * @file AuthHandler.h
 * @brief EIGRP packet authentication (MD5 / SHA-256) for a single interface.
 */

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

/**
 * @brief Handles EIGRP authentication TLV construction and validation for one
 *        interface.
 *
 * @c AuthHandler reads the authentication mode and key-chain configuration
 * from @c EigrpInterfaceRegistry and uses @c KeyChainManager to retrieve the
 * active key material.  It supports MD5 (classic TLV) and SHA-256 (HMAC)
 * authentication as defined in the EIGRP specification.
 *
 * @ingroup EIGRP_INTERFACE
 */
class AuthHandler
{
public:
    /**
     * @brief Constructs an AuthHandler for the given interface configuration.
     * @param iface  Interface-level EIGRP registry providing auth mode and
     *               key-chain name.
     * @param keyMgr Key-chain manager used to look up active keys.
     */
    AuthHandler(config::EigrpInterfaceRegistry& iface, security::authentication::KeyChainManager& keyMgr);

    /**
     * @brief Serialises an authentication TLV into @p out using the
     *        configured mode and current key.
     * @param out Destination buffer; must be large enough to hold the TLV.
     * @return Number of bytes written.
     */
    uint16_t buildAuthTLV(uint8_t* out);

    /**
     * @brief Validates the authentication TLV in a received EIGRP packet.
     * @param packetStart Pointer to the start of the raw EIGRP packet.
     * @param size        Total length of the packet in bytes.
     * @param authOpt     Pointer to the parsed auth TLV option, or @c nullptr
     *                    if no auth TLV was present.
     * @return @c true if authentication passes (or is not required),
     *         @c false if validation fails.
     */
    bool validateAuth(const uint8_t* packetStart, size_t size, const packet::TLV16Option* authOpt);

    /**
     * @brief Appends an HMAC authentication digest to an already-serialised
     *        EIGRP packet.  Used for SHA-256 authentication where the digest
     *        covers the full packet.
     * @param global      Global context providing system-wide services.
     * @param chainName   Name of the key chain to use for key lookup.
     * @param packetStart Pointer to the start of the EIGRP packet buffer.
     * @param size        Length of the packet in bytes.
     * @return @c true on success, @c false if no active key is available.
     */
    static bool appendAuthHMAC(core::Global& global, const std::string& chainName, uint8_t* packetStart, size_t size);

private:
    config::EigrpInterfaceRegistry& configs; ///< Interface EIGRP configuration registry.
    security::authentication::KeyChainManager& keyMgr; ///< Key-chain manager for key retrieval.
};
} // namespace routing

#endif // EIGRP_AUTH_HANDLER_H

