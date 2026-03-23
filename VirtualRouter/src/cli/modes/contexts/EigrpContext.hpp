// EigrpContext.hpp

#ifndef EIGRP_CONTEXT_HPP
#define EIGRP_CONTEXT_HPP

#include "ContextBase.hpp"
#include "configs/registry/router/EigrpInterfaceRegistry.h"

#define EIGRP_PARAMS EigrpContext& ctx, const std::vector<std::string>& args

namespace routing::eigrp { class Eigrp; class EigrpNamed; }

namespace cli
{
struct EigrpContext : ContextBase
{
    EigrpContext(const ContextBase& base, routing::eigrp::Eigrp* eigrp, routing::eigrp::EigrpNamed* named, config::EigrpInterfaceRegistry* iface, routing::eigrp::Eigrp* temp = nullptr)
        : ContextBase(base), currentEigrp(eigrp), currentEigrpNamed(named), currentEigrpInterface(iface), tempEigrp(temp) {}
    routing::eigrp::Eigrp* currentEigrp;
    routing::eigrp::EigrpNamed* currentEigrpNamed;
    config::EigrpInterfaceRegistry* currentEigrpInterface;
    routing::eigrp::Eigrp* tempEigrp;
};
}

#endif // GLOBAL_CONTEXT_HPP
