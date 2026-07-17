/**
 * @file OpaqueOriginatorV2.h
 * @brief OSPFv2 opaque LSA origination: Router Capability (RFC 5250 / RFC 4970).
 */

#ifndef OSPF_OPAQUE_ORIGINATOR_V2_H
#define OSPF_OPAQUE_ORIGINATOR_V2_H

#include <cstdint>

class Internal_OspfTest;

namespace routing::ospf
{
class OriginatorContext;

/**
 * @brief Area-scoped originator for OSPFv2 opaque LSAs (RFC 2370 / RFC 5250).
 * @ingroup OSPF_V2_AREA
 *
 * Currently originates a single opaque consumer: the area-scope (opaque type
 * 10) Router Capability opaque LSA, carrying one @ref RouterCapabilityTlv.
 * V2-only — OSPFv3 opaque origination is out of scope; see the OSPF todo plan.
 *
 * ## Lifecycle & Ownership
 * Owned by @ref Area as the `opaqueOriginator` member, constructed after
 * `originContext`. Only constructed for V2 areas.
 *
 * ## Concurrency Model
 * All methods run on the owning process's single-threaded `ProcessQueue`,
 * consistent with the rest of the origination pipeline.
 *
 * @see OriginatorContext, OpaqueLsaV2, RouterCapabilityTlv
 */
class OpaqueOriginatorV2
{
    friend class ::Internal_OspfTest;
public:
    /**
     * @brief Constructs the opaque originator bound to its owning area's origination context.
     * @param ctx Origination context of the owning area.
     */
    explicit OpaqueOriginatorV2(OriginatorContext& ctx);

    /**
     * @brief Originates (or re-originates) the Router Capability opaque LSA with the given bitmask.
     *
     * No-op if no interface in the owning area currently has opaque capability
     * negotiated (O-bit); withdraws any previously originated instance in that
     * case instead.
     *
     * @param capabilities Bitmask of ROUTER_CAP_* flags to advertise.
     */
    void originateRouterCapability(uint32_t capabilities);

    /**
     * @brief Withdraws (MaxAges) the Router Capability opaque LSA, if one is currently originated.
     */
    void withdrawRouterCapability();

private:
    /// True if at least one interface in the owning area currently has opaque capability negotiated.
    bool anyInterfaceOpaqueCapable() const;

    OriginatorContext& context;
    bool originated = false; ///< True if a Router Capability LSA is currently installed (vs withdrawn/never-originated).
    uint32_t lastCapabilities = 0xFFFFFFFF; ///< Last-originated capabilities bitmask; used to skip redundant re-origination.
};
} // namespace routing::ospf

#endif // OSPF_OPAQUE_ORIGINATOR_V2_H
