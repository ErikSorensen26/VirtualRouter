// OpaqueOriginatorV2.cpp

#include "OpaqueOriginatorV2.h"
#include "ospf/area/OriginatorContext.h"
#include "ospf/area/Area.h"
#include "ospf/OspfProcess.h"
#include "ospf/interface/InterfaceManager.h"
#include "ospf/interface/OspfInterfaceBase.h"
#include "ospf/ospfv2/database/OpaqueLsaV2.hpp"
#include "ospf/ospfv2/database/RouterCapabilityTlv.hpp"
#include "packet/headers/embedded/ospf/Ospfv2LSAHeader.hpp"

namespace routing::ospf
{
/// Opaque type for the Router Information Opaque LSA (RFC 5250 §3).
static constexpr uint8_t ROUTER_INFORMATION_OPAQUE_TYPE = 4;
/// Opaque instance ID of the single Router Information LSA this router originates (RFC 5250 §3).
static constexpr uint32_t ROUTER_CAPABILITY_OPAQUE_ID = 0;
/**
 * @brief Link State ID for the area-scope Router Information Opaque LSA: opaque
 *        type in the top byte, opaque instance ID in the low 24 bits, per
 *        RFC 2370 §2.
 */
static constexpr uint32_t ROUTER_CAPABILITY_LINK_STATE_ID =
    (static_cast<uint32_t>(ROUTER_INFORMATION_OPAQUE_TYPE) << 24) | ROUTER_CAPABILITY_OPAQUE_ID;

OpaqueOriginatorV2::OpaqueOriginatorV2(OriginatorContext& ctx) : context(ctx)
{
}

bool OpaqueOriginatorV2::anyInterfaceOpaqueCapable() const
{
    bool capable = false;
    context.getIfaceMgr().forEach([&](OspfInterfaceId id, const OspfInterfaceBase& iface) {
        if (id.area != context.area.areaId) return;
        if (iface.getOpaqueEnabled()) capable = true;
    });
    return capable;
}

void OpaqueOriginatorV2::originateRouterCapability(uint32_t capabilities)
{
    if (!anyInterfaceOpaqueCapable())
    {
        withdrawRouterCapability();
        return;
    }

    if (originated && capabilities == lastCapabilities) return;

    uint32_t rid = context.area.process.getRouterId();
    LsaKey key(OSPFV2_LSA_OPAQUE_AREA, ROUTER_CAPABILITY_LINK_STATE_ID, rid);

    RouterCapabilityTlv tlv;
    tlv.capabilities = capabilities;

    OpaqueLsaV2 lsa;
    lsa.opaqueType = ROUTER_INFORMATION_OPAQUE_TYPE;
    lsa.opaqueId = ROUTER_CAPABILITY_OPAQUE_ID;
    lsa.payload.resize(RouterCapabilityTlv::size());
    tlv.buildBody(lsa.payload.data(), static_cast<uint16_t>(lsa.payload.size()));

    context.originateLsa<PolicyV2>(key, LsaBody(lsa), false);

    originated = true;
    lastCapabilities = capabilities;
}

void OpaqueOriginatorV2::withdrawRouterCapability()
{
    if (!originated) return;

    uint32_t rid = context.area.process.getRouterId();
    LsaKey key(OSPFV2_LSA_OPAQUE_AREA, ROUTER_CAPABILITY_LINK_STATE_ID, rid);

    context.originateLsa<PolicyV2>(key, context.originationState[key].body, true);

    originated = false;
    lastCapabilities = 0xFFFFFFFF;
}
} // namespace routing::ospf
