/**
 * @file FieldAccessor.hpp
 * @brief Typed accessor wrappers returned by SubRegistry::get().
 * @ingroup CONFIG
 */

#ifndef FIELD_ACCESSOR_HPP
#define FIELD_ACCESSOR_HPP

#include "SubRegistry.hpp"

namespace config
{
template <typename T>
using DefaultGetter = T(*)();

template <auto EF>
class AccessorField {};

template <typename T, auto EF>
constexpr DefaultGetter<T> getDefaultGetter() noexcept
{
    if constexpr (hasV<decltype(EF), EF>)
        return &getV<T, EF>;
    else
        return nullptr;
}

/**
 * @brief Live accessor for an `AtomicField<T>`, with inheritance traversal and applier firing.
 * @ingroup CONFIG
 *
 * `load()` walks the mask chain to find the effective value. `set()` / `unset()` store the
 * new value atomically and fire the applier callback if the effective value actually changed
 * and a live context is registered.
 *
 * @tparam F The concrete `AtomicField<T>` type this accessor wraps.
 */
template <IsAtomicField F>
class AtomicFieldAccessor
{
public:
    using Field = F;

    template <IsSubRegistry S, auto EF>
    requires (!std::is_const_v<std::remove_reference<S>>)
    AtomicFieldAccessor(S& sub, AccessorField<EF>)
        : field(sub.template getValue<EF>()),
          applier(S::applier),
          getDefaultValue(getDefaultGetter<typename F::type, EF>()),
          provider(sub.getProvider())
    {
        static_assert(std::is_same_v<decltype(EF), typename S::type>, "The provided field does not belong to this sub-registry.");
    }

    AtomicFieldAccessor(const AtomicFieldAccessor&) = default;
    AtomicFieldAccessor(AtomicFieldAccessor&&) = default;
    AtomicFieldAccessor& operator=(const AtomicFieldAccessor&) = default;
    AtomicFieldAccessor& operator=(AtomicFieldAccessor&&) = default;

    inline F::type load() const noexcept
    {
        const F* f = &field;
        while (f->state.load(std::memory_order_relaxed) == FieldState::INHERIT && f->mask)
            f = f->mask;
        return f->value.load(std::memory_order_relaxed);
    }

    inline void set(F::type v, uint32_t cmdIdx = NO_COMMAND_INDEX) noexcept
    {
        bool apply = load() != v;
        field.value.store(v, std::memory_order_release);
        field.state.store(FieldState::CANNED, std::memory_order_release);
        field.commandIndex = cmdIdx;
        if (apply && provider.hasCtx())
        {
            if constexpr (RequiresContext<F>)
                F::applier(provider.get());
            if (applier)
                applier(provider.get());
        }
    }

    inline void unset() noexcept
    {
        typename F::type old = load();
        field.value.store(getDefault(), std::memory_order_relaxed);
        field.state.store(FieldState::INHERIT, std::memory_order_relaxed);
        if (load() != old && provider.hasCtx())
        {
            if constexpr (RequiresContext<F>)
                F::applier(provider.get());
            if (applier)
                applier(provider.get());
        }
    }

    void setDefault() noexcept
    {
        set(getDefault());
        field.state.store(FieldState::CANNED, std::memory_order_release);
    }

    F::type getDefault() const noexcept
    {
        return getDefaultValue();
    }

    inline bool overridden() const noexcept
    {
        return field.state.load(std::memory_order_relaxed) == FieldState::CANNED;
    }

private:
    F& field;
    ContextProvider& provider;
    ApplyFn applier;
    DefaultGetter<typename F::type> getDefaultValue;
};

/**
 * @brief Live accessor for an `OptionalAtomicField<T>`, supporting presence queries and inheritance.
 * @ingroup CONFIG
 *
 * `hasValue()` walks the mask chain; returns true only when the effective field state is `CANNED`.
 * `load()` asserts that a value is present before returning it. `set()` / `unset()` fire the
 * applier if the presence or value changes.
 *
 * @tparam F The concrete `OptionalAtomicField<T>` type this accessor wraps.
 */
template <IsOptionalAtomicField F>
class OptionalAtomicFieldAccessor
{
public:
    using Field = F;

    template <IsSubRegistry S, auto EF>
    OptionalAtomicFieldAccessor(S& sub, AccessorField<EF>)
        : field(sub.template getValue<EF>()),
          applier(S::applier),
          provider(sub.getProvider())
    {
        static_assert(std::is_same_v<decltype(EF), typename S::type>, "The provided field does not belong to this sub-registry.");
    }

