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

#ifndef OSPF_ORIGINATOR_V2_H
#define OSPF_ORIGINATOR_V2_H

#include "ospf/area/Originator.h"

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
class OriginatorV2 : public Originator
{
public:
    /**
     * @brief Constructs an OriginatorV2 bound to the given area.
     * @param a Owning OSPF area; must outlive this originator.
     */
    OriginatorV2(Area& a);

    /**
     * @brief Destroys the originator and withdraws any self-originated LSAs still in the LSDB.
     */
    ~OriginatorV2() override;

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

    /**
     * @brief Originates or withdraws an AS-External LSA for a redistributed route.
     * @param asbr   Router ID of the ASBR advertising the external route.
     * @param lsid   Link-State ID to use for the originated LSA.
     * @param remove True to flush (MaxAge) the LSA rather than originate it.
     */
    void addExternal(uint32_t asbr, uint32_t lsid, bool remove) override;

    /**
     * @brief Translates an NSSA-External LSA (Type 7) into an AS-External LSA (Type 5).
     * @param key    LsaKey of the NSSA LSA being translated.
     * @param lsa    Decoded body of the NSSA LSA.
     * @param expire True to withdraw the resulting Type-5 rather than originate it.
     */
    void translateNssaToExternal(const LsaKey& key, const LsaBody& lsa, bool expire) override;

    /**
     * @brief Originates or withdraws the default-route stub LSA for this area.
     * @param add True to inject the default route; false to withdraw it.
     */
    void addStubDefaultRoute(bool add) override;

    /**
     * @brief Originates or withdraws a Type-3 Summary (inter-area prefix) LSA.
     * @param lsid   Link-State ID for the summary LSA.
     * @param prefix The IP prefix to advertise into adjacent areas.
     * @param cost   Metric to attach to the summary.
     * @param expire True to MaxAge the LSA rather than originate it.
     */
    void originateSummary(uint32_t lsid, const types::IPPrefix& prefix, uint32_t cost, bool expire) override;

protected:
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
    void addNetworkLsa(const OspfInterface& iface, bool refresh) override;

    /**
     * @brief MaxAges (expires) an LSA identified by the given key.
     * @param key LsaKey of the LSA to withdraw from the LSDB.
     */
    void expire(LsaKey& key) override;

    /**
     * @brief Originates or refreshes the Type-4 Summary LSA advertising an ASBR.
     * @param asbr    Router ID of the ASBR to advertise.
     * @param refresh True when refreshing an existing Type-4 LSA.
     */
    void addAsbrLsa(uint32_t asbr, bool refresh = false) override;

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
    void addSecondaryLinks(LsaBody& router, const OspfInterface& iface);

    /**
     * @brief Appends a transit (Type 2) link descriptor to a Router LSA body.
     * @param router LSA body being constructed.
     * @param iface  The interface on the transit network.
     * @param nbr    Optional: the Full neighbor that makes the link active. Pass nullptr
     *               to append the link using the DR address from the interface state.
     */
    void addTransitLink(LsaBody& router, const OspfInterface& iface, const Neighbor* nbr = nullptr) override;

    /**
     * @brief Appends a point-to-point (Type 1) link descriptor to a Router LSA body.
     * @param router   LSA body being constructed.
     * @param iface    The point-to-point interface.
     * @param neighbor The Full neighbor reachable through this interface.
     */
    void addP2PLink(LsaBody& router, const OspfInterface& iface, const Neighbor& neighbor) override;

    /**
     * @brief Appends a stub (Type 3) link descriptor to a Router LSA body.
     * @param router   LSA body being constructed.
     * @param iface    Interface whose prefix is being advertised as a stub.
     * @param fullMask True to use a /32 host mask instead of the interface subnet mask.
     */
    void addStubLink(LsaBody& router, const OspfInterface& iface, bool fullMask = false) override;

    /**
     * @brief Appends a virtual link (Type 4) descriptor to a Router LSA body.
     * @param router LSA body being constructed.
     * @param iface  The virtual link OSPF interface.
     * @param vNbr   The Full neighbor at the other end of the virtual link.
     */
    void addVirtualLink(LsaBody& router, const OspfInterface& iface, const Neighbor& vNbr) override;
};
} // namespace routing::ospf

#endif
