// RegistryTypes.hpp

#ifndef REGISTRY_TYPES_HPP
#define REGISTRY_TYPES_HPP

#include <atomic>
#include <concepts>
#include <cassert>
#include <utility>
#include <vector>
#include <algorithm>

namespace Config
{
template <typename...>
class RegistryDatabase;

template <typename T>
class Reference;

using ApplyKey = uint64_t;

template <typename Ctx, typename T>
using ApplyFn = void (*)(Ctx& ctx, const T& value) noexcept;

template <typename Ctx, typename T>
using OptionalApplyFn = void (*)(Ctx& ctx, const std::optional<T> value) noexcept;

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

template <typename T, T D, auto F, typename Ctx, auto H>
class AtomicField;

template <typename T, T D, auto F>
class AtomicField<T, D, F, void, nullptr> : public AtomicFieldFlag
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

template <typename T, T D, auto F, typename Ctx, ApplyFn<Ctx, T> H>
class AtomicField<T, D, F, Ctx, H> : public AtomicFieldFlag
{
public:
    using type = T;
    static constexpr auto dValue = D;
    static constexpr auto field = F;
    static constexpr ApplyFn<Ctx, T> applier = H;

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

    inline void set(Ctx& ctx, T v) noexcept
    {
        bool apply = load() != v;
        value.store(v, std::memory_order_release);
        state.store(MaskState::SET, std::memory_order_release);
        if (apply) applier(ctx, v);
    }

    inline void unset(Ctx& ctx) noexcept
    {
        T old = load();
        value.store(dValue, std::memory_order_relaxed);
        state.store(MaskState::INHERIT, std::memory_order_relaxed);
        if (load() != old) applier(ctx, dValue);
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

template <typename T, auto F, typename Ctx, auto H>
class OptionalAtomicField;

template <typename T, auto F>
class OptionalAtomicField<T, F, void, nullptr> : public OptionalAtomicFieldFlag
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

template <typename T, auto F, typename Ctx, OptionalApplyFn<Ctx, T> H>
class OptionalAtomicField<T, F, Ctx, H> : public OptionalAtomicFieldFlag
{
public:
    using type = T;
    static constexpr auto field = F;
    static constexpr OptionalApplyFn<Ctx, T> applier = H;

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

    inline void set(Ctx& ctx, T v) noexcept
    {
        bool apply = hasValue() || load() != v;
        value.store(v, std::memory_order_release);
        state.store(MaskState::SET, std::memory_order_release);
        if (apply) applier(ctx, v);
    }

    inline void unset(Ctx& ctx) noexcept
    {
        bool apply = state == MaskState::SET;
        state.store(MaskState::INHERIT, std::memory_order_release);
        if (apply) applier(ctx, hasValue() ? load() : std::nullopt);
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

template <typename T, auto F, typename Ctx, auto H>
class ValueField;

template <typename T, auto F>
class ValueField<T, F, void, nullptr> : public ValueFieldFlag
{
public:
    using type = T;
    static constexpr auto field = F;

    ValueField(std::mutex& m) noexcept
        : mu(m)
    {}

    explicit ValueField(std::mutex& m, const ValueField& parent) noexcept
        : mu(m),
          value(),
          state(MaskState::SET),
          base(&parent)
    {}

    template <typename Fn>
    void withRead(Fn&& fn) const
    {
        if (base && state.load(std::memory_order_relaxed) == MaskState::INHERIT)
            return base->withRead(std::forward<Fn>(fn));

        std::lock_guard<std::mutex> lock(mu);
        return std::forward<Fn>(fn)(value);
    }

    template <typename Fn>
    void withWrite(Fn&& fn)
    {
        state.store(MaskState::SET, std::memory_order_release);
        std::lock_guard<std::mutex> lk(mu);
        return std::forward<Fn>(fn)(value);
    }

    inline void unset() noexcept
    {
        state.store(MaskState::INHERIT, std::memory_order_release);
        std::lock_guard<std::mutex> lk(mu);
        value = T{};
    }

    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == MaskState::SET;
    }

    std::mutex& mu;

private:
    T value{};
    std::atomic<MaskState> state{MaskState::INHERIT};
    const ValueField* base{nullptr};
};

template <typename T, auto F, typename Ctx, ApplyFn<Ctx, T> H>
class ValueField<T, F, Ctx, H> : public ValueFieldFlag
{
public:
    using type = T;
    static constexpr auto field = F;
    static constexpr ApplyFn<Ctx, T> applier = H;

    ValueField(std::mutex& m)
        : mu(m)
    {}

    explicit ValueField(std::mutex& m, const ValueField& parent) noexcept
        : mu(m),
          value(),
          state(MaskState::SET),
          base(&parent)
    {}

    void runApply(Ctx& ctx)
    {
        std::lock_guard<std::mutex> lk(mu);
        applier(ctx, value);
    }

    template <typename Fn>
    void withRead(Fn&& fn) const
    {
        if (base && state.load(std::memory_order_relaxed) == MaskState::INHERIT)
            return base->withRead(std::forward<Fn>(fn));

        std::lock_guard<std::mutex> lk(mu);
        return std::forward<Fn>(fn)(value);
    }

    template <typename Fn>
    void withRead(Ctx& ctx, Fn&& fn)
    {
        state.store(MaskState::SET, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lk(mu);
            auto old = value;
            auto result = std::forward<Fn>(fn)(value);
            if (old != value) applier(ctx, value);
        }
    }

    inline void unset(Ctx& ctx) noexcept
    {
        bool apply = state.exchange(MaskState::INHERIT, std::memory_order_relaxed) == MaskState::SET;
        {
            std::lock_guard<std::mutex> lk(mu);
            value = T{};
            if (apply && !base)
            {
                applier(ctx, value);
                return;
            }
        }
        if (apply && base)
            base->runApply(ctx);
    }

    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == MaskState::SET;
    }

    std::mutex& mu;

private:
    T value{};
    std::atomic<MaskState> state{MaskState::INHERIT};
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

    const inline type::const_iterator find(uint32_t key) const noexcept
    {
        return std::find_if(children.begin(), children.end(), [key](const auto& pair) { return pair.first == key; });
    }

    const inline type::const_iterator end() const noexcept
    {
        return children.end();
    }

    const inline type::const_iterator begin() const noexcept
    {
        return children.begin();
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
    template <typename...>
    friend class RegistryDatabase;

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
concept IsIndexedRefContainer =
    IsRefContainer<T> &&
    T::hasRefIndex;

template <typename T>
concept IsUnindexedRefContainer =
    IsRefContainer<T> &&
    !T::hasRefIndex;

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
