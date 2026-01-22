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
concept IsAtomicField = requires
    {
        typename T::type;
        { T::dValue };
        { T::field };
    };

template <typename T>
concept IsUnsetAtomicField = requires
    {
        typename T::type;
        typename T::isUnset;
        { T::field };
    };

template <typename T>
concept IsRefContainer = requires
    {
        typename T::type;
        typename T::refType;
        { T::field };
    };

template <typename T>
concept IsMaskRefContainer = requires
    {
        typename T::type;
        typename T::refType;
        { T::refIndex };
        { T::field };
    } && (!IsRefContainer<T>);

template <typename T>
concept IsVariableField = requires
    {
        typename T::type;
        { T::field };
    } && (!IsAtomicField<T>) && (!IsRefContainer<T>) && (!IsMaskRefContainer<T>);

template <typename T>
struct MaskedFieldSelector;

template <IsAtomicField T>
struct MaskedFieldSelector<T>
{
    using type = MaskedAtomicField<
        typename T::type,
        T::dValue,
        T::field
    >;
};

template <IsUnsetAtomicField T>
struct MaskedFieldSelector<T>
{
    using type = MaskedUnsetAtomicField<
        typename T::type,
        T::field
    >;
};

template <IsRefContainer T>
struct MaskedFieldSelector<T>
{
    using type = T*;
};

template <IsMaskRefContainer T>
struct MaskedFieldSelector<T>
{
    using type = T*;
};

template <IsVariableField T>
struct MaskedFieldSelector<T>
{
    using type = MaskedVariableField<
        typename T::type,
        T::field
    >;
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
        return Config::tupleGetRuntime(fields, Config::toIndex<f>);
    }
};

template <typename Sub>
struct MaskTuple;

template <typename ENUM, typename... Ts>
struct MaskTuple<SubRegistry<ENUM, Ts...>>
{
    using MaskInputType = SubRegistry<ENUM, Ts...>;
    using MaskOutputType = std::tuple<MaskedFieldFor<Ts>...>;

    template <typename S>
    static MaskOutputType apply(S&& s)
    {
        return applyImpl(
            std::forward<S>(s).fields,
            std::index_sequence_for<Ts...>{}
        );
    }

private:
    template <typename Field>
    static auto makeMasked(Field& f)
    {
        if constexpr (IsAtomicField<Field>)
        {
            return MaskedAtomicField<
                typename Field::type,
                Field::dValue,
                Field::field
            >(f);
        }
        else if constexpr (IsUnsetAtomicField<Field>)
        {
            return MaskedUnsetAtomicField<
                typename Field::type,
                Field::field
            >(f);
        }
        else if constexpr (IsRefContainer<Field>)
        {
            return MaskedRefContainer<
                Field::field
            >();
        }
        else
        {
            static_assert(
                IsVariableField<Field>,
                "Field must be AtomicField or VariableField"
            );

            return MaskedVariableField<
                typename Field::type,
                Field::field
            >(f);
        }
    }

    template <typename T, size_t... I>
    static MaskOutputType applyImpl(
        T&& t,
        std::index_sequence<I...>
    )
    {
        return MaskOutputType{
            makeMasked(std::get<I>(t))...
        };
    }
};

template <typename Sub>
class MaskSubRegistry;

template <typename ENUM, typename... Ts>
class MaskSubRegistry<SubRegistry<ENUM, Ts...>>
{
public:
    using InputType = SubRegistry<ENUM, Ts...>;
    using OutputType = typename MaskTuple<InputType>::OutputType;

    OutputType fields;

    template <typename S>
    explicit MaskSubRegistry(S&& s)
        : fields(MaskTuple<InputType>::apply(std::forward<S>(s)))
    {}

    template <ENUM F>
    decltype(auto) get() noexcept
    {
        return std::get<Config::toIndex<F>>(fields);
    }

    decltype(auto) get(ENUM f) noexcept
    {
        return Config::tupleGetRuntime(fields, Config::toIndex<f>);
    }
};
}

#endif // SUB_REGISTRY_HPP
