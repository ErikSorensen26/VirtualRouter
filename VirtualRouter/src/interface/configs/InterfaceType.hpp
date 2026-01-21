// InterfaceType.hpp

#ifndef INTERFACE_TYPE_HPP
#define INTERFACE_TYPE_HPP

#include <string>
#include <cstdint>

/**
 * @enum InterfaceType
 * @brief Enunerates the various types of network interfaces supported
 */
/**
 * @enum InterfaceType
 * @brief Enunerates the various types of network interfaces supported
 */
enum class InterfaceType : uint8_t
{
    UNDEFINED = 0,          ///< Undefined interface type.
    ETHERNET = 1,           ///< Ethernet interface type.
    FAST_ETHERNET = 2,      ///< Fast Ethernet type.
    GIGABIT_ETHERNET = 3,   ///< Gigabit Ethernet interface type.
    LOOPBACK = 4,           ///< Loopback interface type.
    PORT_CHANNEL = 5,       ///< Port-channel interface type.
    TUNNEL = 6,             ///< Tunnel interface type.
    VIRTUAL_TEMPLATE = 7,   ///< Virtual Template interface type.
    VLAN = 8                ///< VLAN interface type.
};

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

#endif // INTERFACE_TYPE_HPP
