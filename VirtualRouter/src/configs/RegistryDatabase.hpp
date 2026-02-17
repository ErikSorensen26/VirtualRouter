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
    Reference<T> create(typename T::keyType key, Reference<T>& parent)
    {
        auto& b = bucket<T>();
        auto h = b.create(key, parent.get());
        return Reference<T>(b, key, h);
    }

    template <typename T CONFIG_INDEX_PARAM>
    Reference<T> emplace(ReferenceContainer<T CONFIG_INDEX_ARG(F)>& container, typename T::keyType key)
    {
        if (container.bound())
        {
            assert(container.ref->getKey() == key);
            return container.ref.value();
        }

        Reference<T> ref = container.base
            ? create<T>(key, *container.base)
            : create<T>(key);
        container.setLocal(ref);
        return ref;
    }

    template <typename T CONFIG_INDEX_PARAM>
    Reference<T> emplace(ReferenceContainer<T CONFIG_INDEX_ARG(F)>& container, Reference<T>& parent, typename T::keyType key)
    {
        if (container.ref.has_value())
        {
            assert(container.ref->getKey() == key);
            return container.ref.value();
        }

        if (!container.base)
            container.base = &parent;

        Reference<T> ref(create<T>(key, *container.base)); // Masked Version
        container.setLocal(ref);
        return ref;
    }

    template <typename T CONFIG_INDEX_PARAM>
    Reference<T> ensure(ReferenceContainer<T CONFIG_INDEX_ARG(F)>& container, typename T::keyType key)
    {
        Reference<T> ref = container.base
            ? create<T>(key, *container.base)
            : create<T>(key);
        container.unsetLocal();
        container.setLocal(ref);
        return ref;
    }

    template <typename T CONFIG_INDEX_PARAM>
    Reference<T> ensure(ReferenceContainer<T CONFIG_INDEX_ARG(F)>& container, Reference<T>& parent, typename T::keyType key)
    {
        container.base = &parent;
        Reference<T> ref = create<T>(key, *container.base); // Masked Version
        container.unsetLocal();
        container.setLocal(ref);
        return ref;
    }


    template <typename T, typename K CONFIG_INDEX_PARAM>
    Reference<T> emplaceBack(OwnedListField<T, K CONFIG_INDEX_ARG(F)>& list, const K& id, typename T::keyType key)
    {
        if (auto it = list.children.find(id); it != list.children.end())
            return it->second;
        return list.getMutable().emplace(id, create<T>(key)).first->second;
    }

    template <typename T, typename K CONFIG_INDEX_PARAM>
    Reference<T> emplaceBack(OwnedListField<T, K CONFIG_INDEX_ARG(F)>& list, const K& id, const Reference<T>& parent, typename T::keyType key)
    {
        if (auto it = list.children.find(id); it != list.children.end())
            return it->second;
        return list.getMutable().emplace({id, create<T>(key, parent)}).first->second;
    }
};
}

#endif // REGISTRY_HPP
