// TupleSchema.hpp

#ifndef TUPLE_SCHEMA_HPP
#define TUPLE_SCHEMA_HPP

#include <tuple>
#include <utility>      // std::declval, std::forward
#include <cstddef>      // std::size_t
#include <type_traits>  // std::tuple_size_v, std::is_nothrow_constructible_v

// ============================================================
//
// Usage:
//
//   #define MY_FIELDS(X) \
//       X(int,    Foo)   \
//       X(double, Bar)
//
//   DEFINE_TUPLE_SCHEMA(MySchema, MY_FIELDS);
//
//   MySchemaTuple t = MakeMySchema(1, 3.14);
//   auto& foo = MySchema::Foo(t);
//   auto& bar = MySchema::get<MySchemaIndex::Index_Bar>(t);
//
// ============================================================


// ------------------------------------------------------------
// Internal X-macro expanders (do not call directly)
// ------------------------------------------------------------

// enum indices (trailing comma OK in enum lists)
#define TS_INDEX_ELEM(T, Name) Index_##Name,

// for building a tuple type without needing comma-joining logic:
// we build it as decltype(tuple_cat(tuple<T1>, tuple<T2>, ..., tuple<>))
#define TS_TUPLE_CAT_ELEM(T, Name) std::declval<std::tuple<T>>(),

// named accessor methods (inside Schema struct)
#define TS_NAMED_ACCESSOR(T, Name)                                                        \
    static T& Name(Tuple& t) noexcept {                                                   \
        return std::get<static_cast<std::size_t>(Index::Index_##Name)>(t);                \
    }                                                                                     \
    static const T& Name(const Tuple& t) noexcept {                                       \
        return std::get<static_cast<std::size_t>(Index::Index_##Name)>(t);                \
    }

#define DEFINE_TUPLE_SCHEMA(Schema, FIELD_LIST)                                           \
    struct Schema final {                                                                 \
        enum class Index : std::size_t {                                                  \
            FIELD_LIST(TS_INDEX_ELEM)                                                     \
            Count                                                                         \
        };                                                                                \
                                                                                          \
        /* Build std::tuple<T...> without needing separator-aware macros. */              \
        using Tuple = decltype(std::tuple_cat(                                            \
            FIELD_LIST(TS_TUPLE_CAT_ELEM)                                                 \
            std::declval<std::tuple<>>()                                                  \
        ));                                                                               \
                                                                                          \
        static constexpr std::size_t Count = static_cast<std::size_t>(Index::Count);      \
                                                                                          \
        template <Index I>                                                                \
        using FieldType = std::tuple_element_t<static_cast<std::size_t>(I), Tuple>;       \
                                                                                          \
        template <Index I>                                                                \
        static decltype(auto) get(Tuple& t) noexcept {                                    \
            return std::get<static_cast<std::size_t>(I)>(t);                              \
        }                                                                                 \
        template <Index I>                                                                \
        static decltype(auto) get(const Tuple& t) noexcept {                              \
            return std::get<static_cast<std::size_t>(I)>(t);                              \
        }                                                                                 \
                                                                                          \
        /* Named accessors: Schema::FieldName(tuple) */                                   \
        FIELD_LIST(TS_NAMED_ACCESSOR)                                                     \
                                                                                          \
        template <typename... Args>                                                       \
        static Tuple make(Args&&... args)                                                 \
            noexcept(noexcept(Tuple{ std::forward<Args>(args)... }))                      \
        {                                                                                 \
            static_assert(sizeof...(Args) == Count,                                       \
                          "Make<Schema>: argument count must match schema field count");  \
            return Tuple{ std::forward<Args>(args)... };                                  \
        }                                                                                 \
    };                                                                                    \

#endif // TUPLE_SCHEMA_HPP

