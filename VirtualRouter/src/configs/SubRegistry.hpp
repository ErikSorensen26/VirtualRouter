// RegistryTypes.hpp

#ifndef SUB_REGISTRY_HPP
#define SUB_REGISTRY_HPP

#include "RegistryTypes.hpp"
#include <tuple>
#include <utility>
#include <type_traits>

namespace Config
{
template <typename T>
struct MaskedFieldSelector
{
    static_assert(IsFieldBase<T>, "T is not a valid field type");
};

template <IsUnsetAtomicField T>
struct MaskedFieldSelector<T>
{
    using type = MaskedUnsetAtomicField<
        typename T::type,
        T::field
    >;
};

template <IsAtomicField T>
    requires (!IsUnsetAtomicField<T>)
struct MaskedFieldSelector<T>
{
    using type = MaskedAtomicField<
        typename T::type,
        T::dValue,
        T::field
    >;
};

template <IsRefContainer T>
struct MaskedFieldSelector<T>
{
    using type = MaskedReferenceContainer<
        typename T::refType,
        typename T::type,
        T::field
    >;
};

template <typename T>
concept IsMultiplicityVariableField =
    IsVariableField<T> &&
    VariableMultiplicityField<typename T::type>;

template <typename T>
    requires IsMultiplicityVariableField<T>
struct MaskedFieldSelector<T>
{
    using type = MaskedVariableField<
        typename T::type,
        T::field
    >;
};

template <typename T>
    requires IsVariableField<T> &&
             (!VariableMultiplicityField<typename T::type>)
struct MaskedFieldSelector<T>
{
    using type = typename T::type*;
};

template <typename T>
using MaskedFieldFor = typename MaskedFieldSelector<T>::type;

template <auto E>
inline constexpr size_t toIndex =
    static_cast<size_t>(E);

template <size_t I = 0, typename T>
decltype(auto) tupleGetRuntime(T& t, std::size_t idx)
{
    if constexpr (I < std::tuple_size_v<std::remove_reference_t<T>>)
    {
        if (I == idx)
            return std::get<I>(t);
        else 
            return tupleGetRuntime<I + 1>(t, idx);
    }
    else
    {
        __builtin_unreachable();
    }
}

template <typename ENUM, typename... Fields>
class SubRegistry
{
public:
    static_assert(sizeof...(Fields) == Config::toIndex<ENUM::COUNT>);
    
    using type = ENUM;

    using FieldTuple = std::tuple<Fields...>;

    static_assert(
        []<size_t... Is>(std::index_sequence<Is...>) constexpr
        {
            return (
                (static_cast<size_t>(
                    std::tuple_element_t<Is, FieldTuple>::field
                ) == Is) && ...
            );
        }(std::make_index_sequence<std::tuple_size_v<FieldTuple>>{}),
        "Tuple order must match enum field indicies"
    );

    FieldTuple fields;

    template <ENUM F>
    decltype(auto) get() noexcept
    {
        return std::get<Config::toIndex<F>>(fields);
    }

    decltype(auto) get(ENUM f) noexcept
    {
        return Config::tupleGetRuntime(fields, static_cast<size_t>(f));
    }
};
}

#endif // SUB_REGISTRY_HPP
