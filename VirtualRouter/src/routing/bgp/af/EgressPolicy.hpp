/**
 * @file EgressPolicy.hpp
 * @brief Reusable BGP egress path-attribute transforms for one address family.
 * @ingroup BGP_AF
 *
 * Holds the self-contained "shape the outbound attributes for this session" logic that
 * was previously inlined in @ref AddressFamilyInstance. Splitting it out lets the
 * address-family instance and the debounced config-apply path share one implementation
 * of group/member egress policy, aggregate egress policy, and ORF filtering.
 *
 * The transforms are pure with respect to instance RIB state: they read scope config
 * and the target @ref Session, and produce/modify a @ref PathAttribute. They depend only
 * on the owning scope and the AFI/SAFI this policy serves.
 */

#ifndef BGP_EGRESS_POLICY_HPP
#define BGP_EGRESS_POLICY_HPP

#include <algorithm>
#include <optional>
#include <vector>
#include <IPAddress.h>

#include "ScopeAccessor.h"
#include "configs/registry/router/BgpRegistry.h"
#include "bgp/BgpTypes.hpp"
#include "bgp/rib/RibTypes.hpp"
#include "bgp/session/Session.h"
#include "bgp/neighbor/NeighborAf.h"
#include "bgp/neighbor/NeighborConfigs.hpp" // prependLocalAs reads session-level config fields
#include "bgp/neighbor/NeighborTable.h"

namespace routing::bgp
{

/**
 * @brief Returns the leading AS_SEQUENCE segment of an AS-PATH, inserting one if absent.
 *
 * Used by egress policy to prepend the local AS. When the AS-PATH is empty or
 * begins with a non-SEQUENCE segment, a new AS_SEQUENCE segment is prepended.
 *
 * @param attrs  Path attributes to modify in-place.
 * @return Reference to the (possibly newly inserted) AS_SEQUENCE segment.
 */
inline AsPathSegment& getAsSegment(Attributes& attrs)
{
    if (!attrs.asPath.empty() && attrs.asPath[0].segmentType == BGP_AS_SEQUENCE)
        return attrs.asPath[0];
    attrs.asPath.insert(attrs.asPath.begin(), AsPathSegment{BGP_AS_SEQUENCE, {}});
    return attrs.asPath.front();
}

/**
 * @brief Returns the leading AS_CONFED_SEQUENCE segment of an AS-PATH, inserting one if absent.
 *
 * Used by confederation egress policy to prepend the member AS. Mirrors
 * getAsSegment but targets the confederation segment type.
 *
 * @param attrs  Path attributes to modify in-place.
 * @return Reference to the (possibly newly inserted) AS_CONFED_SEQUENCE segment.
 */
inline AsPathSegment& getConfedAsSegment(Attributes& attrs)
{
    if (!attrs.asPath.empty() && attrs.asPath[0].segmentType == BGP_AS_CONFED_SEQUENCE)
        return attrs.asPath[0];
    attrs.asPath.insert(attrs.asPath.begin(), AsPathSegment{BGP_AS_CONFED_SEQUENCE, {}});
    return attrs.asPath.front();
}

/**
 * @brief Per-address-family egress path-attribute policy.
 * @ingroup BGP_AF
 *
 * Bound to the owning @ref BgpScope and the AFI/SAFI it serves. All methods are const:
 * they never mutate instance state, only the @ref PathAttribute passed to them (or the
 * copy they return), so a single instance can be shared by the AFI and its config-apply
 * flush.
 *
 * @tparam N  NLRI policy type; provides `N::Nlri`.
 */
template <typename N>
class EgressPolicy
{
public:
    using NlriT = typename N::Nlri;

    /// Default LOCAL_PREF stamped on iBGP updates that carry none (RFC 4271 5.1.5).
    static constexpr uint32_t kDefaultLocalPref = 100;

    EgressPolicy(BgpScope& s, AfiSafi fam) : scope(s), family(fam) {}

