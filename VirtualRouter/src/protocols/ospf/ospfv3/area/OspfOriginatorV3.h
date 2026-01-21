// OspfOriginatorV3.h

#ifndef OSPF_ORIGINATOR_V3_H
#define OSPF_ORIGINATOR_V3_H

#include <OspfOriginator.h>
#include <RouterLsaV3.hpp>
#include <deque>

namespace OSPF
{
class OspfOriginatorV3 : public OspfOriginator
{
public:
    OspfOriginatorV3(OspfArea& a);

    void fullRefresh() override;
    void updateInterface(uint32_t ifaceId) override;
    void addExternal(uint32_t asbr, uint32_t lsid, bool remove) override;

protected:

    void addRouterLsa(std::optional<uint32_t> ifaceId, RefreshInfo& refresh, bool fullRefresh = false) override;
    void addNetworkLsa(const OspfInterface& iface, RefreshInfo& refresh) override;

    std::vector<std::pair<uint32_t, LsaBody>> lastRouterLsas;
    std::vector<std::pair<uint32_t, LsaBody>> lastRouterPrefixes;
    std::vector<std::pair<uint32_t, std::vector<std::pair<uint32_t, LsaBody>>>> lastNetworkPrefixes;

    uint32_t maxPrefixLsid{0};
    std::deque<uint32_t> prefixLsidQueue;
    uint32_t findNextPrefixLsid();

    uint32_t maxRouterLsid{0};
    std::deque<uint32_t> routerLsidQueue;
    uint32_t findNextRouterLsid();

    void expire(LsaKey& key, LsaBody& body) override;
    void addAsbrLsa(uint32_t asbr, RefreshInfo& refresh) override;
    void removeNetworkLsa(uint32_t ifaceId) override;

    void addRouterPrefixLsa(std::vector<std::pair<uint32_t, LsaBody>>& routerLsas, RefreshInfo& refresh);
    void addNetworkPrefixLsa(LsaKey& key, const OspfInterface& iface, RefreshInfo& refresh);

    void addTransitLink(LsaBody& router, const OspfInterface& iface, const Neighbor* nbr = nullptr) override;
    void addP2PLink(LsaBody& router, const OspfInterface& iface, const Neighbor& neighbor) override;
    void addStubLink(LsaBody& router, const OspfInterface& iface, bool fullMask = false) override;
    void addVirtualLink(LsaBody& router, const OspfInterface& iface, const Neighbor& vNbr) override;
};
}

#endif // OSPF_ORIGINATOR_V3_H
