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

template <typename KEY, typename ENUM, typename Ctx, typename... Fields>
class SubRegistry;

using ApplyKey = uint64_t;

template <typename Ctx>
using ApplyFn = void (*)(Ctx& ctx);

struct AtomicFieldFlag {};
struct OptionalAtomicFieldFlag : AtomicFieldFlag {};
struct RefContainerFieldFlag {};
struct ValueFieldFlag {};
struct OptionalValueFieldFlag {};
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

template <typename Ctx>
struct ContextProvider
{
    inline bool hasCtx() noexcept
    {
        return ctx;
    }

    inline Ctx& get() noexcept
    {
        return *ctx;
    }

    void set(Ctx& c) noexcept
    {
        ctx = &c;
    }

    void clear() noexcept
    {
        ctx = nullptr;
    }

private:
    Ctx* ctx{nullptr};
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

template <typename T, T D, auto F, typename Ctx, ApplyFn<Ctx> H>
class AtomicField<T, D, F, Ctx, H> : public AtomicFieldFlag
{
public:
    using type = T;
    static constexpr auto dValue = D;
    static constexpr auto field = F;
    static constexpr ApplyFn<Ctx> applier = H;

    AtomicField(ContextProvider<Ctx>& provider) noexcept
        : provider(provider)
    {}

    AtomicField(ContextProvider<Ctx>& provider, const AtomicField& parent) noexcept
        : provider(provider),
          value(D),
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
        bool apply = load() != v;
        value.store(v, std::memory_order_release);
        state.store(MaskState::SET, std::memory_order_release);
        if (apply && provider.hasCtx()) applier(*provider.get());
    }

    inline void unset() noexcept
    {
        T old = load();
        value.store(dValue, std::memory_order_relaxed);
        state.store(MaskState::INHERIT, std::memory_order_relaxed);
        if (load() != old && provider.hasCtx()) applier(provider.get());
    }

    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == MaskState::SET;
    }

private:
    ContextProvider<Ctx>& provider;
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

template <typename T, auto F, typename Ctx, ApplyFn<Ctx> H>
class OptionalAtomicField<T, F, Ctx, H> : public OptionalAtomicFieldFlag
{
public:
    using type = T;
    static constexpr auto field = F;
    static constexpr ApplyFn<Ctx> applier = H;

    OptionalAtomicField(ContextProvider<Ctx>& provider)
        : provider(provider)
    {}

    explicit OptionalAtomicField(ContextProvider<Ctx>& provider, const OptionalAtomicField& parent) noexcept
        : provider(provider),
          value(),
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
        bool apply = hasValue() || load() != v;
        value.store(v, std::memory_order_release);
        state.store(MaskState::SET, std::memory_order_release);
        if (apply && provider.hasCtx()) applier(provider.get());
    }

    inline void unset() noexcept
    {
        bool apply = state == MaskState::SET;
        state.store(MaskState::INHERIT, std::memory_order_release);
        if (apply && provider.hasCtx()) applier(provider.get());
    }

    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == MaskState::SET;
    }

private:
    ContextProvider<Ctx>& provider;
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

template <typename T, auto F, typename Ctx, ApplyFn<Ctx> H>
class ValueField<T, F, Ctx, H> : public ValueFieldFlag
{
public:
    using type = T;
    static constexpr auto field = F;
    static constexpr ApplyFn<Ctx> applier = H;

    ValueField(ContextProvider<Ctx>& provider, std::mutex& m)
        : provider(provider),
          mu(m)
    {}

    explicit ValueField(ContextProvider<Ctx>& provider, std::mutex& m, const ValueField& parent) noexcept
        : provider(provider),
          mu(m),
          value(),
          state(MaskState::SET),
          base(&parent)
    {}

    void runApply()
    {
        if (!provider.hasCtx())
            return;
        std::lock_guard<std::mutex> lk(mu);
        applier(provider.get());
    }

    template <typename Fn>
    void withRead(Fn&& fn) const
    {
        if (base && state.load(std::memory_order_relaxed) == MaskState::INHERIT)
            return base->withRead(std::forward<Fn>(fn));

        std::lock_guard<std::mutex> lk(mu);
        std::forward<Fn>(fn)(value);
    }

