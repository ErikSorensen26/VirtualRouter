// OspfOriginator.h

#ifndef OSPF_ORIGINATOR_H
#define OSPF_ORIGINATOR_H

#include <Registry.hpp>
#include <LSDB.hpp>
#include <OspfInterfaceId.hpp>
#include <RibEntry.hpp>
#include <OspfTopologyTypes.hpp>

class TimeManager;

namespace OSPF
{
struct OspfInterfaceId;
class OspfArea;
class OspfInterface;
class Neighbor;
class OspfOriginator
{
public:
    OspfOriginator(OspfArea& a);
    ~OspfOriginator();

    struct ExternalOriginateContext
    {
        IPPrefix prefix;
        uint32_t metric;
        uint32_t tag;
        std::optional<IPAddress> nextHop;
        bool metricIsE2; // false = E1, true = E2
    };

    virtual void fullRefresh();
    virtual void updateInterface(uint32_t ifaceId);
    virtual void addExternal(uint32_t asbr, uint32_t lsid, bool expire);
    virtual void translateNssaToExternal(const LsaKey& key, const LsaBody& lsa, bool expire);
    virtual void addStubDefaultRoute(bool add);
    virtual void originateExternal(uint32_t lsid, ExternalOriginateContext& ctx, bool expire);
    virtual void originateSummary(uint32_t lsid, const IPPrefix& prefix, uint32_t cost, bool expire = false);

    void nssaDefaultOriginate(bool add);
    void addDefaultRoute(bool add);

    template<typename Policy>
    void processReoriginatedLsa(const LsaKey& key, LsaBody& body, bool refresh = false, bool expire = false);

protected:

    struct LsaState
    {
        LsaKey key;
        LsaBody lastLsa;
    };

    struct RefreshInfo
    {
        bool isRefresh; // Used for any interface refresh
        std::optional<uint32_t> activeTimerId{std::nullopt}; // Indicates active timer
        std::vector<LsaKey> keys{};
    };

protected:
    template <typename Policy>
    // Refresh being undefined means this value is expired and no longer needs refreshing.
    void processOriginatedLsa(const LsaKey& key, LsaBody& body, bool expire, RefreshInfo* refresh = nullptr);
    bool isValidForwardAddress(const std::optional<IPAddress>& nh) const;

    virtual void addRouterLsa(std::optional<uint32_t> id, RefreshInfo& refresh, bool fullRefresh = false);
    virtual void addNetworkLsa(const OspfInterface& iface, RefreshInfo& refresh);
    virtual void addAsbrLsa(uint32_t asbr, RefreshInfo& refresh);

    virtual void removeNetworkLsa(uint32_t ifaceId);
    virtual void expire(LsaKey& key, LsaBody& body);

    // Refresh
    template <typename Policy>
    void startRefresh(RefreshInfo& info);
    template <typename Policy>
    void handleRefreshTimeout(uint32_t tid);

    std::unordered_map<LsaKey, uint32_t> lsaRefreshes;
    std::unordered_map<uint32_t, std::vector<LsaKey>> refreshTimers;
    std::optional<LsaKey> nssaDefaultRoute = std::nullopt;
    std::optional<LsaKey> stubDefaultRoute = std::nullopt;
    std::optional<LsaKey> defaultRoute = std::nullopt;

    // Lsa Storage
    LsaAdvKey lastRouterKey{};
    std::unordered_map<OspfInterfaceId, LsaState> networkLsas{};
    std::unordered_map<uint32_t, LsaState> asbrLsas{};
    std::unordered_map<uint32_t, std::vector<uint32_t>> externalRoutes{};

    void addRouterLink(LsaBody& router, const OspfInterface& iface, RefreshInfo& info, bool attemptNetLsa = false);
    void processLsa(LsaKey& key, LsaBody& body);

    // Links
    virtual void addTransitLink(LsaBody& router, const OspfInterface& iface, const Neighbor* nbr = nullptr);
    virtual void addP2PLink(LsaBody& router, const OspfInterface& iface, const Neighbor& neighbor);
    virtual void addStubLink(LsaBody& router, const OspfInterface& iface, bool fullMask = false);
    virtual void addVirtualLink(LsaBody& router, const OspfInterface& iface, const Neighbor& vNbr);

    template <typename RouterLink>
    void uniqueLinks(std::vector<RouterLink>& links);

protected:

    OspfArea& area;
    TimeManager& tmgr;
};

template <typename RouterLink>
void OspfOriginator::uniqueLinks(std::vector<RouterLink>& links)
{
    links.erase(std::unique(links.begin(), links.end()), links.end());
}
}

#endif
