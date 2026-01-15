// OspfOriginator.h

#ifndef OSPF_ORIGINATOR_H
#define OSPF_ORIGINATOR_H

#include <LSDB.hpp>
#include <OspfInterfaceId.hpp>

namespace OSPF
{
struct OspfInterfaceId;
class OspfArea;
class OspfInterface;
class Neighbor;
class OspfOriginator
{
public:
    OspfOriginator(OspfArea& a) : area(a) {}

    virtual void updateInterface(uint32_t ifaceId);

    virtual void addRouterLsa(uint32_t ifaceId);
    virtual void addNetworkLsa(const OspfInterface& iface);

protected:
    LsaAdvKey lastRouterKey{};

    struct NetworkState
    {
        LsaKey key;
        LsaBody lastLsa;
    };

    std::unordered_map<OspfInterfaceId, NetworkState> networkLsas{};

    void addRouterLink(LsaBody& router, const OspfInterface& iface, bool attemptNetLsa = false);
    void processLsa(LsaKey& key, LsaBody& body);

    virtual void removeNetworkLsa(uint32_t ifaceId);

    virtual void addTransitLink(LsaBody& router, const OspfInterface& iface, const Neighbor* nbr = nullptr);
    virtual void addP2PLink(LsaBody& router, const OspfInterface& iface, const Neighbor& neighbor);
    virtual void addStubLink(LsaBody& router, const OspfInterface& iface);
    virtual void addVirtualLink(LsaBody& router, const OspfInterface& iface, const Neighbor& vNbr);

    template <typename RouterLink>
    void uniqueLinks(std::vector<RouterLink>& links);

protected:

    OspfArea& area;
};

template <typename RouterLink>
void OspfOriginator::uniqueLinks(std::vector<RouterLink>& links)
{
    links.erase(std::unique(links.begin(), links.end()), links.end());
}
}

#endif
