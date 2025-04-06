// DhcpServer.h

#ifndef DHCP_SERVER_H
#define DHCP_SERVER_H

#include <DhcpServerBase.h>
#include <unordered_map>

// Forward declarations
class DhcpServerTest;
class IPPoolTest;
class Interface;

namespace Protocol
{



    /**
     * @brief Represents a fully functional DHCP server
     * Cabable of managing IP address leases, handling dhcp packets, and supporting relay agents.
     */
    class DhcpServer : public DhcpServerBase
    {
    public:
        friend class ::DhcpServerTest;
        friend class ::IPPoolTest;


        /**
         * @brief COnstructs a new instance of the DhcpServer class and initialized internal structures for lease and network management.
         */
        DhcpServer();

        /**
         * @brief Destructor that cleans up resources, stops the server, and releases any threads or mutexes in use.
         */
        virtual ~DhcpServer();

        /**
         * @brief Starts the DHCP server, enabling it to proces DHCP packets and manage leases for confugured networks.
         */
        virtual void startServer() override;

        /**
         * @brief Stops the DHCP server and ensures that any ongoing operations or threads are safely terminalted.
         */
        virtual void stopServer() override;

        /**
         * @brief Processes an incoming DHCP packet.
         *
         * determines its type, and preforms the appropriate actions based on the DHCP message.
         *
         * @param packet The received PacketInfo object containing the header and payload.
         * @param iface The interface the packet was received on.
         */
        void handleDhcpPacket(const PacketInfo& packet);

    private:
        
        /**
         * @brief The main handler thread for managing DHCP server.
         */
        virtual void dhcpHandler() override;

        /**
         * @brief Finds the matching network configuration for a DHCPv4 header.
         *
         * @param dhcpHeader The DHCP header.
         * @return The matching network identifier if found; othersize, an empty ByteString.
         */
        ByteString findMatchingNetwork(const DhcpHeader& dhcpheader);

        /**
         * @breif Processes a DHCPDISCOVER packet and generates a corrseponding DHCPOFFER if a suitable IP address can be allocated.
         *
         * @param dhcpHeader The DHCP header from the incoming packet.
         */
        void processDiscover(const DhcpHeader& dhcpHeader);

        /**
         * @brief Processes a DHCPREQUEST packet and either acknowledges the lease with a DHCPACK or rejects the request with a DHCPNAK.
         * 
         * @param dhcpHeader The DHCP header from the incoming packet.
         */
        void processRequest(const DhcpHeader& dhcpHeader);

        /**
         * @brief Processes a DHCPRELEASE packet to free up an IP address previously assigned to a client.
         *
         * @param dhcpHeader The DHCP header from the incoming packet
         */
        void processRelease(const DhcpHeader& dhcpHeader);

        /**
         * @brief Process a DHCPDECLINE packet releasing the current queued IP address.
         * 
         * @param dhcpHeader The DHCP header from the incoming packet.
         */
        void processDecline(const DhcpHeader& dhcpHeader);

        /**
         * @brief Process a DHCPINFORM packet sending requested information to the client.
         *
         * @param dhcpHeader The DHCP header from the incoming packet.
         */
        void processInform(const DhcpHeader& dhcpHeader);

        /**
         * @brief Gathers all of the requested options to reply to an inform message with.
         *
         * @param optons Vector containing all options received
         */
        std::vector<ByteString> getRequestedOptions(const std::vector<DhcpHeader::Option>& options);

        /**
         * @brief Builds a dhcp body with common fields.
         *
         * @param sourceIP The source or gateway of the packet.
         * @param destinationIP The destination IP address.
         * @param sourceMac The MAC address of the gateway.
         * @return PacketInfo configured DHCP body packet.
         */
        PacketInfo dhcpBody(const ByteString& sourceIP, const ByteString& destinationIP, const ByteString& sourceMac);

