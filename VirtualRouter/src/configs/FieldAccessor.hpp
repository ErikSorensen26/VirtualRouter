/**
 * @file FieldAccessor.hpp
 * @brief Typed accessor wrappers returned by SubRegistry::get().
 * @ingroup CONFIG
 */

#ifndef FIELD_ACCESSOR_HPP
#define FIELD_ACCESSOR_HPP

#include <algorithm>

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
          getDefaultValue(getDefaultGetter<typename F::type, EF>()),
          provider(&sub)
    {
        static_assert(std::is_same_v<decltype(EF), typename S::type>, "The provided field does not belong to this sub-registry.");
    }

    AtomicFieldAccessor(const AtomicFieldAccessor&) = default;
    AtomicFieldAccessor(AtomicFieldAccessor&&) = default;
    AtomicFieldAccessor& operator=(const AtomicFieldAccessor&) = default;
    AtomicFieldAccessor& operator=(AtomicFieldAccessor&&) = default;

    inline typename F::type load() const noexcept
    {
        const F* f = &field;
        while (f->state.load(std::memory_order_relaxed) == FieldState::INHERIT && f->mask)
            f = f->mask;
        return f->value.load(std::memory_order_relaxed);
    }

    inline bool set(typename F::type v, uint32_t cmdIdx = NO_COMMAND_INDEX) noexcept
    {
        if constexpr (RequiresValidation<F>)
            if (!field.validator(provider, v))
                return false;
        bool apply = load() != v;
        field.value.store(v, std::memory_order_release);
        field.state.store(FieldState::CANNED, std::memory_order_release);
        field.commandIndex = cmdIdx;
        if (apply)
        {
            if constexpr (RequiresContext<F>)
                field.applier(provider, v);
        }
        return true;
    }

    inline void unset() noexcept
    {
        typename F::type old = load();
        field.value.store(getDefault(), std::memory_order_relaxed);
        field.state.store(FieldState::INHERIT, std::memory_order_relaxed);
        typename F::type nw = load();
        if (nw != old)
        {
            if constexpr (RequiresContext<F>)
                field.applier(provider, nw);
        }
    }

    void setDefault() noexcept
    {
        set(getDefault());
        field.state.store(FieldState::CANNED, std::memory_order_release);
    }

    typename F::type getDefault() const noexcept
    {
        return getDefaultValue();
    }

    inline bool overridden() const noexcept
    {
        return field.state.load(std::memory_order_relaxed) != FieldState::INHERIT;
    }

private:
    F& field;
    DefaultGetter<typename F::type> getDefaultValue;
    Context provider;
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
          provider(&sub)
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

    inline typename F::type load() const noexcept
    {
        const F* f = &field;
        while (f->state.load(std::memory_order_relaxed) == FieldState::INHERIT && f->mask)
            f = f->mask;
        assert(f->state.load(std::memory_order_relaxed) == FieldState::CANNED);
        return f->value.load(std::memory_order_relaxed);
    }

    inline bool set(typename F::type v, uint32_t cmdIdx = NO_COMMAND_INDEX) noexcept
    {
        if constexpr (RequiresValidation<F>)
            if (!field.validator(provider, v))
                return false;
        bool apply = !hasValue() || load() != v;
        field.value.store(v, std::memory_order_release);
        field.state.store(FieldState::CANNED, std::memory_order_release);
        field.commandIndex = cmdIdx;
        if (apply)
        {
            if constexpr (RequiresContext<F>)
                field.applier(provider, &v);
        }
        return true;
    }

    inline void unset() noexcept
    {
        if (field.state.load(std::memory_order_relaxed) != FieldState::INHERIT)
        {
            field.state.store(FieldState::INHERIT, std::memory_order_release);
            {
                const F* f = &field;
                while (f->state.load(std::memory_order_relaxed) == FieldState::INHERIT && f->mask)
                    f = f->mask;
                bool hasValue = f->state.load(std::memory_order_relaxed) == FieldState::CANNED;
                typename F::type v = f->value.load(std::memory_order_relaxed);
                if constexpr (RequiresContext<F>)
                    field.applier(provider, hasValue ? &v : nullptr);
            }
        }
    }

    void setDefault() noexcept
    {
        auto state = field.state.load(std::memory_order_relaxed);
        if (state != FieldState::UNSET)
        {
            field.state.store(FieldState::UNSET, std::memory_order_release);
            {
                if constexpr (RequiresContext<F>)
                    field.applier(provider, nullptr);
            }
        }
    }

    inline bool overridden() const noexcept
    requires (IsAtomicField<F> || IsOptionalAtomicField<F> || IsValueField<F>)
    {
        return field.state.load(std::memory_order_relaxed) != FieldState::INHERIT;
    }

