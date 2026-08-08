/**
 * @file BgpRegistry.h
 * @brief BGP configuration registry: process, neighbor, AF, and transport settings.
 * @ingroup BGP
 *
 * Defines the configuration schema for BGP including process-level parameters,
 * address-family activation and policy, per-neighbor session tuning, peer-group
 * and peer-template inheritance, and transport (keepalive, hold-time) settings.
 */

#ifndef BGP_REGISTRY_H
#define BGP_REGISTRY_H

#include <string>
#include <IPAddress.h>

#include "interface/configs/InterfaceType.hpp"
#include "configs/TupleSchema.hpp"
#include "configs/RegistryTypes.hpp"
#include "configs/RegistryReference.hpp"
#include "configs/RegistryDefaultTable.hpp"
#include "configs/SubRegistry.hpp"
#include "configs/FieldAccessor.hpp" // IWYU pragma: keep

namespace config
{
namespace bgp
{

/**
 * @brief Slow-peer detection strategy for a BGP address family.
 * @ingroup BGP
 */
enum class SlowPeerMode
{
    STATIC,           ///< Peer is statically marked as slow.
    DYNAMIC,          ///< Peer is dynamically detected as slow and moved per-update.
    DYNAMIC_PERMANENT ///< Dynamically detected and permanently held in the slow group.
};

}

/**
 * @brief Shared transport parameters inherited by BGP process and neighbor sessions.
 * @ingroup BGP
 */
enum class BgpTransportBase
{
    KEEPALIVE_INTERVAL,
    HOLDTIME,
    MINIMUM_HOLDTIME,
    TRANSPORT_PATH_MTU_DISCOVERY,
    COUNT
};

#define BGP_TRANSPORT_BASE_DEFAULTS(X) \
    X(BgpTransportBase, KEEPALIVE_INTERVAL, 60) \
    X(BgpTransportBase, HOLDTIME, 180) \
    X(BgpTransportBase, TRANSPORT_PATH_MTU_DISCOVERY, false)

CONFIG_DEFAULT_TABLE(BGP_TRANSPORT_BASE_DEFAULTS);

void BgpBaseRestart(void*);

struct BgpBaseFields : FieldTuple<
    AtomicField<uint16_t CONFIG_INDEX_ARG(BgpTransportBase::KEEPALIVE_INTERVAL), BgpBaseRestart>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(BgpTransportBase::HOLDTIME), BgpBaseRestart>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(BgpTransportBase::MINIMUM_HOLDTIME), BgpBaseRestart>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpTransportBase::TRANSPORT_PATH_MTU_DISCOVERY), BgpBaseRestart>
> {};

/**
 * @brief Registry slot for BGP transport base parameters.
 * @ingroup BGP
 */
struct BgpBaseRegistry : SubRegistry<BgpBaseRegistry, BgpTransportBase, nullptr, BgpBaseFields> {};

/**
 * @brief Shared address-family parameters inherited by BGP process and neighbor AF configs.
 * @ingroup BGP
 */
enum class BgpAfBase
{
    ADDITIONAL_PATHS_RECEIVE,
    ADDITIONAL_PATHS_SEND,
    ADVERTISE_ADDITIONAL_PATHS_ALL,
    ADVERTISE_ADDITIONAL_PATHS_BEST,
    ADVERTISE_ADDITIONAL_GROUP_BEST,
    ADVERTISE_BEST_EXTERNAL,
    DEFAULT_ORIGINATE,

    // Only changes how already-detected slow peers are classified; re-evaluate slow-peer
    // group membership on the next detection pass.
    SLOW_PEER_MODE,

    // Enable/disable the periodic slow-peer detection pass for this AF.
    SLOW_PEER_DETECTION,

    // Only used by the next slow-peer detection pass; no immediate action.
    SLOW_PEER_DETECTION_THRESHOLD,

    COUNT
};

#define BGP_AF_BASE_DEFAULTS(X) \
    X(BgpAfBase, ADDITIONAL_PATHS_RECEIVE, false) \
    X(BgpAfBase, ADDITIONAL_PATHS_SEND, false) \
    X(BgpAfBase, ADVERTISE_ADDITIONAL_PATHS_ALL, false) \
    X(BgpAfBase, ADVERTISE_ADDITIONAL_GROUP_BEST, false) \
    X(BgpAfBase, ADVERTISE_BEST_EXTERNAL, false) \
    X(BgpAfBase, DEFAULT_ORIGINATE, false) \
    X(BgpAfBase, SLOW_PEER_DETECTION, false) \
    X(BgpAfBase, SLOW_PEER_DETECTION_THRESHOLD, 300)

CONFIG_DEFAULT_TABLE(BGP_AF_BASE_DEFAULTS);

void BgpAfBaseAdditionalPaths(void*);
void BgpAfBaseDefaultOriginate(void*);
void BgpAfBaseSlowPeer(void*);
void BgpNeighborRestart(void*); // used by ADD-PATH capability fields (session reset)

struct BgpAfBaseFields : FieldTuple<
    AtomicField<bool CONFIG_INDEX_ARG(BgpAfBase::ADDITIONAL_PATHS_RECEIVE), BgpNeighborRestart>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAfBase::ADDITIONAL_PATHS_SEND), BgpNeighborRestart>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAfBase::ADVERTISE_ADDITIONAL_PATHS_ALL), BgpAfBaseAdditionalPaths>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(BgpAfBase::ADVERTISE_ADDITIONAL_PATHS_BEST), BgpAfBaseAdditionalPaths>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAfBase::ADVERTISE_ADDITIONAL_GROUP_BEST), BgpAfBaseAdditionalPaths>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAfBase::ADVERTISE_BEST_EXTERNAL), BgpAfBaseAdditionalPaths>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAfBase::DEFAULT_ORIGINATE), BgpAfBaseDefaultOriginate>,
    OptionalAtomicField<config::bgp::SlowPeerMode CONFIG_INDEX_ARG(BgpAfBase::SLOW_PEER_MODE), BgpAfBaseSlowPeer>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAfBase::SLOW_PEER_DETECTION), BgpAfBaseSlowPeer>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(BgpAfBase::SLOW_PEER_DETECTION_THRESHOLD)>
> {};

/**
 * @brief Registry slot for BGP address-family base parameters.
 * @ingroup BGP
 */
struct BgpAfBaseRegistry : SubRegistry<BgpAfBaseRegistry, BgpAfBase, nullptr, BgpAfBaseFields> {};

/**
 * @brief Per-neighbor, per-address-family BGP configuration fields.
 * @ingroup BGP
 */
enum class BgpNeighbor
{
    AF_BASE,

    // Turning off: withdraw all routes previously advertised/accepted for this AF and stop
    // exchanging NLRI of this type with the peer. Turning on: re-run Adj-RIB-Out/Adj-RIB-In
    // for this AF as if the peer had just come up (send initial UPDATEs).
    ACTIVATE,

    // Recompute the additional-paths candidate set (add backup path) and re-advertise.
    ADVERTISE_DIVERSE_PATH_BACKUP,

    // Recompute the additional-paths candidate set (add multipath members) and re-advertise.
    ADVERTISE_DIVERSE_PATH_MPATH,

    // Route-map changes which locally-originated routes are advertised; re-run Adj-RIB-Out.
    ADVERTISE_MAP, // TODO

    // Changes the exist condition gating ADVERTISE_MAP; re-run Adj-RIB-Out.
    ADVERTISE_MAP_EXIST_CONDITION, // TODO

    // Changes the non-exist condition gating ADVERTISE_MAP; re-run Adj-RIB-Out.
    ADVERTISE_MAP_NON_EXIST_CONDITION, // TODO

    // Reschedule the MinRouteAdvertisementInterval (MRAI) timer for this peer/AF with the
    // new interval; no re-send of already-sent UPDATEs.
    ADVERTISE_INTERVAL,

    // Re-run inbound policy over Adj-RIB-In: routes with the peer's own AS in AS_PATH are
    // now accepted/rejected based on the new occurrence limit.
    ALLOWAS_IN,

    // Re-run inbound policy over Adj-RIB-In using the new allowed-occurrence count.
    ALLOWAS_IN_OCCURANCES,

    // Changes whether RPKI validation state is announced to the peer; re-run Adj-RIB-Out
    // to add/remove the extended community carrying validation state.
    ANNOUNCE_RPKI_STATE, // TODO

