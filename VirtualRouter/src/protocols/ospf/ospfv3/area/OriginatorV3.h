// OspfOriginatorV3.h

#ifndef OSPF_ORIGINATOR_V3_H
#define OSPF_ORIGINATOR_V3_H

#include <deque>

#include "ospf/area/Originator.h"

namespace OSPF
{
class OriginatorV3 : public Originator
{
public:
    OriginatorV3(Area& a);
    ~OriginatorV3() override;

    void fullRefresh() override;
    void updateInterface(uint32_t ifaceId) override;
    void addExternal(uint32_t asbr, uint32_t lsid, bool remove) override;
    void translateNssaToExternal(const LsaKey& key, const LsaBody& lsa, bool expire) override;
    void addStubDefaultRoute(bool add) override;
    void originateSummary(uint32_t lsid, const IPPrefix& prefix, uint32_t cost, bool expire) override;

protected:

    void addRouterLsa(std::optional<uint32_t> ifaceId, bool refresh, bool fullRefresh = false) override;
    void addNetworkLsa(const OspfInterface& iface, bool refresh) override;

    std::vector<LsaKey> lastRouterLsas;
    std::vector<LsaKey> lastRouterPrefixes;
    std::vector<std::pair<uint32_t, std::vector<LsaKey>>> lastNetworkPrefixes;

    uint32_t maxPrefixLsid{0};
    std::deque<uint32_t> prefixLsidQueue;
    uint32_t findNextPrefixLsid();

    uint32_t maxRouterLsid{0};
    std::deque<uint32_t> routerLsidQueue;
    uint32_t findNextRouterLsid();

    void expire(LsaKey& key) override;
    void addAsbrLsa(uint32_t asbr, bool refresh = false) override;
    void removeNetworkLsa(uint32_t ifaceId) override;

    void addRouterPrefixLsa(std::vector<std::pair<LsaKey, std::optional<bool>>>& routerLsas, bool refresh);
    void addNetworkPrefixLsa(const OspfInterface& iface, bool refresh);
    void addLinkLsa(const OspfInterface& iface, bool refresh);

    void addTransitLink(LsaBody& router, const OspfInterface& iface, const Neighbor* nbr = nullptr) override;
    void addP2PLink(LsaBody& router, const OspfInterface& iface, const Neighbor& neighbor) override;
    void addStubLink(LsaBody& router, const OspfInterface& iface, bool fullMask = false) override;
    void addVirtualLink(LsaBody& router, const OspfInterface& iface, const Neighbor& vNbr) override;
};
}

#endif // OSPF_ORIGINATOR_V3_H
