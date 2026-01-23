// RegistryReference.hpp

#ifndef REGISTRY_REFERENCE_HPP
#define REGISTRY_REFERENCE_HPP

#include "RegistryBucket.hpp"

namespace Config
{
template <typename...>
class RegistryDatabase;

template <typename T>
class MaskSubRegistry;

template <typename T>
concept IsSubRegistry = requires
    {
        typename T::type;
        typename T::FieldTuple;
    };

template <typename I, typename T>
class Reference
{
public:
    using keyValue = I;

    Reference() = default;

    // Owning handle type
    Reference(const Reference&) = delete;
    Reference& operator=(const Reference&) = delete;

    // Movable so containers can store it
    Reference(Reference&& other) noexcept
        : key(other.key),
          bucket(other.bucket),
          handle(other.handle),
          ref(other.ref),
          owns(other.owns)
    {
        other.bucket = nullptr;
        other.ref = nullptr;
        other.owns = false;
        other.key = 0;
    }

    Reference& operator=(Reference&& other) noexcept
    {
        if (this == &other)
            return *this;

        reset();

        key = other.key;
        bucket = other.bucket;
        handle = other.handle;
        ref = other.ref;
        owns = other.owns;

        other.bucket = nullptr;
        other.ref = nullptr;
        other.owns = false;
        other.key = 0;

        return *this;
    }

    ~Reference()
    {
        reset();
    }

    bool bound() const noexcept
    {
        return bucket != nullptr && owns;
    }

    uint64_t getKey() const noexcept
    {
        return key;
    }

    T* ptr() const noexcept
    {
        return ref;
    }

    T& get() const noexcept
    {
        assert(ref != nullptr);
        return *ref;
    }

private:
    Reference(Bucket<T>&b, uint64_t k)
        : key(k),
          bucket(&b),
          handle(b.create()),
          ref(b.get(handle)),
          owns(true)
    {}

    Reference(Bucket<T>& b, uint64_t k, T&& initial)
        : key(k),
          bucket(&b),
          handle(b.create(std::move(initial))),
          ref(b.get(handle)),
          owns(true)
    {}

    void reset() noexcept
    {
        if (bucket && owns)
            bucket->erase(handle);

        bucket = nullptr;
        ref = nullptr;
        owns = false;
        key = 0;
    }

    uint64_t key{0};
    Bucket<T>* bucket{nullptr};
    typename Bucket<T>::Handle handle{};
    T* ref{nullptr};
    bool owns{false};

    template <typename...>
    friend class RegistryDatabase;
};

struct RefContainerFieldFlag {};

template <typename I, typename T, auto F>
class ReferenceContainer : public RefContainerFieldFlag
{
public:
    using type = T;
    using refType = I;
    static constexpr auto field = F;

    Reference<I, T>& get()
    {
        return ref;
    }

    const Reference<I, T>& get() const noexcept
    {
        return ref;
    }

private:
    Reference<I, T> ref;

    template <typename...>
    friend class RegistryDatabase;
};

template <typename I, typename T, auto F>
class MaskedReferenceContainer
{
    static_assert(IsSubRegistry<T>, "Reference type myust be a SubRegistry");

public:
    using type = typename MaskSubRegistry<T>::MaskOutputType;
    static constexpr auto field = F;

    MaskedReferenceContainer() = default;

    template <typename DB>
    explicit MaskedReferenceContainer(DB& db, Reference<I, T>& src)
    {
        if (src.bound())
        {
            maskedRef = db.template mask<I, T>(src);
        }
    }

    bool bound() const noexcept
    {
        return maskedRef.bound();
    }

    uint64_t getKey() const noexcept
    {
        return maskedRef.getKey();
    }

    type& get() noexcept
    {
        assert(maskedRef.bound());
        return maskedRef.get().fields;
    }

    const type& get() const noexcept
    {
        assert(maskedRef.bound());
        return maskedRef.get().fields;
    }

private:
    Reference<I, MaskSubRegistry<T>> maskedRef;
};
}

#endif // REGISTRY_REFERENCE_HPP
