// UserExecContext.hpp

#ifndef USER_EXEC_CONTEXT_HPP
#define USER_EXEC_CONTEXT_HPP

#include "ContextBase.hpp"

namespace cli
{
struct UserExecContext : ContextBase
{
    UserExecContext(const ContextBase& base)
        : ContextBase(base) {}
};
}

#endif // USER_EXEC_CONTEXT_HPP
