// EigrpInterfaceRegistry.cpp

#include "EigrpInterfaceRegistry.h"
#include "eigrp/interface/EigrpInterface.h"

namespace config
{
void EigrpIfacePassive(void* i)
{
    routing::eigrp::EigrpInterface& iface = *static_cast<routing::eigrp::EigrpInterface*>(i);
    iface.enqueueSyncPassive();
}

void EigrpIfaceShutdown(void* i)
{
    routing::eigrp::EigrpInterface& iface = *static_cast<routing::eigrp::EigrpInterface*>(i);
    iface.enqueueRefreshInterfaceList();
}

void EigrpIfaceSummary(void* i)
{
    routing::eigrp::EigrpInterface& iface = *static_cast<routing::eigrp::EigrpInterface*>(i);
    iface.enqueueSyncSummary();
}
}
