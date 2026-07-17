/**
 * @file GracefulRestartManager.h
 * @brief OSPFv2 Grace-LSA origination for graceful restart (RFC 3623).
 */

#ifndef OSPF_GRACEFUL_RESTART_MANAGER_H
#define OSPF_GRACEFUL_RESTART_MANAGER_H

#include <cstdint>

class Internal_OspfTest;

namespace routing::ospf
{
class OspfInterface;
enum class GraceRestartReason : uint8_t;

/**
 * @brief Interface-scoped originator for the OSPFv2 Grace-LSA (RFC 3623 SS3).
 * @ingroup OSPF_INTERFACE
 *
 * Grace-LSAs are link-scope opaque LSAs (opaque type 3), so — unlike the
 * area-scope Router Capability opaque LSA (@ref OpaqueOriginatorV2) — this
 * class is owned per-interface rather than per-area, matching graceful
 * restart's per-neighbor/per-interface signaling. V2-only; OSPFv3 opaque
 * origination is out of scope (see the OSPF todo plan).
 *
 * ## Lifecycle & Ownership
 * Owned by @ref OspfInterface, constructed alongside it. Only meaningful for
 * OSPFv2 interfaces.
 *
 * ## Concurrency Model
 * All methods run on the owning process's single-threaded `ProcessQueue`.
 *
 * @see OpaqueOriginatorV2, GraceLsaTlv
 */
class GracefulRestartManager
{
    friend class ::Internal_OspfTest;
public:
    explicit GracefulRestartManager(OspfInterface& iface);

    /**
     * @brief Originates (or re-originates) this interface's Grace-LSA.
     * @param gracePeriodSeconds Grace period advertised to neighbors.
     * @param reason Restart reason code (RFC 3623 SS3).
     */
    void originateGraceLsa(uint32_t gracePeriodSeconds, GraceRestartReason reason);

    /**
     * @brief Withdraws (MaxAges) this interface's Grace-LSA, if one is currently originated.
     */
    void flushGraceLsa();

private:
    OspfInterface& iface;
    bool originated = false; ///< True if a Grace-LSA is currently installed for this interface.
};
} // namespace routing::ospf

#endif // OSPF_GRACEFUL_RESTART_MANAGER_H
