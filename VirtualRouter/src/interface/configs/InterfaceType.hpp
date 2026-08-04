/**
 * @file InterfaceType.hpp
 * @brief Enumeration and conversion functions for interface types (Ethernet, Loopback, Tunnel, etc.).
 */

#ifndef INTERFACE_TYPE_HPP
#define INTERFACE_TYPE_HPP

#include <string>
#include <cstdint>
#include <functional>

namespace interface
{
#define INTERFACE_TYPE_FIRST(X) X(ETHERNET, "Ethernet")

#define INTERFACE_TYPE_REST(X) \
    X(FAST_ETHERNET,        "FastEthernet") \
    X(GIGABIT_ETHERNET,     "GigabitEthernet") \
    X(LOOPBACK,             "Loopback") \
    X(PORT_CHANNEL,         "PortChannel") \
    X(TUNNEL,               "Tunnel") \
    X(VIRTUAL_TEMPLATE,     "Virtual-Template") \
    X(VLAN,                 "Vlan")


/**
 * @enum InterfaceType
 * @brief Enumeration of all supported interface types.
 * @ingroup INTERFACE_CONFIGS
 *
 * Defines the complete set of interface types supported by the router:
 * physical Ethernet types (with speed variants), logical types (Loopback, Tunnel, VLAN),
 * and aggregate types (Port-Channel). Used throughout the system to determine interface
 * behavior, display naming, and capabilities.
 */
enum class InterfaceType : uint8_t
{
#define X(name, str) name,
    INTERFACE_TYPE_FIRST(X)
    INTERFACE_TYPE_REST(X)
#undef X
    COUNT,
    UNDEFINED
};

/**
 * @brief Lookup table mapping each @ref InterfaceType enumerator to its CLI string.
 *
 * Indexed by the underlying `uint8_t` value of @ref InterfaceType. Each entry holds
 * the canonical CLI name (e.g. `"GigabitEthernet"`, `"Loopback"`). Do not index at
 * or beyond `InterfaceType::COUNT`.
 */
constexpr const char* InterfaceTypeLabels[] = {
#define X(name, str) str,
    INTERFACE_TYPE_FIRST(X)
    INTERFACE_TYPE_REST(X)
#undef X
};


/**
 * @brief Converts a CLI string to InterfaceType enum.
 *
 * Maps CLI command strings (e.g., "Ethernet", "FastEthernet") to the corresponding
 * enum value. Case-sensitive matching against standard CLI naming conventions.
 *
 * @param type CLI interface type string.
 * @return InterfaceType enum value, or UNDEFINED if not recognized.
 *
 * @see getInterfaceType(InterfaceType)
 */
inline static InterfaceType getInterfaceType(std::string_view str)
{
    for (size_t i = 0; i < static_cast<size_t>(InterfaceType::COUNT); ++i)
    {
        if (str == InterfaceTypeLabels[i])
            return static_cast<InterfaceType>(i);
    }
    return InterfaceType::UNDEFINED;
}

/**
 * @brief Converts InterfaceType enum to a CLI string.
 *
 * Maps enum values back to their CLI representation for display, logging, and configuration output.
 *
 * @param type InterfaceType enum value.
 * @return CLI string (e.g., "Ethernet", "FastEthernet"), or empty string for UNDEFINED.
 *
 * @see getInterfaceType(const std::string&)
 */
inline static std::string getInterfaceType(const InterfaceType type)
{
    size_t idx = static_cast<size_t>(type);
    return (idx < static_cast<size_t>(InterfaceType::COUNT))
        ? InterfaceTypeLabels[idx] : std::string{};
}

/**
 * @brief Encodes an interface type and fractional interface number into a 32-bit key.
 *
 * The upper 8 bits carry the @ref InterfaceType; the lower 24 bits hold the
 * interface number scaled by 256 (to represent sub-interface fractions like
 * GigabitEthernet0/0.1). The result is suitable for use as an unordered-map
 * key or as a stable interface identifier passed between subsystems.
 *
 * @param type  Interface type to encode.
 * @param id    Interface number, including fractional sub-interface component.
 * @return 32-bit key with type in bits [31:24] and fixed-point id in bits [23:0].
 */
inline uint32_t encodeInterfaceKey(InterfaceType type, float id)
{
    uint8_t typeEncoded = static_cast<uint8_t>(type);
    float clamped = std::max(0.0f, std::min(id, 65535.256f));
    uint32_t fixed = static_cast<uint32_t>(clamped * 256.0f);
    fixed &= 0x00FFFFFF;
    return (static_cast<uint32_t>(typeEncoded) << 24) | fixed;
}

/**
 * @brief Decodes a 32-bit key back into its interface type and fractional interface number.
 *
 * Reverses the encoding produced by @ref encodeInterfaceKey: extracts the
 * @ref InterfaceType from bits [31:24] and reconstructs the floating-point
 * interface number from the fixed-point value in bits [23:0].
 *
 * @param key  32-bit encoded interface key.
 * @return     Pair of `{InterfaceType, float id}`.
 *
 * @see encodeInterfaceKey
 */
inline std::pair<InterfaceType, float> decodeInterfaceKey(uint32_t key)
{
    InterfaceType type = static_cast<InterfaceType>((key >> 24) & 0xFF);
    uint32_t fixed = key & 0x00FFFFFF;
    float id = static_cast<float>(fixed) / 256.0f;
    return { type, id };
}


/**
 * @brief Compact, hashable identifier for a logical interface.
 * @ingroup INTERFACE_CONFIGS
 *
 * Packs an @ref InterfaceType and a floating-point interface number into a
 * single 32-bit integer via @ref encodeInterfaceKey. The result is trivially
 * copyable and directly usable as an `unordered_map` key (a `std::hash`
 * specialisation is provided in `namespace std`).
 */
struct InterfaceKey
{
    InterfaceKey() = default;

