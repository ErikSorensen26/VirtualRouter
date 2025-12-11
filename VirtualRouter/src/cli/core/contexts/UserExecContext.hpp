// UserExecContext.hpp

#ifndef USER_EXEC_CONTEXT_HPP
#define USER_EXEC_CONTEXT_HPP

#include "ContextBase.hpp"

namespace Cli
{
struct UserExecContext : ContextBase
{
    UserExecContext(ContextBase& base)
        : ContextBase(base) {}
};
}

#endif // USER_EXEC_CONTEXT_HPP
