// EigrpContext.hpp

#ifndef EIGRP_CONTEXT_HPP
#define EIGRP_CONTEXT_HPP

#include "ContextBase.hpp"
#include "configs/registry/router/EigrpInterfaceRegistry.h"

#define EIGRP_PARAMS EigrpContext& ctx, const std::vector<std::string>& args

namespace EIGRP
{
class Eigrp;
class EigrpNamed;
}

namespace Cli
{
struct EigrpContext : ContextBase
{
    EigrpContext(const ContextBase& base, EIGRP::Eigrp* eigrp, EIGRP::EigrpNamed* named, Config::EigrpInterfaceRegistry* iface, EIGRP::Eigrp* temp = nullptr)
        : ContextBase(base), currentEigrp(eigrp), currentEigrpNamed(named), currentEigrpInterface(iface), tempEigrp(temp) {}
    EIGRP::Eigrp* currentEigrp;
    EIGRP::EigrpNamed* currentEigrpNamed;
    Config::EigrpInterfaceRegistry* currentEigrpInterface;
    EIGRP::Eigrp* tempEigrp;
};
}

#endif // GLOBAL_CONTEXT_HPP
