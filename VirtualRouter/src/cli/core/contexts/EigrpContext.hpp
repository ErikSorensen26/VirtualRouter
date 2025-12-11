// EigrpContext.hpp

#ifndef EIGRP_CONTEXT_HPP
#define EIGRP_CONTEXT_HPP

#include <ContextBase.hpp>

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
    EigrpContext(ContextBase& base, Eigrp::Eigrp* eigrp, Eigrp::EigrpNamed* named)
        : ContextBase(base), currentEigrp(eigrp), currentEigrpNamed(named) {}
    Eigrp::Eigrp* currentEigrp;
    Eigrp::EigrpNamed* currentEigrpNamed;
};
}

#endif // GLOBAL_CONTEXT_HPP
