// OspfContext.hpp

#ifndef OSPF_CONTEXT_HPP
#define OSPF_CONTEXT_HPP

#include "ContextBase.hpp"

#define OSPF_PARAMS OspfContext& ctx, const std::vector<std::string>& args

namespace routing::ospf { class OspfProcess; }

namespace cli
{
struct OspfContext : ContextBase
{
    OspfContext(const ContextBase& base, routing::ospf::OspfProcess& process)
        : ContextBase(base), ospf(process) {}

    routing::ospf::OspfProcess& ospf;
};
}

#endif // GLOBAL_CONTEXT_HPP
