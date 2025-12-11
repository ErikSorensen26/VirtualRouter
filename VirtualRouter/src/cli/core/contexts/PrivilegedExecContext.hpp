// PrivilegedExecContext.hpp

#ifndef PRIVILEGED_EXEC_CONTEXT_HPP
#define PRIVILEGED_EXEC_CONTEXT_HPP

#include "ContextBase.hpp"

namespace Cli
{
struct PrivilegedExecContext : ContextBase
{
    PrivilegedExecContext(ContextBase& base)
        : ContextBase(base) {}
};
}

#endif // PRIVILEGED_EXEC_CONTEXT_HPP
