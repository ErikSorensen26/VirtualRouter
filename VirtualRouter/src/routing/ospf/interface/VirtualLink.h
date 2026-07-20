/**
 * @file VirtualLink.h
 * @brief OSPF virtual link (RFC 2328 SS15): a backbone adjacency tunneled through a transit area.
 */

#ifndef OSPF_VIRTUAL_LINK_H
#define OSPF_VIRTUAL_LINK_H

#include "OspfInterfaceBase.h"
#include "configs/registry/router/OspfRegistry.h"
#include "ospf/topology/TopologyTypes.hpp"

namespace routing::ospf
{
class OspfInterface;

/**
 * @brief One RFC 2328 SS15 virtual link: a logical point-to-point backbone
 *        adjacency reached through a non-backbone transit area.
 * @ingroup OSPF_INTERFACE
 *
 * A virtual link behaves as an unnumbered point-to-point interface in area 0
 * (`OspfInterfaceBase::area` is always the backbone): it forms a Hello/DBD/LSR/LSU
 * adjacency, contributes a Type-4 virtual-link descriptor to this router's
 * backbone Router-LSA (see @ref IntraOriginator::addVirtualLink), and once FULL
 * can carry backbone traffic.
 *
 * Unlike `OspfInterface`, it has no bound hardware `interface::Interface`.
 * Its packets are ordinary unicast IP packets addressed to the remote
 * endpoint's transit-area address; RFC 2328 requires them to follow the
 * transit area's own current intra-area shortest path, so the egress
 * interface and next-hop IP are re-resolved from `transitAreaId`'s SPF result
 * on every transmit rather than bound once at construction (see
 * @ref RouteManagerUtility::resolveVirtualLinkPath).
 *
 * ## Lifecycle & Ownership
 * Created and destroyed by `InterfaceManager` from the transit area's
 * `area <id> virtual-link <router-id>` configuration entries.
 *
 * @see OspfInterfaceBase, OspfInterface, RouteManagerUtility::resolveVirtualLinkPath
 */
class VirtualLink : public OspfInterfaceBase
{
public:
    /**
     * @brief Constructs a virtual link and attaches it to the backbone area.
     *
     * @param proc          Owning OSPF process.
     * @param id            Composite key: `interfaceId` = remote neighbor's router ID, `area` = 0 (backbone).
     * @param transitAreaId Non-backbone area this virtual link is configured against (RFC 2328 SS15).
     * @param cfgs          This virtual link's configuration registry entry.
     */
    VirtualLink(OspfProcess& proc, const OspfInterfaceId& id, uint32_t transitAreaId, const config::OspfVirtualLinkRegistry& cfgs);

    const uint32_t transitAreaId;   ///< Non-backbone area this virtual link tunnels through.
    const uint32_t remoteRouterId;  ///< Router ID of the neighbor at the other end (mirrored from id.interfaceId).

    // OspfInterfaceBase overrides

    config::ospf::NetworkType getNetworkType() const override { return config::ospf::NetworkType::POINT_TO_POINT; }
    bool getPassive() const override { return false; }
    uint8_t getPriority() const override { return 0; }
    bool getMtuIgnore() const override { return true; }
    bool getDatabaseFilter() const override { return false; }
    bool getDemandCircuitIgnore() const override { return true; }
    bool getLls() const override { return getProcessConfigs().get<config::Ospf::LLS>().load(); }
    bool isVirtualLink() const override { return true; }
    interface::Interface* getTransmitInterface() const override;
    const types::IPPrefix& getTransmitAddress() const override { resolveTransitPath(); return transmitAddress; }
    uint16_t getCost() const override { resolveTransitPath(); return transmitCost; }

    /**
     * @brief Resolves the forwarding next hop for data-plane routes whose SPF
     *        path traverses this virtual link as its first-hop edge.
     *
     * A virtual link is not a real physical hop (RFC 2328 SS15): the actual
     * forwarding next hop for anything routed "through" it is the transit
     * area's own currently-resolved intra-area next hop toward the remote
     * endpoint -- the same interface/address `getTransmitInterface()` and
     * `getTransmitAddress()` use for OSPF control packets. Used by
     * `RouteManagerUtility::resolveDirectNextHop` instead of the ordinary
     * neighbor-on-a-local-segment lookup, which does not apply here.
     *
     * @return `{local OSPF interface id, next-hop address}`, or
     *         `std::nullopt` if the transit area currently has no path.
     */
    std::optional<OspfNextHop> getRoutingNextHop() const;

private:
    const config::OspfVirtualLinkRegistry& configs;

    /**
     * @brief Re-resolves the current transit-area path to `remoteRouterId` and
     *        caches the result in `transmitAddress`/`transmitCost`.
     *
     * Called independently by `getTransmitInterface()`, `getTransmitAddress()`,
     * and `getCost()` -- each resolves fresh rather than trusting a cache a
     * sibling accessor may or may not have populated first (callers outside
     * the Tx path, e.g. Router-LSA Type-4 origination, call these directly
     * without ever calling `getTransmitInterface()`).
     *
     * @return The resolved local `OspfInterface`, or `nullptr` if the transit
     *         area currently has no intra-area path to the remote endpoint.
     */
    OspfInterface* resolveTransitPath() const;

    mutable types::IPPrefix transmitAddress; ///< Last-resolved unicast next hop toward the remote endpoint; updated by resolveTransitPath().
    mutable uint16_t transmitCost = 0;       ///< Last-resolved transit-area intra-area cost to the remote endpoint; updated by resolveTransitPath().
};
}

#endif // OSPF_VIRTUAL_LINK_H
