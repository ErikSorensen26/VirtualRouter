// RegistryDatabase.hpp

#ifndef REGISTRY_DATABASE_HPP
#define REGISTRY_DATABASE_HPP

#include "RegistryReference.hpp"
#include <tuple>
#include <cassert>

namespace Config
{

template <typename... Entries>
class RegistryDatabase
{
    template <typename Entry>
    struct Holder
    {
        Bucket<Entry> bucket;
    };

    std::tuple<Holder<Entries>...> holders;

    template <typename T>
    Holder<T>& getHolder()
    {
        return std::get<Holder<T>>(holders);
    }

public:
    RegistryDatabase() = default;

    template <typename T>
    Bucket<T>& bucket()
    {
        return getHolder<T>().bucket;
    }

    template <typename T>
    Reference<T> create(typename T::keyType key)
    {
        auto& b = bucket<T>();
        auto h = b.create(key);
        return Reference<T>(b, key, h);
    }

    template <typename T>
    Reference<T> create(typename T::keyType key, const T& parent)
    {
        auto& b = bucket<T>();
        auto h = b.create(key, *this, parent);
        return Reference<T>(b, key, h);
    }

    template <typename T, auto F>
    Reference<T> emplace(ReferenceContainer<T, F>& container, typename T::keyType key)
    {
        if (container.ref.bound())
        {
            assert(container.ref.getKey() == key);
            return container.ref;
        }

        Reference<T> ref;
        if (container.base && assert(container.base->bound()))
            ref = create<T>(key, *container.base); // Masked Version
        ref = create<T>(key);
        container.setLocal(ref);
        return ref;
    }

    template <typename T, auto F>
    Reference<T> ensure(ReferenceContainer<T, F>& container, typename T::keyType key)
    {
        Reference<T> ref;
        if (container.base && assert(container.base->bound()))
            ref = create<T>(key, *container.base); // Masked Version
        ref = create<T>(key);
        container.unsetLocal();
        container.setLocal(ref);
        return ref;
    }
};
}

#endif // REGISTRY_HPP
