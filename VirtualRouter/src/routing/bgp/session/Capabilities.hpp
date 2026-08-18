/**
 * @file Capabilities.hpp
 * @brief BGP session capabilities: multiprotocol, graceful restart, add-path, etc.
 */

#ifndef BGP_CAPABILITIES_HPP
#define BGP_CAPABILITIES_HPP

#include <string>
#include <unordered_set>

#include "bgp/BgpTypes.hpp"

namespace routing::bgp
{

/**
 * @brief Represents the capabilities advertised by a BGP peer.
 * @ingroup BGP_SESSION
 *
 * Populated during BGP OPEN message processing. Indicates which optional
 * features the remote or local peer supports. AFI/SAFI lists are expected
 * to contain no duplicates.
 */
struct Capabilities
{
    std::vector<AfiSafi> mpFamilies; ///< Multiprotocol families supported by the peer.

    bool routeRefresh = false;          ///< Indicates standard route refresh support.
    bool enhancedRouteRefresh = false;  ///< Indicates enhanced route refresh support.
    bool asn32bit = false;              ///< Indicates 4-byte ASN support.
    uint32_t asn = 0;                   ///< Local ASN value used for 4-byte ASNs.

    bool extendedMessage = false;       ///< Indicates extended message size capability.

    // Graceful restart support
    struct GracefulRestartFamily
    {
        AfiSafi family;                 ///< Address family the restart applies to.
        bool forwardingStatePreserved;  ///< True if forwarding state is preserved.
    };
    bool gracefulRestart = false;       ///< Indicates GR support.
    bool restarting = false;            ///< True if session is currently restarting.
    uint16_t restartTime = 0;           ///< Graceful restart timer in seconds.
    std::vector<GracefulRestartFamily> gracefulFamilies; ///< Per-family GR info.

    // Long-lived graceful restart (LLGR)
    struct LlgrFamily
    {
        AfiSafi family; ///< Address family.
        uint32_t staleTime; ///< Time in seconds routes remain stale.
        uint8_t flags;      ///< Flags associated with LLGR.
    };
    bool llgr = false;                 ///< Indicates LLGR support.
    std::vector<LlgrFamily> llgrFamilies; ///< Per-family LLGR info.

    // Multipath sessions
    bool multiSess = false;                  ///< Supports multiple sessions.
    std::vector<AfiSafi> multiSessionFamilies; ///< AFI/SAFI families for multi-session.

    // Add-path
    struct AddPathFamily
    {
        AfiSafi family;        ///< Address family.
        uint8_t sendReceive;   ///< Bitmask: send/receive directions.
    };
    bool addPath = false;                  ///< Indicates add-path support.
    std::vector<AddPathFamily> addPathFamilies; ///< Per-family add-path info.

    // Outbound route filtering (ORF)
    struct OrfEntry
    {
        AfiSafi family;       ///< Address family.
        uint8_t orfType;      ///< Type of ORF message.
        uint8_t sendReceive;  ///< Bitmask: send/receive directions.
    };
    bool outboundRouteFiltering = false; ///< Indicates ORF support.
    std::vector<OrfEntry> orfEntries;   ///< Per-family ORF information.

    // Extended next-hop encoding
    struct ExtendedNextHop
    {
        AfiSafi family;      ///< Address family.
        uint16_t nextHopAfi; ///< AFI for next-hop encoding.
    };
    bool extendedNextHop = false;                    ///< Indicates extended next-hop support.
    std::vector<ExtendedNextHop> extendedNextHopEntries; ///< Per-family extended next-hop info.

    // Multiple labels
    bool multipleLabels = false; ///< Indicates support for multiple MPLS labels.
    std::vector<AfiSafi> labeledFamilies; ///< Families with multiple label support.

    // Route-target constraints
    bool routeTargetConstraint = false; ///< Indicates RT constraint support.
    std::vector<AfiSafi> RtConstraintFamily; ///< AFI/SAFI families for RT constraints.

    // BGPsec
    bool bgpsec = false; ///< Indicates BGPsec support.
    std::vector<AfiSafi> bgpsecFamilies; ///< Per-family BGPsec support.

    // Fully qualified domain name (FQDN)
    bool fqdn = false; ///< Indicates FQDN support.
    std::string hostname; ///< Local hostname advertised.
    std::string domain;   ///< Local domain name advertised.

    // Link-local next hop
    bool linkLocalNextHop = false; ///< RFC 8950 support for link-local next-hop.

