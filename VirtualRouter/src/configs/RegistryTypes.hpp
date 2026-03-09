// RegistryTypes.hpp

#ifndef REGISTRY_TYPES_HPP
#define REGISTRY_TYPES_HPP

#include <atomic>
#include <concepts>
#include <cassert>
#include <utility>
#include <unordered_map>
#include <algorithm>

#define ENABLE_CONFIG_INDEX 0

#if defined(NDEBUG)
    #define USE_CONFIG_INDEX 0
#else
    #if ENABLE_CONFIG_INDEX
        #define USE_CONFIG_INDEX 1
    #else
        #define USE_CONFIG_INDEX 0
    #endif
#endif

#if USE_CONFIG_INDEX
    #define CONFIG_INDEX_PARAM , auto F
    #define CONFIG_INDEX_ARG(x) , x
    #define CONFIG_INDEX_MEMBER static constexpr auto field = F;
#else
    #define CONFIG_INDEX_PARAM
    #define CONFIG_INDEX_ARG(x)
    #define CONFIG_INDEX_MEMBER
#endif

namespace Config
{
template <typename...>
class RegistryDatabase;

template <typename T>
class Reference;

template <typename KEY, typename ENUM, typename... Fields>
class SubRegistry;

using ApplyKey = uint64_t;

using ApplyFn = void (*)(void* ctx);

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
    };

enum class MaskState : uint8_t
{
    INHERIT,
    SET
};

struct ContextProvider
{
    inline bool hasCtx() noexcept
    {
        return ctx != nullptr;
    }

    inline void* get() noexcept
    {
        return ctx;
    }

    void set(void* c) noexcept
    {
        ctx = &c;
    }

    void clear() noexcept
    {
        ctx = nullptr;
    }

private:
    void* ctx{nullptr};
};

template <typename T CONFIG_INDEX_PARAM, auto H = nullptr>
class AtomicField;

template <typename T CONFIG_INDEX_PARAM>
class AtomicField<T CONFIG_INDEX_ARG(F), nullptr> : public AtomicFieldFlag
{
public:
    using type = T;
    CONFIG_INDEX_MEMBER

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
        value.store(getDefault(), std::memory_order_relaxed);
        state.store(MaskState::INHERIT, std::memory_order_relaxed);
    }

    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == MaskState::SET;
    }

    void setDefault(T d) noexcept
    {
        defaultValue = d;
    }

private:
    template <typename KEY, typename ENUM, typename... Fields>
    friend class SubRegistry;

    void setMask(const AtomicField* parent) noexcept
    {
        base = parent;
    }

    T getDefault() noexcept
    {
        if (base) return base->defaultValue;
        else return defaultValue;
    }

    std::atomic<T> value{T{}};
    std::atomic<MaskState> state{MaskState::INHERIT};
    AtomicField* base{nullptr};

    T defaultValue{T{}};
};

template <typename T CONFIG_INDEX_PARAM, ApplyFn H>
class AtomicField<T CONFIG_INDEX_ARG(F), H> : public AtomicFieldFlag
{
public:
    using type = T;
    static constexpr ApplyFn applier = H;
    CONFIG_INDEX_MEMBER

    AtomicField(ContextProvider& p) noexcept
        : provider(p)
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
        if (apply && provider.hasCtx()) applier(provider.get());
    }

    inline void unset() noexcept
    {
        T old = load();
        value.store(getDefault(), std::memory_order_relaxed);
        state.store(MaskState::INHERIT, std::memory_order_relaxed);
        if (load() != old && provider.hasCtx()) applier(provider.get());
    }

    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == MaskState::SET;
    }

    void setDefault(T d) noexcept
    {
        defaultValue = d;
    }

private:
    template <typename KEY, typename ENUM, typename... Fields>
    friend class SubRegistry;

    void setMask(const AtomicField* parent) noexcept
    {
        base = parent;
    }

    T getDefault() noexcept
    {
        if (base) return base->defaultValue;
        else return defaultValue;
    }

    ContextProvider& provider;
    std::atomic<T> value{T{}};
    std::atomic<MaskState> state{MaskState::INHERIT};
    AtomicField* base{nullptr};

    T defaultValue{T{}};
};

template <typename T CONFIG_INDEX_PARAM, auto H = nullptr>
class OptionalAtomicField;

template <typename T CONFIG_INDEX_PARAM>
class OptionalAtomicField<T CONFIG_INDEX_ARG(F), nullptr> : public OptionalAtomicFieldFlag
{
public:
    using type = T;
#if USE_CONFIG_INDEX
    static constexpr auto field = F;
#endif

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
    template <typename KEY, typename ENUM, typename... Fields>
    friend class SubRegistry;

    void setMask(const OptionalAtomicField* parent) noexcept
    {
        base = parent;
    }

    std::atomic<T> value{};
    std::atomic<MaskState> state{MaskState::INHERIT};
    OptionalAtomicField* base{nullptr};
};

