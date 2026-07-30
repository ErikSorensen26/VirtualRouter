/**
 * @file NetworkSpan.hpp
 * @brief Endian-transparent byte-level view over an unsigned integer address type.
 */

#ifndef NETWORK_SPAN_HPP
#define NETWORK_SPAN_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <bit>
#include <iterator>

namespace types
{

/**
 * @brief Zero-overhead wrapper that exposes an unsigned integer as a
 *        network-order (big-endian) byte sequence, regardless of host endianness.
 * @ingroup TYPES
 *
 * Trie and prefix-matching algorithms in this codebase require addresses to be
 * accessible as arrays of bytes in network bit-order: byte 0 holds the
 * most-significant octet, bit 0 of byte 0 is the most-significant bit of the
 * address. On a little-endian host a raw @c uint32_t lays out its most-significant
 * byte at the highest address — the opposite of what the tries expect.
 *
 * @c NetworkSpan solves this by wrapping a value in-place (zero allocation,
 * zero copy) and remapping byte indices through its @c operator[] so that
 * index 0 always refers to the most-significant byte. The iterator exposes the
 * same remapping so that range-based algorithms see bytes in network order.
 *
 * ## Architectural Role
 * @c NetworkSpan is a pure adapter: it holds no data of its own and adds no
 * members. It is intended to be used by value (e.g. as a local variable derived
 * from a @c uint32_t or @c uint128_t field) and passed by const reference to
 * lookup functions in @ref LPCTrie. It is not a storage type.
 *
 * ## Concurrency Model
 * @c NetworkSpan is stateless beyond the address it wraps; all operations are
 * @c constexpr or @c noexcept and impose no synchronization requirements.
 *
 * @warning @c NetworkSpan reinterpret_casts itself to @c uint8_t* internally.
 *          The wrapped value must remain valid (not moved or destroyed) for the
 *          entire lifetime of any @c NetworkSpan that views it.
 *
 * @tparam T An unsigned integer type (@c uint32_t, @c uint64_t, etc.) whose
 *           size determines the byte extent of the view. Must satisfy
 *           @c std::unsigned_integral. Mixed-endian platforms are not supported.
 *
 * @see LPCTrie
 */
template<std::unsigned_integral T>
class NetworkSpan
{
public:
    /// Number of bytes in the address (sizeof(T)).
    static constexpr size_t extent = sizeof(T);

private:
    /**
     * @brief Byte-level iteration direction: -1 on little-endian (reverse
     *        physical layout), +1 on big-endian (natural layout).
     *
     * This is the only endianness knob.
     */
    static constexpr std::ptrdiff_t stride = (std::endian::native == std::endian::little) ? -1 : 1;
    static_assert(std::endian::native == std::endian::little || std::endian::native == std::endian::big,
                  "mixed-endian systems not supported");

public:
    NetworkSpan(const NetworkSpan&) = delete;
    NetworkSpan(NetworkSpan&) noexcept = delete;
    NetworkSpan(const NetworkSpan&&) = delete;
    NetworkSpan(NetworkSpan&&) noexcept = delete;

    /**
     * @brief Random-access iterator that yields bytes in network (big-endian) order.
     * @ingroup TYPES
     *
     * Logical index 0 maps to the most-significant byte of the wrapped value.
     * The mapping is compiled away entirely for big-endian hosts (stride == 1)
     * and inverts physical addressing for little-endian hosts (stride == -1).
     *
     * @tparam S Compile-time stride constant inherited from @c NetworkSpan::stride.
     *           Callers do not supply this directly; use @c NetworkSpan::iterator.
     */
    template<std::ptrdiff_t S>
    class Iterator
    {
    public:
        constexpr Iterator() noexcept = default;
        constexpr Iterator(uint8_t* base, std::ptrdiff_t pos) noexcept
            : base(base), pos(pos)
        {}

        constexpr uint8_t& operator*() const noexcept
        {
            return base[mapIndex(pos)];
        }
        constexpr uint8_t* operator->() const noexcept
        {
            return base + mapIndex(pos);
        }
        constexpr uint8_t& operator[](ptrdiff_t n) const noexcept
        {
            return *(*this + n);
        }

        constexpr Iterator& operator++()    noexcept { ++pos; return *this; }
        constexpr Iterator  operator++(int) noexcept { auto tmp = *this; ++*this; return tmp; }
        constexpr Iterator& operator--()    noexcept { --pos; return *this; }
        constexpr Iterator  operator--(int) noexcept { auto tmp = *this; --*this; return tmp; }

        constexpr Iterator& operator+=(ptrdiff_t n) noexcept { pos += n; return *this; }
        constexpr Iterator& operator-=(ptrdiff_t n) noexcept { pos -= n; return *this; }

