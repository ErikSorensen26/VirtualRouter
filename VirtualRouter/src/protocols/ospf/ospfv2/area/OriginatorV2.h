// OspfOriginatorV2.h

#ifndef OSPF_ORIGINATOR_V2_H
#define OSPF_ORIGINATOR_V2_H

#include "ospf/area/Originator.h"

namespace OSPF
{
class OriginatorV2 : public Originator
{
public:
    OriginatorV2(Area& a);
    ~OriginatorV2() override;

    void fullRefresh() override;
    void updateInterface(uint32_t ifaceId) override;
    void addExternal(uint32_t asbr, uint32_t lsid, bool remove) override;
    void translateNssaToExternal(const LsaKey& key, const LsaBody& lsa, bool expire) override;
    void addStubDefaultRoute(bool add) override;
    void originateSummary(uint32_t lsid, const IPPrefix& prefix, uint32_t cost, bool expire) override;

protected:
    void addRouterLsa(std::optional<uint32_t> ifaceId, bool refresh, bool fullRefresh = false) override;
    void addNetworkLsa(const OspfInterface& iface, bool refresh) override;

    void expire(LsaKey& key) override;
    void addAsbrLsa(uint32_t asbr, bool refresh = false) override;
    void removeNetworkLsa(uint32_t ifaceId) override;

    void addSecondaryLinks(LsaBody& router, const OspfInterface& iface);
    void addTransitLink(LsaBody& router, const OspfInterface& iface, const Neighbor* nbr = nullptr) override;
    void addP2PLink(LsaBody& router, const OspfInterface& iface, const Neighbor& neighbor) override;
    void addStubLink(LsaBody& router, const OspfInterface& iface, bool fullMask = false) override;
    void addVirtualLink(LsaBody& router, const OspfInterface& iface, const Neighbor& vNbr) override;
};
}

#endif