    // HELPER FUNCTIONS

    /**
     * @brief Checks if a given AFI/SAFI is supported by the peer.
     *
     * @param fam Address family to check.
     * @return True if the peer supports this AFI/SAFI.
     */
    bool supportsFamily(const AfiSafi& fam) const noexcept;

    /**
     * @brief Checks if add-path sending is enabled for a given AFI/SAFI.
     *
     * @param fam Address family to check.
     * @return True if send capability is enabled.
     */
    bool addPathSend(const AfiSafi& fam) const noexcept;

    /**
     * @brief Checks if add-path receiving is enabled for a given AFI/SAFI.
     *
     * @param fam Address family to check.
     * @return True if receive capability is enabled.
     */
    bool addPathReceive(const AfiSafi& fam) const noexcept;
};

/**
 * @brief Represents the actual negotiated capabilities for a BGP session.
 * @ingroup BGP_SESSION
 *
 * Populated after OPEN message exchange and negotiation. Indicates which
 * features are enabled for the session and includes per-family data for
 * add-path, graceful restart, LLGR, and ORF. Used by the session FSM, route
 * advertisement, and timers to determine behavior.
 */
struct NegotiatedCapabilities
{
    bool asn32bit = false;              ///< 4-byte ASN support.
    bool routeRefresh = false;          ///< Standard route refresh support.
    bool enhancedRR = false;            ///< Enhanced route refresh support.
    bool gracefulRestart = false;       ///< Graceful restart support.
    bool llgr = false;                  ///< LLGR support.
    bool extendedMessage = false;       ///< Extended message size support.
    bool addpath = false;               ///< Add-path support.
    bool multiSess = false;             ///< Multi-session support.
    bool linkLocalNextHop = false;      ///< Link-local next-hop support.
    bool orf = false;                   ///< ORF support.

    std::vector<Capabilities::AddPathFamily> addPathFamilies; ///< Negotiated add-path.
    std::vector<Capabilities::GracefulRestartFamily> grFamilies; ///< Negotiated GR.
    std::vector<Capabilities::LlgrFamily> llgrFamilies;       ///< Negotiated LLGR.
    std::vector<Capabilities::OrfEntry> orfEntries;           ///< Negotiated ORF.

    std::unordered_set<AfiSafi> activeFamilies;              ///< AFI/SAFI currently active.
    std::unordered_set<AfiSafi> multiSessionFamilies;        ///< Multi-session families.

    /**
     * @brief Returns true if add-path send is enabled for a given AFI/SAFI.
     *
     * @param fam Address family to query.
     * @return True if sending add-path is negotiated.
     */
    bool addPathSend(const AfiSafi& fam) const noexcept;

    /**
     * @brief Finds the negotiated AddPath entry for a given AFI/SAFI.
     *
     * @param afi AFI/SAFI to query.
     * @return Pointer to AddPathFamily if present; nullptr otherwise.
     */
    Capabilities::AddPathFamily* findAddPath(AfiSafi afi);

    /**
     * @brief Finds the negotiated Graceful Restart entry for a given AFI/SAFI.
     *
     * @param afi AFI/SAFI to query.
     * @return Pointer to GracefulRestartFamily if present; nullptr otherwise.
     */
    Capabilities::GracefulRestartFamily* findGracefulRestart(AfiSafi afi);

    /**
     * @brief Finds the negotiated LLGR entry for a given AFI/SAFI.
     *
     * @param afi AFI/SAFI to query.
     * @return Pointer to LlgrFamily if present; nullptr otherwise.
     */
    Capabilities::LlgrFamily* findLlgr(AfiSafi afi);

    /**
     * @brief Determines if ORF receive is enabled for a given AFI/SAFI and ORF type.
     *
     * @param fam Address family to query.
     * @param orfType ORF type to check.
     * @return True if receive is allowed for this ORF type.
     */
    bool canReceiveOrf(const AfiSafi& fam, uint8_t orfType) const noexcept;

    /**
     * @brief Determines if ORF send is enabled for a given AFI/SAFI and ORF type.
     *
     * @param fam Address family to query.
     * @param orfType ORF type to check.
     * @return True if send is allowed for this ORF type.
     */
    bool canSendOrf(const AfiSafi& fam, uint8_t orfType) const noexcept;
};

} // namespace routing::bgp

#endif // BGP_CAPABILITIES_HPP