    /**
     * @brief Prepends the appropriate local AS to the leading AS_SEQUENCE for an eBGP peer.
     *
     * Honours the neighbor's local-as configuration:
     * - no local-as: prepend the confederation identifier (or plain local AS).
     * - `replace-as`: prepend only the configured local AS, hiding the real one.
     * - `no-prepend`: prepend nothing, leaving the AS-PATH as received.
     * - otherwise: prepend the real AS then the local AS, so the local AS leads.
     */
    void prependLocalAs(Attributes& attrs, const NeighborConfigs& sesCfgs) const
    {
        const uint32_t confedId = getConfedId();
        auto localAsField = sesCfgs.get<config::BgpNeighborSession::LOCAL_AS>();

        if (!localAsField.hasValue())
        {
            AsPathSegment& seg = getAsSegment(attrs);
            seg.asns.insert(seg.asns.begin(), confedId);
            return;
        }

        const auto localAsTuple = localAsField.load();
        const uint32_t localAs = localAsTuple.as();
        const auto& localAsProps = localAsTuple.props();

        if (localAsProps.test(config::bgp::BgpLocalAsProps::NO_PREPEND))
            return; // no-prepend: advertise the AS-PATH unchanged

        AsPathSegment& seg = getAsSegment(attrs);
        if (localAsProps.test(config::bgp::BgpLocalAsProps::REPLACE_AS))
            seg.asns.insert(seg.asns.begin(), localAs);
        else
        {
            seg.asns.insert(seg.asns.begin(), confedId);
            seg.asns.insert(seg.asns.begin(), localAs);
        }
    }

    /**
     * @brief Returns the configured cluster ID, falling back to the BGP router ID.
     *
     * @return Configured BGP_CLUSTER_ID, or the scope's router ID if not set.
     */
    uint32_t getClusterId() const
    {
        auto cidField = ScopeAccessor::getConfigs(scope).get<config::Bgp::BGP_CLUSTER_ID>();
        return cidField.hasValue() ? cidField.load() : ScopeAccessor::getRid(scope);
    }

    /**
     * @brief Returns the configured confederation identifier, falling back to the local AS.
     *
     * @return Configured BGP_CONFEDERATION_IDENTIFIER, or the local AS number if not set.
     */
    uint32_t getConfedId() const
    {
        auto cidField = ScopeAccessor::getConfigs(scope).get<config::Bgp::BGP_CONFEDERATION_IDENTIFIER>();
        return cidField.hasValue() ? cidField.load() : ScopeAccessor::getAsNum(scope);
    }

    /**
     * @brief Applies group-level egress transformations to a route's path attributes.
     *
     * The result is the same for all members of a peer group with the same eBGP/iBGP
     * classification and can therefore be cached and shared across group members.
     *
     * @param route    Best-path route to apply egress policy to.
     * @param session  Outbound session; determines eBGP/iBGP classification and LOCAL_AS config.
     * @return Modified PathAttribute, or `std::nullopt` if `route` has no path ID.
     */
    std::optional<PathAttribute> applyGroupEgressPolicy(const InboundRoute<NlriT>& route, const Session& session) const
    {
        if (!route.pathId.has_value())
            return std::nullopt;

        PathAttribute pa = route.getPathAttributes();

        if (session.neighbor.isEbgp())
        {
            auto& sesCfgs = session.getNeighborConfigs();

            pa.attrs.localPref    = std::nullopt;
            pa.attrs.originatorId = std::nullopt;
            pa.attrs.clusterList.clear();

            pa.attrs.asPath.erase(
                std::remove_if(pa.attrs.asPath.begin(), pa.attrs.asPath.end(),
                    [](const AsPathSegment& s) {
                        return s.segmentType == BGP_AS_CONFED_SEQUENCE ||
                               s.segmentType == BGP_AS_CONFED_SET;
                    }),
                pa.attrs.asPath.end());

            prependLocalAs(pa.attrs, sesCfgs);
        }
        else if (session.neighbor.isConfedEbgp())
        {
            // RR attributes; prepend local member AS as a new AS_CONFED_SEQUENCE entry.
            AsPathSegment& seg = getConfedAsSegment(pa.attrs);
            const uint32_t routerAs = ScopeAccessor::getAsNum(scope);
            seg.asns.insert(seg.asns.begin(), routerAs);
        }
        else
        {
            // RFC 4271 5.1.5: LOCAL_PREF is mandatory on iBGP updates.
            if (!pa.attrs.localPref.has_value())
                pa.attrs.localPref = kDefaultLocalPref;
        }

        return pa;
    }

