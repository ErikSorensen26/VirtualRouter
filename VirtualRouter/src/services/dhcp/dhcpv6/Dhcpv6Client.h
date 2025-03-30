// Dhcpv6Client.h

#ifndef DHCPV6_CLIENT_H
#define DHCPV6_CLIENT_H

#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

// Forward declarations
class Interface;

namespace Protocol
{
    /**
     * @brief Represents a DHCPv6 client.
     *
     * Handles sending DHCPv6 SOLICIT and REQUEST messages and processing responses.
     */
    class DhcpClientV6 {
    public:
        /**
         * @brief Constructs a DHCPv6 client.
         *
         * @param CurrentInterface Pointer to the network interface used for DHCPv6.
         */
        DhcpClientV6(Interface* CurrentInterface);

        /**
         * @brief Destructor.
         */
        ~DhcpClientV6();

        /**
         * @brief Sends a DHCPv6 SOLICIT message.
         */
        void sendDhcpSolicit();

        /**
         * @brief Sends a DHCPv6 REQUEST message.
         */
        void sendDhcpRequest();

        /**
         * @brief Processes incoming DHCPv6 responses.
         */
        void processDhcpResponse();

    private:
        Interface* currentInterface; ///< Associated network interface.
        std::mutex dhcpMutex;        ///< Mutex for DHCP operations.
        std::condition_variable cv;  ///< Condition variable for state changes.
        std::thread dhcpThread;      ///< Thread for handling DHCPv6 operations.
        std::atomic<bool> stopFlag;  ///< Flag to signal termination.
    };
}
#endif //DHCPV6_CLIENT_H
