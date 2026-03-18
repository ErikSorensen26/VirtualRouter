// NlriPolicy.hpp

#ifndef BGP_NLRI_POLICY_HPP
#define BGP_NLRI_POLICY_HPP

#include "bgp/rib/RibTypes.hpp"

class VirtualRouter;

namespace BGP
{
enum class LocRibType;
template <typename N, LocRibType T>
class LocRib;
class BgpProcess;

template <typename N, LocRibType LR, AfiSafi A>
class NlriPolicy
{
public:
    NlriPolicy(VirtualRouter& v, BgpProcess& proc)
        : vrf(v), process(proc) {}

    using LocRib = LocRib<N, LR>;
    using Nlri = N;
    static constexpr AfiSafi afi = A;

    struct NlriInstall
    {
        BGP::LocalRoute<N>& route;
        BGP::PathAttribute attrs;
        uint64_t metric;
        uint8_t distance;
        bool recursiveHost;
    };

    //static size_t nlriEncodedSize(const N& nlri);
    //static void encodeNlri(uint8_t* buf, const N& n);
    //static size_t decodeNlri(uint8_t* buf, N& n);
    //static AfiSafi afi();

    virtual void installRoute(const NlriInstall& install) = 0;
    virtual void withdrawRoute(const N& nlri) = 0;

    virtual void installRoutes(const std::vector<NlriInstall>& install) = 0;
    virtual void withdrawRoutes(const std::vector<N>& nlri) = 0;
     
protected:
    VirtualRouter& vrf;
    BgpProcess& process;
};
}

#endif // BGP_NLRI_POLICY_HPP