    /**
     * @brief Applies per-member next-hop and community adjustments after group-level egress policy.
     *
     * @param pa      Path attributes to modify in-place (already processed by group policy).
     * @param route   Source inbound route (used to check originator identity for NEXT_HOP_SELF).
     * @param afNbr   AF-level neighbor state for this specific peer.
     * @param session Outbound session; provides the local socket address and eBGP flag.
     */
    void applyMemberNexthop(PathAttribute& pa, const InboundRoute<NlriT>& route,
                            const NeighborAf& afNbr, const Session& session) const
    {
        const auto& cfgs = afNbr.configs;

        if (session.neighbor.isEbgp())
        {
            // RFC 4271 5.1.3: an eBGP speaker advertises its own interface address
            // facing the peer, not the peer's address.
            if (!cfgs.get<config::BgpNeighbor::NEXT_HOP_UNCHANGED>().load() ||
                cfgs.get<config::BgpNeighbor::NEXT_HOP_SELF_ALL>().load())
                pa.path.nextHop = localAddress(session, pa.path.nextHop);

            // SEND_COMMUNITY: strip communities for eBGP unless explicitly enabled.
            bool sendStd = cfgs.get<config::BgpNeighbor::SEND_COMMUNITY>().load()
                        || cfgs.get<config::BgpNeighbor::SEND_COMMUNITY_BOTH>().load()
                        || cfgs.get<config::BgpNeighbor::SEND_COMMUNITY_STANDARD>().load();
            bool sendExt = cfgs.get<config::BgpNeighbor::SEND_COMMUNITY_EXTENDED>().load()
                        || cfgs.get<config::BgpNeighbor::SEND_COMMUNITY_BOTH>().load();

            if (!sendStd)
                pa.attrs.communities.clear();
            if (!sendExt)
                pa.attrs.extendedCommunities.clear();
            if (!sendStd && !sendExt)
                pa.attrs.largeCommunities.clear();

            stripPrivateAsIfConfigured(pa.attrs.asPath, afNbr);
        }
        else
        {
            if ((cfgs.get<config::BgpNeighbor::NEXT_HOP_SELF>().load() &&
                 route.sourceNeighbor &&
                 route.sourceNeighbor->getParent().getRouterId() != ScopeAccessor::getRid(scope)) ||
                cfgs.get<config::BgpNeighbor::NEXT_HOP_SELF_ALL>().load())
                pa.path.nextHop = localAddress(session, pa.path.nextHop);
        }
    }

    /**
     * @brief Applies the combined group and member egress policy for an ungrouped peer.
     *
     * @param route    Route to apply egress policy to.
     * @param session  Outbound session.
     * @return Fully-adjusted PathAttribute, or `std::nullopt` if the route has no path ID.
     */
    std::optional<PathAttribute> applyEgressPolicy(const InboundRoute<NlriT>& route, const Session& session) const
    {
        auto pa = applyGroupEgressPolicy(route, session);
        if (!pa.has_value())
            return std::nullopt;

        const NeighborAf& nbrAf = session.neighbor.getAfNeighbor(family);
        applyMemberNexthop(*pa, route, nbrAf, session);
        return pa;
    }

