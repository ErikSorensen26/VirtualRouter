// RegistryReference.hpp

#ifndef REGISTRY_REFERENCE_HPP
#define REGISTRY_REFERENCE_HPP

#include "RegistryBucket.hpp"

namespace Config
{
template <typename...>
class Registry;

template <typename I, typename T>
class Reference
{
public:
    using keyType = I;

    Reference() = delete;
    Reference(const Reference&) = delete;
    Reference(Reference&&) = delete;

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
        bucket.erase(handle);
    }

    const uint64_t key;
    Bucket<T>& bucket;
    Bucket<T>::Handle& handle;
    T& ref;

    template <typename...>
    friend class Registry;
};
}

#endif // REGISTRY_REFERENCE_HPP
