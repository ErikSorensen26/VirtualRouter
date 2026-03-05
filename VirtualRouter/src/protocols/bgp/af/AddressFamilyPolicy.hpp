// AddressFamilyPolicy.hpp

#ifndef BGP_ADDRESS_FAMILY_POLICY_HPP
#define BGP_ADDRESS_FAMILY_POLICY_HPP

#include "bgp/rib/RibTypes.hpp"

class VirtualRouter;

namespace BGP
{
template <typename N>
class AddressFamilyPolicy
{
public:
    AddressFamilyPolicy(VirtualRouter& v)
        : vrf(v) {}

    using Nlri = N;

    virtual size_t nlriEncodedSize(const N& nlri) const = 0;
    virtual void encodeNlri(uint8_t* buf, const N& n) const = 0;

    virtual void installRoute(RouteCanidate<N>& nlri) = 0;
    virtual void withdrawRoute(const N& nlri) = 0;
     
protected:
    VirtualRouter& vrf;
};
}

#endif // BGP_ADDRESS_FAMILY_POLICY_HPP