private:
    F& field;
    Context provider;
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
          provider(&sub)
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

    inline typename F::type load() const noexcept
    {
        const F* f = &field;
        while (f->state.load(std::memory_order_relaxed) == FieldState::INHERIT && f->mask)
            f = f->mask;
        utils::RCU::Guard g;
        typename F::type val = *f->value.load(std::memory_order_relaxed);
        return val;
    }

    inline bool set(typename F::type v, uint32_t cmdIdx = NO_COMMAND_INDEX) noexcept
    {
        if constexpr (RequiresValidation<F>)
            if (!field.validator(provider, v))
                return false;
        bool apply = !hasValue() || load() != v;
        typename F::type* val = new typename F::type(v);
        typename F::type* old = field.value.exchange(val, std::memory_order_relaxed);
        utils::RCU::retire([](void* retireCtx) {
            typename F::type* o = static_cast<typename F::type*>(retireCtx);
            delete o;
        }, old);
        field.state.store(FieldState::CANNED, std::memory_order_release);
        field.commandIndex = cmdIdx;
        if (apply)
        {
            if constexpr (RequiresContext<F>)
                field.applier(provider, &v);
        }
        return true;
    }

    inline void unset() noexcept
    {
        if (field.state.load(std::memory_order_relaxed) != FieldState::INHERIT)
        {
            const bool hadValue = hasValue();
            utils::RCU::Guard oldGuard;
            typename F::type* oldPtr = hadValue ? field.value.load(std::memory_order_relaxed) : nullptr;

            field.state.store(FieldState::INHERIT, std::memory_order_relaxed);

            const F* f = &field;
            while (f->state.load(std::memory_order_relaxed) == FieldState::INHERIT && f->mask)
                f = f->mask;
            bool hasNewValue = f->state.load(std::memory_order_relaxed) == FieldState::CANNED;
            utils::RCU::Guard g;
            typename F::type* v = hasNewValue ? f->value.load(std::memory_order_relaxed) : nullptr;

            if (hadValue != hasNewValue || (hadValue && hasNewValue && *oldPtr != *v))
            {
                if constexpr (RequiresContext<F>)
                    field.applier(provider, hasNewValue ? v : nullptr);
            }
        }
    }

    void setDefault() noexcept
    {
        auto state = field.state.load(std::memory_order_relaxed);
        if (state != FieldState::UNSET)
        {
            field.state.store(FieldState::UNSET, std::memory_order_release);
            {
                if constexpr (RequiresContext<F>)
                    field.applier(provider, nullptr);
            }
        }
    }

    inline bool overridden() const noexcept
    {
        return field.state.load(std::memory_order_relaxed) != FieldState::INHERIT;
    }

private:
    F& field;
    Context provider;
};

/**
 * @brief Live accessor for a `ListField<T>`, with mutex-guarded read and write windows.
 * @ingroup CONFIG
 *
 * `readEach(fn)` acquires the list mutex and calls `fn` once per element; returning
 * `bool` from `fn` lets it stop early by returning `true`, otherwise every element is
 * visited. `get()` returns a locked copy of the whole list. `add()` / `erase()` take
 * the mutex to mutate the list and fire the applier once the lock is released if the
 * value was actually inserted / removed.
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
          provider(&sub)
    {
        static_assert(std::is_same_v<decltype(EF), typename S::type>, "The provided field does not belong to this sub-registry.");
    }

    ListFieldAccessor(const ListFieldAccessor&) = default;
    ListFieldAccessor(ListFieldAccessor&&) = default;

    F::type get() const noexcept
    {
        typename F::type& list = *field.value.load(std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(mu);
        return list;
    }

    template <typename Fn>
    void readEach(Fn&& fn) const
    {
        typename F::type& list = *field.value.load(std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(mu);

        for (const auto& element : list)
        {
            if constexpr (std::is_same_v<std::invoke_result_t<Fn&, const typename F::element&>, bool>)
            {
                if (fn(element))
                    break;
            }
            else
            {
                fn(element);
            }
        }
    }

    std::optional<typename F::element> front() const
    {
        typename F::type& list = *field.value.load(std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(mu);
        if (!list.empty()) return list.front();
        return std::nullopt;
    }

    std::optional<typename F::element> back() const
    {
        typename F::type& list = *field.value.load(std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(mu);
        if (!list.empty()) return list.back();
        return std::nullopt;
    }

    bool contains(typename F::element value) const
    {
        typename F::type& list = *field.value.load(std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(mu);
        return std::any_of(list.begin(), list.end(), [&](typename F::element& v) { return value == v; });
    }

    bool size() const 
    {
        typename F::type& list = *field.value.load(std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(mu);
        return list.size();
    }

    void add(typename F::element value, uint32_t cmdIdx = NO_COMMAND_INDEX)
    {
        addMatching([&](const typename F::element& entry) { return entry == value; }, value, cmdIdx);
    }

    void erase(typename F::element value)
    {
        eraseMatching([&](const typename F::element& entry) { return entry == value; });
    }

    /**
     * @brief Like @ref add, but identifies the entry to replace with `match`
     *        instead of `operator==` (e.g. `compareTuple`'s partial-pattern
     *        matching, which lets an omitted/`IgnoreCompare`-wrapped member
     *        pass through without disqualifying the match).
     */
    template <typename Match>
    void addMatching(Match&& match, typename F::element value, uint32_t cmdIdx = NO_COMMAND_INDEX)
    {
        bool shouldApply{true};
        if constexpr (RequiresValidation<F>)
            shouldApply = field.validator(provider, value);
        if (!shouldApply)
            return;

        field.commandIndex = cmdIdx;
        {
            std::lock_guard<std::mutex> lk(mu);
            typename Field::type& list = *field.value.load(std::memory_order_relaxed);
            auto it = std::find_if(list.begin(), list.end(), match);
            if (it != list.end())
                *it = value;
            else
                list.push_back(value);
        }
        if constexpr (RequiresContext<F>)
            field.applier(provider, value, true);
    }

    /**
     * @brief Like @ref erase, but identifies entries to remove with `match`
     *        instead of `operator==` (e.g. `compareTuple`'s partial-pattern
     *        matching). Removes every entry `match` accepts, same as
     *        `std::erase_if`; fires the applier once per removed entry.
     */
    template <typename Match>
    void eraseMatching(Match&& match, uint32_t cmdIdx = NO_COMMAND_INDEX)
    {
        std::vector<typename F::element> removed;
        {
            std::lock_guard<std::mutex> lk(mu);
            typename Field::type& list = *field.value.load(std::memory_order_relaxed);
            for (auto it = list.begin(); it != list.end();)
            {
                if (match(*it))
                {
                    removed.push_back(std::move(*it));
                    it = list.erase(it);
                }
                else
                    ++it;
            }
        }
        if (!removed.empty())
            field.commandIndex = cmdIdx;
        if constexpr (RequiresContext<F>)
            for (auto& value : removed)
                field.applier(provider, value, false);
    }