    // Outbound Route Filtering (RFC 5291) is a BGP capability negotiated in OPEN;
    // session must be reset so the capability is renegotiated.
    // FSM: RESTART
    ORF_BOTH,

    // ORF capability negotiated in OPEN; session must be reset.
    // FSM: RESTART
    ORF_RECEIVE,

    // ORF capability negotiated in OPEN; session must be reset.
    // FSM: RESTART
    ORF_SEND,

    // Route-map rewrites attributes of locally-originated routes before advertisement;
    // re-run Adj-RIB-Out.
    ORIGINATE_ROUTE_MAP, // TODO

    // Re-run inbound policy over Adj-RIB-In with the new access-list filter.
    DISTRIBUTE_LIST_IN, // TODO

    // Re-run inbound policy over Adj-RIB-In with the new interface-based filter.
    DISTRIBUTE_LIST_IN_INTERFACE, // TODO

    // Re-run outbound policy / recompute Adj-RIB-Out with the new access-list filter.
    DISTRIBUTE_LIST_OUT, // TODO

    // Re-run outbound policy / recompute Adj-RIB-Out with the new interface-based filter.
    DISTRIBUTE_LIST_OUT_INTERFACE, // TODO

    // Recompute Adj-RIB-Out: adds/removes the DMZ-link-bandwidth extended community on
    // advertised routes.
    DMZLINK_BW, // TODO

    // Re-run inbound policy over Adj-RIB-In with the new AS_PATH regex filter.
    FILTER_LIST_IN, // TODO

    // Re-run outbound policy / recompute Adj-RIB-Out with the new AS_PATH regex filter.
    FILTER_LIST_OUT, // TODO

    // Changes which peer-policy template this neighbor/AF inherits from; rebuild the
    // effective config for this neighbor/AF and re-run inbound+outbound policy.
    INHERIT_PEER_POLICY,

    // Only checked against the running prefix count on each Adj-RIB-In update; no
    // immediate re-evaluation of the current count.
    MAXIMUM_PREFIX,

    // Only checked against the running prefix count on each Adj-RIB-In update.
    MAXIMUM_PREFIX_THRESHOLD,

    // Reschedule the auto-restart timer if the session is currently torn down for
    // exceeding MAXIMUM_PREFIX.
    MAXIMUM_PREFIX_RESTART,

    // Only affects behavior the next time MAXIMUM_PREFIX is exceeded (log vs. tear down
    // the session); no immediate action.
    MAXIMUM_PREFIX_WARNING_ONLY,

    // Recompute Adj-RIB-Out: rewrites NEXT_HOP to the local address on advertised routes.
    NEXT_HOP_SELF,

    // Recompute Adj-RIB-Out: rewrites NEXT_HOP to the local address on all routes,
    // including those already carrying the peer's own address (iBGP reflection case).
    NEXT_HOP_SELF_ALL,

    // Recompute Adj-RIB-Out: stops rewriting NEXT_HOP for eBGP-learned routes reflected
    // or re-advertised to iBGP peers.
    NEXT_HOP_UNCHANGED,

    // Re-run inbound policy over Adj-RIB-In with the new prefix-list filter.
    PREFIX_LIST_IN, // TODO

    // Re-run outbound policy / recompute Adj-RIB-Out with the new prefix-list filter.
    // Prefix-list and distribute-list are mutually exclusive in the same direction.
    PREFIX_LIST_OUT, // TODO:       prefix/distribute list can not co-exist

    // Recompute Adj-RIB-Out: strips/restores private AS numbers from the advertised AS_PATH.
    REMOVE_PRIVATE_AS,

    // Recompute Adj-RIB-Out: strips/restores private AS numbers, including ones adjacent
    // to a public AS in the advertised AS_PATH.
    REMOVE_PRIVATE_AS_ALL,

    // Re-run inbound policy over Adj-RIB-In with the new route-map.
    ROUTE_MAP_IN, // TODO

    // Re-run outbound policy / recompute Adj-RIB-Out with the new route-map.
    ROUTE_MAP_OUT, // TODO

    // Route-reflector client status (RFC 4456) changes whether this peer receives
    // reflected routes and whether ORIGINATOR_ID/CLUSTER_LIST are applied; re-run
    // Adj-RIB-Out toward this peer. Does not change the best-path winner itself.
    ROUTE_REFLECTOR_CLIENT,

    // Route-server client status changes whether NEXT_HOP/AS_PATH/LOCAL_PREF/MED are
    // left unmodified when advertising to this peer; re-run Adj-RIB-Out toward this
    // peer. Does not change the best-path winner itself.
    ROUTE_SERVER_CLIENT, // TODO

    // Changes which routing-context/VRF the route-server client is evaluated against;
    // rebuild the effective config and re-run Adj-RIB-In/Out.
    ROUTE_SERVER_CLIENT_CONTEXT, // TODO

    // Recompute Adj-RIB-Out: adds/removes standard COMMUNITY attribute on advertised routes.
    SEND_COMMUNITY,

    // Recompute Adj-RIB-Out: adds/removes both standard and extended COMMUNITY attributes.
    SEND_COMMUNITY_BOTH,

    // Recompute Adj-RIB-Out: adds/removes EXTENDED_COMMUNITY attribute on advertised routes.
    SEND_COMMUNITY_EXTENDED,

    // Recompute Adj-RIB-Out: adds/removes standard COMMUNITY attribute on advertised routes.
    SEND_COMMUNITY_STANDARD,

    // Enabling: start retaining a pre-policy copy of Adj-RIB-In for this peer so a future
    // "soft reconfiguration inbound" (RFC 2918 style, without a route refresh) can
    // re-run inbound policy without querying the peer. Disabling: free the retained copy.
    SOFT_RECONFIGURATION,

    // Recompute Adj-RIB-Out: rewrites attributes of routes being translated between AFs
    // (e.g. NLRI translation) before advertisement.
    TRANSLATE_UPDATE, // TODO

    // Recompute Adj-RIB-Out: un-suppresses routes that were suppressed by aggregation,
    // subject to the new route-map.
    UNSUPPRESS_MAP, // TODO

    // Recompute best-path selection: WEIGHT is the first (highest-priority, non-transitive)
    // tiebreaker, so a change can alter the chosen best path and requires re-running
    // Adj-RIB-Out.
    WEIGHT,

    COUNT
};

#define BGP_NEIGHBOR_DEFAULTS(X) \
    X(BgpNeighbor, ACTIVATE, false) \
    X(BgpNeighbor, ADVERTISE_DIVERSE_PATH_BACKUP, false) \
    X(BgpNeighbor, ADVERTISE_DIVERSE_PATH_MPATH, false) \
    X(BgpNeighbor, ADVERTISE_INTERVAL, 30) \
    X(BgpNeighbor, ALLOWAS_IN, false) \
    X(BgpNeighbor, ANNOUNCE_RPKI_STATE, false) \
    X(BgpNeighbor, ORF_BOTH, false) \
    X(BgpNeighbor, ORF_RECEIVE, false) \
    X(BgpNeighbor, ORF_SEND, false) \
    X(BgpNeighbor, DMZLINK_BW, false) \
    X(BgpNeighbor, MAXIMUM_PREFIX_WARNING_ONLY, false) \
    X(BgpNeighbor, NEXT_HOP_SELF, false) \
    X(BgpNeighbor, NEXT_HOP_SELF_ALL, false) \
    X(BgpNeighbor, NEXT_HOP_UNCHANGED, false) \
    X(BgpNeighbor, REMOVE_PRIVATE_AS, false) \
    X(BgpNeighbor, REMOVE_PRIVATE_AS_ALL, false) \
    X(BgpNeighbor, ROUTE_REFLECTOR_CLIENT, false) \
    X(BgpNeighbor, ROUTE_SERVER_CLIENT, false) \
    X(BgpNeighbor, SEND_COMMUNITY, false) \
    X(BgpNeighbor, SEND_COMMUNITY_BOTH, false) \
    X(BgpNeighbor, SEND_COMMUNITY_EXTENDED, false) \
    X(BgpNeighbor, SEND_COMMUNITY_STANDARD, false) \
    X(BgpNeighbor, SOFT_RECONFIGURATION, false) \
    X(BgpNeighbor, TRANSLATE_UPDATE, false)

CONFIG_DEFAULT_TABLE(BGP_NEIGHBOR_DEFAULTS);

