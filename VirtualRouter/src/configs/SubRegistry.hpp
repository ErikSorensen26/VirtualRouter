// SubRegistry.hpp

#ifndef SUB_REGISTRY_HPP
#define SUB_REGISTRY_HPP

#include <tuple>
#include <utility>
#include <cstddef>
#include <mutex>
#include <optional>

#include "RegistryTypes.hpp"
#include "RegistryDefaultTable.hpp"

namespace config
{
template <auto E>
inline constexpr size_t toIndex = static_cast<size_t>(E);

template <typename ENUM, typename... Fields>
class SubRegistry
{
public:
    using type    = ENUM;

    // Meta tuple: used ONLY for compile-time checks / type indexing.
    using FieldTuple = std::tuple<Fields...>; // Storage tuple: holds non-movable fields without ever moving/copying them.
    using StorageTuple = std::tuple<std::optional<Fields>...>;

    static_assert(sizeof...(Fields) == config::toIndex<ENUM::COUNT>);

#if USE_CONFIG_INDEX
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
#endif

    ContextProvider& context() noexcept { return ctxProvider; }

    explicit SubRegistry() noexcept
        : ctxProvider(),
          fields(),
          base(nullptr)
    {
        constructFields(std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
        installDefaults(std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
    }

    SubRegistry(SubRegistry& parent)
        : ctxProvider(parent.ctxProvider),
          fields(),
          base(&parent)
    {
        constructFields(std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
        installDefaults(std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
        applyMask(base, std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
    }

    template <ENUM F>
    decltype(auto) get() noexcept
    {
        constexpr size_t I = config::toIndex<F>;
        return (*std::get<I>(fields));
    }

    template <ENUM F>
    decltype(auto) get() const noexcept
    {
        constexpr size_t I = config::toIndex<F>;
        return (*std::get<I>(fields));
    }

    bool isMasked() const noexcept
    {
        return base != nullptr;
    }

    void setMask(SubRegistry* parent)
    {
        applyMask(parent, std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
    }

    std::mutex mu;

private:

    template <typename F>
    auto createField()
    {
        if constexpr (IsValueField<F> || IsOptionalValueField<F>)
        {
            if constexpr (RequiresContext<F>)
                return std::forward_as_tuple(ctxProvider, mu);
            else
                return std::forward_as_tuple(mu);
        }
        else if constexpr (RequiresContext<F>)
            return std::forward_as_tuple(ctxProvider);
        else
            return std::tuple<>{};
    }

    template <typename F>
    auto createMaskedField(const F& parentField)
    {
        if constexpr (IsValueField<F> || IsOptionalValueField<F>)
        {
            if constexpr (RequiresContext<F>)
                return std::forward_as_tuple(ctxProvider, mu, parentField);
            else
                return std::forward_as_tuple(mu, parentField);
        }
        else if constexpr (RequiresContext<F>)
            return std::forward_as_tuple(ctxProvider, parentField);
        else
            return std::forward_as_tuple(parentField);
    }

    template <size_t... I>
    void constructFields(std::index_sequence<I...>) noexcept
    {
        (constructOne<I, std::tuple_element_t<I, FieldTuple>>(), ...);
    }

    template <size_t I, typename F>
    void constructOne() noexcept
    {
        auto& opt = std::get<I>(fields); // std::optional<F>
        std::apply(
            [&](auto&&... args)
            {
                opt.emplace(std::forward<decltype(args)>(args)...);
            },
            this->template createField<F>()
        );
    }

    template <size_t... I>
    void installDefaults(std::index_sequence<I...>) noexcept
    {
        (installDefaultOne<I>(), ...);
    }

    template <size_t I>
    void installDefaultOne() noexcept
    {
        using Field = std::tuple_element_t<I, FieldTuple>;

        if constexpr (config::IsAtomicField<Field>)
        {
            constexpr ENUM E = static_cast<ENUM>(I);

            using T = typename Field::type;

            static_assert(hasV<ENUM, E>, "Missing default for an AtomicField<...> entry (ENUM,E).");

            auto& f = *std::get<I>(fields);
            f.setDefault(getV<ENUM, E, T>());
        }
    }

    template <size_t... I>
    void applyMask(SubRegistry* parent, std::index_sequence<I...>) noexcept
    {
        (applyMaskOne<I>(parent), ...);
    }

    template <size_t I>
    void applyMaskOne(SubRegistry* parent) noexcept
    {
        using Field = std::tuple_element_t<I, FieldTuple>;

        auto& local = *std::get<I>(fields);

        if constexpr (requires(Field& f, const Field* p) { f.setMask(p); })
        {
            const Field* parentField =
                parent ? std::get<I>(parent->fields).operator->() : nullptr;

            local.setMask(parentField);
        }
    }

    ContextProvider ctxProvider;
    StorageTuple fields;
    SubRegistry* base{nullptr};
};
}

#endif // SUB_REGISTRY_HPP
