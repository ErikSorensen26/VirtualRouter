// RegistryTypes.hpp

#ifndef SUB_REGISTRY_HPP
#define SUB_REGISTRY_HPP

#include <cstddef>
#include <tuple>
#include <utility>

namespace Config
{
template <auto E>
inline constexpr size_t toIndex = static_cast<size_t>(E);

template <typename KEY, typename ENUM, typename... Fields>
class SubRegistry
{
public:
    using keyType = KEY;
    using type = ENUM;
    using FieldTuple = std::tuple<Fields...>;

    static_assert(sizeof...(Fields) == Config::toIndex<ENUM::COUNT>);

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

    explicit SubRegistry() noexcept
        : fields(Fields{}...)
    {}

    SubRegistry(SubRegistry& parent)
        : fields(Fields{}...),
          base(&parent)
    {}

    template <ENUM F>
    decltype(auto) get() noexcept
    {
        constexpr size_t I = Config::toIndex<F>;
        return (std::get<I>(fields));
    }

    template <ENUM F>
    decltype(auto) get() const noexcept
    {
        constexpr size_t I = Config::toIndex<F>;
        return (std::get<I>(fields));
    }

    bool isMasked() const noexcept
    {
        return base != nullptr;
    }

private:

    template <size_t... I>
    static FieldTuple makeMaskedFields(const SubRegistry& parent, std::index_sequence<I...>) noexcept
    {
        return FieldTuple(Fields(std::get<I>(parent.fields))...);
    }

    FieldTuple fields;
    const SubRegistry* base{nullptr};
};
}

#endif // SUB_REGISTRY_HPP
