/**
 * @file EnumBitMap.hpp
 * @brief Compact bitset indexed by a scoped enum.
 * @ingroup Types
 */

#ifndef ENUM_BIT_MAP_HPP
#define ENUM_BIT_MAP_HPP

#include <type_traits>
#include <ByteUtils.hpp>

/**
 * @defgroup Types Types
 * @brief Shared primitive types: addresses, prefixes, bitfields, and small utilities.
 */

namespace types
{
/**
 * @brief Compact bitset whose bits are addressed by a scoped enum.
 * @ingroup Types
 *
 * Maps each enumerator to one bit in the smallest integer that fits all
 * values. The enum must end with a `COUNT` sentinel so the storage width
 * can be computed at compile time.
 *
 * Each instance contains:
 * - A single integer of the smallest type that holds `E::COUNT` bits.
 *
 * ## Architectural Role
 * Used wherever a lightweight set-of-flags is needed but a raw integer
 * would be error-prone.  The enum type name documents which flags are
 * valid; the template enforces that only those values are tested or set.
 *
 * ## Lifecycle & Ownership
 * Value type — trivially constructible and copyable.
 *
 * @tparam E  Scoped enum type. Must have a `COUNT` enumerator whose value
 *            equals the number of meaningful enumerators. All enumerators
 *            must be non-negative and less than `COUNT`.
 */
template <typename E>
requires std::is_enum_v<E>
class EnumBitMap
{
private:
    static constexpr size_t bitCount = static_cast<size_t>(E::COUNT);
    using Storage = utils::SmallestInteger<bitCount>::type;
    Storage bits{0};
    static Storage bit(E e) noexcept
    {
        return Storage(1) << static_cast<Storage>(e);
    }
public:
    using Enum = E;
    using type = Storage;

    EnumBitMap() = default;

    /**
     * @brief Constructs from a raw integer, treating each bit as a flag.
     * @param storage  Raw bit pattern; bits beyond `E::COUNT` are ignored.
     */
    EnumBitMap(Storage storage)
        : bits(storage)
    {}

    /// @brief Sets the bit corresponding to `e`.
    void set(Enum e) noexcept
    {
        bits |= bit(e);
    }

    /// @brief Clears the bit corresponding to `e`.
    void clear(Enum e) noexcept
    {
        bits &= ~bit(e);
    }

    /// @brief Flips the bit corresponding to `e`.
    void toggle(Enum e) noexcept
    {
        bits ^= bit(e);
    }

    /// @brief Clears all bits.
    void reset() noexcept
    {
        bits = 0;
    }

    /// @brief Returns true if the bit corresponding to `e` is set.
    bool test(Enum e) const noexcept
    {
        return (bits & bit(e)) != 0;
    }

    /// @brief Returns true if at least one bit is set.
    bool any() const noexcept
    {
        return bits != 0;
    }

    /// @brief Returns true if no bits are set.
    bool none() const noexcept
    {
        return bits == 0;
    }

    /// @brief Returns the underlying raw integer value.
    Storage raw() const noexcept
    {
        return bits;
    }
};
}

#endif // ENUM_BIT_MAP_HPP
