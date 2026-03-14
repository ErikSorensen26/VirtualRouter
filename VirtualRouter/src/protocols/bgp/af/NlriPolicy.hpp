// NlriPolicy.hpp

#ifndef BGP_NLRI_POLICY_HPP
#define BGP_NLRI_POLICY_HPP

#include "bgp/rib/RibTypes.hpp"

class VirtualRouter;

namespace BGP
{
template <typename N, AfiSafi A>
class NlriPolicy
{
public:
    NlriPolicy(VirtualRouter& v)
        : vrf(v) {}

    using Nlri = N;
    static constexpr AfiSafi afi = A;

    //static size_t nlriEncodedSize(const N& nlri);
    //static void encodeNlri(uint8_t* buf, const N& n);
    //static size_t decodeNlri(uint8_t* buf, N& n);
    //static AfiSafi afi();

    virtual void installRoute(LocalRoute<N>& nlri) = 0;
    virtual void withdrawRoute(const N& nlri) = 0;
     
protected:
    VirtualRouter& vrf;
};
}

#endif // BGP_NLRI_POLICY_HPP