        friend constexpr Iterator operator+(Iterator it, ptrdiff_t n) noexcept { it += n; return it; }
        friend constexpr Iterator operator+(ptrdiff_t n, Iterator it) noexcept { it += n; return it; }
        friend constexpr Iterator operator-(Iterator it, ptrdiff_t n) noexcept { it -= n; return it; }
        friend constexpr ptrdiff_t operator-(const Iterator& a, const Iterator& b) noexcept
        {
            return a.pos - b.pos;
        }

        friend constexpr bool operator==(const Iterator& a, const Iterator& b) noexcept
        {
            return a.base == b.base && a.pos == b.pos;
        }
        friend constexpr auto operator<=>(const Iterator& a, const Iterator& b) noexcept
        {
            if (a.base != b.base)
                return a.base <=> b.base;
            return a.pos <=> b.pos;
        }

    private:
        uint8_t* base = nullptr;
        std::ptrdiff_t pos = 0;

        /// Translates a logical network-order index to the physical byte offset.
        static constexpr size_t mapIndex(std::ptrdiff_t idx) noexcept
        {
            static_assert(stride == 1 || stride == -1);
            if constexpr (stride == 1)
                return static_cast<size_t>(idx);
            else
                return NetworkSpan::extent - 1 - static_cast<size_t>(idx);
        }
    };

    using iterator       = Iterator<stride>;
    using const_iterator = iterator;

    constexpr NetworkSpan() noexcept = default;

    /**
     * @brief Returns the byte at logical network-order index @p idx.
     *
     * Index 0 is the most-significant byte; index @c extent-1 is the
     * least-significant byte. On little-endian hosts this reverses the
     * physical byte order of the underlying @c T value.
     *
     * @param idx Logical byte index in [0, extent).
     */
    constexpr uint8_t& operator[](size_t idx) noexcept
    {
        return reinterpret_cast<uint8_t*>(this)[mapIndex(idx)];
    }
    constexpr const uint8_t& operator[](size_t idx) const noexcept
    {
        return reinterpret_cast<const uint8_t*>(this)[mapIndex(idx)];
    }

    constexpr uint8_t& front() noexcept       { return (*this)[0]; }
    constexpr const uint8_t& front() const noexcept { return (*this)[0]; }
    constexpr uint8_t& back() noexcept        { return (*this)[extent - 1]; }
    constexpr const uint8_t& back() const noexcept  { return (*this)[extent - 1]; }

    static constexpr size_t size()        noexcept { return extent; }
    static constexpr bool   empty()       noexcept { return false; }

    /**
     * @brief Returns a pointer to the raw bytes of the underlying value in
     *        physical (host) memory order.
     *
     * @note This pointer is in host byte order, not network byte order.
     *       Use @c operator[] or the iterators for network-order access.
     */
    constexpr const uint8_t* data() const noexcept { return reinterpret_cast<const uint8_t*>(this); }

    constexpr const_iterator begin()  const noexcept { return cbegin(); }
    constexpr const_iterator end()    const noexcept { return cend(); }
    constexpr const_iterator cbegin() const noexcept { return {reinterpret_cast<uint8_t*>(const_cast<NetworkSpan*>(this)), 0}; }
    constexpr const_iterator cend()   const noexcept { return {reinterpret_cast<uint8_t*>(const_cast<NetworkSpan*>(this)), static_cast<std::ptrdiff_t>(extent)}; }

    constexpr auto rbegin()  const noexcept { return std::reverse_iterator{cend()}; }
    constexpr auto rend()    const noexcept { return std::reverse_iterator{cbegin()}; }
    constexpr auto crbegin() const noexcept { return std::reverse_iterator{cend()}; }
    constexpr auto crend()   const noexcept { return std::reverse_iterator{cbegin()}; }

    /**
     * @brief Implicit conversion to the underlying integer type.
     *
     * Allows a @c NetworkSpan<uint32_t> to be used anywhere a @c uint32_t&
     * is expected, preserving the in-place layout guarantee.
     */
    constexpr operator T&() noexcept
    {
        return *reinterpret_cast<T*>(this);
    }
    constexpr operator const T&() const noexcept
    {
        return *reinterpret_cast<const T*>(this);
    }

private:
    /// Maps a logical network-order byte index to the physical byte offset within *this.
    static constexpr size_t mapIndex(size_t idx) noexcept
    {
        if constexpr (std::endian::native == std::endian::little)
            return extent - 1 - idx;
        else
            return idx;
    }
};

} // namespace types

#endif // NETWORK_SPAN_HPP
