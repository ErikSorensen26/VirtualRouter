// GlobalRegistry.cpp

#include <Global.h>
#include "GlobalRegistry.h"

namespace config
{
void globalInterface(void* g)
{
    core::Global& global = *static_cast<core::Global*>(g);
    global.interfaceRefresh();
}
}