    OptionalAtomicFieldAccessor(const OptionalAtomicFieldAccessor&) = default;
    OptionalAtomicFieldAccessor(OptionalAtomicFieldAccessor&&) = default;

    inline bool hasValue() const noexcept
    {
        const F* f = &field;
        while (f->state.load(std::memory_order_relaxed) == FieldState::INHERIT && f->mask)
            f = f->mask;
        return f->state.load(std::memory_order_relaxed) == FieldState::CANNED;
    }

    inline F::type load() const noexcept
    {
        const F* f = &field;
        while (f->state.load(std::memory_order_relaxed) == FieldState::INHERIT && f->mask)
            f = f->mask;
        assert(f->state.load(std::memory_order_relaxed) == FieldState::CANNED);
        return f->value.load(std::memory_order_relaxed);
    }

    inline void set(F::type v, uint32_t cmdIdx = NO_COMMAND_INDEX) noexcept
    {
        bool apply = !hasValue() || load() != v;
        field.value.store(v, std::memory_order_release);
        field.state.store(FieldState::CANNED, std::memory_order_release);
        field.commandIndex = cmdIdx;
        if (apply && provider.hasCtx())
        {
            if constexpr (RequiresContext<F>)
                F::applier(provider.get());
            if (applier)
                applier(provider.get());
        }
    }

    inline void unset() noexcept
    {
        if (field.state.load(std::memory_order_relaxed) != FieldState::INHERIT)
        {
            field.state.store(FieldState::INHERIT, std::memory_order_release);
            if (provider.hasCtx())
            {
                if constexpr (RequiresContext<F>)
                    F::applier(provider.get());
                if (applier)
                    applier(provider.get());
            }
        }
    }

    void setDefault() noexcept
    {
        auto state = field.state.load(std::memory_order_relaxed);
        if (state != FieldState::UNSET)
        {
            field.state.store(FieldState::UNSET, std::memory_order_release);
            if (provider.hasCtx())
            {
                if constexpr (RequiresContext<F>)
                    F::applier(provider.get());
                if (applier)
                    applier(provider.get());
            }
        }
    }

    inline bool overridden() const noexcept
    requires (IsAtomicField<F> || IsOptionalAtomicField<F> || IsValueField<F>)
    {
        return field.state.load(std::memory_order_relaxed) == FieldState::CANNED;
    }

private:
    F& field;
    ContextProvider& provider;
    ApplyFn applier;
};

/**
 * @brief Live accessor for a `ValueField<T>`, using RCU-protected heap-allocated storage.
 * @ingroup CONFIG
 *
 * `load()` acquires an RCU read guard and dereferences the atomic pointer, returning a copy
 * of the current value. `set()` allocates a new value, swaps the pointer, and retires the
 * old allocation via `RCU::retire`. Inheritance traversal follows the mask chain.
 *
 * @tparam F The concrete `ValueField<T>` type this accessor wraps.
 */
template <IsValueField F>
class ValueFieldAccessor
{
public:
    using Field = F;
    
    template <IsSubRegistry S, auto EF>
    ValueFieldAccessor(S& sub, AccessorField<EF>)
        : field(sub.template getValue<EF>()),
          applier(S::applier),
          provider(sub.getProvider())
    {
        static_assert(std::is_same_v<decltype(EF), typename S::type>, "The provided field does not belong to this sub-registry.");
    }

    ValueFieldAccessor(const ValueFieldAccessor&) = default;
    ValueFieldAccessor(ValueFieldAccessor&&) = default;

    inline bool hasValue() const noexcept
    {
        const F* f = &field;
        while (f->state.load(std::memory_order_relaxed) == FieldState::INHERIT && f->mask)
            f = f->mask;
        return f->state.load(std::memory_order_relaxed) == FieldState::CANNED;
    }

    inline F::type load() const noexcept
    {
        const F* f = &field;
        while (f->state.load(std::memory_order_relaxed) == FieldState::INHERIT && f->mask)
            f = f->mask;
        utils::RCU::Guard g;
        typename F::type val = *f->value.load(std::memory_order_relaxed);
        return val;
    }

    inline void set(F::type v, uint32_t cmdIdx = NO_COMMAND_INDEX) noexcept
    {
        bool apply = !hasValue() || load() != v;
        typename F::type* val = new F::type(v);
        typename F::type* old = field.value.exchange(val, std::memory_order_relaxed);
        utils::RCU::retire([](void* ctx) {
            typename F::type* o = static_cast<F::type*>(ctx);
            delete o;
        }, old);
        field.state.store(FieldState::CANNED, std::memory_order_release);
        field.commandIndex = cmdIdx;
        if (apply && provider.hasCtx())
        {
            if constexpr (RequiresContext<F>)
                F::applier(provider.get());
            if (applier)
                applier(provider.get());
        }
    }