template <typename T CONFIG_INDEX_PARAM, ApplyFn H>
class OptionalAtomicField<T CONFIG_INDEX_ARG(F), H> : public OptionalAtomicFieldFlag
{
public:
    using type = T;
    static constexpr ApplyFn applier = H;
    CONFIG_INDEX_MEMBER

    OptionalAtomicField(ContextProvider& provider)
        : provider(provider)
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
    template <typename KEY, typename ENUM, typename... Fields>
    friend class SubRegistry;

    void setMask(const OptionalAtomicField* parent) noexcept
    {
        base = parent;
    }

    ContextProvider& provider;
    std::atomic<T> value{};
    std::atomic<MaskState> state{MaskState::INHERIT};
    OptionalAtomicField* base{nullptr};
};

template <typename T CONFIG_INDEX_PARAM, auto H = nullptr>
class ValueField;

template <typename T CONFIG_INDEX_PARAM>
class ValueField<T CONFIG_INDEX_ARG(F), nullptr> : public ValueFieldFlag
{
public:
    using type = T;
    CONFIG_INDEX_MEMBER

    ValueField(std::mutex& m) noexcept
        : mu(m)
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
    template <typename KEY, typename ENUM, typename... Fields>
    friend class SubRegistry;

    void setMask(ValueField* parent)
    {
        base = parent;
    }

    T value{};
    std::atomic<MaskState> state{MaskState::INHERIT};
    ValueField* base{nullptr};
};

template <typename T CONFIG_INDEX_PARAM, ApplyFn H>
class ValueField<T CONFIG_INDEX_ARG(F), H> : public ValueFieldFlag
{
public:
    using type = T;
    static constexpr ApplyFn applier = H;
    CONFIG_INDEX_MEMBER

    ValueField(ContextProvider& provider, std::mutex& m)
        : provider(provider),
          mu(m)
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
            runApply();
    }

    inline bool overridden() const noexcept
    {
        return state.load(std::memory_order_relaxed) == MaskState::SET;
    }

    std::mutex& mu;

private:
    template <typename KEY, typename ENUM, typename... Fields>
    friend class SubRegistry;

    void setMask(ValueField* parent)
    {
        base = parent;
    }

    ContextProvider& provider;
    T value{};
    std::atomic<MaskState> state{MaskState::INHERIT};
    ValueField* base{nullptr};
};

template <typename T CONFIG_INDEX_PARAM, auto H = nullptr>
class OptionalValueField;

template <typename T CONFIG_INDEX_PARAM>
class OptionalValueField<T CONFIG_INDEX_ARG(F), nullptr> : public OptionalValueFieldFlag
{
public:
    using type = T;
    CONFIG_INDEX_MEMBER

    OptionalValueField(std::mutex& m) noexcept
        : mu(m)
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

        std::lock_guard<std::mutex> lock(mu);
        return value;
    }

    inline void set(T v) noexcept
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
    template <typename KEY, typename ENUM, typename... Fields>
    friend class SubRegistry;

    void setMask(OptionalValueField* parent)
    {
        base = parent;
    }

    T value{};
    std::atomic<MaskState> state{MaskState::INHERIT};
    OptionalValueField* base{nullptr};
};

template <typename T CONFIG_INDEX_PARAM, ApplyFn H>
class OptionalValueField<T CONFIG_INDEX_ARG(F), H> : public OptionalValueFieldFlag
{
public:
    using type = T;
    static constexpr ApplyFn applier = H;
    CONFIG_INDEX_MEMBER

    OptionalValueField(ContextProvider& provider, std::mutex& m)
        : provider(provider),
          mu(m)
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

    inline void set(T v) noexcept
    {
        bool apply = load() != v;
        {
            std::lock_guard<std::mutex> lk(mu);
            value = v;
        }
        state.store(MaskState::SET, std::memory_order_release);
        if (apply && provider.hasCtx()) applier(provider.get());
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
    template <typename KEY, typename ENUM, typename... Fields>
    friend class SubRegistry;

    void setMask(OptionalValueField* parent)
    {
        base = parent;
    }

    ContextProvider& provider;
    T value{};
    std::atomic<MaskState> state{MaskState::INHERIT};
    OptionalValueField* base{nullptr};
};

template <typename T, typename K CONFIG_INDEX_PARAM>
class OwnedListField : public OwnedListFieldFlag
{
public:
    using type = std::unordered_map<K, Reference<T>>;
    using key = K;
    CONFIG_INDEX_MEMBER

    inline type& getMutable() noexcept
    {
        state = MaskState::SET;
        return children;
    }

    const inline type::const_iterator find(const K& key) const noexcept
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

    inline void erase(const K& key) noexcept
    {
        children.erase(key);
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
    template <typename KEY, typename ENUM, typename... Fields>
    friend class SubRegistry;
    template <typename...>
    friend class RegistryDatabase;

    void setMask(OwnedListField* parent)
    {
        base = parent;
    }

    type children{};
    MaskState state{MaskState::INHERIT};
    OwnedListField* base{nullptr};
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

