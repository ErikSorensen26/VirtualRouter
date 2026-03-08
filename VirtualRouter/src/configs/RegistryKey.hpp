// RegistryKey.hpp

#ifndef REGISTRY_KEY_HPP
#define REGISTRY_KEY_HPP

#include <array>
#include <stddef.h>
#include <cstring>
#include <bit>
#include <type_traits>
#include <cstdint>
#include <algorithm>

namespace Config
{
template <size_t N>
struct RegistryKey
{
    static constexpr size_t size = N;

private:
    std::array<uint8_t, N> data{};

public:

    constexpr RegistryKey() = default;

    template <size_t S>
    constexpr RegistryKey(const RegistryKey<S>& other) { size_t siz = std::min(S, N); std::memcpy(data.data(), other.dataPtr(), siz); }

    template <size_t S>
    constexpr RegistryKey(const std::array<uint8_t, S>& d) { std::memcpy(data.data(), d.data(), S); }
    constexpr RegistryKey(const uint8_t* d) { std::memcpy(data.data(), d, N); }

    // Access
    constexpr uint8_t& operator[](size_t i) noexcept { return data[i]; }
    constexpr const uint8_t& operator[](size_t i) const noexcept { return data[i]; }

    constexpr uint8_t* dataPtr() noexcept { return data.data(); }
    constexpr const uint8_t* dataPtr() const noexcept { return data.data(); }

    constexpr auto begin() noexcept { return data.begin(); }
    constexpr auto end() noexcept { return data.end(); }

private:

    template <typename Int>
    static constexpr void writeInteger(std::array<uint8_t, N>& out, Int v)
    {
        constexpr size_t siz = std::min(sizeof(Int), N);

        if constexpr (std::endian::native == std::endian::big)
        {
            std::memcpy(out.data(), &v, siz);
        }
        else
        {
            for (size_t i = 0; i < siz; ++i)
            {
                size_t shift = (N - 1 - i) * 8;
                out[i] = static_cast<uint8_t>((v >> shift) & 0xFF);
            }
        }
    }

public:

    template <typename Int>
    requires (std::is_unsigned_v<Int>)
    constexpr RegistryKey(Int v)
    {
        writeInteger(data, v);
    }
};

template <size_t N>
struct RegistryKeyHash
{
    size_t operator()(const RegistryKey<N>& key) const noexcept
    {
        const uint8_t* bytes =
            reinterpret_cast<const uint8_t*>(key.dataPtr());

        size_t hash = 0xcbf29ce484222325ULL;

        size_t len = N;

        for (size_t i = 0; i < len; ++i)
        {
            hash ^= bytes[i];
            hash *= 0x100000001b3ULL;
        }

        return hash;
    }
};
template <typename>
struct isRegistryKey : std::false_type {};

template <size_t N>
struct isRegistryKey<RegistryKey<N>> : std::true_type {};
}

#endif // REGISTRY_KEY_HPP
