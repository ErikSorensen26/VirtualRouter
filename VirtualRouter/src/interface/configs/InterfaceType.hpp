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
    UNDEFINED = 0,          ///< Undefined or unknown interface type.
    ETHERNET = 1,           ///< Base Ethernet interface (1 Mbps or legacy).
    FAST_ETHERNET = 2,      ///< Fast Ethernet interface (10/100 Mbps).
    GIGABIT_ETHERNET = 3,   ///< Gigabit Ethernet interface (1+ Gbps).
    LOOPBACK = 4,           ///< Loopback interface (always up, no neighbors).
    PORT_CHANNEL = 5,       ///< Aggregated physical interfaces (LAG/LACP).
    TUNNEL = 6,             ///< Tunnel interface (virtual IP-in-IP).
    VIRTUAL_TEMPLATE = 7,   ///< Virtual Template interface (template for cloning).
    VLAN = 8                ///< VLAN subinterface (L2 virtual interface).
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
inline static InterfaceType getInterfaceType(const std::string& type)
{
    if (type == "Ethernet") return InterfaceType::ETHERNET;
    else if (type == "FastEthernet") return InterfaceType::FAST_ETHERNET;
    else if (type == "GigabitEthernet") return InterfaceType::GIGABIT_ETHERNET;
    else if (type == "Loopback") return InterfaceType::LOOPBACK;
    else if (type == "Portchannel") return InterfaceType::PORT_CHANNEL;
    else if (type == "Tunnel") return InterfaceType::TUNNEL;
    else if (type == "Virtual-Template") return InterfaceType::VIRTUAL_TEMPLATE;
    else if (type == "Vlan") return InterfaceType::VLAN;
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
    switch (type)
    {
        case InterfaceType::ETHERNET:
            return "Ethernet";
        case InterfaceType::FAST_ETHERNET:
            return "FastEthernet";
        case InterfaceType::GIGABIT_ETHERNET:
            return "GigabitEthernet";
        case InterfaceType::LOOPBACK:
            return "Loopback";
        case InterfaceType::PORT_CHANNEL:
            return "Portchannel";
        case InterfaceType::TUNNEL:
            return "Tunnel";
        case InterfaceType::VIRTUAL_TEMPLATE:
            return "Virtual-Template";
        case InterfaceType::VLAN:
            return "Vlan";
        case InterfaceType::UNDEFINED:
            return {};
    }
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
 * @brief decodes a 32-bit key into a interface type and fractional interface number.
 *
 * @param id    Interface number, including fractional sub-interface component.
 * @return std::pair<InterfaceType, float> Interface type and interface id.
 */
inline std::pair<InterfaceType, float> decodeInterfaceKey(uint32_t key)
{
    InterfaceType type = static_cast<InterfaceType>((key >> 24) & 0xFF);
    uint32_t fixed = key & 0x00FFFFFF;
    float id = static_cast<float>(fixed) / 256.0f;
    return { type, id };
}


/**
 * @brief Interface Key type
 * @ingroup INTERFACE_CONFIGS
 *
 * Defines the interface key type for configurations
 */
struct InterfaceKey
{
    // TODO finish doxy
    InterfaceKey() = default;

    bool operator==(const InterfaceKey& k) const noexcept
    {
        return k.getId() == id;
    }

    InterfaceKey(InterfaceType type, float id)
        : id(encodeInterfaceKey(type, id))
    {}

    InterfaceKey(uint32_t ifaceId)
        : id(ifaceId)
    {}

    uint32_t getId() const { return id; }

    std::pair<InterfaceType, float> decode() { return decodeInterfaceKey(id); }

private:
    uint32_t id;
};
} // namespace interface

namespace std
{
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