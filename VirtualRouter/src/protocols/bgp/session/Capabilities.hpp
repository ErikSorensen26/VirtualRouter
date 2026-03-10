// Capabilities.hpp

#ifndef BGP_CAPABILITIES_HPP
#define BGP_CAPABILITIES_HPP

#include <string>
#include <unordered_set>

#include "bgp/BgpTypes.hpp"

namespace BGP
{
// Negotiated capability set
struct Capabilities
{
    // Multiprotocol extensions
    std::vector<AfiSafi> mpFamilies;

    // Route refresh
    bool routeRefresh = false;
    bool enhancedRouteRefresh = false;

    // 4-byte ASN
    bool asn32bit = false;
    uint32_t asn;

    // Extended message size
    bool extendedMessage = false;

    // Graceful restart
    struct GracefulRestartFamily
    {
        AfiSafi family;
        bool forwardingStatePreserved;
    };

    bool gracefulRestart = false;
    bool restarting = false;
    uint16_t restartTime = 0;
    std::vector<GracefulRestartFamily> gracefulFamilies;

    // Long-lived graceful restart
    struct LlgrFamily
    {
        AfiSafi family;
        uint32_t staleTime;
        uint8_t flags;
    };

    bool llgr = false;
    std::vector<LlgrFamily> llgrFamilies;

    bool multiSess = false;
    std::vector<AfiSafi> multiSessionFamilies;

    // ADD-PATH
    struct AddPathFamily
    {
        AfiSafi family;
        uint8_t sendReceive;
    };

    bool addPath = false;
    std::vector<AddPathFamily> addPathFamilies;

    // Outbound route filtering
    struct OrfEntry
    {
        AfiSafi family;
        uint8_t orfType;
        uint8_t sendReceive;
    };

    bool outboundRouteFiltering = false;
    std::vector<OrfEntry> orfEntries;

    // Extended next-hop encoding
    struct ExtendedNextHop
    {
        AfiSafi family;
        uint16_t nextHopAfi;
    };

    bool extendedNextHop = false;
    std::vector<ExtendedNextHop> extendedNextHopEntries;

    // Multiple labels
    bool multipleLabels = false;
    std::vector<AfiSafi> labeledFamilies;

    // Route-target constraints
    bool routeTargetConstraint = false;
    std::vector<AfiSafi> RtConstraintFamily;

    // BGPsec
    bool bgpsec = false;
    std::vector<AfiSafi> bgpsecFamilies;

    // FQDN
    bool fqdn = false;
    std::string hostname;
    std::string domain;

    // Link-local next hop (RFC 8950)
    bool linkLocalNextHop = false;

    // Helpers
    bool supportsFamily(const AfiSafi& fam) const noexcept
    {
        for (const auto& f : mpFamilies)
            if (f == fam) return true;
        return false;
    }

    bool addPathSend(const AfiSafi& fam) const noexcept
    {
        for (const auto& ap : addPathFamilies)
            if (ap.family == fam) return (ap.sendReceive & BGP_ADD_PATH_SEND) != 0;
        return false;
    }

    bool addPathReceive(const AfiSafi& fam) const noexcept
    {
        for (const auto& ap : addPathFamilies)
            if (ap.family == fam) return (ap.sendReceive & BGP_ADD_PATH_RECEIVE) != 0;
        return false;
    }
};

// Negotiated session result
struct NegotiatedCapabilities
{
    bool asn32bit = false;
    bool routeRefresh = false;
    bool enhancedRR = false;
    bool gracefulRestart = false;
    bool llgr = false;
    bool extendedMessage = false;
    bool addpath = false;
    bool multiSess = false;
    bool linkLocalNextHop = false;
    std::vector<Capabilities::AddPathFamily> addPathFamilies;
    std::vector<Capabilities::GracefulRestartFamily> grFamilies;
    std::vector<Capabilities::LlgrFamily> llgrFamilies;
    std::unordered_set<AfiSafi> activeFamilies;
    std::unordered_set<AfiSafi> multiSessionFamilies;
};
}

#endif // BGP_CAPABILITIES_HPP
