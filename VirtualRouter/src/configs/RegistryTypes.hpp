// RegistryTypes.hpp

#ifndef REGISTRY_TYPES_HPP
#define REGISTRY_TYPES_HPP

#include <cstdint>
#include <atomic>
#include <span>

#define FIELD(field) \
    (FieldBase*)&field

namespace Config
{
enum class ValueState : uint8_t
{
    UNSET,
    SET
};

enum class MaskState : uint8_t
{
    INHERIT,
    SET
};

template <typename T, T dval>
struct AtomicValue
{
    std::atomic<T> value;

    AtomicValue(T def)
        : value(def) {}

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
        value.store(dval, std::memory_order_relaxed);
    }
};

template <typename T, T dval>
struct MaskedAtomicValue
{
    std::atomic<T> value;
    std::atomic<MaskState> state;
    AtomicValue<T, dval>* base;

    MaskedAtomicValue(AtomicValue<T, dval>& fallback)
        : value(fallback.load()),
          state(MaskState::INHERIT),
          base(&fallback)
    {}

    inline T load() const noexcept
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
}
}

namespace Config2
{
using FieldId = uint32_t;
using RecordTypeId = uint32_t;

template <typename T>
struct Field
{
    FieldId id;
    const char* name;
};

struct FieldBase
{
    FieldId id;
    const char* name;
};

template <typename T>
struct FieldDef : FieldBase
{
    using type = T;
    T defaultValue;
};

struct RecordType
{
    RecordTypeId id;
    const char* name;
    std::span<const Field<void>*> fields;
};



enum MaskState : uint8_t
{
    INHERIT,
    SET
};

template <typename T>
struct Masked
{
    MaskState state{MaskState::INHERIT};
    T value{};
};

template <typename T>
T resolve(const Masked<T>& local, const Masked<T>* inherit, const FieldDef<T>& field)
{
    if (local.state == MaskState::SET)
        return local.value;
    if (inherit && inherit->state == MaskState::SET)
        return inherit->value;

    return field.defaultValue;
}
}

#endif // REGISTRY_TYPES_HPP
