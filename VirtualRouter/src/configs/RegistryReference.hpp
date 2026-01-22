// RegistryReference.hpp

#ifndef REGISTRY_REFERENCE_HPP
#define REGISTRY_REFERENCE_HPP

#include "RegistryBucket.hpp"

namespace Config
{
template <typename...>
class RegistryDatabase;

template <typename T>
concept IsSubRegistry = requires
    {
        typename T::InputType;
        typename T::OutputType;
    };

template <typename I, typename T>
class Reference
{
public:
    using keyType = I;

    Reference() = default;

    bool bound() const noexcept
    {
        return ref != nullptr;
    }

    T* ptr() const noexcept
    {
        return ref;
    }

private:
    Reference(Bucket<T>& b, uint64_t k)
        : bucket(b), handle(b.create()), ref(b.get(handle)), key(k)
    {}

    ~Reference()
    {
        if (bucket && handle)
            bucket->erase(handle);
    }

    const uint64_t key;
    Bucket<T>* bucket{nullptr};
    Bucket<T>::Handle* handle{nullptr};
    T* ref{nullptr};

    template <typename...>
    friend class RegistryDatabase;
};

template <typename I, typename T, auto F>
class ReferenceContainer
{
    static_assert(IsSubRegistry<T>, "Type must be a SubRegistry");
public:
    using type = T;
    using refType = I;
    static constexpr auto field = F;

    Reference<I, T>& get()
    {
        return *ref;
    }

private:
    Reference<I, T> ref;
};
}

#endif // REGISTRY_REFERENCE_HPP
