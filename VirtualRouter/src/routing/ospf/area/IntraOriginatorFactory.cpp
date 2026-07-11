
#include "ospf/ospfv2/area/IntraOriginatorV2.h"
#include "ospf/ospfv3/area/IntraOriginatorV3.h"
#include "ospf/OspfProcess.h"

namespace routing::ospf
{
    IntraOriginator& IntraOriginator::create(OriginatorContext& ctx)
    {
        if (ctx.area.process.isV3)
            return *new IntraOriginatorV3(ctx);
        return *new IntraOriginatorV2(ctx);
    }
}
