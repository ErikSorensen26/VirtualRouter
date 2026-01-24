// RegistryReference.hpp

#ifndef REGISTRY_REFERENCE_HPP
#define REGISTRY_REFERENCE_HPP

#include "RegistryBucket.hpp"
#include <RegistryTypes.hpp>
#include <cassert>

namespace Config
{
template <typename...>
class RegistryDatabase;

template <typename T>
concept IsSubRegistry = requires
    {
        typename T::type;
        typename T::FieldTuple;
    };

template <typename T>
class Reference
{
public:
    using keyType = T::type;

    Reference() = default;

    Reference(const Reference& other) noexcept
        : key(other.key), bucket(other.bucket), handle(other.handle)
    {
        if (bucket)
            bucket->addRef(handle);
    }

    Reference& operator=(const Reference& other) noexcept
    {
        if (this == &other)
            return *this;

        reset();
        key = other.key;
        bucket = other.bucket;
        handle = other.handle;

        if (bucket)
            bucket->addRef(handle);

        return *this;
    }

    Reference(Reference&& other) noexcept
        : key(other.key), bucket(other.bucket), handle(other.handle)
    {
        other.key = 0;
        other.bucket = nullptr;
        other.handle = {};
    }

    Reference& operator=(Reference&& other) noexcept
    {
        if (this == &other)
            return *this;

        reset();
        key = other.key;
        bucket = other.bucket;
        handle = other.handle;

        other.key = 0;
        other.bucket = nullptr;
        other.handle = {};

        return *this;
    }

    ~Reference()
    {
        reset();
    }

    bool bound() const noexcept
    {
        return bucket != nullptr && bucket->get(handle) != nullptr;
    }

    T::keyType getKey() const noexcept
    {
        return key;
    }

    T* ptr() const noexcept
    {
        return bucket ? bucket->get(handle) : nullptr;
    }

    T& get() const noexcept
    {
        T* p = ptr();
        assert(p != nullptr);
        return *p;
    }

private:
    Reference(Bucket<T>& bucket, T::keyType k, const typename Bucket<T>::Handle& h)
        : key(k), bucket(&bucket), handle(h)
    {
        assert(bucket->get(handle) != nullptr);
        bucket->addRef(handle);
    }

    void reset() noexcept
    {
        if (bucket)
            bucket->releaseRef(handle);

        key = 0;
        bucket = nullptr;
        handle = {};
    }

    keyType key{0};
    Bucket<T>* bucket{nullptr};
    typename Bucket<T>::Handle handle{};

    template <typename...>
    friend class RegistryDatabase;
};

template <typename T, auto F>
class ReferenceContainer : public RefContainerFieldFlag
{
public:
    using type = T;
    using fieldType = decltype(F);
    static constexpr auto field = F;

    ReferenceContainer() = default;

    explicit ReferenceContainer(const ReferenceContainer& parent) noexcept
        : ref(),
          state(MaskState::INHERIT),
          base(&parent)
    {}

    const Reference<T>& effective() const noexcept
    {
        if (base && state == MaskState::INHERIT)
            return base->effective();

        assert(ref.bound());
        return ref;
    }

    const Reference<T>& get() const noexcept
    {
        const auto& eff = effective();
        assert(eff.bound());
        return eff;
    }

    Reference<T>& local() noexcept
    {
        assert(ref.bound());
        return ref;
    }

    const Reference<T>& local() const noexcept
    {
        assert(ref.bound());
        return ref;
    }

private:
    template <typename...>
    friend class RegistryDatabase;

    void setLocal(const Reference<T>& r) noexcept
    {
        ref = r;
        state = MaskState::SET;
    }

    void unsetLocal() noexcept
    {
        ref = Reference<T>{};
        state = MaskState::INHERIT;
    }

    Reference<T> ref{};
    MaskState state{MaskState::INHERIT};
    const ReferenceContainer* base{nullptr};
};
}

#endif // REGISTRY_REFERENCE_HPP
