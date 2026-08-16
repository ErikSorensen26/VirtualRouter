// EigrpInterfaceRegistry.cpp

#include "EigrpInterfaceRegistry.h"
#include "eigrp/interface/EigrpInterface.h"

namespace config
{
DEFINE_CONFIG_APPLIER(EigrpInterface, PASSIVE_INTERFACE, ctx, p)
{
    if (routing::eigrp::EigrpInterface* iface = Context::cast<routing::eigrp::EigrpInterface*>(ctx); iface)
        iface->enqueueSetPassive(p);
}

DEFINE_CONFIG_APPLIER(EigrpInterface, SHUTDOWN, ctx, shut)
{
    if (routing::eigrp::EigrpInterface* iface = Context::cast<routing::eigrp::EigrpInterface*>(ctx); iface)
        iface->enqueueSetShutdown(shut);
}

DEFINE_CONFIG_APPLIER(EigrpInterface, SUMMARY_ADDRESS, ctx, sum, add)
{
    if (routing::eigrp::EigrpInterface* iface = Context::cast<routing::eigrp::EigrpInterface*>(ctx); iface)
        iface->enqueueSetSummary(sum.prefix(), sum.leakMap(), add);
}
}
