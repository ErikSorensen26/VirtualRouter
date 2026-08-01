/**
 * @file TupleSchema.hpp
 */

// TupleSchema.hpp

#ifndef TUPLE_SCHEMA_HPP
#define TUPLE_SCHEMA_HPP

#include <tuple>
#include <utility>      // std::declval
#include <cstddef>      // std::size_t
#include <type_traits>  // std::tuple_element_t

/**
 * Usage:
 *
 *   #define MY_FIELDS(X) \
 *       X(int,    Foo)   \
 *       X(double, Bar)
 *
 *   DEFINE_TUPLE_SCHEMA(MySchema, MY_FIELDS);
 *
 *   MySchema::Tuple t{ 1, 3.14 };
 *   auto& foo = MySchema::Foo(t);
 *
 * If a field type contains a top level comma the preprocessor would read it as
 * two arguments, so parenthesize it. The parentheses are stripped by TS_TYPE
 * and are not part of the resulting type.
 *
 *       X((std::variant<A, B>), Foo)
 */
namespace config::ts
{
/**
 * void(T) is a function type taking one parameter; peel T back out of it.
 * This is what lets a parenthesized (T) survive as a single macro argument.
 */
template <typename> struct Unparen;
template <typename T> struct Unparen<void(T)> { using type = T; };

template <typename T> using Unparen_t = typename Unparen<T>::type;
}

// Accepts either a bare type or a parenthesized one: TS_TYPE(int), TS_TYPE((A<x,y>)).
#define TS_TYPE(T) config::ts::Unparen_t<void(T)>

#define TS_INDEX_ELEM(T, Name)    Index_##Name,
#define TS_TUPLE_ELEM(T, Name)    std::declval<std::tuple<TS_TYPE(T)>>(),

#define TS_ACCESSOR_ELEM(T, Name)                                                         \
    static TS_TYPE(T)& Name(Tuple& t) noexcept {                                          \
        return std::get<static_cast<std::size_t>(Index::Index_##Name)>(t);                \
    }                                                                                     \
    static const TS_TYPE(T)& Name(const Tuple& t) noexcept {                              \
        return std::get<static_cast<std::size_t>(Index::Index_##Name)>(t);                \
    }

#define DEFINE_TUPLE_SCHEMA(Schema, FIELD_LIST)                                           \
    struct Schema final {                                                                 \
        enum class Index : std::size_t {                                                  \
            FIELD_LIST(TS_INDEX_ELEM)                                                     \
            Count                                                                         \
        };                                                                                \
                                                                                          \
        using Tuple = decltype(std::tuple_cat(                                            \
            FIELD_LIST(TS_TUPLE_ELEM)                                                     \
            std::declval<std::tuple<>>()                                                  \
        ));                                                                               \
                                                                                          \
        static constexpr std::size_t Count = static_cast<std::size_t>(Index::Count);      \
                                                                                          \
        template <Index I>                                                                \
        using FieldType = std::tuple_element_t<static_cast<std::size_t>(I), Tuple>;       \
                                                                                          \
        FIELD_LIST(TS_ACCESSOR_ELEM)                                                      \
    };

#endif // TUPLE_SCHEMA_HPP
