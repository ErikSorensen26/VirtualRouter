// MaskedSubRegistry.hpp

#ifndef MASKED_SUB_REGISTRY_HPP
#define MASKED_SUB_REGISTRY_HPP

#include "RegistryReference.hpp"
#include "SubRegistry.hpp"

namespace Config
{
template <typename Sub>
struct MaskTuple;

template <typename ENUM, typename... Ts>
struct MaskTuple<SubRegistry<ENUM, Ts...>>
{
    using MaskInputType = SubRegistry<ENUM, Ts...>;
    using MaskOutputType = std::tuple<MaskedFieldFor<Ts>...>;

    template <typename DB, typename S>
    static MaskOutputType apply(DB& db, S&& s)
    {
        return applyImpl(
            db,
            std::forward<S>(s).fields,
            std::index_sequence_for<Ts...>{}
        );
    }

private:
    template <typename DB, typename Field>
    static auto makeMasked(DB& db, Field& f)
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
            return MaskedReferenceContainer<
                typename Field::refType,
                typename Field::type,
                Field::field
            >(db, f.get());
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

    template <typename DB, typename T, size_t... I>
    static MaskOutputType applyImpl(
        DB& db,
        T&& t,
        std::index_sequence<I...>
    )
    {
        return MaskOutputType{
            makeMasked(db, std::get<I>(t))...
        };
    }
};

template <typename Sub>
class MaskSubRegistry;

template <typename ENUM, typename... Ts>
class MaskSubRegistry<SubRegistry<ENUM, Ts...>>
{
public:
    using type = ENUM;
    using MaskInputType = SubRegistry<ENUM, Ts...>;
    using MaskOutputType = typename MaskTuple<MaskInputType>::MaskOutputType;

    MaskOutputType fields;

    template <typename DB, typename S>
    explicit MaskSubRegistry(DB& db, S&& s)
        : fields(MaskTuple<MaskInputType>::apply(db, std::forward<S>(s)))
    {}

    template <ENUM F>
    decltype(auto) get() const noexcept
    {
        return std::get<Config::toIndex<F>>(fields);
    }

    decltype(auto) get(ENUM f) const noexcept
    {
        return Config::tupleGetRuntime(fields, static_cast<size_t>(f));
    }
};
}

#endif // MASKED_SUB_REGISTRY_HPP