void BgpNeighborActivate(void*);
void BgpNeighborAdvertiseDiverse(void*);
void BgpNeighborAdvertiseMap(void*);
void BgpNeighborAdvertiseInterval(void*);
void BgpNeighborAllowasIn(void*);
void BgpNeighborAnnounceRpki(void*);
void BgpNeighborInboundRefresh(void*);
void BgpNeighborOutboundRefresh(void*);
void BgpNeighborDmzLinkBw(void*);
void BgpNeighborRestart(void*);
void BgpNeighborMaxPrefixRestart(void*);
void BgpNeighborNextHop(void*);
void BgpNeighborPrivateAs(void*);
void BgpNeighborReflector(void*);
void BgpNeighborSendCommunity(void*);
void BgpNeighborSoftReconfig(void*);
void BgpNeighborTranslationUpdate(void*);
void BgpNeighborWeight(void*);

struct BgpNeighborFields : FieldTuple<
    RegistryContainer<BgpAfBaseRegistry CONFIG_INDEX_ARG(BgpNeighbor::AF_BASE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ACTIVATE), BgpNeighborActivate>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ADVERTISE_DIVERSE_PATH_BACKUP), BgpNeighborAdvertiseDiverse>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ADVERTISE_DIVERSE_PATH_MPATH), BgpNeighborAdvertiseDiverse>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::ADVERTISE_MAP), BgpNeighborAdvertiseMap>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::ADVERTISE_MAP_EXIST_CONDITION), BgpNeighborAdvertiseMap>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::ADVERTISE_MAP_NON_EXIST_CONDITION), BgpNeighborAdvertiseMap>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(BgpNeighbor::ADVERTISE_INTERVAL), BgpNeighborAdvertiseInterval>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ALLOWAS_IN), BgpNeighborAllowasIn>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(BgpNeighbor::ALLOWAS_IN_OCCURANCES), BgpNeighborAllowasIn>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ANNOUNCE_RPKI_STATE), BgpNeighborAnnounceRpki>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ORF_BOTH), BgpNeighborRestart>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ORF_RECEIVE), BgpNeighborRestart>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ORF_SEND), BgpNeighborRestart>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::ORIGINATE_ROUTE_MAP), BgpNeighborInboundRefresh>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::DISTRIBUTE_LIST_IN), BgpNeighborInboundRefresh>,
    OptionalAtomicField<interface::InterfaceKey CONFIG_INDEX_ARG(BgpNeighbor::DISTRIBUTE_LIST_IN_INTERFACE), BgpNeighborInboundRefresh>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::DISTRIBUTE_LIST_OUT), BgpNeighborOutboundRefresh>,
    OptionalAtomicField<interface::InterfaceKey CONFIG_INDEX_ARG(BgpNeighbor::DISTRIBUTE_LIST_OUT_INTERFACE), BgpNeighborOutboundRefresh>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::DMZLINK_BW), BgpNeighborOutboundRefresh>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::FILTER_LIST_IN), BgpNeighborInboundRefresh>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::FILTER_LIST_OUT), BgpNeighborOutboundRefresh>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::INHERIT_PEER_POLICY), BgpNeighborInboundRefresh>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(BgpNeighbor::MAXIMUM_PREFIX)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(BgpNeighbor::MAXIMUM_PREFIX_THRESHOLD)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(BgpNeighbor::MAXIMUM_PREFIX_RESTART), BgpNeighborMaxPrefixRestart>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::MAXIMUM_PREFIX_WARNING_ONLY)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::NEXT_HOP_SELF), BgpNeighborNextHop>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::NEXT_HOP_SELF_ALL), BgpNeighborNextHop>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::NEXT_HOP_UNCHANGED), BgpNeighborNextHop>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::PREFIX_LIST_IN), BgpNeighborInboundRefresh>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::PREFIX_LIST_OUT), BgpNeighborOutboundRefresh>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::REMOVE_PRIVATE_AS), BgpNeighborPrivateAs>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::REMOVE_PRIVATE_AS_ALL), BgpNeighborPrivateAs>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::ROUTE_MAP_IN), BgpNeighborInboundRefresh>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::ROUTE_MAP_OUT), BgpNeighborOutboundRefresh>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ROUTE_REFLECTOR_CLIENT), BgpNeighborReflector>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::ROUTE_SERVER_CLIENT)>, // TODO
    ValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::ROUTE_SERVER_CLIENT_CONTEXT)>, // TODO
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::SEND_COMMUNITY), BgpNeighborSendCommunity>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::SEND_COMMUNITY_BOTH), BgpNeighborSendCommunity>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::SEND_COMMUNITY_EXTENDED), BgpNeighborSendCommunity>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::SEND_COMMUNITY_STANDARD), BgpNeighborSendCommunity>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::SOFT_RECONFIGURATION), BgpNeighborSoftReconfig>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighbor::TRANSLATE_UPDATE), BgpNeighborTranslationUpdate>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpNeighbor::UNSUPPRESS_MAP), BgpNeighborOutboundRefresh>,
    ValueField<uint16_t CONFIG_INDEX_ARG(BgpNeighbor::WEIGHT), BgpNeighborWeight>
> {};

/**
 * @brief Registry slot for per-neighbor, per-AF BGP configuration.
 * @ingroup BGP
 */
struct BgpNeighborRegistry : SubRegistry<BgpNeighborRegistry, BgpNeighbor, nullptr, BgpNeighborFields> {};

/**
 * @brief Session-level BGP neighbor configuration fields (transport, timers, auth, path attributes).
 * @ingroup BGP
 */
enum class BgpNeighborSession
{
    BGP_BASE,

    // Cosmetic only; no protocol or state effect.
    DESCRIPTION, // TODO

    // Only checked the next time a passive connection from this peer is accepted
    // (skips/enforces the "source matches configured neighbor address" check).
    DISABLE_CONNECTION_CHECK,

    // Changes the TTL used to originate the TCP connection; requires tearing down and
    // re-establishing the session so the new TTL is used on SYN.
    // FSM: RESTART
    EBGP_MULTIHOP, // TODO: need full tcp first

    // Changes the TTL value itself for EBGP_MULTIHOP; requires session reset.
    // FSM: RESTART
    EBGP_MAX_HOP_COUNT, // TODO: need full tcp first

    // Enable/disable registering a RIB next-hop-reachability watch used to trigger
    // fast external failover on next-hop loss; (un)register the watch accordingly.
    FALL_OVER, // TODO: needs rib callbacks

    // Changes whether loss of BFD control-plane-failure signaling alone triggers
    // failover; (un)register the corresponding BFD event hook.
    FALL_OVER_BFD_CHECK_CONTROL_PLANE_FAILURE, // TODO

    // Enable/disable BFD-triggered failover for multi-hop eBGP sessions; (un)register
    // the BFD session monitor.
    FALL_OVER_BFD_MULTI_HOP, // TODO

    // Enable/disable BFD-triggered failover for single-hop eBGP sessions; (un)register
    // the BFD session monitor.
    FALL_OVER_BFD_SINGLE_HOP, // TODO

    // Changes which next-hop route changes are allowed to trigger FALL_OVER; re-evaluate
    // the route-map on the next RIB change, no immediate session action.
    FALL_OVER_ROUTE_MAP, // TODO

    // Graceful Restart is a BGP capability (RFC 4724) negotiated in OPEN; session must be
    // reset so the capability is renegotiated with the peer.
    // FSM: RESTART
    HAMODE_GRACEFUL_RESTART, // TODO

    // Changes which peer-session template this neighbor inherits from; rebuild the
    // effective session config (may itself require a session reset if inherited
    // transport/timer/capability fields changed).
    INHERIT_PEER_SESSION,

    // Local-AS is sent as the AS in this session's OPEN message; session must be reset
    // to renegotiate with the new AS.
    // FSM: RESTART
    LOCAL_AS,

    // Changes which AS number LOCAL_AS uses; session must be reset.
    // FSM: RESTART
    LOCAL_AS_AS,

    // Changes whether the real local AS is still prepended to AS_PATH alongside
    // LOCAL_AS on outbound UPDATEs; re-run Adj-RIB-Out to re-advertise with the new
    // AS_PATH construction (no session reset needed).
    LOCAL_AS_NO_PREPEND,

    // Changes whether the real local AS is replaced (vs. prepended) in AS_PATH on
    // outbound UPDATEs; re-run Adj-RIB-Out to re-advertise with the new AS_PATH
    // construction (no session reset needed).
    LOCAL_AS_REPLACE_AS,

