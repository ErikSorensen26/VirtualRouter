/**
 * @file TupleStaging.hpp
 * @brief Collects the members of a tuple-valued field and writes them as one entry.
 *
 * Most commands write a single field, and @ref ExecutorUtils.hpp translates those
 * a token at a time. Tuple fields cannot work that way: `range A.B.C.D cost 100`
 * names two members of one list entry, and writing each as it is parsed would
 * insert two half-filled entries instead of one whole one.
 *
 * So the executor stages instead of writing. Each bound token contributes a
 * @ref TupleMember naming its field and its index within that field's tuple, and
 * nothing reaches the config until the line is done. At that point the members
 * are grouped by field, each group fills one default-constructed tuple, and the
 * result is inserted once. Members the line never named keep their defaults,
 * which is what makes `range A.B.C.D` and `range A.B.C.D cost 100` differ only
 * in the cost rather than in how many entries they leave behind.
 *
 * Staging is sequencing, not translation, which is why it lives apart from the
 * token-to-field helpers it calls into.
 */

#ifndef CLI_TUPLE_STAGING_HPP
#define CLI_TUPLE_STAGING_HPP

#include <algorithm>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include "cli/execution/ExecutorUtils.hpp"
#include "cli/session/Token.hpp"
#include "cli/modes/Context.hpp"
#include "cli/tree/nodes/Command.h"

namespace cli::execution
{

/**
 * @brief Writes one member into a staging tuple, by runtime index.
 *
 * std::get needs the index at compile time, so the runtime one is matched
 * against an expansion over the tuple's size and the branch that matches
 * does the write.
 */
template <typename Node>
bool writeTupleMember(cli::ContextBase& ctx, Node& staging, Token* value, Token* extra)
{
    const tree::CommandNode& bound = value->node.node();

    return [&]<std::size_t... Is>(std::index_sequence<Is...>)
    {
        bool done = false;
        ([&]{
            if (bound.tupleMember() != Is) return;

            auto& elem = std::get<Is>(staging);
            using Elem = std::remove_cvref_t<decltype(elem)>;

            // An enum member is set by being named: the value rides the node
            // rather than the word, so no token is read for it.
            if (bound.hasTupleEnum())
            {
                if constexpr (std::is_enum_v<Elem>)
                {
                    elem = static_cast<Elem>(bound.tupleEnumIndex());
                    done = true;
                }
                return;
            }

            // A bool member is set by being named, so it takes no token and
            // must not be failed for arriving without one.
            if constexpr (std::is_same_v<Elem, bool>)
            {
                elem = !ctx.negate;
                done = true;
            }
            else if constexpr (utils::isOptional<Elem>::value)
            {
                typename Elem::value_type v{};
                if constexpr (utils::DoubleValued<typename Elem::value_type>)
                {
                    if (utils::setDoubleTupleElement(v, value, extra))
                    {
                        elem = v;
                        done = true;
                    }
                }
                else if (utils::setTupleElement(v, value))
                {
                    elem = v;
                    done = true;
                }
            }
            else if constexpr (utils::DoubleValued<Elem>)
            {
                done = utils::setDoubleTupleElement(elem, value, extra);
            }
            else
            {
                done = utils::setTupleElement(elem, value);
            }
        }(), ...);
        return done;
    }(std::make_index_sequence<std::tuple_size_v<Node>>{});
}

/**
 * @brief Builds one tuple from a field's staged members.
 *
 * Members the line did not name keep the tuple's default. A member that was
 * named but could not be translated fails the whole tuple, because a partly
 * filled entry is worse than none: it would differ from the intended one in a
 * way nothing downstream can detect.
 */
template <typename Node>
bool fillTuple(cli::ContextBase& ctx, Node& staging, std::span<Token*> members)
{
    bool filled = true;

    for (size_t i = 0; i < members.size(); )
    {
        // A member spelled with two words binds twice on the same slot, and the
        // second token is the rest of the value rather than another member.
        Token* extra = nullptr;
        if (i + 1 < members.size() &&
            members[i + 1]->node.node().tupleMember() == members[i]->node.node().tupleMember())
        {
            extra = members[i + 1];
        }

        if (!writeTupleMember(ctx, staging, members[i], extra))
            filled = false;

        i += extra ? 2 : 1;
    }

    return filled;
}

/**
 * @brief Fills one tuple from the members staged for a single field and writes it.
 *
 * Both tuple-carrying field kinds land here. They differ only in where the
 * tuple type comes from and how a finished tuple is stored -- a list field
 * inserts one entry among many, a value field holds exactly one -- so the
 * staging and filling above is shared and only the write is chosen per kind.
 */
inline bool commitOneTuple(cli::ContextBase& ctx, std::span<Token*> members)
{
    // Every member in the span shares a field, so any of them identifies it.
    const tree::CommandNode probe{ .configId = members[0]->node.node().configId };

    bool ok = false;
    utils::visitBound(ctx, probe, [&](auto&& accessor)
    {
        using Visited = std::remove_cvref_t<decltype(accessor)>;

        if constexpr (requires { typename Visited::Field; })
        {
            using Field = typename Visited::Field;

            if constexpr (config::IsListField<Field>)
            {
                using Node = typename Field::element;

                if constexpr (utils::IsTuple<Node>::value)
                {
                    Node staging{};
                    if (fillTuple(ctx, staging, members))
                        ok = utils::setListEntry(accessor, ctx, staging,
                                                 members[0]->node.nodeIndex());
                }
            }
            else if constexpr (config::IsValueField<Field>)
            {
                using Node = typename Field::type;

                if constexpr (utils::IsTuple<Node>::value)
                {
                    Node staging = accessor.hasValue() ? accessor.load() : Node{};
                    if (fillTuple(ctx, staging, members))
                        ok = utils::setValueEntry(accessor, ctx, staging,
                                                  members[0]->node.nodeIndex());
                }
            }
        }
    });

    return ok;
}

/**
 * @brief Writes each staged field's members as one list entry.
 *
 * Members of the same field are gathered into a single staging tuple and
 * inserted once. Anything not named on the line keeps the tuple's default,
 * which is what makes `range A.B.C.D` and `range A.B.C.D cost 100` differ
 * only in the cost rather than in how many entries they leave behind.
 */
inline bool commitTuples(cli::ContextBase& ctx, std::vector<Token*>& staged)
{
    if (staged.empty()) return true;

    // Grouped by field so each one commits once, and stable so a member spelled
    // with two words keeps the order its tokens arrived in.
    std::stable_sort(staged.begin(), staged.end(),
        [](const Token* a, const Token* b)
        { return a->node.node().configId < b->node.node().configId; });

    bool ok = true;

    for (size_t i = 0; i < staged.size(); )
    {
        const uint32_t field = staged[i]->node.node().configId;

        size_t end = i;
        while (end < staged.size() && staged[end]->node.node().configId == field) ++end;

        if (!commitOneTuple(ctx, std::span<Token*>(staged).subspan(i, end - i)))
            ok = false;

        i = end;
    }

    return ok;
}
}

#endif // CLI_TUPLE_STAGING_HPP
