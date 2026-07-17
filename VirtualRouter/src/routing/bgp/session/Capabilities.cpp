// Capabilities.cpp

#include "Capabilities.hpp"
#include "packet/headers/BgpHeader.hpp"

namespace routing::bgp
{

bool Capabilities::supportsFamily(const AfiSafi& fam) const noexcept
{
    for (const auto& f : mpFamilies)
        if (f == fam)
            return true;
    return false;
}

bool Capabilities::addPathSend(const AfiSafi& fam) const noexcept
{
    for (const auto& ap : addPathFamilies)
        if (ap.family == fam)
            return (ap.sendReceive & BGP_ADD_PATH_SEND) != 0;
    return false;
}

bool Capabilities::addPathReceive(const AfiSafi& fam) const noexcept
{
    for (const auto& ap : addPathFamilies)
        if (ap.family == fam)
            return (ap.sendReceive & BGP_ADD_PATH_RECEIVE) != 0;
    return false;
}

bool NegotiatedCapabilities::addPathSend(const AfiSafi& fam) const noexcept
{
    for (const auto& ap : addPathFamilies)
        if (ap.family == fam)
            return (ap.sendReceive & BGP_ADD_PATH_SEND) != 0;
    return false;
}

Capabilities::AddPathFamily* NegotiatedCapabilities::findAddPath(AfiSafi afi)
{
    for (auto& ap : addPathFamilies)
        if (ap.family == afi)
            return &ap;
    return nullptr;
}

Capabilities::GracefulRestartFamily* NegotiatedCapabilities::findGracefulRestart(AfiSafi afi)
{
    for (auto& gf : grFamilies)
        if (gf.family == afi)
            return &gf;
    return nullptr;
}

Capabilities::LlgrFamily* NegotiatedCapabilities::findLlgr(AfiSafi afi)
{
    for (auto& lf : llgrFamilies)
        if (lf.family == afi)
            return &lf;
    return nullptr;
}

bool NegotiatedCapabilities::canReceiveOrf(const AfiSafi& fam, uint8_t orfType) const noexcept
{
    for (const auto& oe : orfEntries)
        if (oe.family == fam && oe.orfType == orfType)
            return (oe.sendReceive & BGP_ORF_RECEIVE) != 0;
    return false;
}

bool NegotiatedCapabilities::canSendOrf(const AfiSafi& fam, uint8_t orfType) const noexcept
{
    for (const auto& oe : orfEntries)
        if (oe.family == fam && oe.orfType == orfType)
            return (oe.sendReceive & BGP_ORF_SEND) != 0;
    return false;
}

} // namespace routing::bgp
