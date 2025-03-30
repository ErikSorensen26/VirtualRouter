//Dhcpv6Relay.h

#ifndef DHCPV6_RELAY_H
#define DHCPV6_RELAY_H

#include <ByteString.hpp>
#include <PacketStructure.h>

// Forward declarations
class Interface;

namespace Protocol
{
    /**
     * @brief Represents a DHCPv6 relay agent.
     *
     * Forwards DHCPv6 packet between clients and servers.
     */
    class DhcpRelayV6
    {
    public:
        /**
         * @brief Constructs a DHCPv6 relay agent.
         *
         * @param interface Pointer to the associated network interface.
         */
        explicit DhcpRelayV6(Interface* interface);

        /**
         * @brief Destructor.
         */
        ~DhcpRelayV6();

        /**
         * @brief Adds a helper address for DHCPv6 forwarding.
         *
         * @param helperAddress The IPv6 Address of a DHCPv6 server.
         */
        void addHelperAddress(const ByteString& helperAddress);

        /**
         * @brief Removes a helper address.
         *
         * @param helperAddress The IPv6 address to remove.
         */
        void removeHelperAddress(const ByteString& helperAddress);

        /**
         * @brief Processes an incoming DHCPv6 packet from a client
         *
         * @param packet The received DHCPv6 packet.
         */
        void handleClientPacket(PacketInfo& packet);
        
        /**
         * @brief Processes a DHCPv6 response from a server.
         *
         * @param packet The received DHCPv6 packet.
         */
        void handleServerResponse(PacketInfo& packet);

    private:
        Interface* associatedInterface;         ///< Associated network interface.
        std::vector<ByteString> helperAddress;  ///< DHCPv6 helper address.
        std::mutex relayMutex;                  ///< Mutex for synchronizing access.

        /**
         * @brief Modifies the Relay option in DHCPv6 packet.
         *
         * @param packet The DHCPv6 packet.
         */
        void modifyRelay(PacketInfo& packet);

        /**
         * @brief Forwards a DHCPv6 packet to a helper address.
         *
         * @param packet The packet to forward.
         */
        void forwardToHelper(PacketInfo& packet);

        /**
         * @brief Forwards a DHCPv6 packet to the client.
         *
         * @param packet The packet to forward.
         */
        ByteString extractAddress(PacketInfo& packet) const;
   };
}

#endif //DHCPV6_RELAY_H