    // Changes whether both the real and LOCAL_AS AS numbers are accepted from the peer;
    // session must be reset since it affects AS_PATH validation during OPEN/UPDATE.
    // FSM: RESTART
    LOCAL_AS_DUAL_AS,

    // MD5 authentication (RFC 2385) is applied to the TCP socket at connect/listen time;
    // session must be reset so the new key/no-key is used on the TCP handshake.
    // FSM: RESTART
    PASSWORD, // TODO

    // Changes how specific path-attribute types/ranges are treated on receipt
    // (discard vs. treat-as-withdraw, RFC 7606); re-run parsing/validation on the next
    // UPDATE received, no immediate session action.
    PATH_ATTRIBUTE,

    // Changes which peer-group this neighbor belongs to; rebuild the effective config by
    // re-applying the group's inherited transport/timer/policy settings, then apply
    // whatever session-reset or re-advertisement each changed inherited field requires.
    // FSM: indirect — no fixed event, depends on which inherited fields actually changed.
    PEER_GROUP,

    // Remote AS is validated against the peer's OPEN message; session must be reset to
    // renegotiate against the new expected AS.
    // FSM: RESTART
    REMOTE_AS,

    // Administratively tear down the session (send CEASE) if set; bring the session back
    // up (start connecting again) if cleared.
    // FSM: MANUAL_STOP on set; MANUAL_START / MANUAL_START_PASSIVE_TCP on clear
    // (BgpProcess::shutdownNeighbor / unshutdownNeighbor).
    SHUTDOWN,

    // Changes active vs. passive-only connection mode; session must be reset so the new
    // connect/listen behavior takes effect.
    // FSM: RESTART
    TRANSPORT_CONNECTION_MODE,

    // Changes whether multiple parallel TCP sessions are permitted for this peer;
    // requires session reset to add/remove the extra connection.
    // FSM: RESTART
    TRANSPORT_MULTI_SESSION,

    // GTSM (RFC 5082): TTL/hop-count check applied to received packets at the socket
    // level; session must be reset so the new TTL security check takes effect.
    // FSM: RESTART
    TTL_SEC, // TODO

    // Changes the expected hop count for TTL_SEC; session must be reset.
    // FSM: RESTART
    TTL_SEC_HOP, // TODO

    // Adding an AF here activates capability negotiation for that AFI/SAFI on this
    // session; removing withdraws all routes of that AF and stops exchanging its NLRI.
    // Session need not be reset for existing AFs, but a newly-added AF's capability
    // won't take effect until the next OPEN (see BgpNeighbor::ACTIVATE for per-AF policy).
    AF_NEIGHBOR,

    COUNT
};

#define BGP_NEIGHBOR_SESSION_DEFAULTS(X) \
    X(BgpNeighborSession, DISABLE_CONNECTION_CHECK, false) \
    X(BgpNeighborSession, EBGP_MULTIHOP, false) \
    X(BgpNeighborSession, EBGP_MAX_HOP_COUNT, 1) \
    X(BgpNeighborSession, FALL_OVER, false) \
    X(BgpNeighborSession, FALL_OVER_BFD_CHECK_CONTROL_PLANE_FAILURE, false) \
    X(BgpNeighborSession, FALL_OVER_BFD_MULTI_HOP, false) \
    X(BgpNeighborSession, FALL_OVER_BFD_SINGLE_HOP, false) \
    X(BgpNeighborSession, LOCAL_AS, false) \
    X(BgpNeighborSession, LOCAL_AS_NO_PREPEND, false) \
    X(BgpNeighborSession, LOCAL_AS_REPLACE_AS, false) \
    X(BgpNeighborSession, LOCAL_AS_DUAL_AS, false) \
    X(BgpNeighborSession, SHUTDOWN, false) \
    X(BgpNeighborSession, TRANSPORT_MULTI_SESSION, false) \
    X(BgpNeighborSession, TTL_SEC, false) \
    X(BgpNeighborSession, TTL_SEC_HOP, 1)

CONFIG_DEFAULT_TABLE(BGP_NEIGHBOR_SESSION_DEFAULTS);

void BgpNeighborSessionShutdown(void*);
void BgpNeighborSessionPathAttribute(void*);
void BgpNeighborSessionRestart(void*);
void BgpNeighborSessionRemoteAs(void*);
void BgpNeighborSessionLocalAsPrepend(void*);

struct BgpNeighborSessionFields : FieldTuple<
    RegistryContainer<BgpBaseRegistry CONFIG_INDEX_ARG(BgpNeighborSession::BGP_BASE)>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpNeighborSession::DESCRIPTION)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::DISABLE_CONNECTION_CHECK)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::EBGP_MULTIHOP), BgpNeighborSessionRestart>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpNeighborSession::EBGP_MAX_HOP_COUNT), BgpNeighborSessionRestart>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::FALL_OVER)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::FALL_OVER_BFD_CHECK_CONTROL_PLANE_FAILURE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::FALL_OVER_BFD_MULTI_HOP)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::FALL_OVER_BFD_SINGLE_HOP)>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpNeighborSession::FALL_OVER_ROUTE_MAP)>,
    OptionalAtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::HAMODE_GRACEFUL_RESTART), BgpNeighborSessionRestart>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpNeighborSession::INHERIT_PEER_SESSION)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::LOCAL_AS), BgpNeighborSessionRestart>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(BgpNeighborSession::LOCAL_AS_AS), BgpNeighborSessionRestart>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::LOCAL_AS_NO_PREPEND), BgpNeighborSessionLocalAsPrepend>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::LOCAL_AS_REPLACE_AS), BgpNeighborSessionLocalAsPrepend>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::LOCAL_AS_DUAL_AS), BgpNeighborSessionRestart>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpNeighborSession::PASSWORD), BgpNeighborSessionRestart>,
    ListField<std::tuple<
        bool,    // true = discard, false = treat-as-withdraw
        uint8_t, // start
        uint8_t  // end
    > CONFIG_INDEX_ARG(BgpNeighborSession::PATH_ATTRIBUTE), BgpNeighborSessionPathAttribute>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpNeighborSession::PEER_GROUP)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(BgpNeighborSession::REMOTE_AS), BgpNeighborSessionRemoteAs>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::SHUTDOWN), BgpNeighborSessionShutdown>,
    OptionalAtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::TRANSPORT_CONNECTION_MODE), BgpNeighborSessionRestart>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::TRANSPORT_MULTI_SESSION), BgpNeighborSessionRestart>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpNeighborSession::TTL_SEC), BgpNeighborSessionRestart>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpNeighborSession::TTL_SEC_HOP), BgpNeighborSessionRestart>,
    OwnedListField<BgpNeighborRegistry, uint32_t CONFIG_INDEX_ARG(BgpNeighborSession::AF_NEIGHBOR)>
> {};

/**
 * @brief Registry slot for BGP session-level neighbor configuration.
 * @ingroup BGP
 */
struct BgpNeighborSessionRegistry : SubRegistry<BgpNeighborSessionRegistry, BgpNeighborSession, nullptr, BgpNeighborSessionFields> {};

/**
 * @brief Process-level BGP address-family configuration fields (network statements, redistribution, best-path).
 * @ingroup BGP
 */
enum class BgpAddressFamily
{
    AF_BASE,

    // Adding/removing/changing an aggregate re-triggers aggregate computation: withdraw
    // the old aggregate (and un-suppress its more-specifics) and install/advertise the new one.
    AGGREGATE_ADDRESS,

    // Enable/disable installing additional paths (beyond the single best) into the local RIB.
    BGP_ADDITIONAL_PATHS_INSTALL, // TODO

    // Changes how many additional paths are selected as install/advertise candidates;
    // recompute the additional-paths candidate set for every peer in this AF.
    BGP_ADDITIONAL_PATHS_SELECT, // TODO

    // Recompute the additional-paths candidate set (backup path) for every peer in this AF.
    BGP_ADDITIONAL_PATHS_SELECT_BACKUP,

    // Recompute the additional-paths candidate set (best-external) for every peer in this AF.
    BGP_ADDITIONAL_PATHS_SELECT_BEST_EXTERNAL,

    // Reschedule the periodic aggregate-recomputation timer with the new delay.
    BGP_AGGREGATE_TIMER,

    // Best-path tiebreaker step (compare BGP router ID as a last resort, RFC 4271
    // section 9.1.2.2); rerun best-path selection over every prefix in this AF's RIB.
    BGP_BEST_PATH_COMPARE_ROUTER_ID,

