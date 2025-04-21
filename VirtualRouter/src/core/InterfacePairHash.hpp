// InterfacePairHash.hpp

#ifndef INTERFACE_PAIR_HASH_HPP
#define INTERFACE_PAIR_HASH_HPP

#include <unordered_map>

enum class InterfaceType;

/**
 * @struct InterfacePairHash
 * @brief Hashes InterfaceType and float for interface operations.
 */
struct InterfacePairHash
{
    std::size_t operator()(const std::pair<InterfaceType, float>& p) const
    {
        std::size_t h1 = std::hash<std::underlying_type_t<InterfaceType>>{}(static_cast<std::underlying_type_t<InterfaceType>>(p.first));
        int quantized = static_cast<int>(p.second * 100.0f); //rounds to nearest 0.01
        std::size_t h2 = std::hash<int>{}(quantized);
        return h1 ^ (h2 << 1);
    }
};

#endif // INTERFACE_PAIR_HASH_HPP
