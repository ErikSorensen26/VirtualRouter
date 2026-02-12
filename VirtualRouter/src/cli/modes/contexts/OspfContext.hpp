// OspfContext.hpp

#ifndef OSPF_CONTEXT_HPP
#define OSPF_CONTEXT_HPP

#include "ContextBase.hpp"

#define OSPF_PARAMS OspfContext& ctx, const std::vector<std::string>& args

namespace OSPF
{
class OspfProcess;
}

namespace Cli
{
struct OspfContext : ContextBase
{
    OspfContext(const ContextBase& base, OSPF::OspfProcess& process)
        : ContextBase(base), ospf(process) {}

    OSPF::OspfProcess& ospf;
};
}

#endif // GLOBAL_CONTEXT_HPP
