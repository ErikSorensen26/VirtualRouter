// Registry.hpp

#ifndef REGISTRY_DATABASE_HPP
#define REGISTRY_DATABASE_HPP

#include "RegistryReference.hpp"

namespace Config
{

template <typename... Entries>
class RegistryDatabase
{
    template <typename Entry>
    struct BucketHolder
    {
        using keyValue = typename Entry::type;
        using valueType = Entry;

        Bucket<valueType> bucket;
    };

    std::tuple<BucketHolder<Entries>...> buckets;

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
                >::keyType
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

    template <typename Key>
    auto& bucket()
    {
        return getHolder<Key>().bucket;
    }

    template <typename Key>
    const auto& bucket() const
    {
        return getHolder<Key>().bucket;
    }

    template <typename I, typename T>
    Reference<I, T> createReference(uint64_t key)
    {
        return Reference<I, T>(bucket<I>(), key);
    }
};
}

#endif // REGISTRY_HPP
