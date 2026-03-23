// NetworkSpan.hpp

#ifndef NETWORK_SPAN_HPP
#define NETWORK_SPAN_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <bit>
#include <iterator>

namespace types
{

template<std::unsigned_integral T>
class NetworkSpan
{
public:
    static constexpr size_t extent = sizeof(T);

private:
    static constexpr std::ptrdiff_t stride = (std::endian::native == std::endian::little) ? -1 : 1;
    static_assert(std::endian::native == std::endian::little || std::endian::native == std::endian::big, "mixed-endian systems not supported");

public:
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

        static constexpr size_t mapIndex(std::ptrdiff_t idx) noexcept
        {
            static_assert(stride == 1 || stride == -1);
            if constexpr (stride == 1)
                return static_cast<size_t>(idx);
            else
                return NetworkSpan::extent - 1 - static_cast<size_t>(idx);
        }
    };

    using iterator = Iterator<stride>;
    using const_iterator = iterator;

    constexpr NetworkSpan() noexcept = default;

    constexpr uint8_t& operator[](size_t idx) noexcept
    {
        return reinterpret_cast<uint8_t*>(this)[mapIndex(idx)];
    }
    constexpr const uint8_t& operator[](size_t idx) const noexcept
    {
        return reinterpret_cast<const uint8_t*>(this)[mapIndex(idx)];
    }

    constexpr uint8_t& front() noexcept { return (*this)[0]; }
    constexpr const uint8_t& front() const noexcept { return (*this)[0]; }
    constexpr uint8_t& back() noexcept { return (*this)[extent - 1]; }
    constexpr const uint8_t& back() const noexcept { return (*this)[extent - 1]; }

    static constexpr size_t size()       noexcept { return extent; }
    static constexpr bool   empty()      noexcept { return false; }
    constexpr const uint8_t* data() const noexcept { return reinterpret_cast<const uint8_t*>(this); }

    constexpr const_iterator begin()  const noexcept { return cbegin(); }
    constexpr const_iterator end()    const noexcept { return cend(); }
    constexpr const_iterator cbegin() const noexcept { return {reinterpret_cast<uint8_t*>(const_cast<NetworkSpan*>(this)), 0}; }
    constexpr const_iterator cend()   const noexcept { return {reinterpret_cast<uint8_t*>(const_cast<NetworkSpan*>(this)), static_cast<std::ptrdiff_t>(extent)}; }

    constexpr auto rbegin()  const noexcept { return std::reverse_iterator{cend()}; }
    constexpr auto rend()    const noexcept { return std::reverse_iterator{cbegin()}; }
    constexpr auto crbegin() const noexcept { return std::reverse_iterator{cend()}; }
    constexpr auto crend()   const noexcept { return std::reverse_iterator{cbegin()}; }

    constexpr operator T&() noexcept
    {
        return *reinterpret_cast<T*>(this);
    }
    constexpr operator const T&() const noexcept
    {
        return *reinterpret_cast<const T*>(this);
    }

private:
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

