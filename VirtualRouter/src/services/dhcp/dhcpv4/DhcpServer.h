// DhcpServer.h

#ifndef DHCP_SERVER_H
#define DHCP_SERVER_H

#include <DhcpServerBase.h>
#include <unordered_map>

// Forward declarations
class DhcpServerTest;
class IPPoolTest;
class Interface;
enum class InterfaceType;

namespace Protocol
{
    namespace Dhcp
    {
        struct Configs
        {
            //TODO implement all of this
            std::shared_mutex configMutex;

            std::atomic<uint16_t> bindingCleanup = 3600;
            std::atomic<uint16_t> conflictResolution = 10; // minutes
            std::atomic<uint16_t> leasesPerInterface = 1;
            std::atomic<uint16_t> pingTimeout = 750; // milliseconds

            std::unordered_map<std::string, uint16_t> databaseSaveInterval;
            std::unordered_map<std::string, std::pair<uint32_t, uint16_t>> writeDelay;
            std::vector<ByteString> globalDnsServers;

            std::atomic<uint16_t> declineQuarintine = 3600;
            std::atomic<uint8_t> conflictInterval = 1;
            std::atomic<uint8_t> conflictRetry = 10;
            std::atomic<uint8_t> pingRetryCount = 2;
            std::atomic<uint16_t> offerExpiration = 600;
            std::atomic<uint8_t> leaseExpirationOffset = 0;
            std::atomic<uint8_t> hartbeatInterval = 10;
            std::atomic<uint8_t> forceRenewInterval = 0;

            std::atomic<bool> logConflicts = false;
            std::atomic<bool> logAsciiClientID = false; //TODO
            std::atomic<bool> limitLeases = false;
            std::atomic<bool> limitBroadcastAddress = false; //TODO
            std::atomic<bool> remember = false; //TODO
            std::atomic<bool> addConnected = false; //TODO
            std::atomic<bool> addStatic = false; //TODO
            std::atomic<bool> smartRelay = false; //TODO
            std::atomic<bool> option55Override = false; //TODO
            std::atomic<bool> sipParameterNak = false; //TODO
            std::atomic<bool> tunnelUnicastParameter = false; //TODO

            struct DNS
            {
                std::atomic<bool> before = false;
                std::atomic<bool> both = false;
                std::atomic<bool> override = false;
            } updateDNS;

            struct Snooping
            {
                std::set<std::string> databases; // TODO later
                std::map<uint16_t, std::set<uint16_t>> vlans;
                std::atomic<bool> informationOption = false; //TODO
                std::atomic<bool> allowUntrusted = false;
                std::atomic<bool> verifyMac = false;
                std::atomic<bool> verifyGiaddr = false;
            } snooping;

            struct BOOTP
            {
                //TODO later
                std::atomic<bool> ignore = false;
                std::atomic<bool> relayIgnore = false;
                std::atomic<bool> validateRelay = false;
                std::atomic<bool> includeRelayInfo = false;
                std::atomic<bool> includeVPNrelayInfo = false;

                std::atomic<bool> drop = false;
                std::atomic<bool> encapsulate = false;
                std::atomic<bool> keep = false;
                std::atomic<bool> replace = false;

                std::atomic<bool> trustAll = false;
                std::atomic<bool> linkSelectOverride = false;
            } bootp;
        };

        struct SnoopingEntry
        {
            ByteString mac;
            ByteString ip;
            std::pair<InterfaceType, float> interface;
            std::chrono::steady_clock::time_point expiration;
        };
    }

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
        DhcpServer(Global& global);

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
        void handleDhcpPacket(const PacketInfo& packet, Interface* iface);

        Dhcp::Configs globalConfig;

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
        
        std::vector<DhcpHeader::Option> buildDnsAndIdentityOptions(const std::vector<DhcpHeader::Option>& options, const Dhcp::DhcpNetworkConfig* config, bool isAck);

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

        PacketInfo buildDhcpOffer(const DhcpHeader& dhcpHeader, const Dhcp::DhcpNetworkConfig* config, const ByteString& ipAddress);

        PacketInfo buildDhcpAck(const DhcpHeader& dhcpHeader, const Dhcp::DhcpNetworkConfig* config, const ByteString& ipAddress);

        /**
         * @brief Builds a DHCPACK packet to send back data requested by a clinet from a inform message.
         *
         * @param dhcpHeader The DHCP header of the request.
         * @param config The network configuration for the acknowledgment.
         * @param requestedOptions A vector of Options requested in ByteStrings.
         * @return A PacketInfo object representing the constructed DHCPACK.
         */
        PacketInfo buildDhcpAckForInform(const DhcpHeader& dhcpHeader, const Dhcp::DhcpNetworkConfig* config, const std::vector<ByteString>& requestedOptions);

        /**
         * @brief Builds all requested options from an inform request
         *
         * @param requestedOptions Vector holding all requested options.
         * @param config NetworkConfig object holding the information to fill in.
         * @return A vector of fully made and ready options.
         */
        std::vector<DhcpHeader::Option> buildRequestedOptions(const std::vector<ByteString>& requestedOptions, const Dhcp::DhcpNetworkConfig* config);

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
        ByteString allocateWithValidation(const ByteString& networkID, IPPool* pool, const DhcpHeader& dhcpHeader);
        bool isTrustedInterface(Interface* iface);
        void addSnoopingEntry(const DhcpHeader& header, const ByteString& ip, Interface* iface, uint32_t leaseTime);
        
        std::unordered_map<ByteString, Dhcp::SnoopingEntry> snoopingTable;
        std::mutex snoopingMutex;
    };
}

#endif // DHCP_SERVER_H
