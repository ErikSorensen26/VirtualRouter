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
    Reference<T> create(uint64_t key)
    {
        auto& b = bucket<T>();
        auto h = b.create(key);
        return Reference<T>(b, key, h);
    }

    template <typename T>
    Reference<T> create(uint64_t key, Reference<T>& parent)
    {
        auto& b = bucket<T>();
        auto h = b.create(key, parent.get());
        return Reference<T>(b, key, h);
    }

    template <typename T>
    Reference<T> create()
    {
        auto& b = bucket<T>();
        auto h = b.createAuto();
        return Reference<T>(b, b.slotKey(h), h);
    }

    template <typename T>
    Reference<T> create(Reference<T>& parent)
    {
        auto& b = bucket<T>();
        auto h = b.createAuto(parent.get());
        return Reference<T>(b, b.slotKey(h), h);
    }

    template <typename T CONFIG_INDEX_PARAM>
    Reference<T> emplace(ReferenceContainer<T CONFIG_INDEX_ARG(F)>& container)
    {
        if (container.bound())
            return container.ref.value();

        Reference<T> ref = container.base
            ? create<T>(*container.base)
            : create<T>();
        container.setLocal(ref);
        return ref;
    }

    template <typename T CONFIG_INDEX_PARAM>
    Reference<T> emplace(ReferenceContainer<T CONFIG_INDEX_ARG(F)>& container, Reference<T>& parent)
    {
        if (container.ref.has_value())
            return container.ref.value();

        if (!container.base)
            container.base = &parent;

        Reference<T> ref(create<T>(*container.base));
        container.setLocal(ref);
        return ref;
    }

    template <typename T CONFIG_INDEX_PARAM>
    Reference<T> ensure(ReferenceContainer<T CONFIG_INDEX_ARG(F)>& container)
    {
        Reference<T> ref = container.base
            ? create<T>(*container.base)
            : create<T>();
        container.unsetLocal();
        container.setLocal(ref);
        return ref;
    }

    template <typename T CONFIG_INDEX_PARAM>
    Reference<T> ensure(ReferenceContainer<T CONFIG_INDEX_ARG(F)>& container, Reference<T>& parent)
    {
        container.base = &parent;
        Reference<T> ref = create<T>(*container.base);
        container.unsetLocal();
        container.setLocal(ref);
        return ref;
    }

    template <typename T, typename K CONFIG_INDEX_PARAM>
    Reference<T> emplaceBack(OwnedListField<T, K CONFIG_INDEX_ARG(F)>& list, const K& id)
    {
        if (auto it = list.children.find(id); it != list.children.end())
            return it->second;
        return list.getMutable().emplace(id, create<T>()).first->second;
    }

    template <typename T, typename K CONFIG_INDEX_PARAM>
    Reference<T> emplaceBack(OwnedListField<T, K CONFIG_INDEX_ARG(F)>& list, const K& id, const Reference<T>& parent)
    {
        if (auto it = list.children.find(id); it != list.children.end())
            return it->second;
        return list.getMutable().emplace(id, create<T>(const_cast<Reference<T>&>(parent))).first->second;
    }
};
}

#endif // REGISTRY_HPP
