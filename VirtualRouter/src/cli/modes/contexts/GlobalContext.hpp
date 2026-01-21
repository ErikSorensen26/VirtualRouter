// GlobalContext.hpp

#ifndef GLOBAL_CONTEXT_HPP
#define GLOBAL_CONTEXT_HPP

#include <ContextBase.hpp>

#define GLOBAL_PARAMS GlobalContext& ctx, const std::vector<std::string>& args

class Global;
class VirtualRouter;

namespace Cli
{
struct GlobalContext : ContextBase
{
    GlobalContext(const ContextBase& base, Global& glob, VirtualRouter& vrf)
        : ContextBase(base), global(glob), vrf(vrf) {}
    Global& global;
    VirtualRouter& vrf;
};
}

#endif // GLOBAL_CONTEXT_HPP
