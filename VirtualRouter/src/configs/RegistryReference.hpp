// RegistryReference.hpp

#ifndef REGISTRY_REFERENCE_HPP
#define REGISTRY_REFERENCE_HPP

#include "RegistryBucket.hpp"
#include "RegistryTypes.hpp"
#include <cassert>

namespace Config
{
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
    using keyType = T::keyType;

    Reference(const Reference& other) noexcept
        : key(other.key),
          bucket(other.bucket),
          handle(other.handle),
          ref(other.ref)
    {
        bucket.addRef(handle);
    }

    Reference(Reference&& other) noexcept
        : key(other.key),
          bucket(other.bucket),
          handle(other.handle),
          ref(other.ref)
    {
        bucket.addRef(handle);
    }

    Reference& operator=(const Reference&) = delete;
    Reference& operator=(Reference&&) = delete;

    ~Reference()
    {
        bucket.releaseRef(handle);
    }

    keyType getKey() const noexcept
    {
        return key;
    }

    T* operator->() noexcept
    {
        return &ref;
    }

    const T* operator->() const noexcept
    {
        return &ref;
    }

    T& get() noexcept
    {
        return ref;
    }

    const T& get() const noexcept
    {
        return ref;
    }

private:
    Reference(Bucket<T>& b, T::keyType k, const typename Bucket<T>::Handle& h)
        : key(k),
          bucket(b),
          handle(h),
          ref(b.get(h))
    {
        bucket.addRef(handle);
    }

    keyType key{};
    Bucket<T>& bucket;
    typename Bucket<T>::Handle handle{};
    T& ref;

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

    explicit ReferenceContainer(const Reference<T>& parent) noexcept
        : ref(std::nullopt),
          state(MaskState::INHERIT),
          base(&parent)
    {}

    explicit ReferenceContainer(const ReferenceContainer& parent) noexcept
        : ref(std::nullopt),
          state(MaskState::INHERIT),
          base(&parent.local())
    {}

    bool bound() const noexcept
    {
        if (state == MaskState::SET)
            return ref.has_value();

        return base ? base->bound() : false;
    }

    const Reference<T>& effective() const noexcept
    {
        if (base && state == MaskState::INHERIT)
            return base->effective();

        assert(ref.has_value());
        return *ref;
    }

    const Reference<T>& get() const noexcept
    {
        assert(bound());
        return effective();
    }

    Reference<T>& local() noexcept
    {
        assert(ref.has_value());
        return *ref;
    }

    const Reference<T>& local() const noexcept
    {
        assert(ref.has_value());
        return *ref;
    }

private:
    template <typename...>
    friend class RegistryDatabase;

    void setLocal(const Reference<T>& r) noexcept
    {
        ref.emplace(r);
        state = MaskState::SET;
    }

    void unsetLocal() noexcept
    {
        ref.reset();
        state = MaskState::INHERIT;
    }

    std::optional<Reference<T>> ref{std::nullopt};
    MaskState state{MaskState::INHERIT};
    const Reference<T>* base{nullptr};
};
}

#endif // REGISTRY_REFERENCE_HPP