    /**
     * @brief Sets @p pa's NEXT_HOP, rewriting it to our own address when eBGP or
     *        `NEXT_HOP_SELF`/`NEXT_HOP_SELF_ALL` require it.
     *
     * For eBGP peers the next hop is always ours unless `NEXT_HOP_UNCHANGED` is
     * set (and `NEXT_HOP_SELF_ALL` doesn't override it). For iBGP peers the
     * source's next hop is kept unless `NEXT_HOP_SELF` applies to a route we
     * didn't originate ourselves, or `NEXT_HOP_SELF_ALL` is set.
     */
    void deriveNextHop(PathAttribute& pa, const PathAttribute& src,
                       const InboundRoute<NlriT>& route, const NeighborAf& afNbr,
                       const Session& session) const
    {
        const auto& cfgs = afNbr.configs;
        pa.path.nextHop = src.path.nextHop;

        if (session.neighbor.isEbgp())
        {
            if (!cfgs.get<config::BgpNeighbor::NEXT_HOP_UNCHANGED>().load() ||
                cfgs.get<config::BgpNeighbor::NEXT_HOP_SELF_ALL>().load())
                pa.path.nextHop = localAddress(session, pa.path.nextHop);
        }
        else
        {
            if ((cfgs.get<config::BgpNeighbor::NEXT_HOP_SELF>().load() &&
                 route.sourceNeighbor &&
                 route.sourceNeighbor->getParent().getRouterId() != ScopeAccessor::getRid(scope)) ||
                cfgs.get<config::BgpNeighbor::NEXT_HOP_SELF_ALL>().load())
                pa.path.nextHop = localAddress(session, pa.path.nextHop);
        }
    }

    /**
     * @brief Returns our own address on the session's connection, or @p fallback if unavailable.
     *
     * The next hop we advertise is the local end of the TCP connection to this peer
     * (RFC 4271 5.1.3). When the connection or its socket key is not available the
     * caller's current next hop is preserved rather than writing a bogus address.
     */
    static types::IPAddress localAddress(const Session& session, const types::IPAddress& fallback)
    {
        if (auto* conn = session.getPrimaryConnection())
            if (auto key = conn->socketKey())
                return key->local.address;
        return fallback;
    }

    /**
     * @brief Sets @p pa's AS_PATH, applying confederation stripping, AS prepend,
     *        and private-AS removal as configured.
     *
     * For eBGP peers, confederation segments are stripped, our own AS is
     * prepended (per `LOCAL_AS`/`REMOVE_PRIVATE_AS`), and private ASNs are
     * removed if configured. For confederation eBGP peers, a confederation
     * segment carrying our AS is prepended instead. iBGP peers get the
     * source's AS_PATH unchanged.
     */
    void deriveAsPath(PathAttribute& pa, const PathAttribute& src,
                      const NeighborAf& afNbr, const Session& session) const
    {
        pa.attrs.asPath = src.attrs.asPath;

        if (session.neighbor.isEbgp())
        {
            auto& sesCfgs = session.getNeighborConfigs();

            pa.attrs.asPath.erase(
                std::remove_if(pa.attrs.asPath.begin(), pa.attrs.asPath.end(),
                    [](const AsPathSegment& s) {
                        return s.segmentType == BGP_AS_CONFED_SEQUENCE ||
                               s.segmentType == BGP_AS_CONFED_SET;
                    }),
                pa.attrs.asPath.end());

            prependLocalAs(pa.attrs, sesCfgs);

            stripPrivateAsIfConfigured(pa.attrs.asPath, afNbr);
        }
        else if (session.neighbor.isConfedEbgp())
        {
            AsPathSegment& seg = getConfedAsSegment(pa.attrs);
            seg.asns.insert(seg.asns.begin(), ScopeAccessor::getAsNum(scope));
        }
    }

    /**
     * @brief Sets @p pa's standard COMMUNITIES, clearing them for eBGP peers
     *        unless @ref sendStd allows sending them.
     */
    void deriveCommunities(PathAttribute& pa, const PathAttribute& src, const NeighborAf& afNbr,
                           const Session& session) const
    {
        pa.attrs.communities = src.attrs.communities;
        if (session.neighbor.isEbgp() && !sendStd(afNbr))
            pa.attrs.communities.clear();
    }

