/**
 * @file Mac.hpp
 * @brief Mac address (Mainly for config parsing)
 * @ingroup TYPES
 */

#ifndef MAC_HPP
#define MAC_HPP

#include <cstdint>
#include <functional>

namespace types
{
struct Mac
{
    Mac() = default;
    Mac(uint64_t m)
        : mac(m)
    {}

    bool operator==(const Mac& other) const
    {
        return mac == other.mac;
    }
    operator uint64_t() const { return mac; }

    uint64_t mac;
};
}

namespace std
{
template <>
struct hash<types::Mac>
{
    size_t operator()(const types::Mac& k) const noexcept
    {
        return std::hash<uint64_t>{}(k.mac);
    }
};
}

#endif // MAC_HPP
