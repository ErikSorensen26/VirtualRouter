// OspfOriginator.h

#ifndef OSPF_ORIGINATOR_H
#define OSPF_ORIGINATOR_H

#include "ospf/database/LSDB.hpp"

class ProcessQueueRef;

namespace OSPF
{
struct OspfInterfaceId;
class Area;
class OspfInterface;
class Neighbor;

class Originator
{
public:

    Originator(Area& a);
    virtual ~Originator();

    // Public Originations
    virtual void fullRefresh() = 0;
    virtual void updateInterface(uint32_t ifaceId) = 0;
    virtual void addExternal(uint32_t asbr, uint32_t lsid, bool expire) = 0;
    virtual void translateNssaToExternal(const LsaKey& key, const LsaBody& lsa, bool expire) = 0;
    virtual void addStubDefaultRoute(bool add) = 0;
    virtual void originateSummary(uint32_t lsid, const IPPrefix& prefix, uint32_t cost, bool expire = false) = 0;

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
    virtual void addRouterLsa(std::optional<uint32_t> id, bool refresh, bool fullRefresh = false) = 0;
    virtual void addNetworkLsa(const OspfInterface& iface, bool refresh) = 0;
    virtual void addAsbrLsa(uint32_t asbr, bool refresh) = 0;

    // Removing
    virtual void removeNetworkLsa(uint32_t ifaceId) = 0;
    virtual void expire(LsaKey& key) = 0;

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
    virtual void addTransitLink(LsaBody& router, const OspfInterface& iface, const Neighbor* nbr = nullptr) = 0;
    virtual void addP2PLink(LsaBody& router, const OspfInterface& iface, const Neighbor& neighbor) = 0;
    virtual void addStubLink(LsaBody& router, const OspfInterface& iface, bool fullMask = false) = 0;
    virtual void addVirtualLink(LsaBody& router, const OspfInterface& iface, const Neighbor& vNbr) = 0;

    template <typename RouterLink>
    void uniqueLinks(std::vector<RouterLink>& links);

protected:

    Area& area;
};

template <typename RouterLink>
void Originator::uniqueLinks(std::vector<RouterLink>& links)
{
    links.erase(std::unique(links.begin(), links.end()), links.end());
}
}

#endif
