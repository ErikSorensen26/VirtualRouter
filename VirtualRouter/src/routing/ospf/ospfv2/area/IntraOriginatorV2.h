/**
 * @file OriginatorV2.h
 * @brief OSPFv2-specific LSA origination: Router LSA, Network LSA, and Summary LSAs.
 */

/**
 * @defgroup OSPF_V2 OSPFv2
 * @ingroup OSPF
 * @brief OSPFv2-specific implementations: area originator, LSA database, and packet dispatcher.
 */

/**
 * @defgroup OSPF_V2_AREA OSPFv2 Area
 * @ingroup OSPF_V2
 * @brief OSPFv2 LSA origination for one area.
 */

#ifndef OSPF_INTRA_ORIGINATOR_V2_H
#define OSPF_INTRA_ORIGINATOR_V2_H

#include "ospf/area/IntraOriginator.h"

namespace routing::ospf
{
/**
 * @brief OSPFv2 concrete originator that builds and floods LSAs using IPv4 wire encoding.
 * @ingroup OSPF_V2_AREA
 *
 * Inherits the version-independent origination contract from @ref Originator and
 * implements each virtual hook with OSPFv2-specific logic:
 * - Router LSA (Type 1) with IPv4 link descriptors (RFC 2328 §12.4.1).
 * - Network LSA (Type 2) originated by the Designated Router.
 * - Summary LSAs (Type 3/4) for inter-area prefix and ASBR advertisements.
 * - AS-External LSA (Type 5) / NSSA-External LSA (Type 7) for redistributed routes.
 *
 * One OriginatorV2 instance exists per OSPFv2 @ref Area.
 */
class IntraOriginatorV2 : public IntraOriginator
{
public:
    /**
     * @brief Constructs an OriginatorV2 bound to the given area.
     * @param ctx Originator context for the owning area; must outlive this originator.
     */
    IntraOriginatorV2(OriginatorContext& ctx);

    /**
     * @brief Destroys the originator and withdraws any self-originated LSAs still in the LSDB.
     */
    ~IntraOriginatorV2() override;

    // PUBLIC ORIGINATION INTERFACE

    /**
     * @brief Re-originates all self-originated LSAs unconditionally.
     *
     * Invoked after a configuration change or Router ID change that requires
     * every LSA to be regenerated from scratch.
     */
    void fullRefresh() override;

    /**
     * @brief Re-evaluates and re-originates LSAs affected by a single interface state change.
     * @param ifaceId Internal interface identifier whose state has changed.
     */
    void updateInterface(uint32_t ifaceId) override;

private:
    // ROUTER / NETWORK LSA ORIGINATION

    /**
     * @brief Builds and originates the Router LSA (Type 1) for this router.
     * @param ifaceId    If set, restricts link evaluation to that single interface.
     * @param refresh    True when re-originating an existing LSA with a new sequence number.
     * @param fullRefresh True to rebuild all links unconditionally regardless of cache.
     */
    void addRouterLsa(std::optional<uint32_t> ifaceId, bool refresh, bool fullRefresh = false) override;

    /**
     * @brief Builds and originates the Network LSA (Type 2) for a transit network.
     * @param iface   The DR's OSPF interface on the transit link.
     * @param refresh True when refreshing an existing Network LSA.
     */
    void addNetworkLsa(const OspfInterfaceBase& iface, bool refresh) override;

    /**
     * @brief MaxAges (expires) an LSA identified by the given key.
     * @param key LsaKey of the LSA to withdraw from the LSDB.
     */
    void expire(LsaKey& key) override;

    /**
     * @brief Removes the Network LSA previously originated for the given interface.
     * @param ifaceId Internal interface identifier whose Network LSA should be flushed.
     */
    void removeNetworkLsa(uint32_t ifaceId) override;

    // LINK DESCRIPTOR HELPERS

    /**
     * @brief Appends secondary stub links for unnumbered or multi-subnet interfaces.
     * @param router LSA body being constructed.
     * @param iface  Interface whose secondary address prefixes are being advertised.
     */
    void addSecondaryLinks(LsaBody& router, const OspfInterfaceBase& iface);

    /**
     * @brief Appends a transit (Type 2) link descriptor to a Router LSA body.
     * @param router LSA body being constructed.
     * @param iface  The interface on the transit network.
     * @param nbr    Optional: the Full neighbor that makes the link active. Pass nullptr
     *               to append the link using the DR address from the interface state.
     */
    void addTransitLink(LsaBody& router, const OspfInterfaceBase& iface, const Neighbor* nbr = nullptr) override;

    /**
     * @brief Appends a point-to-point (Type 1) link descriptor to a Router LSA body.
     * @param router   LSA body being constructed.
     * @param iface    The point-to-point interface.
     * @param neighbor The Full neighbor reachable through this interface.
     */
    void addP2PLink(LsaBody& router, const OspfInterfaceBase& iface, const Neighbor& neighbor) override;

    /**
     * @brief Appends a stub (Type 3) link descriptor to a Router LSA body.
     * @param router   LSA body being constructed.
     * @param iface    Interface whose prefix is being advertised as a stub.
     * @param fullMask True to use a /32 host mask instead of the interface subnet mask.
     */
    void addStubLink(LsaBody& router, const OspfInterfaceBase& iface, bool fullMask = false) override;

    /**
     * @brief Appends a virtual link (Type 4) descriptor to a Router LSA body.
     * @param router LSA body being constructed.
     * @param iface  The virtual link OSPF interface.
     * @param vNbr   The Full neighbor at the other end of the virtual link.
     */
    void addVirtualLink(LsaBody& router, const OspfInterfaceBase& iface, const Neighbor& vNbr) override;
};
} // namespace routing::ospf

#endif // INTRA_ORIGINATOR_V2_H
