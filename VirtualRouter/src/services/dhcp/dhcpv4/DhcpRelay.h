// DhcpRelay.h

#if 0
#ifndef DHCP_RELAY_H
#define DHCP_RELAY_H

#include <ByteString.hpp>
#include <PacketStructure.h>

namespace interface { class Interface; }

namespace services
{

// Forward declarations
class DhcpRelayTest;

namespace protocol
{
    /**
     * @brief Represents a dhcp relay agent
     */
    class DhcpRelay
    {
    public:
        friend class DhcpRelayTest;

        /**
         * @brief Constructor for DHCP relay.
         * @param interface Reference to the associated interface.
         */
        explicit DhcpRelay(interface::Interface* interface);

        /**
         * @brief Destructor for DHCP Relay.
         */
        ~DhcpRelay();

        /**
         * @brief Adds a helper address for forwarding DHCP packets.
         *
         * @param helperAddress The IP address of the DHCP server.
         */
        void addHelperAddress(const ByteString& helperAddress);

        /**
         * @brief Removes a helper address from an interface
         *
         * @param helperAddress The IP address of the DHCP server.
         */
        void removeHelperAddress(const ByteString& helperAddress);

        /**
         * @brief Handles an incoming DHCP packet from a client.
         * 
         * @param packet Thre received DHCP packet.
         */
        void handleClientPacket(PacketInfo& packet);

        /**
         * @brief Handles a response from the DHCP server.
         * 
         * @param packet The received DHCP packet.
         */
        void handleServerResponse(PacketInfo& packet);

    private:
        interface::Interface* associatedInterface; ///< Reference to the associated interface.
        std::vector<ByteString> helperAddresses; ///< List of helper addresses for this relay.
        std::mutex relayMutex; ///< Mutex for synchronizing access to helper addresses.

        /**
         * @brief Modifies the GIADDR field in the DHCP packet
         *
         * @param packet The DHCP packet to modify.
         */
        void modifyGiaddr(PacketInfo& packet);

        /**
         * @brief Forwards a DHCP packet to the ocnfigured helper address.
         *
         * @param packet The DHCP packet to forward.
         */
        void forwardToHelper(PacketInfo& packet);

        /**
         * @brief Forwards a DHCP packet back to the client.
         * @param packet The DHCP packet to forward.
         */
        void forwardToClient(PacketInfo& packet);

        /**
         * @brief Extracts the source or destination address from a packet.
         *
         * @param packet The DHCP packet.
         * @return The extracted IP address
         */
        ByteString extractAddress(PacketInfo& packet) const;
    };

}

#endif // DHCP_RELAY_H

} // namespace services

#endif

