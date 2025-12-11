// ContextBase.hpp

#ifndef CONTEXT_BASE_HPP
#define CONTEXT_BASE_HPP

class CliSession;

namespace Cli
{
struct ContextBase
{
    ContextBase(CliSession& term) : terminal(term) {}
    ContextBase(ContextBase& base) : terminal(base.terminal), negate(base.negate) {}
    virtual ~ContextBase() = default;
    CliSession& terminal;
    bool negate = false;
};
}

#endif // CONTEXT_BASE_HPP