    // Changes whether the (non-transitive) cost extended community is used as a
    // best-path tiebreaker; rerun best-path selection over every prefix.
    BGP_BEST_PATH_COST_COMMUNITY_IGNORE, // TODO

    // Changes whether the IGP metric to the next hop is used as a best-path tiebreaker;
    // rerun best-path selection over every prefix.
    BGP_BEST_PATH_IGP_METRIC_IGNORE,

    // Changes whether MED is compared only among paths from the same confederation
    // sub-AS; rerun best-path selection over every prefix.
    BGP_BEST_PATH_MED_CONFED, // TODO

    // Changes whether a missing MED attribute is treated as the worst value instead of
    // the best (0); rerun best-path selection over every prefix.
    BGP_BEST_PATH_MED_MISSING_AS_WORST,

    // Changes whether RPKI Invalid routes remain eligible for best-path selection;
    // rerun best-path selection over every prefix.
    BGP_BEST_PATH_PREFIX_VALIDATE_ALLOW_INVALID, // TODO

    // Enabling: start tracking flap statistics and applying suppression for this AF.
    // Disabling: clear all penalty/suppression state and un-suppress any suppressed routes.
    BGP_DAMPENING,

    // Only changes the decay rate applied to a route's penalty going forward; does not
    // retroactively change the current penalty or suppression state of any route.
    BGP_DAMPENING_HALF_LIFE,

    // Only affects the reuse decision the next time a suppressed route's decaying
    // penalty is checked; does not immediately re-evaluate already-suppressed routes.
    BGP_DAMPENING_REUSE_THRESHOLD,

    // Only affects future flap events; does not retroactively suppress or un-suppress
    // routes already past their penalty check.
    BGP_DAMPENING_SUPPRESS_THRESHOLD,

    // Only bounds the ceiling used for future penalty decay calculations; no immediate
    // effect on currently-suppressed routes.
    BGP_DAMPENING_MAXIMUM_SUPPRESS_TIME,

    // Changes which routes are eligible for dampening; re-evaluate the route-map against
    // Adj-RIB-In on the next update for this AF (does not retroactively touch existing
    // penalty state for routes no longer matched).
    BGP_DAMPENING_ROUTE_MAP, // TODO

    // Recompute Adj-RIB-Out for every peer in this AF: adds/removes the DMZ-link-bandwidth
    // extended community on advertised routes.
    BGP_DMZLINK_BW, // TODO

    // Changes which routes are conditionally injected as more-specifics of an aggregate;
    // re-run the inject-map evaluation and update the RIB/Adj-RIB-Out accordingly.
    BGP_INJECT_MAP, // TODO

    // Changes the exist-map condition gating BGP_INJECT_MAP; re-run inject-map evaluation.
    BGP_INJECT_MAP_EXIST_MAP, // TODO

    // Changes whether injected routes copy attributes from the covering aggregate;
    // re-run inject-map evaluation and reinstall affected routes.
    BGP_INJECT_MAP_COPY_ATTRIBUTES, // TODO

    // Route-map changes which next hops are eligible for next-hop tracking or how they're
    // rewritten; re-evaluate next-hop reachability for affected routes.
    BGP_NEXT_HOP_ROUTE_MAP, // TODO

    // Reschedule the pending next-hop-tracking reaction timer with the new delay.
    BGP_NEXT_HOP_TRIGGER_DELAY,

    // Enabling: start watching the RIB for next-hop reachability changes and rerun
    // best-path selection when a next hop's reachability changes. Disabling: stop
    // watching and treat all next hops as reachable (clear any suppression from unreachable
    // next hops).
    BGP_NEXT_HOP_TRACKING,

    // Changes whether a host route to a recursively-resolved next hop is installed;
    // reinstall/remove that host route for existing recursive next hops in this AF.
    BGP_RECURSIVE_HOST,

    // Changes whether iBGP-learned routes are eligible for redistribution into other
    // protocols; re-run redistribution for this AF.
    BGP_REDISTRIBUTE_INTERNAL, // TODO

    // Changes route-map evaluation order relative to other outbound filters; re-run
    // outbound policy for every peer in this AF.
    BGP_ROUTE_MAP_PRIORITY, // TODO

    // Enabling: start retaining a soft-reconfiguration backup Adj-RIB-In for peers in this
    // AF that have SOFT_RECONFIGURATION set. Disabling: free the retained backup copies.
    BGP_SOFT_RECONFIG_BACKUP, // TODO

    // Changes the MED applied to locally-originated routes (network/aggregate/redistribute)
    // with no MED already set; reinstall those routes and rerun best-path selection.
    DEFAULT_METRIC,

    // Per-prefix administrative-distance override; reinstall matching routes into the
    // global RIB with the new distance so downstream (non-BGP) preference is correct.
    DISTANCE_RANGE,

    // Changes the administrative distance used for eBGP-learned routes; reinstall
    // affected routes into the global RIB with the new distance.
    DISTANCE_BGP_EXTERNAL,

    // Changes the administrative distance used for iBGP-learned routes; reinstall
    // affected routes into the global RIB with the new distance.
    DISTANCE_BGP_INTERNAL,

    // Changes the administrative distance used for locally-originated routes; reinstall
    // affected routes into the global RIB with the new distance.
    DISTANCE_BGP_LOCAL,

    // Same as DISTANCE_BGP_EXTERNAL, for the multicast SAFI.
    DISTANCE_MBGP_EXTERNAL,

    // Same as DISTANCE_BGP_INTERNAL, for the multicast SAFI.
    DISTANCE_MBGP_INTERNAL,

    // Same as DISTANCE_BGP_LOCAL, for the multicast SAFI.
    DISTANCE_MBGP_LOCAL,

    // Re-run inbound policy over Adj-RIB-In (access-list) for every peer in this AF.
    DISTRIBUTE_LIST_IN, // TODO

    // Re-run inbound policy over Adj-RIB-In (interface match) for every peer in this AF.
    DISTRIBUTE_LIST_IN_INTERFACE, // TODO

    // Changes whether DISTRIBUTE_LIST_IN is matched as a prefix-list instead of an
    // access-list; re-run inbound policy.
    DISTRIBUTE_LIST_IN_PREFIX, // TODO

    // Re-run outbound policy / recompute Adj-RIB-Out (access-list) for every peer in this AF.
    DISTRIBUTE_LIST_OUT, // TODO

    // Re-run outbound policy / recompute Adj-RIB-Out (interface match) for every peer.
    DISTRIBUTE_LIST_OUT_INTERFACE, // TODO

    // Changes whether DISTRIBUTE_LIST_OUT is matched as a prefix-list instead of an
    // access-list; re-run outbound policy.
    DISTRIBUTE_LIST_OUT_PREFIX, // TODO

    // Changes gateway-address matching applied alongside the distribute list; re-run
    // inbound policy over Adj-RIB-In.
    DISTRIBUTE_LIST_GATEWAY, // TODO

    // Changes how many eBGP paths are eligible for multipath; rerun best-path selection
    // (multipath set) over every prefix and reinstall the RIB.
    MAXIMUM_PATHS_EBGP,

    // Changes how many iBGP paths are eligible for multipath; rerun best-path selection
    // (multipath set) over every prefix and reinstall the RIB.
    MAXIMUM_PATHS_IBGP,

    // Adding/removing a network statement originates/withdraws that route as a
    // locally-originated candidate and reruns best-path selection for the prefix.
    NETWORK,

    // Route-map rewrites attributes before installing into the routing table; reinstall
    // affected routes with the new attributes.
    TABLE_MAP, // TODO

    // Changes whether routes denied by TABLE_MAP are filtered out of the RIB entirely
    // instead of installed unmodified; reinstall/remove affected routes.
    TABLE_MAP_FILTER, // TODO

    COUNT
};

