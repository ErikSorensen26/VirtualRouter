// RegistryTypes.hpp

#ifndef REGISTRY_TYPES_HPP
#define REGISTRY_TYPES_HPP

#include <atomic>
#include <concepts>
#include <cassert>

#define FIELD(field) \
    (FieldBase*)&field

namespace Config
{
template <typename T>
concept HasEmpty = requires(const T& t)
    {
        { t.empty() } -> std::convertible_to<bool>;
    };

template <typename T>
concept HasClear = requires(T t)
    {
        t.clear();
    };

template <typename T>
concept VariableMultiplicityField =
    HasEmpty<T> && HasClear<T>;

enum class MaskState : uint8_t
{
    INHERIT,
    SET
};

template <typename T, T D, auto F>
class AtomicField
{
public:
    using type = T;
    static constexpr auto dValue = D;
    static constexpr auto field = F;

    inline T load() const noexcept
    {
        return value.load(std::memory_order_relaxed);
    }

    inline void set(T v) noexcept
    {
        value.store(v, std::memory_order_release);
    }

    inline void unset() noexcept
    {
        value.store(dValue, std::memory_order_relaxed);
    }

private:
    std::atomic<T> value = D;
};

template <typename T, auto F>
class UnsetAtomicField
{
public:
    using type = T;
    using isUnset = void; // Used for identification
    static constexpr auto field = F;

    inline T load() const noexcept
    {
        assert(valueSet.load(std::memory_order_relaxed) == true);
        return value.load(std::memory_order_relaxed);
    }

    inline T set(T v) noexcept
    {
        value.store(v, std::memory_order_release);
        valueSet.store(true, std::memory_order_release);
    }

    inline T unset() noexcept
    {
        valueSet.store(false, std::memory_order_release);
    }

    inline bool hasValue() noexcept
    {
        return valueSet.load(std::memory_order_relaxed);
    }

private:
    std::atomic<bool> valueSet{false};
    std::atomic<T> value;
};

template <VariableMultiplicityField T, auto F>
class VariableField
{
public:
    using type = T;
    static constexpr auto field = F;

    inline T& get() const noexcept
    {
        return value;
    }

    inline void unset() noexcept
    {
        value = {};
    }
private:
    T value = {};
};

template <typename T, T D, auto F>
class MaskedAtomicField
{
public:
    MaskedAtomicField(AtomicField<T, D, F>& fallback)
        : state(MaskState::INHERIT),
          base(&fallback)
    {}

    inline T get() const noexcept
    {
        if (state.load(std::memory_order_relaxed) == MaskState::SET)
            return value.load(std::memory_order_relaxed);
        return base->load();
    }

    inline void set(T v) noexcept
    {
        value.store(v, std::memory_order_relaxed);
        state.store(MaskState::SET, std::memory_order_relaxed);
    }

    inline void unset() noexcept
    {
        state.store(MaskState::INHERIT, std::memory_order_relaxed);
    }

private:
    std::atomic<T> value;
    std::atomic<MaskState> state;
    AtomicField<T, D, F>* base;
};

template <auto F>
class MaskedRefContainer
{
    static constexpr auto type = F;
};

template <typename T, auto F>
class MaskedUnsetAtomicField
{
public:
    MaskedUnsetAtomicField(UnsetAtomicField<T, F>& fallback)
        : state(MaskState::INHERIT),
          base(&fallback)
    {}

    inline T get() const noexcept
    {
        if (state.load(std::memory_order_relaxed) == MaskState::SET)
            return value.load(std::memory_order_relaxed);
        return base->load();
    }

    inline void set(T v) noexcept
    {
        value.store(v, std::memory_order_relaxed);
        state.store(MaskState::SET, std::memory_order_relaxed);
    }

    inline void unset() noexcept
    {
        state.store(MaskState::INHERIT, std::memory_order_relaxed);
    }

    inline bool hasValue() noexcept
    {
        return state.load(std::memory_order_relaxed) == MaskState::SET || base->hasValue();
    }

private:
    std::atomic<T> value;
    std::atomic<MaskState> state;
    UnsetAtomicField<T, F>* base;
};

template <VariableMultiplicityField T, auto F>
class MaskedVariableField
{ 
public:
    using type = T;
    static constexpr T field = F;

    inline T& get() const noexcept
    {
        if (!value.empty())
            return value;
        return base->get();
    }

    inline void unset() noexcept
    {
        value.clear();
    }
private:
    T value;
    VariableField<T, F>* base;
};
}

#endif // REGISTRY_TYPES_HPP
