// RegistryTypes.hpp

#ifndef REGISTRY_TYPES_HPP
#define REGISTRY_TYPES_HPP

#include <atomic>
#include <concepts>
#include <cassert>
#include <utility>
#include <optional>
#include <vector>

namespace Config
{
template <typename T, auto F>
class MaskedReferenceContainer;

template <typename T>
class Reference;

struct AtomicFieldFlag {};
struct OptionalAtomicFieldFlag : AtomicFieldFlag {};
struct RefContainerFieldFlag {};
struct ValueFieldFlag {};
struct OwnedListFieldFlag {};

template <typename T>
concept IsFieldBase =
    requires
    {
        typename T::type;
        { T::field };
    };

enum class MaskState : uint8_t
{
    INHERIT,
    SET
};

template <typename T, T D, auto F>
class AtomicField : public AtomicFieldFlag
{
public:
    using type = T;
    static constexpr auto dValue = D;
    static constexpr auto field = F;

    AtomicField() = default;

    AtomicField(const AtomicField& parent) noexcept
        : value(D),
          state(MaskState::INHERIT),
          base(&parent)
    {}

    inline T load() const noexcept
    {
        if (base && state.load(std::memory_order_relaxed) == MaskState::INHERIT)
            return base->load();

        return value.load(std::memory_order_relaxed);
    }

    inline void set(T v) noexcept
    {
        value.store(v, std::memory_order_release);
        state.store(MaskState::SET, std::memory_order_release);
    }

    inline void unset() noexcept
    {
        value.store(dValue, std::memory_order_relaxed);
        state.store(MaskState::INHERIT, std::memory_order_relaxed);
    }

    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == MaskState::SET;
    }

private:
    std::atomic<T> value{D};
    std::atomic<MaskState> state{MaskState::INHERIT};
    const AtomicField* base{nullptr};
};

template <typename T, auto F>
class OptionalAtomicField : public OptionalAtomicFieldFlag
{
public:
    using type = T;
    static constexpr auto field = F;

    OptionalAtomicField() = default;

    explicit OptionalAtomicField(const OptionalAtomicField& parent) noexcept
        : value(),
          state(MaskState::INHERIT),
          base(&parent)
    {}

    inline bool hasValue() const noexcept
    {
        if (state.load(std::memory_order_relaxed) == MaskState::SET)
            return true;

        if (base)
            return base->hasValue();

        return false;
    }

    inline T load() const noexcept
    {
        if (base && state.load(std::memory_order_relaxed) == MaskState::INHERIT)
        {
            assert(base->hasValue());
            return base->load();
        }
        return value.load(std::memory_order_relaxed);
    }

    inline void set(T v) noexcept
    {
        value.store(v, std::memory_order_release);
        state.store(MaskState::SET, std::memory_order_release);
    }

    inline void unset() noexcept
    {
        state.store(MaskState::INHERIT, std::memory_order_release);
    }

    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == MaskState::SET;
    }

private:
    std::atomic<T> value{};
    std::atomic<MaskState> state{MaskState::INHERIT};
    const OptionalAtomicField* base{nullptr};
};

template <typename T, auto F>
class ValueField : public ValueFieldFlag
{
public:
    using type = T;
    static constexpr auto field = F;

    ValueField() = default;

    explicit ValueField(const ValueField& parent) noexcept
        : value(),
          state(MaskState::SET),
          base(&parent)
    {}

    inline T& get() noexcept
    {
        if (base && state == MaskState::INHERIT)
            return base->get();
        return value;
    }

    inline const T& get() const noexcept
    {
        if (base && state == MaskState::INHERIT)
            return base->get();
        return value;
    }

    void set(const T& v)
    {
        value = v;
        state = MaskState::SET;
    }

    void set(T&& v)
    {
        value = std::move(v);
        state = MaskState::SET;
    }

    inline void unset() noexcept
    {
        value = T{};
        state = MaskState::INHERIT;
    }

    inline bool overridden() const noexcept
    {
        return state == MaskState::SET;
    }

private:
    T value{};
    MaskState state;
    const ValueField* base{nullptr};
};

template <typename T, auto F>
class OwnedListField : public OwnedListFieldFlag
{
public:
    using type = std::vector<std::pair<uint32_t, Reference<T>>>;
    static constexpr auto field = F;

    OwnedListField() = default;

    explicit OwnedListField(const OwnedListField& parent) noexcept
        : children(),
          state(MaskState::INHERIT),
          base(&parent)
    {}

    inline type& getMutable() noexcept
    {
        state = MaskState::SET;
        return children;
    }

    inline const type& get() const noexcept
    {
        if (base && state == MaskState::INHERIT)
            return base->get();
        return children;
    }

    inline void clear() noexcept
    {
        children.clear();
        state = MaskState::INHERIT;
    }

    inline bool overridden() const noexcept
    {
        return state == MaskState::SET;
    }

private:
    type children{};
    MaskState state{MaskState::INHERIT};
    const OwnedListField* base{nullptr};
};

template <typename T>
concept IsAtomicField =
    IsFieldBase<T> &&
    std::derived_from<T, AtomicFieldFlag> &&
    (!std::derived_from<T, OptionalAtomicFieldFlag>);

template <typename T>
concept IsOptionalAtomicField =
    IsFieldBase<T> &&
    std::derived_from<T, OptionalAtomicFieldFlag>;

template <typename T>
concept IsRefContainer =
    IsFieldBase<T> &&
    std::derived_from<T, RefContainerFieldFlag>;

template <typename T>
concept IsValueField =
    IsFieldBase<T> &&
    std::derived_from<T, ValueFieldFlag>;

template <typename T>
concept IsOwnedListField =
    IsFieldBase<T> &&
    std::derived_from<T, OwnedListFieldFlag>;
}

#endif // REGISTRY_TYPES_HPP