    inline void unset() noexcept
    {
        typename F::type old = load();
        field.state.store(FieldState::INHERIT, std::memory_order_relaxed);
        if (load() != old && provider.hasCtx())
        {
            if constexpr (RequiresContext<F>)
                F::applier(provider.get());
            if (applier)
                applier(provider.get());
        }
    }

    void setDefault() noexcept
    {
        auto state = field.state.load(std::memory_order_relaxed);
        if (state != FieldState::UNSET)
        {
            field.state.store(FieldState::UNSET, std::memory_order_release);
            if (provider.hasCtx())
            {
                if constexpr (RequiresContext<F>)
                    F::applier(provider.get());
                if (applier)
                    applier(provider.get());
            }
        }
    }

    inline bool overridden() const noexcept
    {
        return field.state.load(std::memory_order_relaxed) == FieldState::CANNED;
    }

private:
    F& field;
    ContextProvider& provider;
    ApplyFn applier;
};

/**
 * @brief Live accessor for a `ListField<T>`, with mutex-guarded read and write windows.
 * @ingroup CONFIG
 *
 * `withRead(fn)` acquires the list mutex and calls `fn` with a const reference to the
 * underlying vector (no-op if the list has never been written). `withWrite(fn)` allocates
 * the list on first write, calls `fn` with a mutable reference, and fires the applier if
 * `fn` returns `true` (indicating a structural change).
 *
 * @tparam F The concrete `ListField<T>` type this accessor wraps.
 */
template <IsListField F>
class ListFieldAccessor
{
public:
    using Field = F;

    template <IsSubRegistry S, auto EF>
    ListFieldAccessor(S& sub, AccessorField<EF>)
        : field(sub.template getValue<EF>()),
          mu(sub.mu),
          applier(S::applier),
          provider(sub.getProvider())
    {
        static_assert(std::is_same_v<decltype(EF), typename S::type>, "The provided field does not belong to this sub-registry.");
    }

    ListFieldAccessor(const ListFieldAccessor&) = default;
    ListFieldAccessor(ListFieldAccessor&&) = default;

    template <typename Fn>
    void withRead(Fn&& fn) const
    {
        if (auto* lst = field.value.load(std::memory_order_relaxed); lst)
        {
            std::lock_guard<std::mutex> lock(mu);
            std::forward<Fn>(fn)(*lst);
        }
    }

    template <typename Fn>
    void withWrite(Fn&& fn, uint32_t cmdIdx = NO_COMMAND_INDEX)
    {
        field.commandIndex = cmdIdx;
        bool runApplier{false};
        {
            std::lock_guard<std::mutex> lk(mu);
            if (!field.value.load(std::memory_order_relaxed))
            {
                typename Field::type* list = new Field::type{};
                field.value.store(list, std::memory_order_release);
            }
            runApplier = std::forward<Fn>(fn)(*field.value.load(std::memory_order_relaxed));
        }
        if (runApplier && provider.hasCtx())
        {
            if constexpr (RequiresContext<F>)
                F::applier(provider.get());
            if (applier)
                applier(provider.get());
        }
    }

private:
    F& field;
    std::mutex& mu;
    ContextProvider& provider;
    ApplyFn applier;
};

/**
 * @brief Live accessor for an `OwnedListField<T, K>`, backed by an `unordered_map<K, T>`.
 * @ingroup CONFIG
 *
 * Owns child registry slots keyed by `K`. `emplaceBack()` inserts or returns an existing
 * slot and fires `notifyChanged()` on insertion. `erase()` and `clear()` remove entries and
 * fire the applier so that the owning protocol process can react (e.g., drop a peer session
 * when its neighbor config is removed).
 *
 * @tparam F The concrete `OwnedListField<T, K>` type this accessor wraps.
 */
template <IsOwnedListField F>
class OwnedListFieldAccessor
{
public:
    using Field = F;
    using type = F::type;
    using key = F::key;

    template <IsSubRegistry S, auto EF>
    OwnedListFieldAccessor(S& sub, AccessorField<EF>)
        : field(sub.template getValue<EF>()),
          applier(S::applier),
          provider(sub.getProvider())
    {
        static_assert(std::is_same_v<decltype(EF), typename S::type>, "The provided field does not belong to this sub-registry.");
    }

    OwnedListFieldAccessor(const OwnedListFieldAccessor&) = default;
    OwnedListFieldAccessor(OwnedListFieldAccessor&&) = default;