        /**
         * @brief Builds a DHCP header with common fields.
         *
         * @param messageType The DHCP message type.
         * @param clientIP The assigned client IP.
         * @param relayAgentIP The relay agent's IP, if applicable.
         * @param transID The Transit ID used by the client.
         * @return the constructed DHCP header.
         */
        DhcpHeader buildDhcpHeader(const ByteString& messageType, const ByteString& clientIP, const ByteString& relayAgentIP, const ByteString& transID);

        PacketInfo buildDhcpOffer(const DhcpHeader& dhcpHeader, const DhcpNetworkConfig* config, const ByteString& ipAddress);

        PacketInfo buildDhcpAck(const DhcpHeader& dhcpHeader, const DhcpNetworkConfig* config, const ByteString& ipAddress);

        /**
         * @brief Builds a DHCPACK packet to send back data requested by a clinet from a inform message.
         *
         * @param dhcpHeader The DHCP header of the request.
         * @param config The network configuration for the acknowledgment.
         * @param requestedOptions A vector of Options requested in ByteStrings.
         * @return A PacketInfo object representing the constructed DHCPACK.
         */
        PacketInfo buildDhcpAckForInform(const DhcpHeader& dhcpHeader, const DhcpNetworkConfig* config, const std::vector<ByteString>& requestedOptions);

        /**
         * @brief Builds all requested options from an inform request
         *
         * @param requestedOptions Vector holding all requested options.
         * @param config NetworkConfig object holding the information to fill in.
         * @return A vector of fully made and ready options.
         */
        std::vector<DhcpHeader::Option> buildRequestedOptions(const std::vector<ByteString>& requestedOptions, const DhcpNetworkConfig* config);

        /**
         * @brief Sends a DHCPNAK packet to the client to indicate that its lease request
         *        has been rejected.
         * 
         * @param dhcpHeader The DHCP header of the rejected request.
         * @param interface The interface to send the packet out of.
         */
        void sendNak(const DhcpHeader& dhcpHeader, Interface* interface);

        /**
         * @brief Extracts the value of a specific DHCP option from the provided list of options.
         * 
         * @param options The list of DHCP options in the header.
         * @param optionType The type of the option to retrieve.
         * @return The value of the option as a ByteString, or an empty string if not found.
         */
        ByteString getOption(const std::vector<DhcpHeader::Option>& options, const ByteString& optionType);

        /**
         * @brief Sends a constructed packet to the network interface for delivery to the client.
         * 
         * @param packet The PacketInfo object containing the data to be sent.
         */
        void sendPacket(PacketInfo& packet, Interface* interface);

        /**
         * @brief Generates a random transaction ID.
         *
         * @return A ByteString representing the transaction ID.
         */
        ByteString generateTransactionID();

        // RFC 2131 required timers

        /**
         * @brief Validates mandatory options in DHCPREQUEST messages
         * 
         * @param options Vector of DHCP options
         * @return true if all mandatory options are present and valid
         */
        bool validateMandatoryOptions(const std::vector<DhcpHeader::Option>& options);

        /**
         * @brief Handles BOOTP client requests for backward compatibility
         * 
         * @param packet The received PacketInfo object
         */
        void handleBootpRequest(const PacketInfo& packet);

        /**
         * @brief Implements authentication as per RFC 3118
         *
         * @param dhcpHeader The DHCP header to authenticate
         * @return true if authentication succeeds
         */
        bool authenticateMessage(const DhcpHeader& dhcpHeader);

        /**
         * @brief Processes DHCPFORCERENEW messages (RFC 3203)
         * @param dhcpHeader The DHCP header from the incoming packet
         */
        void processForceRenew(const DhcpHeader& dhcpHeader);

        // Authentication related members (RFC 3118)
        std::unordered_map<ByteString, uint32_t> replayCache;  // MAC -> last timestamp
        ByteString authenticationSecret;
    };
}

#endif // DHCP_SERVER_H