    /**
     * @brief Sets @p pa's EXTENDED_COMMUNITIES, clearing them for eBGP peers
     *        unless @ref sendExt allows sending them.
     */
    void deriveExtCommunities(PathAttribute& pa, const PathAttribute& src, const NeighborAf& afNbr,
                              const Session& session) const
    {
        pa.attrs.extendedCommunities = src.attrs.extendedCommunities;
        if (session.neighbor.isEbgp() && !sendExt(afNbr))
            pa.attrs.extendedCommunities.clear();
    }

    /**
     * @brief Sets @p pa's LARGE_COMMUNITIES, clearing them for eBGP peers unless
     *        @ref sendStd or @ref sendExt allows sending them.
     */
    void deriveLargeCommunities(PathAttribute& pa, const PathAttribute& src, const NeighborAf& afNbr,
                                const Session& session) const
    {
        pa.attrs.largeCommunities = src.attrs.largeCommunities;
        if (session.neighbor.isEbgp() && !sendStd(afNbr) && !sendExt(afNbr))
            pa.attrs.largeCommunities.clear();
    }

    /**
     * @brief Sets @p pa's LOCAL_PREF, cleared entirely for eBGP peers (RFC 4271:
     *        LOCAL_PREF is only exchanged between iBGP peers).
     */
    void deriveLocalPref(PathAttribute& pa, const PathAttribute& src, const Session& session) const
    {
        pa.attrs.localPref = session.neighbor.isEbgp() ? std::nullopt : src.attrs.localPref;
    }

    /**
     * @brief True if standard COMMUNITIES should be sent to @p afNbr
     *        (`SEND_COMMUNITY`, `SEND_COMMUNITY_BOTH`, or `SEND_COMMUNITY_STANDARD`).
     */
    bool sendStd(const NeighborAf& afNbr) const
    {
        const auto& c = afNbr.configs;
        return c.get<config::BgpNeighbor::SEND_COMMUNITY>().load()
            || c.get<config::BgpNeighbor::SEND_COMMUNITY_BOTH>().load()
            || c.get<config::BgpNeighbor::SEND_COMMUNITY_STANDARD>().load();
    }

    /**
     * @brief True if extended COMMUNITIES should be sent to @p afNbr
     *        (`SEND_COMMUNITY_EXTENDED` or `SEND_COMMUNITY_BOTH`).
     */
    bool sendExt(const NeighborAf& afNbr) const
    {
        const auto& c = afNbr.configs;
        return c.get<config::BgpNeighbor::SEND_COMMUNITY_EXTENDED>().load()
            || c.get<config::BgpNeighbor::SEND_COMMUNITY_BOTH>().load();
    }

    /// True for the private-use AS ranges of RFC 6996 (16-bit and 32-bit).
    static bool isPrivateAs(uint32_t asn) noexcept
    {
        return (asn >= 64512u && asn <= 65534u) ||
               (asn >= 4200000000u && asn <= 4294967294u);
    }

    /**
     * @brief Applies the neighbor's remove-private-as configuration to an egress AS-PATH.
     *
     * `remove-private-as all` strips every private ASN unconditionally. Plain
     * `remove-private-as` only strips when the path contains no public ASNs at all;
     * a path that mixes public and private ASNs is left intact, because removing
     * interior ASNs would misrepresent the path length to the peer.
     */
    void stripPrivateAsIfConfigured(std::vector<AsPathSegment>& asPath, const NeighborAf& afNbr) const
    {
        const auto& cfgs = afNbr.configs;
        auto removePrivateAsField = cfgs.get<config::BgpNeighbor::REMOVE_PRIVATE_AS>();
        const bool removePrivate = removePrivateAsField.hasValue();
        const bool removeAll = removePrivate && removePrivateAsField.load().all();

        if (!removePrivate)
            return;

        if (!removeAll)
        {
            for (const auto& seg : asPath)
                for (uint32_t asn : seg.asns)
                    if (!isPrivateAs(asn))
                        return; // mixed public/private path: leave untouched
        }

        stripPrivateAs(asPath);
    }

