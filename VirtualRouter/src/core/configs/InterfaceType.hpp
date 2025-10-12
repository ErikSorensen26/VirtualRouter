// InterfaceType.hpp

#ifndef INTERFACE_TYPE_HPP
#define INTERFACE_TYPE_HPP

#include <string>

/**
 * @enum InterfaceType
 * @brief Enunerates the various types of network interfaces supported
 */
enum class InterfaceType
{
    UNDEFINED,          ///< Undefined interface type.
    DIALER,             ///< Dialer interface type.
    ETHERNET,           ///< Ethernet interface type.
    FAST_ETHERNET,      ///< Fast Ethernet type.
    GIGABIT_ETHERNET,   ///< Gigabit Ethernet interface type.
    LOOPBACK,           ///< Loopback interface type.
    PORT_CHANNEL,       ///< Port-channel interface type.
    TUNNEL,             ///< Tunnel interface type.
    VIRTUAL_TEMPLATE,   ///< Virtual Template interface type.
    VLAN                ///< VLAN interface type.
};

inline static InterfaceType getInterfaceType(const std::string& type)
{
    if (type == "Dialer") return InterfaceType::DIALER;
    else if (type == "Ethernet") return InterfaceType::ETHERNET;
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
        case InterfaceType::DIALER:
            return "Dialer";
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
