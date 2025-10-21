// EigrpInterface.h

#ifndef EIGRP_INTERFACE_H
#define EIGRP_INTERFACE_H

#include <IPAddress.hpp>
#include <atomic>

class Internal_EigrpTest;
class Interface;
class InterfaceConfigs;
namespace EigrpConfigs
{
class InterfaceConfigs;
}

namespace Protocol
{
class Eigrp;

/**
 * @class EigrpInterface
 * @brief Represents an interface participating in the EIGRP process.
 *
 * The EigrpInterface class manages EIGRP operations specific to a network interface,
 * including sending and receiving EIGRP packets, maintaining neighbor relationships,
 * handling routing updates, and managing timers and retransmissions.
 */
class EigrpInterface
{
public:
    friend class ::Internal_EigrpTest;

    /**
     * @brief Constructs an EigrpInterface instance.
     *
     * Initializes the EigrpInterface with the provided EIGRP process and network interface.
     * Sets up necessary configurations and starts the Hello timer.
     * 
     * @param eigrpSystem Reference to the EIGRP process.
     * @param interface Shared pointer to the network interface.
     */
    EigrpInterface(Eigrp& eigrpSystem, EigrpConfigs::InterfaceConfigs* intConfigs, Interface* interface);

    /**
     * @brief Destructor for EigrpInterface.
     *
     * Cancels all active timers, stops the worker thread, and performs necessary cleanup
     * to gracefully terminate the EIGRP interface operations.
     */
    virtual ~EigrpInterface();

    /**
     * @brief Sets the interface to passive or active mode.
     *
     * Configures the interface's operational mode, determining whether it actively
     * sends and receives EIGRP packets or remains passive, only responding to received packets.
     *
     * @param passive True to set the interface to passive, false to make it active.
     */
    void setPassiveMode(bool passive);

    /**
     * @brief enables/disables multicast on the interface.
     */
    void setMulticast(bool state);

    /**
     * @brief Retrieves the multicast address based on the address family.
     *
     * Determines and returns the appropriate multicast address for EIGRP packet
     * transmission based on whether IPv4 or IPv6 is being used.
     *
     * @return ByteString representing the multicast address.
     */
    const uint8_t* multicastEnabled();

    // Get the ip address of the interaface
    inline IPAddress localAddress();

    EigrpConfigs::InterfaceConfigs* configs; ///< Configuration settings for the interface.

    Interface* currentInterface; ///< Pointer to the current network interface.
    InterfaceConfigs* currentInterfaceInfo; ///< Pointer to the current interface's IP information.
    uint32_t interfaceKey;
    std::atomic<bool> destroy{false}; ///< Destroy boolean for destruction of eigrp class.

    std::atomic<uint32_t> nextSequenceNumber = 1; ///< Next sequence number for packets.
};
}

#endif // EIGRP_INTERFACE_H