    /**
     * @brief Constructs a key from an interface type and sub-interface number.
     *
     * Encodes both fields into a single 32-bit integer via @c encodeInterfaceKey
     * so the key is trivially hashable and copyable.
     *
     * @param type Interface type (Ethernet, Loopback, etc.).
     * @param id   Sub-interface number, including fractional part for sub-interfaces.
     */
    InterfaceKey(InterfaceType type, float id)
        : id(encodeInterfaceKey(type, id))
    {}

    /**
     * @brief Constructs a key directly from an already-encoded 32-bit value.
     *
     * Used when round-tripping a key that was previously stored as a uint32_t
     * (e.g. in a config registry or on-wire format).
     *
     * @param ifaceId Pre-encoded interface key integer.
     */
    InterfaceKey(uint32_t ifaceId)
        : id(ifaceId)
    {}

    /// Returns true if both keys encode the same interface type and number.
    bool operator==(const InterfaceKey& k) const noexcept
    {
        return k.getId() == id;
    }

    /// Returns the raw 32-bit encoded value.
    uint32_t getId() const { return id; }

    /**
     * @brief Decodes the key into its component type and interface number.
     * @see decodeInterfaceKey
     */
    std::pair<InterfaceType, float> decode() const { return decodeInterfaceKey(id); }

private:
    uint32_t id;
};
} // namespace interface

namespace std
{
/**
 * @brief `std::hash` specialisation for @ref interface::InterfaceKey.
 *
 * Delegates to `std::hash<uint32_t>` on the raw encoded key value, making
 * `InterfaceKey` directly usable in `std::unordered_map` and
 * `std::unordered_set` without a custom hash argument.
 */
template<>
struct hash<interface::InterfaceKey>
{
    size_t operator()(const interface::InterfaceKey& key) const noexcept
    {
        return std::hash<uint32_t>()(key.getId());
    }
};
}

#endif // INTERFACE_TYPE_HPP
