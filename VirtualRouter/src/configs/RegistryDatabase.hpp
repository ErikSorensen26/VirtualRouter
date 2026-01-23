// Registry.hpp

#ifndef REGISTRY_DATABASE_HPP
#define REGISTRY_DATABASE_HPP

#include "RegistryReference.hpp"

namespace Config
{
enum RegistryRole
{
    BASE,
    MASK
};

template <typename K, RegistryRole R>
struct RegistryKey
{
    using keyType = K;
    static constexpr RegistryRole role = R;
};

template <typename T>
concept IsMask =
    requires
    {
        typename T::MaskInputType;
        typename T::MaskOutputType;
    };

template <typename T>
concept IsBase =
    requires
    {
        typename T::FieldTuple;
    } && (!IsMask<T>);

template <typename T>
struct RegistrySelector
{
    static_assert(IsBase<T> || IsMask<T>,
        "RegistrySelector error: Entry is not a leaf value type");
};

template <IsBase T>
struct RegistrySelector<T>
{
    using key = RegistryKey<typename T::type, BASE>;
};

template <IsMask T>
struct RegistrySelector<T>
{
    using key = RegistryKey<typename T::type, MASK>;
};

template <typename... Entries>
class RegistryDatabase
{
    template <typename Entry>
    struct BucketHolder
    {
        using keyValue = typename RegistrySelector<Entry>::key;
        using valueType = Entry;

        Bucket<valueType> bucket;
    };

    using Holders = std::tuple<BucketHolder<Entries>...>;
    Holders buckets;

    template <typename Key, std::size_t I = 0>
    static constexpr size_t findIndex()
    {
        if constexpr (I == sizeof...(Entries))
        {
            static_assert(I != sizeof...(Entries),
                "Registry does not contain a bucket for this key type");
            return 0;
        }
        else if constexpr (
            std::is_same_v<
                Key,
                typename std::tuple_element_t<I,
                    std::tuple<BucketHolder<Entries>...>
                >::keyValue
            >
        )
        {
            return I;
        }
        else
        {
            return findIndex<Key, I + 1>();
        }
    }

    template <typename Key>
    auto& getHolder()
    {
        constexpr size_t index = findIndex<Key>();
        return std::get<index>(buckets);
    }

public:
    RegistryDatabase() = default;
    RegistryDatabase(const RegistryDatabase&) = delete;
    RegistryDatabase& operator=(const RegistryDatabase&) = delete;

    template <typename K, RegistryRole R>
    auto& bucket()
    {
        return getHolder<RegistryKey<K, R>>().bucket;
    }

    template <typename K, RegistryRole R>
    const auto& bucket() const
    {
        return getHolder<RegistryKey<K, R>>().bucket;
    }

    template <typename K, typename T, typename... Args>
    Reference<K, T> createBase(uint64_t key, Args&&... args)
    {
        auto& b = bucket<K, BASE>();
        return Reference<K, T>(b, key, T(std::forward<Args>(args)...));
    }

    template <typename K, typename T, auto F, typename... Args>
    void emplaceBase(ReferenceContainer<K, T, F>& out, uint64_t key, Args&&... args)
    {
        out.ref = createBase<K, T>(key, std::forward<Args>(args)...);
    }

    template <typename K, typename T>
    Reference<K, MaskSubRegistry<T>> mask(Reference<K, T>& baseRef)
    {
        static_assert(IsSubRegistry<T>, "mask(): T must be a SubRegistry type");
        assert(baseRef.bound());

        auto& mb = bucket<K, MASK>();

        MaskSubRegistry<T> maskObj(*this, baseRef.get());

        return Reference<K, MaskSubRegistry<T>>(mb, baseRef.getKey(), std::move(maskObj));
    }
};
}

#endif // REGISTRY_HPP
