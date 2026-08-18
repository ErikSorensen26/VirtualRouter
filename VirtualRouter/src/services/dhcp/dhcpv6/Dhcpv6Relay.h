/**
 * @file Dhcpv6Relay.h
 * @brief DHCPv6 relay agent: forwards client/server traffic through helper addresses.
 */

//Dhcpv6Relay.h

#ifndef DHCPV6_RELAY_H
#define DHCPV6_RELAY_H

#include <ByteString.hpp>
#include <PacketStructure.h>

namespace interface { class Interface; }

namespace services::dhcp
{

// Forward declarations

/**
 * @brief DHCPv6 relay agent.
 * @ingroup SERVICES_DHCP_V6
 *
 * Forwards DHCPv6 packets between clients and servers.
 */
class DhcpRelayV6
    {
    public:
        /**
         * @brief Constructs a DHCPv6 relay agent.
         *
         * @param interface Pointer to the associated network interface.
         */
        explicit DhcpRelayV6(interface::Interface* interface);

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
        interface::Interface* associatedInterface;         ///< Associated network interface.
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
         * @brief Extracts the client's IPv6 address from a DHCPv6 packet.
         *
         * @param packet The packet to inspect.
         * @return The extracted address.
         */
        ByteString extractAddress(PacketInfo& packet) const;
};

} // namespace services::dhcp

#endif //DHCPV6_RELAY_H