    /// Unconditionally removes every private ASN, dropping segments left empty.
    static void stripPrivateAs(std::vector<AsPathSegment>& asPath)
    {
        for (auto& seg : asPath)
        {
            auto it = std::remove_if(seg.asns.begin(), seg.asns.end(), isPrivateAs);
            seg.asns.erase(it, seg.asns.end());
        }
        auto it = std::remove_if(asPath.begin(), asPath.end(),
            [](const AsPathSegment& s) { return s.asns.empty(); });
        asPath.erase(it, asPath.end());
    }

    /**
     * @brief Applies egress transformations to an aggregate's PathAttribute before sending.
     *
     * Modifies `pa` in-place; the caller owns the copy.
     *
     * @param pa       Aggregate path attributes to modify.
     * @param session  Outbound session used to determine eBGP classification and addresses.
     */
    void applyAggregateEgressPolicy(PathAttribute& pa, const Session& session) const
    {
        auto& sesCfgs = session.getNeighborConfigs();

        if (session.neighbor.isEbgp())
        {
            pa.attrs.localPref    = std::nullopt;
            pa.attrs.originatorId = std::nullopt;
            pa.attrs.clusterList.clear();

            // Strip CONFED segments before advertising to real eBGP.
            pa.attrs.asPath.erase(
                std::remove_if(pa.attrs.asPath.begin(), pa.attrs.asPath.end(),
                    [](const AsPathSegment& s) {
                        return s.segmentType == BGP_AS_CONFED_SEQUENCE ||
                               s.segmentType == BGP_AS_CONFED_SET;
                    }),
                pa.attrs.asPath.end());

            prependLocalAs(pa.attrs, sesCfgs);
        }
        else if (session.neighbor.isConfedEbgp())
        {
            // Prepend member AS as AS_CONFED_SEQUENCE; keep LOCAL_PREF.
            AsPathSegment& seg = getConfedAsSegment(pa.attrs);
            seg.asns.insert(seg.asns.begin(), ScopeAccessor::getAsNum(scope));
            pa.attrs.localPref = 100;
        }
        else
        {
            pa.attrs.localPref = 100;
        }

        if (auto* conn = session.getPrimaryConnection())
            pa.path.nextHop = conn->socketKey()->local.address;
    }

    /**
     * @brief Applies a peer-sent ORF prefix-list to one outbound NLRI.
     *
     * @param nlri    Outbound NLRI to evaluate.
     * @param filter  ORF prefix-list entries received from the peer.
     * @return `true` if the NLRI should be advertised; `false` if the ORF filter denies it.
     */
    bool passesOrfFilter(const NlriT& nlri, const std::vector<OrfPrefixEntry>& filter) const
    {
        if constexpr (types::IsIPPrefix<NlriT>)
        {
            const types::IPPrefix candidate(nlri.addr, nlri.prefixLength);

            for (const auto& e : filter)
            {
                if (e.action != BGP_ORF_ACTION_ADD) continue;

                uint8_t minLen = e.minLen;
                uint8_t maxLen = e.maxLen;
                if (minLen == 0 && maxLen == 0)
                    minLen = maxLen = e.prefix.prefixLength;

                if (nlri.prefixLength < minLen || nlri.prefixLength > maxLen)
                    continue;

                // Check if nlri is a subnet of e.prefix
                if (!e.prefix.contains(candidate))
                    continue;

                return e.match == BGP_ORF_MATCH_PERMIT;
            }
            return false; // implicit deny when filter is non-empty and nothing matched
        }
        else
        {
            return true; // ORF prefix-list not applicable for this NLRI type
        }
    }

private:
    BgpScope& scope; ///< Owning scope; provides config/AS/RID accessors.
    AfiSafi     family;  ///< AFI/SAFI this egress policy serves.
};

} // namespace routing::bgp

#endif // BGP_EGRESS_POLICY_HPP