private:
    F& field;
    std::mutex& mu;
    Context provider;
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
          parent(&sub),
          provider(&sub)
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
    void notifyChanged(typename F::type* reg, const key& k) noexcept
    {
        if constexpr (RequiresContext<F>)
            field.applier(provider, reg, k);
    }

    /**
     * @brief Inserts a new child entry keyed by `k`, or returns the existing one.
     *
     * If the entry is newly inserted, fires the applier callback via
     * @ref notifyChanged so that the owning protocol process can react to the
     * structural change (e.g., re-evaluate neighbor configuration).
     *
     * @param k  Key identifying the child entry.
     * @return Pointer to the (new or existing) child entry, or `nullptr` if
     *         `k` was rejected by the field's validator (new insertions only;
     *         an already-existing entry is always returned regardless).
     */
    type* emplaceBack(const key& k, uint32_t cmdIdx = NO_COMMAND_INDEX) noexcept
    {
        auto [it, ok] = field.children.try_emplace(k, nullptr);
        if (ok)
        {
            if constexpr (RequiresValidation<F>)
            {
                if (!field.validator(provider, const_cast<key&>(it->first)))
                {
                    field.children.erase(it);
                    return nullptr;
                }
            }
            it->second = new type(parent);
            field.commandIndex = cmdIdx;
            notifyChanged(it->second, k);
        }
        return it->second;
    }

    /**
     * @brief Finds a child entry by key.
     *
     * @param k  Key to search for.
     * @return Const iterator to the matching entry, or `end()` if not found.
     */
    inline typename std::unordered_map<key, type*>::const_iterator find(const key& k) const noexcept
    {
        return field.children.find(k);
    }

    /**
     * @brief Returns the past-the-end iterator for the local children map.
     */
    inline typename std::unordered_map<key, type*>::const_iterator end() const noexcept
    {
        return field.children.end();
    }

    /**
     * @brief Returns the begin iterator for the local children map.
     */
    inline typename std::unordered_map<key, type*>::const_iterator begin() const noexcept
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
            notifyChanged(nullptr, k);
            if (it->second)
                delete it->second;
            field.children.erase(it);
        }
    }

    /**
     * @brief Removes all child entries and fires the applier.
     */
    inline void clear() noexcept
    {
        for (auto& [k, v] : field.children)
        {
            notifyChanged(nullptr, k);
            if (v) delete v;
        }
        field.children.clear();
    }

private:
    F& field;
    utils::Any parent;
    Context provider;
};

template <typename Base, typename ENUM, typename Fields>
template <ENUM F>
decltype(auto) SubRegistry<Base, ENUM, Fields>::get() noexcept
{
    using Field = FieldTypeAt<F>;
    if constexpr (IsRefContainer<Field>)
    {
        Field& container = getValue<F>();
        container.get().setParent(static_cast<Base*>(this));
        return container;
    }
    else if constexpr (IsOptionalRefContainer<Field>)
    {
        Field& container = getValue<F>();
        container.setOwner(static_cast<Base*>(this));
        return container;
    }
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

template <typename Base, typename ENUM, typename Fields>
template <ENUM F>
decltype(auto) SubRegistry<Base, ENUM, Fields>::get() const noexcept
{
    return const_cast<SubRegistry&>(*this).template get<F>();
}
}

#endif // FIELD_ACCESSOR_HPP
