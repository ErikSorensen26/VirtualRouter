/**
 * @file Expander.hpp
 * @brief Sub-parser expander: forwards a parent context field into a child parser.
 * @ingroup CLI_PARSER
 */

#include <type_traits>
#include "cli/modes/contexts/Context.hpp"
#include "cli/grammar/Token.hpp"

#ifndef EXPANDER_HPP
#define EXPANDER_HPP

namespace cli
{
/**
 * @brief Forwards a child context stored inside a parent context field to a sub-parser.
 * @ingroup CLI_PARSER
 *
 * `Expander` bridges two parser levels when the parent context holds a
 * `ReferenceContainer<ChildCtx>` field. On `tryExecute`, it retrieves the
 * child context from that field, wraps it in a `Context<ChildContextType>`,
 * propagates `negate` and `defaulted`, and delegates to `Parser::execute`.
 *
 * ## Architectural Role
 * Enables hierarchical command parsing where a parent mode (e.g., GlobalMode)
 * delegates a subtree of commands to a child parser (e.g., VrfParser) without
 * the child knowing about the parent mode's structure.
 *
 * ## Lifecycle & Ownership
 * Pure-static type — no instances, no data members. All work is done in the
 * single static `tryExecute` call.
 *
 * @tparam Parser     Child `CliModeParser` type to delegate to.
 * @tparam ParentCtx  SubRegistry type of the parent context. Must own a field
 *                    at enum index `CtxField` of type `ReferenceContainer<T>`
 *                    where `T` matches `Parser::ContextType`.
 * @tparam CtxField   Enum constant identifying the `ReferenceContainer` field
 *                    inside `ParentCtx` that holds the child context.
 *
 * @see expAdder
 */
template <
    typename Parser,
    typename ParentCtx,
    auto CtxField
>
struct Expander
{
    using ContextType = ParentCtx; ///< Context type shared with the parent parser.
    using ChildContextType = Parser::ContextType; ///< Context type sub parser uses.
    using ContextField = typename ParentCtx::template FieldTypeAt<CtxField>;

    static_assert(
        std::is_same_v<decltype(CtxField), typename ParentCtx::type>,
        "Parent must own the context field enumeration type."
    );
    static_assert(
        config::IsRefContainer<ContextField>,
        "Enumerated parent field must be a ReferenceContainer<T>"
    );
    static_assert(
        std::is_same_v<ChildContextType, typename ContextField::type>,
        "Enumerated reference container field type must match SubParser's context type."
    );

    /**
     * @brief Retrieves the child context and delegates token dispatch to the child parser.
     *
     * @param pctx    Parent context; the child context is extracted from its `CtxField` field.
     * @param tokens  Full flat token span from `CliSession`.
     * @param idx     Offset at which matching begins (forwarded unchanged to the child).
     * @return True if the child parser recognized and executed the command.
     */
    static bool tryExecute(Context<ContextType>& pctx, std::span<Token> tokens, size_t idx)
    {
        auto& childCtx = pctx.configs.template get<CtxField>().get();
        Context<ChildContextType> ctx(pctx.terminal, childCtx);
        ctx.negate = pctx.negate;
        ctx.defaulted = pctx.defaulted;
        return Parser::execute(ctx, tokens, idx);
    }
};

/**
 * @brief Convenience alias for @ref Expander used at command-definition sites.
 * @ingroup CLI_PARSER
 *
 * @tparam Parser     Child parser type.
 * @tparam ParentCtx  Parent SubRegistry type.
 * @tparam CtxField   Enum constant identifying the child context field.
 */
template <
    typename Parser,
    typename ParentCtx,
    auto CtxField
>
using expAdder = Expander<Parser, ParentCtx, CtxField>;
}

#endif // EXPANDER_HPP
