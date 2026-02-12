// EigrpContext.hpp

#ifndef EIGRP_CONTEXT_HPP
#define EIGRP_CONTEXT_HPP

#include "ContextBase.hpp"

#define EIGRP_PARAMS EigrpContext& ctx, const std::vector<std::string>& args

namespace Eigrp
{
class Eigrp;
class EigrpNamed;
}
namespace EigrpConfigs
{
struct InterfaceConfigs;
}

namespace Cli
{
struct EigrpContext : ContextBase
{
    EigrpContext(const ContextBase& base, Eigrp::Eigrp* eigrp, Eigrp::EigrpNamed* named, EigrpConfigs::InterfaceConfigs* iface, Eigrp::Eigrp* temp = nullptr)
        : ContextBase(base), currentEigrp(eigrp), currentEigrpNamed(named), currentEigrpInterface(iface), tempEigrp(temp) {}
    Eigrp::Eigrp* currentEigrp;
    Eigrp::EigrpNamed* currentEigrpNamed;
    EigrpConfigs::InterfaceConfigs* currentEigrpInterface;
    Eigrp::Eigrp* tempEigrp;
};
}

#endif // GLOBAL_CONTEXT_HPP