#define BGP_ADDRESS_FAMILY_DEFAULTS(X) \
    X(BgpAddressFamily, BGP_ADDITIONAL_PATHS_INSTALL, false) \
    X(BgpAddressFamily, BGP_ADDITIONAL_PATHS_SELECT_BACKUP, false) \
    X(BgpAddressFamily, BGP_ADDITIONAL_PATHS_SELECT_BEST_EXTERNAL, false) \
    X(BgpAddressFamily, BGP_AGGREGATE_TIMER, 30) \
    X(BgpAddressFamily, BGP_BEST_PATH_COMPARE_ROUTER_ID, false) \
    X(BgpAddressFamily, BGP_BEST_PATH_COST_COMMUNITY_IGNORE, false) \
    X(BgpAddressFamily, BGP_BEST_PATH_IGP_METRIC_IGNORE, false) \
    X(BgpAddressFamily, BGP_BEST_PATH_MED_CONFED, false) \
    X(BgpAddressFamily, BGP_BEST_PATH_MED_MISSING_AS_WORST, false) \
    X(BgpAddressFamily, BGP_BEST_PATH_PREFIX_VALIDATE_ALLOW_INVALID, false) \
    X(BgpAddressFamily, BGP_DAMPENING, false) \
    X(BgpAddressFamily, BGP_DAMPENING_HALF_LIFE, 15) \
    X(BgpAddressFamily, BGP_DAMPENING_REUSE_THRESHOLD, 750) \
    X(BgpAddressFamily, BGP_DAMPENING_SUPPRESS_THRESHOLD, 2000) \
    X(BgpAddressFamily, BGP_DAMPENING_MAXIMUM_SUPPRESS_TIME, 60) \
    X(BgpAddressFamily, BGP_DMZLINK_BW, false) \
    X(BgpAddressFamily, BGP_INJECT_MAP_COPY_ATTRIBUTES, false) \
    X(BgpAddressFamily, BGP_NEXT_HOP_TRACKING, true) \
    X(BgpAddressFamily, BGP_RECURSIVE_HOST, true) \
    X(BgpAddressFamily, BGP_REDISTRIBUTE_INTERNAL, false) \
    X(BgpAddressFamily, BGP_ROUTE_MAP_PRIORITY, false) \
    X(BgpAddressFamily, BGP_SOFT_RECONFIG_BACKUP, false) \
    X(BgpAddressFamily, DISTANCE_BGP_EXTERNAL, 20) \
    X(BgpAddressFamily, DISTANCE_BGP_INTERNAL, 200) \
    X(BgpAddressFamily, DISTANCE_BGP_LOCAL, 200) \
    X(BgpAddressFamily, DISTANCE_MBGP_EXTERNAL, 20) \
    X(BgpAddressFamily, DISTANCE_MBGP_INTERNAL, 200) \
    X(BgpAddressFamily, DISTANCE_MBGP_LOCAL, 200) \
    X(BgpAddressFamily, DISTRIBUTE_LIST_IN_PREFIX, false) \
    X(BgpAddressFamily, DISTRIBUTE_LIST_OUT_PREFIX, false) \
    X(BgpAddressFamily, MAXIMUM_PATHS_EBGP, 1) \
    X(BgpAddressFamily, MAXIMUM_PATHS_IBGP, 1) \
    X(BgpAddressFamily, TABLE_MAP_FILTER, false)

CONFIG_DEFAULT_TABLE(BGP_ADDRESS_FAMILY_DEFAULTS);

#define BGP_AGGREGATE_ADDRESS_FIELDS(X) \
    X(types::IPPrefix,    prefix) \
    X(std::string, advertiseMap) \
    X(bool,        asConfedSet) \
    X(std::string, attributeMap) \
    X(std::string, routeMap) \
    X(bool,        summaryOnly) \
    X(std::string, suppressMap)

DEFINE_TUPLE_SCHEMA(BgpAggregateAddress, BGP_AGGREGATE_ADDRESS_FIELDS)

// Process-level address-family appliers. These fire with the AddressFamilyInstance
// (via routing::bgp::AfInstanceBase) as context; each marks a dirty category that the
// instance's debounced flush recomputes.
void BgpAfBestPath(void*);
void BgpAfMaxPaths(void*);
void BgpAfDistance(void*);
void BgpAfDampening(void*);
void BgpAfAggregate(void*);
void BgpAfNetwork(void*);
void BgpAfAddPathSelect(void*);

struct BgpAddressFamilyFields : FieldTuple<
    RegistryContainer<BgpAfBaseRegistry CONFIG_INDEX_ARG(BgpAddressFamily::AF_BASE)>,
    ListField<BgpAggregateAddress::Tuple CONFIG_INDEX_ARG(BgpAddressFamily::AGGREGATE_ADDRESS), BgpAfAggregate>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_ADDITIONAL_PATHS_INSTALL), BgpAfAddPathSelect>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::BGP_ADDITIONAL_PATHS_SELECT), BgpAfAddPathSelect>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_ADDITIONAL_PATHS_SELECT_BACKUP), BgpAfAddPathSelect>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_ADDITIONAL_PATHS_SELECT_BEST_EXTERNAL), BgpAfAddPathSelect>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(BgpAddressFamily::BGP_AGGREGATE_TIMER)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_BEST_PATH_COMPARE_ROUTER_ID), BgpAfBestPath>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_BEST_PATH_COST_COMMUNITY_IGNORE), BgpAfBestPath>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_BEST_PATH_IGP_METRIC_IGNORE), BgpAfBestPath>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_BEST_PATH_MED_CONFED), BgpAfBestPath>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_BEST_PATH_MED_MISSING_AS_WORST), BgpAfBestPath>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_BEST_PATH_PREFIX_VALIDATE_ALLOW_INVALID), BgpAfBestPath>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_DAMPENING), BgpAfDampening> ,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::BGP_DAMPENING_HALF_LIFE), BgpAfDampening>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(BgpAddressFamily::BGP_DAMPENING_REUSE_THRESHOLD), BgpAfDampening>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(BgpAddressFamily::BGP_DAMPENING_SUPPRESS_THRESHOLD), BgpAfDampening>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::BGP_DAMPENING_MAXIMUM_SUPPRESS_TIME), BgpAfDampening>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpAddressFamily::BGP_DAMPENING_ROUTE_MAP)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_DMZLINK_BW)>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpAddressFamily::BGP_INJECT_MAP)>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpAddressFamily::BGP_INJECT_MAP_EXIST_MAP)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_INJECT_MAP_COPY_ATTRIBUTES)>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpAddressFamily::BGP_NEXT_HOP_ROUTE_MAP)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(BgpAddressFamily::BGP_NEXT_HOP_TRIGGER_DELAY)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_NEXT_HOP_TRACKING)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_RECURSIVE_HOST)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_REDISTRIBUTE_INTERNAL)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_ROUTE_MAP_PRIORITY)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::BGP_SOFT_RECONFIG_BACKUP)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(BgpAddressFamily::DEFAULT_METRIC), BgpAfDistance>,
    ListField<std::tuple<uint8_t, std::vector<std::tuple<types::IPPrefix, std::string>>> CONFIG_INDEX_ARG(BgpAddressFamily::DISTANCE_RANGE), BgpAfDistance>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::DISTANCE_BGP_EXTERNAL), BgpAfDistance>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::DISTANCE_BGP_INTERNAL), BgpAfDistance>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::DISTANCE_BGP_LOCAL), BgpAfDistance>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::DISTANCE_MBGP_EXTERNAL), BgpAfDistance>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::DISTANCE_MBGP_INTERNAL), BgpAfDistance>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::DISTANCE_MBGP_LOCAL), BgpAfDistance>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpAddressFamily::DISTRIBUTE_LIST_IN)>,
    OptionalAtomicField<interface::InterfaceKey CONFIG_INDEX_ARG(BgpAddressFamily::DISTRIBUTE_LIST_IN_INTERFACE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::DISTRIBUTE_LIST_IN_PREFIX)>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpAddressFamily::DISTRIBUTE_LIST_OUT)>,
    OptionalAtomicField<interface::InterfaceKey CONFIG_INDEX_ARG(BgpAddressFamily::DISTRIBUTE_LIST_OUT_INTERFACE)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::DISTRIBUTE_LIST_OUT_PREFIX)>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpAddressFamily::DISTRIBUTE_LIST_GATEWAY)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::MAXIMUM_PATHS_EBGP), BgpAfMaxPaths>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(BgpAddressFamily::MAXIMUM_PATHS_IBGP), BgpAfMaxPaths>,
    ListField<std::tuple<types::IPPrefix, bool, std::string> CONFIG_INDEX_ARG(BgpAddressFamily::NETWORK), BgpAfNetwork>,
    ValueField<std::string CONFIG_INDEX_ARG(BgpAddressFamily::TABLE_MAP)>,
    AtomicField<bool CONFIG_INDEX_ARG(BgpAddressFamily::TABLE_MAP_FILTER)>
