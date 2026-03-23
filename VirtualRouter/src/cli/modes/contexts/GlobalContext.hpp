// GlobalContext.hpp

#ifndef GLOBAL_CONTEXT_HPP
#define GLOBAL_CONTEXT_HPP

#include "ContextBase.hpp"

namespace core { class Global; }
namespace core { class VirtualRouter; }

#define GLOBAL_PARAMS GlobalContext& ctx, const std::vector<std::string>& args

namespace cli
{
struct GlobalContext : ContextBase
{
    GlobalContext(const ContextBase& base, core::Global& glob, core::VirtualRouter& vrf)
        : ContextBase(base), global(glob), vrf(vrf) {}
    core::Global& global;
    core::VirtualRouter& vrf;
};
}

#endif // GLOBAL_CONTEXT_HPP