    /**
     * @brief Fires the applier if a context pointer has been registered.
     *
     * Called internally by `erase()`, `clear()`, and @ref RegistryDatabase::emplaceBack
     * after every structural change to the children map.
     */
    void notifyChanged() noexcept
    {
        if (applier && provider.hasCtx())
            applier(provider.get());
    }

    /**
     * @brief Inserts a new child entry keyed by `k`, or returns the existing one.
     *
     * If the entry is newly inserted, fires the applier callback via
     * @ref notifyChanged so that the owning protocol process can react to the
     * structural change (e.g., re-evaluate neighbor configuration).
     *
     * @param k  Key identifying the child entry.
     * @return Reference to the (new or existing) child entry.
     */
    type& emplaceBack(const key& k, uint32_t cmdIdx = NO_COMMAND_INDEX) noexcept
    {
        if (!field.delFn)
            field.delFn = [](type* p) { delete p; };
        auto [it, ok] = field.children.try_emplace(k, nullptr);
        if (ok) {
            it->second = new type();
            field.commandIndex = cmdIdx;
            notifyChanged();
        }
        return *it->second;
    }

    /**
     * @brief Finds a child entry by key.
     *
     * @param k  Key to search for.
     * @return Const iterator to the matching entry, or `end()` if not found.
     */
    inline std::unordered_map<key, type*>::const_iterator find(const key& k) const noexcept
    {
        return field.children.find(k);
    }

    /**
     * @brief Returns the past-the-end iterator for the local children map.
     */
    inline std::unordered_map<key, type*>::const_iterator end() const noexcept
    {
        return field.children.end();
    }

    /**
     * @brief Returns the begin iterator for the local children map.
     */
    inline std::unordered_map<key, type*>::const_iterator begin() const noexcept
    {
        return field.children.begin();
    }

    /**
     * @brief Returns a reference to the pointer-map of local children.
     *
     * @return Reference to the `unordered_map<K, T*>`.
     */
    inline std::unordered_map<key, type*>& get() noexcept
    {
        return field.children;
    }

    /**
     * @brief Returns a const reference to the pointer-map of local children.
     *
     * @return Const reference to the `unordered_map<K, T*>`.
     */
    inline const std::unordered_map<key, type*>& get() const noexcept
    {
        return field.children;
    }

    /**
     * @brief Returns a bool depending on if the key exists in the children map.
     *
     * @return returns true if the key was found, otherwise false.
     */
    inline bool contains(key& k) const noexcept
    {
        return field.children.contains(k);
    }

    /**
     * @brief Removes the child entry identified by `k` and fires the applier.
     *
     * @param k  Key of the entry to remove.
     */
    inline void erase(const key& k) noexcept
    {
        auto it = field.children.find(k);
        if (it != field.children.end()) {
            if (field.delFn) field.delFn(it->second);
            field.children.erase(it);
        }
        notifyChanged();
    }

    /**
     * @brief Removes all child entries and fires the applier.
     */
    inline void clear() noexcept
    {
        for (auto& [k, v] : field.children)
            if (field.delFn) field.delFn(v);
        field.children.clear();
        notifyChanged();
    }

private:
    F& field;
    ContextProvider& provider;
    ApplyFn applier;
};

template <typename Base, typename ENUM, ApplyFn H, typename Fields>
template <ENUM F>
decltype(auto) SubRegistry<Base, ENUM, H, Fields>::get() noexcept
{
    using Field = FieldTypeAt<F>;
    if constexpr (IsRefContainer<Field>)
        return getValue<F>();
    else if constexpr (IsAtomicField<Field>)
        return AtomicFieldAccessor<Field>(*static_cast<Base*>(this), AccessorField<F>{});
    else if constexpr (IsOptionalAtomicField<Field>)
        return OptionalAtomicFieldAccessor<Field>(*static_cast<Base*>(this), AccessorField<F>{});
    else if constexpr (IsValueField<Field>)
        return ValueFieldAccessor<Field>(*static_cast<Base*>(this), AccessorField<F>{});
    else if constexpr (IsListField<Field>)
        return ListFieldAccessor<Field>(*static_cast<Base*>(this), AccessorField<F>{});
    else
        return OwnedListFieldAccessor<Field>(*static_cast<Base*>(this), AccessorField<F>{});
}

template <typename Base, typename ENUM, ApplyFn H, typename Fields>
template <ENUM F>
decltype(auto) SubRegistry<Base, ENUM, H, Fields>::get() const noexcept
{
    return const_cast<SubRegistry&>(*this).template get<F>();
}
}

#endif // FIELD_ACCESSOR_HPP
