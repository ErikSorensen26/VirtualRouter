// GracefulRestartManager.cpp

#include "GracefulRestartManager.h"
#include "OspfInterfaceBase.h"
#include "ospf/area/Area.h"
#include "ospf/area/OriginatorContext.h"
#include "ospf/OspfProcess.h"
#include "ospf/ospfv2/database/OpaqueLsaV2.hpp"
#include "ospf/ospfv2/database/GraceLsa.hpp"
#include "packet/headers/embedded/ospf/Ospfv2LSAHeader.hpp"

namespace routing::ospf
{
// Link State ID for this interface's Grace-LSA: opaque type in the top byte,
// opaque instance ID (this interface's index) in the low 24 bits, per RFC 2370 SS2.
static uint32_t graceLinkStateId(uint32_t interfaceId)
{
    return (static_cast<uint32_t>(GRACE_LSA_OPAQUE_TYPE) << 24) | (interfaceId & 0x00FFFFFF);
}

GracefulRestartManager::GracefulRestartManager(OspfInterfaceBase& iface) : iface(iface)
{}

void GracefulRestartManager::originateGraceLsa(uint32_t gracePeriodSeconds, GraceRestartReason reason)
{
    uint32_t rid = iface.process.getRouterId();
    LsaKey key(OSPFV2_LSA_OPAQUE_LINK, graceLinkStateId(iface.interfaceId), rid);

    GraceLsaTlv tlv;
    tlv.gracePeriodSeconds = gracePeriodSeconds;
    tlv.restartReason = reason;

    OpaqueLsaV2 lsa;
    lsa.opaqueType = GRACE_LSA_OPAQUE_TYPE;
    lsa.opaqueId = iface.interfaceId & 0x00FFFFFF;
    lsa.payload.resize(tlv.size());
    tlv.buildBody(lsa.payload.data(), static_cast<uint16_t>(lsa.payload.size()));

    iface.area.originContext.originateLsa<PolicyV2>(key, LsaBody(lsa), false);

    originated = true;
}

void GracefulRestartManager::flushGraceLsa()
{
    if (!originated) return;

    uint32_t rid = iface.process.getRouterId();
    LsaKey key(OSPFV2_LSA_OPAQUE_LINK, graceLinkStateId(iface.interfaceId), rid);

    iface.area.originContext.originateLsa<PolicyV2>(key, iface.area.originContext.originationState[key].body, true);

    originated = false;
}
} // namespace routing::ospf
