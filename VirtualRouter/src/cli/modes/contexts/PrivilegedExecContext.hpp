// PrivilegedExecContext.hpp

#ifndef PRIVILEGED_EXEC_CONTEXT_HPP
#define PRIVILEGED_EXEC_CONTEXT_HPP

#include "ContextBase.hpp"

namespace cli
{
struct PrivilegedExecContext : ContextBase
{
    PrivilegedExecContext(const ContextBase& base)
        : ContextBase(base) {}
};
}

#endif // PRIVILEGED_EXEC_CONTEXT_HPP