> {};

/**
 * @brief Registry slot for process-level BGP address-family configuration.
 * @ingroup BGP
 */
struct BgpAddressFamilyRegistry : SubRegistry<BgpAddressFamilyRegistry, BgpAddressFamily, nullptr, BgpAddressFamilyFields> {};

/**
 * @brief Top-level BGP process configuration fields.
 * @ingroup BGP
 */
enum class Bgp
{
    // Sent as the local AS in every session's OPEN message; every existing session
    // must be reset to renegotiate under the new AS.
    // FSM: RESTART fanned out to every session.
    AUTONOMOUS_SYSTEM,

    BGP_BASE,
    ADDRESS_FAMILIES,

    // Changes whether MED is compared between paths learned from different neighboring
    // ASes; rerun best-path selection over every prefix in every AF.
    BGP_ALWAYS_COMPARE_MED,

    // Cosmetic display-only setting (asplain vs. asdot AS-number formatting); no
    // protocol or state effect.
    BGP_AS_DOT_NOTATION, // TODO

    // Route-reflector behavior (RFC 4456): changes whether reflected routes are passed
    // between clients of the same cluster; rerun outbound policy for all RR clients.
    BGP_CLIENT_TO_CLIENT_REFLECTION,

    // Changes the CLUSTER_ID attribute this reflector stamps on reflected routes;
    // re-advertise all currently-reflected routes with the new CLUSTER_ID (also affects
    // reflection loop detection on receipt).
    BGP_CLUSTER_ID,

    // Confederation identifier (RFC 5065) is the AS number presented to eBGP peers
    // outside the confederation, in OPEN and as the outermost AS_PATH hop; every
    // session (both to confederation members and to external eBGP peers) must be
    // reset to renegotiate/re-advertise under the new identifier.
    // FSM: RESTART fanned out to every session.
    BGP_CONFEDERATION_IDENTIFIER,

    // Changes which neighboring ASes are treated as confederation members (AS_CONFED
    // segments) vs. true eBGP; every affected session must be reset.
    // FSM: RESTART fanned out to every affected session.
    BGP_CONFEDERATION_PEERS,

    // Reschedules the periodic consistency-checker error-logging timer; no session impact.
    BGP_CONSISTENCY_CHECKER_ERROR_MESSAGE_INTERVAL, // base // TODO

    // Changes whether MED comparison groups candidates by neighboring AS before
    // comparing; rerun best-path selection over every prefix in every AF.
    BGP_DETERMINISTIC_MED,

    // Recompute Adj-RIB-Out for every peer: adds/removes the DMZ-link-bandwidth
    // extended community on advertised routes.
    BGP_DMZLINK_BW, // both // TODO

    // Changes whether an UPDATE with the receiver's own AS first in AS_PATH is rejected;
    // re-run inbound policy over Adj-RIB-In for every eBGP peer.
    BGP_ENFORCE_FIRST_AS,

    // Enhanced error handling (RFC 7606) capability negotiated in OPEN; every session
    // must be reset to renegotiate.
    // FSM: RESTART fanned out to every session.
    BGP_ENHANCED_ERROR, // base // TODO

    // Changes whether losing the local interface to an eBGP peer immediately tears the
    // session down instead of waiting for hold-timer expiry; (un)register the interface
    // down/up event hook for every directly-connected eBGP session.
    // FSM: indirect — no immediate postEvent, only fires TCP_CONNECTION_FAILS later via the hook.
    BGP_FAST_EXTERNAL_FAILOVER, // base // TODO

    // Graceful Restart is a BGP capability (RFC 4724) negotiated in OPEN; every session
    // must be reset so the capability is renegotiated.
    // FSM: RESTART fanned out to every session.
    BGP_GRACEFUL_RESTART, // base // TODO

    // Long-lived/extended graceful restart is negotiated as part of the same OPEN
    // capability; every session must be reset.
    // FSM: RESTART fanned out to every session.
    BGP_GRACEFUL_RESTART_EXTENDED, // base // TODO

    // Restart-time is advertised in the Graceful Restart capability; every session must
    // be reset to advertise the new value.
    // FSM: RESTART fanned out to every session.
    BGP_GRACEFUL_RESTART_RESTART_TIME, // base // TODO

    // Only used the next time this router itself restarts (governs how long stale
    // routes from peers are retained); no action on running sessions.
    BGP_GRACEFUL_RESTART_STALEPATH_TIME, // base // TODO

    // Changes which globally-redistributed/aggregate routes are conditionally injected;
    // re-run inject-map evaluation across all AFs that reference it.
    BGP_INJECT_MAP, // both // TODO

    // Changes the exist-map condition gating BGP_INJECT_MAP; re-run inject-map evaluation.
    BGP_INJECT_MAP_EXIST_MAP, // both // TODO

    // Changes whether injected routes copy attributes from the covering aggregate;
    // re-run inject-map evaluation and reinstall affected routes.
    BGP_INJECT_MAP_COPY_ATTRIBUTES, // both // TODO

    // Enabling: open the passive TCP listen socket for dynamic neighbor acceptance.
    // Disabling: close the listen socket (existing sessions from already-known
    // neighbors are unaffected).
    // FSM: none — this is not a per-session postEvent, it just opens/closes the listen socket.
    BGP_LISTEN,

    // Only checked against the current count of dynamically-accepted sessions when a
    // new connection arrives; does not affect already-accepted sessions.
    BGP_LISTEN_LIMIT,

    // Changes which source addresses are matched against a peer-group template for
    // dynamic neighbor acceptance; checked only on the next incoming connection.
    BGP_LISTEN_RANGE,

    // Enable/disable emitting a log message on neighbor state transitions; no protocol effect.
    BGP_LOG_NEIGHBOR_CHANGES, // base // TODO

    // Changes the AS_PATH length limit enforced on incoming UPDATEs; re-run inbound
    // policy over Adj-RIB-In for every peer.
    BGP_MAX_AS_LIMIT,

    // Changes the COMMUNITY attribute count limit enforced on incoming UPDATEs; re-run
    // inbound policy over Adj-RIB-In for every peer.
    BGP_MAX_COMMUNITY_LIMIT,

    // Changes the EXTENDED_COMMUNITY attribute count limit enforced on incoming
    // UPDATEs; re-run inbound policy over Adj-RIB-In for every peer.
    BGP_MAX_EXT_COMMUNITY_LIMIT,

    // Only used the next time a session establishes after a cold boot; no action on
    // running sessions.
    BGP_NOPEERUP_DELAY_COLD_BOOT, // base // TODO

    // Only used the next time a session establishes after an NSF switchover; no action
    // on running sessions.
    BGP_NOPEERUP_DELAY_NSF_SWITCHOVER, // base // TODO

    // Only used the next time a session establishes after process startup; no action on
    // running sessions.
    BGP_NOPEERUP_DELAY_POST_BOOT, // base // TODO

    // Only used the next time a session establishes after a user-initiated restart; no
    // action on running sessions.
    BGP_NOPEERUP_DELAY_USER_INITIATED, // base // TODO

    // Reschedule the pending end-of-RIB timeout timer with the new value for any
    // session currently in graceful-restart recovery.
    BGP_REFRESH_MAX_EOR_TIME,

    // Reschedule the pending stale-path retention timer with the new value for any
    // session currently in graceful-restart recovery.
    BGP_REFRESH_STALEPATH_TIME,

    // Changes internal regex-engine determinism for AS_PATH/community matching; no
    // protocol effect, purely an implementation/performance detail.
    BGP_REGEX_DETERMINISTIC, // TODO

    // Router ID is exchanged in every session's OPEN message and used for route-reflection
    // and confederation loop detection; every existing session must be reset.
    // FSM: RESTART fanned out to every session.
    BGP_ROUTER_ID,

    // Changes which RPKI cache server(s) validation data is fetched from; tear down the
    // current RPKI-to-router session and reconnect to the new server(s), then
    // re-run inbound policy over Adj-RIB-In using the refreshed validation state.
    BGP_RPKI_SERVER, // base // TODO

    // Reschedules the periodic scanner timer (next-hop tracking / dampening decay
    // sweep) with the new interval.
    BGP_SCAN_TIME,

    // Changes whether routes with an unreachable next hop are excluded from best-path
    // selection; rerun best-path selection over every prefix in every AF.
    BGP_SUPPRESS_INACTIVE,