    template <typename Fn>
    void withWrite(Fn&& fn)
    {
        bool runApplier{false};
        state.store(MaskState::SET, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lk(mu);
            auto old = value;
            std::forward<Fn>(fn)(value);
            runApplier = old != value;
        }

        if (runApplier && provider.hasCtx())
        {
            applier(provider.get());
        }
    }

    inline void unset() noexcept
    {
        bool apply = state.exchange(MaskState::INHERIT, std::memory_order_relaxed) == MaskState::SET;
        {
            std::lock_guard<std::mutex> lk(mu);
            value = T{};
        }
        if (apply && provider.hasCtx())
            runApply(provider.get());
    }

    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == MaskState::SET;
    }

    std::mutex& mu;

private:
    ContextProvider<Ctx>& provider;
    T value{};
    std::atomic<MaskState> state{MaskState::INHERIT};
    const ValueField* base{nullptr};
};

template <typename T, auto F, typename Ctx, auto H>
class OptionalValueField;

template <typename T, auto F>
class OptionalValueField<T, F, void, nullptr> : public OptionalValueFieldFlag
{
public:
    using type = T;
    static constexpr auto field = F;

    OptionalValueField(std::mutex& m) noexcept
        : mu(m)
    {}

    explicit OptionalValueField(std::mutex& m, const OptionalValueField& parent) noexcept
        : mu(m),
          value(),
          state(MaskState::SET),
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
            base->load();
        }

        std::lock_guard<std::mutex> lock(mu);
        return value;
    }

    inline T set(T v) noexcept
    {
        {
            std::lock_guard<std::mutex> lk(mu);
            value = v;
        }
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

    std::mutex& mu;

private:
    T value{};
    std::atomic<MaskState> state{MaskState::INHERIT};
    const OptionalValueField* base{nullptr};
};

template <typename T, auto F, typename Ctx, ApplyFn<Ctx> H>
class OptionalValueField<T, F, Ctx, H> : public OptionalValueFieldFlag
{
public:
    using type = T;
    static constexpr auto field = F;
    static constexpr ApplyFn<Ctx> applier = H;

    OptionalValueField(ContextProvider<Ctx>& provider, std::mutex& m)
        : provider(provider),
          mu(m)
    {}

    explicit OptionalValueField(ContextProvider<Ctx>& provider, std::mutex& m, const OptionalValueField& parent) noexcept
        : provider(provider),
          mu(m),
          value(),
          state(MaskState::SET),
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
            base->load();
        }

        std::lock_guard<std::mutex> lock(mu);
        return value;
    }

    inline T set(T v) noexcept
    {
        bool apply = load() != v;
        {
            std::lock_guard<std::mutex> lk(mu);
            value = v;
        }
        state.store(MaskState::SET, std::memory_order_release);
        if (apply && provider.hasCtx()) applier(*provider.get());
    }

    inline void unset() noexcept
    {
        T old = load();
        state.store(MaskState::INHERIT, std::memory_order_relaxed);
        if (load() != old && provider.hasCtx()) applier(provider.get());
    }

    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == MaskState::SET;
    }

    std::mutex& mu;

private:
    ContextProvider<Ctx>& provider;
    T value{};
    std::atomic<MaskState> state{MaskState::INHERIT};
    const OptionalValueField* base{nullptr};
};

template <typename T, typename K, auto F>
class OwnedListField : public OwnedListFieldFlag
{
public:
    using type = std::vector<std::pair<K, Reference<T>>>;
    using key = K;
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

    const inline type::const_iterator find(K key) const noexcept
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
concept RequiresContext =
    requires { T::applier; };

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
concept IsOptionalValueField = 
    IsFieldBase<T> &&
    std::derived_from<T, OptionalValueFieldFlag>;

template <typename T>
concept IsOwnedListField =
    IsFieldBase<T> &&
    std::derived_from<T, OwnedListFieldFlag>;
}

#endif // REGISTRY_TYPES_HPP
