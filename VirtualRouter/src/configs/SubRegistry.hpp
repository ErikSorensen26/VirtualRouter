// RegistryTypes.hpp

#ifndef SUB_REGISTRY_HPP
#define SUB_REGISTRY_HPP

#include <tuple>
#include <utility>
#include <cstddef>
#include <mutex>
#include <RegistryTypes.hpp>

namespace Config
{
template <auto E>
inline constexpr size_t toIndex = static_cast<size_t>(E);

template <typename KEY, typename ENUM, typename Ctx, typename... Fields>
class SubRegistry
{
public:
    using keyType = KEY;
    using type = ENUM;
    using ctxType = Ctx;
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

    ContextProvider<Ctx>& context() noexcept { return ctxProvider; }

    explicit SubRegistry() noexcept
        : fields(this->template createField<Fields>()...),
          base(nullptr)
    {}

    SubRegistry(SubRegistry& parent)
        : fields(parent, std::make_index_sequence<std::tuple_size_v<FieldTuple>>{}),
          base(&parent),
          ctxProvider(parent.ctxProvider)
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

    std::mutex mu;

private:

    template <typename F>
    F createField()
    {
        if constexpr (IsValueField<F>)
        {
            if constexpr (RequiresContext<F>)
                return F(ctxProvider, mu);
            else
                return F(mu);
        }
        else if constexpr (RequiresContext<F>)
            return F(ctxProvider);
        else
            return F{};
    }

    template <size_t... I>
    FieldTuple makeMaskedFields(
        const SubRegistry& parent,
        std::index_sequence<I...>
    ) noexcept
    {
        return FieldTuple(
            createMaskedField(
                std::get<I>(parent.fields)
            )...
        );
    }

    template <typename F>
    F createMaskedField(const F& parentField)
    {
        if constexpr (IsValueField<F>)
        {
            if constexpr (RequiresContext<F>)
                return F(ctxProvider, mu, parentField);
            else
                return F(mu, parentField);
        }
        else if constexpr (RequiresContext<F>)
            return F(ctxProvider, parentField);
        else
            return F(parentField);
    }

    FieldTuple fields;
    const SubRegistry* base{nullptr};
    ContextProvider<Ctx> ctxProvider;
};

template <typename KEY, typename ENUM, typename... Fields>
class SimpleSubRegistry
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

    explicit SimpleSubRegistry() noexcept
        : fields(this->template createField<Fields>()...),
          base(nullptr)
    {}

    SimpleSubRegistry(SimpleSubRegistry& parent)
        : fields(parent, std::make_index_sequence<std::tuple_size_v<FieldTuple>>{}),
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

    std::mutex mu;

private:

    template <typename F>
    F createField()
    {
        if constexpr (IsValueField<F>)
            return F(mu);
        return F{};
    }

    template <size_t... I>
    FieldTuple makeMaskedFields(
        const SimpleSubRegistry& parent,
        std::index_sequence<I...>
    ) noexcept
    {
        return FieldTuple(
            createMaskedField(
                std::get<I>(parent.fields)
            )...
        );
    }

    template <typename F>
    F createMaskedField(const F& parentField)
    {
        if constexpr (IsValueField<F>)
            return F(mu, parentField);
        return F(parentField);
    }

    FieldTuple fields;
    const SimpleSubRegistry* base{nullptr};
};
}

#endif // SUB_REGISTRY_HPP