    // Reschedules the pending initial-convergence delay timer (if the process is still
    // within its post-startup update-delay window) with the new value.
    BGP_UPDATE_DELAY,

    // Adding a neighbor starts a new session (connect/listen for it); removing one tears
    // its session down and withdraws all its routes. Other neighbors are unaffected.
    // FSM: MANUAL_START / MANUAL_START_PASSIVE_TCP on add (BgpProcess::startActiveSession /
    // startPassiveSession); MANUAL_STOP on remove (BgpProcess::shutdownNeighbor).
    NEIGHBOR,

    // Adding/removing a peer-group itself has no direct session effect; changing a
    // peer-group's own settings requires rebuilding the effective config (and possibly
    // resetting the session) for every neighbor that references it via PEER_GROUP.
    // FSM: indirect — no fixed event, depends on which inherited fields actually changed.
    PEER_GROUP,

    // Changes which routing-context/VRF route-server clients are evaluated against;
    // rebuild the effective config and re-run Adj-RIB-In/Out for all route-server clients.
    ROUTE_SERVER_CONTEXT, // base // TODO

    // Changes to a peer-policy template propagate to every neighbor/AF that inherits
    // from it via INHERIT_PEER_POLICY; re-run inbound/outbound policy for each.
    TEMPLATE_PEER_POLICY, // base // TODO

    // Changes to a peer-session template propagate to every neighbor that inherits from
    // it via INHERIT_PEER_SESSION; rebuild the effective session config (may require a
    // session reset) for each.
    TEMPLATE_PEER_SESSION, // base // TODO

    COUNT
};

#define BGP_DEFAULTS(X) \
    X(Bgp, BGP_ALWAYS_COMPARE_MED, false) \
    X(Bgp, BGP_AS_DOT_NOTATION, false) \
    X(Bgp, BGP_CLIENT_TO_CLIENT_REFLECTION, true) \
    X(Bgp, BGP_CONSISTENCY_CHECKER_ERROR_MESSAGE_INTERVAL, 60) \
    X(Bgp, BGP_DETERMINISTIC_MED, false) \
    X(Bgp, BGP_DMZLINK_BW, false) \
    X(Bgp, BGP_ENFORCE_FIRST_AS, true) \
    X(Bgp, BGP_ENHANCED_ERROR, false) \
    X(Bgp, BGP_FAST_EXTERNAL_FAILOVER, true) \
    X(Bgp, BGP_GRACEFUL_RESTART, false) \
    X(Bgp, BGP_GRACEFUL_RESTART_EXTENDED, false) \
    X(Bgp, BGP_GRACEFUL_RESTART_RESTART_TIME, 120) \
    X(Bgp, BGP_GRACEFUL_RESTART_STALEPATH_TIME, 360) \
    X(Bgp, BGP_INJECT_MAP_COPY_ATTRIBUTES, false) \
    X(Bgp, BGP_LISTEN, false) \
    X(Bgp, BGP_LOG_NEIGHBOR_CHANGES, false) \
    X(Bgp, BGP_REFRESH_MAX_EOR_TIME, 60) \
    X(Bgp, BGP_REFRESH_STALEPATH_TIME, 120) \
    X(Bgp, BGP_REGEX_DETERMINISTIC, true) \
    X(Bgp, BGP_SCAN_TIME, 60) \
    X(Bgp, BGP_SUPPRESS_INACTIVE, false)

CONFIG_DEFAULT_TABLE(BGP_DEFAULTS);

void BgpProcNeighbors(void*);
void BgpProcAddressFamilies(void*);
void BgpProcBestPath(void*);
void BgpProcInbound(void*);
void BgpProcReflection(void*);
void BgpProcRestart(void*);
void BgpProcConfed(void*);

struct BgpFields : FieldTuple<
    AtomicField<uint32_t CONFIG_INDEX_ARG(Bgp::AUTONOMOUS_SYSTEM), BgpProcConfed>,
    RegistryContainer<BgpBaseRegistry CONFIG_INDEX_ARG(Bgp::BGP_BASE)>,
    OwnedListField<BgpAddressFamilyRegistry, uint32_t CONFIG_INDEX_ARG(Bgp::ADDRESS_FAMILIES), BgpProcAddressFamilies>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_ALWAYS_COMPARE_MED), BgpProcBestPath>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_AS_DOT_NOTATION)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_CLIENT_TO_CLIENT_REFLECTION), BgpProcReflection>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Bgp::BGP_CLUSTER_ID), BgpProcReflection>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Bgp::BGP_CONFEDERATION_IDENTIFIER), BgpProcConfed>,
    ListField<uint32_t CONFIG_INDEX_ARG(Bgp::BGP_CONFEDERATION_PEERS), BgpProcConfed>,
    AtomicField<uint32_t CONFIG_INDEX_ARG(Bgp::BGP_CONSISTENCY_CHECKER_ERROR_MESSAGE_INTERVAL)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_DETERMINISTIC_MED), BgpProcBestPath>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_DMZLINK_BW)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_ENFORCE_FIRST_AS), BgpProcInbound>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_ENHANCED_ERROR)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_FAST_EXTERNAL_FAILOVER)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_GRACEFUL_RESTART), BgpProcRestart>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_GRACEFUL_RESTART_EXTENDED), BgpProcRestart>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_GRACEFUL_RESTART_RESTART_TIME), BgpProcRestart>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_GRACEFUL_RESTART_STALEPATH_TIME)>,
    ValueField<std::string CONFIG_INDEX_ARG(Bgp::BGP_INJECT_MAP)>,
    ValueField<std::string CONFIG_INDEX_ARG(Bgp::BGP_INJECT_MAP_EXIST_MAP)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_INJECT_MAP_COPY_ATTRIBUTES)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_LISTEN)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_LISTEN_LIMIT)>,
    ListField<std::tuple<uint32_t, uint32_t, std::string> CONFIG_INDEX_ARG(Bgp::BGP_LISTEN_RANGE)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_LOG_NEIGHBOR_CHANGES)>,
    OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(Bgp::BGP_MAX_AS_LIMIT), BgpProcInbound>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_MAX_COMMUNITY_LIMIT), BgpProcInbound>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_MAX_EXT_COMMUNITY_LIMIT), BgpProcInbound>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_NOPEERUP_DELAY_COLD_BOOT)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_NOPEERUP_DELAY_NSF_SWITCHOVER)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_NOPEERUP_DELAY_POST_BOOT)>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_NOPEERUP_DELAY_USER_INITIATED)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_REFRESH_MAX_EOR_TIME)>,
    AtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_REFRESH_STALEPATH_TIME)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_REGEX_DETERMINISTIC)>,
    OptionalAtomicField<uint32_t CONFIG_INDEX_ARG(Bgp::BGP_ROUTER_ID), BgpProcRestart>,
    ListField<std::tuple<
        types::IPAddress,
        uint16_t, // port
        uint16_t, // refresh time
        std::string, // ssh username
        std::string // ssh password
    > CONFIG_INDEX_ARG(Bgp::BGP_RPKI_SERVER)>,
    AtomicField<uint8_t CONFIG_INDEX_ARG(Bgp::BGP_SCAN_TIME)>,
    AtomicField<bool CONFIG_INDEX_ARG(Bgp::BGP_SUPPRESS_INACTIVE), BgpProcBestPath>,
    OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(Bgp::BGP_UPDATE_DELAY)>,
    OwnedListField<BgpNeighborSessionRegistry, types::IPAddress CONFIG_INDEX_ARG(Bgp::NEIGHBOR), BgpProcNeighbors>,
    OwnedListField<BgpNeighborSessionRegistry, std::string CONFIG_INDEX_ARG(Bgp::PEER_GROUP)>,
    ValueField<std::string CONFIG_INDEX_ARG(Bgp::ROUTE_SERVER_CONTEXT)>,
    OwnedListField<BgpNeighborRegistry, std::string CONFIG_INDEX_ARG(Bgp::TEMPLATE_PEER_POLICY)>,
    OwnedListField<BgpNeighborSessionRegistry, std::string CONFIG_INDEX_ARG(Bgp::TEMPLATE_PEER_SESSION)>
> {};

/**
 * @brief Registry slot for a BGP process instance.
 * @ingroup BGP
 */
struct BgpRegistry : SubRegistry<BgpRegistry, Bgp, nullptr, BgpFields> {};
}

#endif // BGP_REGISTRY_H

