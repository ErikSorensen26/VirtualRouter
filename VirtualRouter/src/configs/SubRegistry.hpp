// SubRegistry.hpp

#ifndef SUB_REGISTRY_HPP
#define SUB_REGISTRY_HPP

#include <tuple>
#include <utility>
#include <cstddef>
#include <mutex>
#include <optional>

#include "RegistryTypes.hpp"

namespace Config
{
template <auto E>
inline constexpr size_t toIndex = static_cast<size_t>(E);

template <typename KEY, typename ENUM, typename Ctx, typename... Fields>
class SubRegistry
{
public:
    using keyType = KEY;
    using type    = ENUM;
    using ctxType = Ctx;

    // Meta tuple: used ONLY for compile-time checks / type indexing.
    using FieldTuple = std::tuple<Fields...>;

    // Storage tuple: holds non-movable fields without ever moving/copying them.
    using StorageTuple = std::tuple<std::optional<Fields>...>;

    static_assert(sizeof...(Fields) == Config::toIndex<ENUM::COUNT>);

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

    ContextProvider<Ctx>& context() noexcept { return ctxProvider; }

    explicit SubRegistry() noexcept
        : ctxProvider(),
          fields(),
          base(nullptr)
    {
        constructFields(std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
    }

    SubRegistry(SubRegistry& parent)
        : ctxProvider(parent.ctxProvider),
          fields(),
          base(&parent)
    {
        constructMaskedFields(parent, std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
    }

    template <ENUM F>
    decltype(auto) get() noexcept
    {
        constexpr size_t I = Config::toIndex<F>;
        return (*std::get<I>(fields));
    }

    template <ENUM F>
    decltype(auto) get() const noexcept
    {
        constexpr size_t I = Config::toIndex<F>;
        return (*std::get<I>(fields));
    }

    bool isMasked() const noexcept
    {
        return base != nullptr;
    }

    std::mutex mu;

private:
    // -------------------------
    // Field argument factories
    // -------------------------

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

    // ------------------------------------
    // In-place construction (NO moves/copies)
    // ------------------------------------

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
    void constructMaskedFields(const SubRegistry& parent, std::index_sequence<I...>) noexcept
    {
        (constructMaskedOne<I, std::tuple_element_t<I, FieldTuple>>(
            *std::get<I>(parent.fields)
        ), ...);
    }

    template <size_t I, typename F>
    void constructMaskedOne(const F& parentField) noexcept
    {
        auto& opt = std::get<I>(fields); // std::optional<F>
        std::apply(
            [&](auto&&... args)
            {
                opt.emplace(std::forward<decltype(args)>(args)...);
            },
            createMaskedField(parentField)
        );
    }

    ContextProvider<Ctx> ctxProvider;
    StorageTuple fields;
    const SubRegistry* base{nullptr};
};

template <typename KEY, typename ENUM, typename... Fields>
class SimpleSubRegistry
{
public:
    using keyType = KEY;
    using type    = ENUM;

    // Meta tuple (compile-time)
    using FieldTuple = std::tuple<Fields...>;

    // Storage tuple (runtime)
    using StorageTuple = std::tuple<std::optional<Fields>...>;

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

    explicit SimpleSubRegistry() noexcept
        : fields(),
          base(nullptr)
    {
        constructFields(std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
    }

    SimpleSubRegistry(SimpleSubRegistry& parent)
        : fields(),
          base(&parent)
    {
        constructMaskedFields(parent, std::make_index_sequence<std::tuple_size_v<FieldTuple>>{});
    }

    template <ENUM F>
    decltype(auto) get() noexcept
    {
        constexpr size_t I = Config::toIndex<F>;
        return (*std::get<I>(fields));
    }

    template <ENUM F>
    decltype(auto) get() const noexcept
    {
        constexpr size_t I = Config::toIndex<F>;
        return (*std::get<I>(fields));
    }

    bool isMasked() const noexcept
    {
        return base != nullptr;
    }

    std::mutex mu;

private:
    template <typename F>
    auto createField()
    {
        if constexpr (IsValueField<F> || IsOptionalValueField<F>)
            return std::forward_as_tuple(mu);
        return std::tuple<>{};
    }

    template <typename F>
    auto createMaskedField(const F& parentField)
    {
        if constexpr (IsValueField<F> || IsOptionalValueField<F>)
            return std::forward_as_tuple(mu, parentField);
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
    void constructMaskedFields(const SimpleSubRegistry& parent, std::index_sequence<I...>) noexcept
    {
        (constructMaskedOne<I, std::tuple_element_t<I, FieldTuple>>(
            *std::get<I>(parent.fields)
        ), ...);
    }

    template <size_t I, typename F>
    void constructMaskedOne(const F& parentField) noexcept
    {
        auto& opt = std::get<I>(fields); // std::optional<F>
        std::apply(
            [&](auto&&... args)
            {
                opt.emplace(std::forward<decltype(args)>(args)...);
            },
            createMaskedField(parentField)
        );
    }

    StorageTuple fields;
    const SimpleSubRegistry* base{nullptr};
};
}

#endif // SUB_REGISTRY_HPP

