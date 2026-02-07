// OspfOriginator.h

#ifndef OSPF_ORIGINATOR_H
#define OSPF_ORIGINATOR_H

#include <Registry.hpp>
#include <LSDB.hpp>
#include <OspfInterfaceId.hpp>
#include <RibEntry.hpp>
#include <OspfTopologyTypes.hpp>

class ProcessQueue;

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

    // Public Originations
    virtual void fullRefresh();
    virtual void updateInterface(uint32_t ifaceId);
    virtual void addExternal(uint32_t asbr, uint32_t lsid, bool expire);
    virtual void translateNssaToExternal(const LsaKey& key, const LsaBody& lsa, bool expire);
    virtual void addStubDefaultRoute(bool add);
    virtual void originateSummary(uint32_t lsid, const IPPrefix& prefix, uint32_t cost, bool expire = false);

    template <typename Policy>
    void originateLsa(const LsaKey& key, const LsaBody& body, bool expire);

    void nssaDefaultOriginate(bool add);

protected:
    struct LsaThrottleState
    {
        std::atomic<bool> pending = false;

        uint32_t backoffMs = 0;
        std::chrono::steady_clock::time_point nextFire{};
        std::chrono::steady_clock::time_point lastOriginate{};
        uint32_t timerId = 0;
    };

    struct OriginationInfo
    {
        LsaThrottleState throttleInfo;
        LsaBody body;
        bool refresh = false;
        bool expire = false;
    };

    struct RefreshBucket
    {
        uint32_t timerId = 0;
        std::vector<LsaKey> keys;
    };

private:
    template<typename Policy>
    void processReoriginatedLsa(const LsaKey& key, const OriginationInfo& body);

protected:
    template <typename Policy>
    void requestReorigination(const LsaKey& state);

    template <typename Policy>
    void runReorigination(const LsaKey& key);

    template <typename Policy>
    void processOriginatedLsa(const LsaKey& key);

    // Adding
    virtual void addRouterLsa(std::optional<uint32_t> id, bool refresh, bool fullRefresh = false);
    virtual void addNetworkLsa(const OspfInterface& iface, bool refresh);
    virtual void addAsbrLsa(uint32_t asbr, bool refresh);

    // Removing
    virtual void removeNetworkLsa(uint32_t ifaceId);
    virtual void expire(LsaKey& key);

    // Group Pacing
    template <typename Policy>
    void initGroupPacing();

    template <typename Policy>
    void handleGroupPackingBucket(uint32_t tid, uint32_t bucketIndex);
    void cancelGroupPacing();
    void scheduleForGroupPacing(const LsaKey& key);
    void unscheduleForGroupPacing(const LsaKey& key);

    // Default routes
    std::optional<uint32_t> nssaDefaultRoute = std::nullopt;
    std::optional<LsaKey> stubDefaultRoute = std::nullopt;

    // Lsa Cache Storage
    LsaAdvKey lastRouterKey{};
    std::unordered_set<uint32_t> networkLsas{};
    std::unordered_map<uint32_t, LsaKey> asbrLsas{};
    std::unordered_map<uint32_t, std::vector<uint32_t>> externalRoutes{};
    std::unordered_map<LsaKey, OriginationInfo> originationState;

    // Group Pacing
    std::vector<RefreshBucket> refreshBuckets;
    std::unordered_map<LsaKey, uint32_t> keyToBucket;

    void processLsa(LsaKey& key, LsaBody& body);

    // Links
    void addRouterLink(LsaBody& router, const OspfInterface& iface, bool refresh, bool attemptNetLsa = false);
    virtual void addTransitLink(LsaBody& router, const OspfInterface& iface, const Neighbor* nbr = nullptr);
    virtual void addP2PLink(LsaBody& router, const OspfInterface& iface, const Neighbor& neighbor);
    virtual void addStubLink(LsaBody& router, const OspfInterface& iface, bool fullMask = false);
    virtual void addVirtualLink(LsaBody& router, const OspfInterface& iface, const Neighbor& vNbr);

    template <typename RouterLink>
    void uniqueLinks(std::vector<RouterLink>& links);

protected:

    OspfArea& area;
    ProcessQueue& scheduler;
};

template <typename RouterLink>
void OspfOriginator::uniqueLinks(std::vector<RouterLink>& links)
{
    links.erase(std::unique(links.begin(), links.end()), links.end());
}
}

#endif
