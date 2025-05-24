// Dhcpv6Server.h

#ifndef DHCPV6_SERVER_H
#define DHCPV6_SERVER_H

#include <ByteString.hpp>
#include <DhcpInfo.hpp>
#include <PacketStructure.h>
#include <LeaseManager.h>
#include <IPPool.h>
#include <PrefixLeaseManager.h>
#include <PrefixPool.h>
#include <DhcpServerBase.h>
#include <atomic>
#include <unordered_map>

// Forward declarations
class NetworkConfigs;
class Dhcpv6ServerTest;
enum class InterfaceType;

namespace Protocol 
{

    namespace Dhcpv6
    {
        /**
         * @brief Contains dhcpv6 configuration parameters
         */
        struct Configs
        {
            std::atomic<uint8_t> recTimeout = 2; ///< Initial Reconfigure timeout before retrying (used by server if initiating Reconfigure).
            std::atomic<uint8_t> recMaxRc = 8; ///< Max Reconfigure attempts sent by server.
            std::atomic<uint8_t> hopCountLimit = 8; ///< Max hop count allowed in Relay-forward messages (server should discard beyond this).
            std::atomic<uint32_t> irtDefault = 86400; ///< Default Information Refresh Time (sent to clients in Reply to Info-Request).
            std::atomic<uint16_t> offerTimeout = 60; ///< How long to hold an offered lease before it's reclaimed (e.g., after ADVERTISE).
            std::atomic<uint16_t> clientRequestTimeout = 30; ///< Max time to wait for client to send REQUEST after ADVERTISE.
            std::atomic<uint16_t> confirmTimeout = 10; ///< Max time to wait for client to respond to Reconfigure with Confirm/Reply.
            std::atomic<uint16_t> releaseHoldTime = 5; ///< Optional short hold time after RELEASE before making IP available again.
            std::atomic<uint16_t> declineHoldTime = 10; ///< Hold time for declined IPs before they can be reallocated.

            std::atomic<double> t1Percentage = 0.5; ///< Initial T1 percentage for calcualting renewal times.
            std::atomic<double> t2Percentage = 0.8; ///< Initial T2 percentage for calcualting rebinding times.

            std::atomic<bool> allowUnicast{false};     // Whether to allow unicast messages
            std::atomic<bool> requireReconfigureAccept{true}; // Require reconfigure-accept
        };


        /**
         * @struct AuthConfig
         *
         * This object holds Authentication configurations for DHCPv6
         */
        struct AuthConfig
        {
            /**
             * @struct key
             * @brief Authentication key
             */
            struct Key
            {
                uint64_t keyId;
                ByteString secret;
                std::chrono::steady_clock::time_point created;
                std::chrono::seconds lifetime;

                bool isExpired() const 
                {
                    return std::chrono::steady_clock::now() - created > lifetime;
                }
            };

            std::vector<Key> delayedKeys; // All configured keys
            std::vector<Key> rkapKeys;
            std::map<ByteString, uint64_t> clientToKey; // Duid -> keyID
            std::map<ByteString, uint64_t> replayCounter; // Per-client DUID -> replay counter
            std::atomic<uint64_t> globalCounter = 1; // Used when sending replies

            enum class AuthProtocol { DELAYED = 2, RKAP = 3 };
            enum class AuthAlgorithm { HMACMD5 = 1, HMACSHA1 = 2 };
            enum class AuthRDM { MONO = 0, TIMESTAMP = 1, };
            
            AuthProtocol protocol = AuthProtocol::DELAYED;
            AuthAlgorithm algorithm = AuthAlgorithm::HMACMD5;
            AuthRDM rdm = AuthRDM::MONO;
        };

        /**
         * @struct ReconfigureState
         *
         * This object holds reconfiguration states for certain clients.
         */
        struct ReconfigureState
        {
            ByteString duid;
            Interface* iface;
            int attempts = 0;
            uint32_t timerID = 0;
            ByteString transactionID;
            ByteString localAddress;
        };

        /**
         * @enum ReconfigReason
         *
         * This holds resonds for the reconfiguration.
         */
        enum class ReconfigReason
        {
            RENEW = 5,
            REBIND = 6,
            INFORMATION = 11
        };

        /**
         * @brief Represents a IA_NA identifier block
         */
        struct IANABlock
        {
            ByteString iaid;
            uint32_t t1, t2;
            std::vector<ByteString> addresses;
            Dhcp::DhcpNetwork* network;
        };

        /**
         * @brief Represents a IA_PD identifier block
         */
        struct IAPDBlock 
        {
            ByteString iaid;
            uint32_t t1, t2;
            std::vector<std::pair<ByteString, uint8_t>> prefixes;
            Dhcp::DhcpNetwork* network;
        };

        /**
         * @struct RelayInfo
         * @brief holds information on a relay hop.
         */
        struct RelayClient
        {
            std::vector<Dhcpv6RelayHeader> relayChain; // Full relay chain
            Interface* iface; // Interface
            std::chrono::steady_clock::time_point lastSeen;
        };
    }

    /**
     * @brief Represents a DHCPv6 server.
     *
     * Handles DHCPv6 message processing (SOLICIT, REQUEST, RELEASE, DECLINE),
     * IPv6 address allocation, and lease management.
     */
    class Dhcpv6Server : public DhcpServerBase
    {
    public:
        friend class ::Dhcpv6ServerTest;
        /**
         * @brief Constructs a DHCPv6 server.
         */
        Dhcpv6Server(Global& global);

        /**
         * @brief Destructor
         */
        virtual ~Dhcpv6Server();

        void removeInterface(Interface* iface);

        /**
         * @brief Starts the DHCPv6 server.
         *
         * Begins processing DHCPv6 packets and managing leases.
         */
        virtual void startServer() override;

        /**
         * @brief Stops the DHCPv6 server.
         *
         * Terminates packet processing and cleans up resources.
         */
        virtual void stopServer() override;

        /**
         * @brief Process an incoming DHCPv6 packet.
         *
         * @param packet The received DHCPv6 packet.
         * @param iface Interface that the header was received on.
         * @param multicast Indicates if the message was sent with multicast.
         * @param localAddress The local-link address for the client.
         */
        void handleDhcpPacket(const PacketInfo& packet, Interface* iface, bool multicast, const ByteString& localAddress);

        Dhcpv6::Configs dhcpConfigs;

    private:

        ByteString dhcpUniqueIdentifier; ///< Unique Identifier for DHCP server.

        std::mutex trackingMutex;
        std::unordered_map<ByteString, std::chrono::steady_clock::time_point> declineTimestamps;
        std::unordered_map<ByteString, std::pair<ByteString, Interface*>> clientReconfAccept;
        std::unordered_map<ByteString, ByteString> recentLeases;
        std::unordered_map<ByteString, uint16_t> clientElapsedTime;
        std::unordered_map<ByteString, uint16_t> clientStatusCodes;
        std::unordered_map<ByteString, Dhcpv6::RelayClient> relayClients;

        std::atomic<bool> rkapAuthenticationEnabled = false;
        std::atomic<bool> delayedAuthenticationEnabled = false;
        Dhcpv6::AuthConfig authConfig;
        std::mutex authMutex;

        /**
         * @brief Main loop for DHCPv6 server operations.
         */
        virtual void dhcpHandler() override;

        // PROCESSORS--------------------------------------------

        /**
         * @brief Processes a DHCPv6 SOLICIT message.
         *
         * A client sends a Solicit message to locate servers.
         *
         * @param dhcpHeader The DHCPv6 header from the packet.
         * @param iface Interface the packet was received on.
         * @param multicast Indicates if this was sent via multicast.
         * @param localAddress Local Address of the client.
         * @return Will return the packet if no local address is given.
         */
        std::optional<ByteString> processSolicit(const Dhcpv6Header& dhcpHeader, Interface* iface, bool multicast, const ByteString* localAddress);

        /**
         * @brief Process a DHCPv6 REQUEST message.
         *
         * A client sends a Request message to request configuration parameters,
         * including addresses and/or delegated prefixes from a specific server.
         *
         * @param dhcpHeader The DHCPv6 header from the packet.
         * @param iface Interface the packet was received on.
         * @param multicast Indicates if this was sent via multicast.
         * @param localAddress Local Address of the client.
         * @return Will return the packet if no local address is given.
         */
        std::optional<ByteString> processRequest(const Dhcpv6Header& dhcpHeader, Interface* iface, bool multicast, const ByteString* localAddress);

        /**
         * @brief Processes a DHCPv6 CONFIRM message.
         *
         * A client sends a Confirm message to any available server to 
         * determine whether it was assigned are still approprieate to
         * the link to which the client is connected.
         *
         * @param dhcpHeader The DHCPv6 header from the packet.
         * @param iface Interface the packet was received on.
         * @param multicast Indicates if this was sent via multicast.
         * @param localAddress Local Address of the client.
         * @return Will return the packet if no local address is given.
         */
        std::optional<ByteString> processConfirm(const Dhcpv6Header& dhcpHeader, Interface* iface, bool multicast, const ByteString* localAddress);

        /**
         * @brief Processes a DHCPv6 RENEW message.
         *
         * A client sends a renew message to the server that originally
         * provided the client's leases and configuration parameters to
         * extend the lifetimes on the leases assigned to the client and
         * to update other configuration parameters.
         * 
         * @param dhcpHeader The DHCPv6 header from the packet.
         * @param iface Interface the packet was received on.
         * @param multicast Indicates if this was sent via multicast.
         * @param localAddress Local Address of the client.
         * @return Will return the packet if no local address is given.
         */
        std::optional<ByteString> processRenew(const Dhcpv6Header& dhcpHeader, Interface* iface, bool multicast, const ByteString* localAddress);
        
        /**
         * @brief Processes a DHCPv6 REBIND message.
         *
         * A client sends a Rebind message to any available server to extend
         * the lifetimes on the leases assigned to the client and to update
         * other configuration parameters; this message is sent if the server
         * did not respond the the clients original Renew message.
         *
         * @param dhcpHeader The DHCPv6 
         * @param iface Interface the packet was received on.
         * @param multicast Indicates if this was sent via multicast.
         * @param localAddress Local Address of the client.
         * @return Will return the packet if no local address is given.
         */
        std::optional<ByteString> processRebind(const Dhcpv6Header& dhcpHeader, Interface* iface, bool multicast, const ByteString* localAddress);

        /**
         * @brief Processes a DHCPv6 RELEASE message.
         *
         * A client sends a Release message to the server that assigned leases 
         * to the client to indicate that the client will no longer use one or more
         * of the assigned leases.
         *
         * @param dhcpHeader The DHCPv6 header from the packet.
         * @param iface Interface the packet was received on.
         * @param multicast Indicates if this was sent via multicast.
         * @param localAddress Local Address of the client.
         * @return Will return the packet if no local address is given.
         */
        std::optional<ByteString> processRelease(const Dhcpv6Header& dhcpHeader, Interface* iface, bool multicast, const ByteString* localAddress);

        /**
         * @brief Processes a DHCPv6 DECLINE message.
         *
         * A client sends a Decline message to a server to indicate that the client
         * has determines that one or more addresses assigned by the server are already
         * in use on the link to which the client is connected.
         *
         * @param dhcpHeader The DHCPv6 header for the packet.
         * @param iface Interface the packet was received on.
         * @param multicast Indicates if this was sent via multicast.
         * @param localAddress Local Address of the client.
         * @return Will return the packet if no local address is given.
         */
        std::optional<ByteString> processDecline(const Dhcpv6Header& dhcpHeader, Interface* iface, bool multicast, const ByteString* localAddress);

        /**
         * @brief Processes a DHCPv6 INFORM message.
         *
         * A client sends a Information-request message to a server to request configuration
         * parameters without the assignment of my leases to the client.
         *
         * @param dhcpHeader The DHCPv6 header for the packet.
         * @param iface Interface the packet was received on.
         * @param localAddress Local Address of the client.
         * @return Will return the packet if no local address is given.
         */
        std::optional<ByteString> processInformationRequest(const Dhcpv6Header& header, Interface* iface, bool multicast, const ByteString* localAddress);

        /**
         * @brief Process a DHCPv6 RELAY-FORW
         *
         * A relay agent sends a Relay-forward message to relay messages to servers,
         * either directly or through another relay agent.
         *
         * @param relay DHCPv6 relay header.
         * @param iface Interface the packet was received on.
         */
        void processRelayForward(const Dhcpv6RelayHeader& relay, Interface* iface);
        
        // Enhanced relay support
        std::optional<Dhcpv6Header> processRelayChain(const Dhcpv6RelayHeader& relay, std::vector<Dhcpv6RelayHeader>& chain);

        /**
         * @brief Finds and processes a DHCPv6 reconfig-accept option
         *
         * @param header DHCPv6 header.
         * @param bool Indicates if this is a relay packet.
         * @param interface
         * @return Will return the packet if relay is enabled.
         */
        void processReconfigAccept(const Dhcpv6Header& header, const ByteString& localAddress, Interface*& iface);

        // RECONFIGURE-------------------------------------

        /**
         * @brief Sends a DHCPv6 RECONF message.
         *
         * A server sends a reconfigure message to all client
         */
        void sendReconfigure(const ByteString& duid, Dhcpv6::ReconfigReason reason, bool relay = false);

        // EXTRACTIONS-------------------------------------

        /**
         * @brief Extracts the client IA_NA data: IAID, T1, T2, and IAADDR.
         *
         * @param header DHCPv6 message header.
         * @param iface Interface the packet was received on.
         * @return True if IA_NA and IAADDR were found and parsed.
         */
        std::vector<Dhcpv6::IANABlock> extractIA_NA(const Dhcpv6Header& header, Interface* iface);

        /**
         * @brief Extracts IA_PD and IAPREFIX for prefix deligation support.
         *
         * @param header DHCPv6 message header.
         * @param iface Interface the packet was received on.
         * @return True if IA_NA and IAADDR were found and parsed.
         */
        std::vector<Dhcpv6::IAPDBlock> extractIA_PD(const Dhcpv6Header& header, Interface* iface);

        /**
         * @brief Extracts IA_TA and TEMP address for temporary address support.
         *
         * @param header DHCPv6 message header.
         * @param iface Interface the packet was received on.
         * @return True if IA_TA and IAADDR were found and parsed
         */
        std::vector<Dhcpv6::IANABlock> extractIA_TA(const Dhcpv6Header& header, Interface* iface);

        /**
         * @brief Extracts requested options from Option Request Option (ORO).
         *
         * @param header DHCPv6 header.
         * @return A list of option codes (2 bytes each) the client wants.
         */
        std::vector<ByteString> extractORO(const Dhcpv6Header& header);

        // BUILDING -------------------------------------------------

        /**
         * @brief Builds a standard DHCPv6 reply header with all configured options.
         *
         * @param type Message type to respond with.
         * @param transactionID transaction ID of the client message.
         * @param config Network config.
         * @param iaidBlocks Blocks of IAID information.
         * @param iapdBlocks Blocks of IAPD information.
         * @return A fully constructed DHCPv6 object.
         */
        Dhcpv6Header buildResponse(const ByteString& type, const ByteString& transactionID, const ByteString& duid, const std::vector<Dhcpv6::IANABlock>& ianaBlocks, const std::vector<Dhcpv6::IAPDBlock>& iapdBlocks, const std::vector<Dhcpv6::IANABlock>& iataBlocks);

        /**
         * @brief Constructs DHCPv6 options to send in response.
         *
         * @param config DHCP network configuration.
         * @param oroOptions Options requested by client (via ORO).
         * @return Vector of DHCPv6 options.
         */
        std::vector<Dhcpv6Header::Option> buildOptions(Dhcp::DhcpNetworkConfig* config, const std::vector<ByteString>& oroOptions);

        // UTILS---------------------------------------------------

        /**
         * @brief Finds the matching network configuration for a DHCPv6 header.
         *
         * @param dhcpHeader The DHCP header.
         * @return The network identifier if found, or an empty ByteString otherwise.
         */
        ByteString findMatchingNetwork(const ByteString& ip);

        /**
         * @brief Generates a unique identifier for the DHCP server to use.
         */
        ByteString generateUniqueIdentifier();

        /**
         * @brief Sends a DHCPv6 packet through the specified interface.
         * 
         * @param header The dhcpv6 header to send.
         * @param iface The interface to send it through.
         */
        void sendPacket(Dhcpv6Header& header, Interface* iface, const ByteString& destination);
        
        void sendRelayPacket(Dhcpv6RelayHeader& header, Interface* iface);

        /**
         * @brief validates server ID
         *
         * @param header Dhcp header received.
         * @return True if serverID is correct, otherwise false;
         */
        bool validateServerID(const Dhcpv6Header& header);

        /**
         * @brief Extracts the clients id
         *
         * @param header The clients header that was received.
         * @return The Clients DUID if found, otherwise empty.
         */
        ByteString extractDUID(const Dhcpv6Header& header);

        /**
         * @brief Configures authentication for DHCPv6.
         *
         * @param id ID used for tracking the secret key.
         * @param secret Secret key shared with clients for authentication.
         */
        void addDelayedAuthKey(uint64_t id, const ByteString& secret, std::chrono::seconds lifetime);
        void addRKAPAuthKey(const ByteString& secret, std::chrono::seconds lifetime);

        void cleanupExpiredKeys();

        /**
         * @brief Builds a Authentication Option for secure dhcp handling.
         */
        bool addAuthenticationOption(Dhcpv6Header& header, const ByteString& clientID);

        /**
         * @brief Validates the Authentication on a incoming dhcpv6 packet
         *
         * @param header The incoming DHCPv6 header.
         * @param duid The Client ID of the incoming packet.
         */
        bool validateAuthentication(const Dhcpv6Header& header, const ByteString& duid);

        /**
         * @brief Builds a status option to send back to the client indicating status of request.
         *
         * @param code The status code to place in the option.
         * @param message The message to be sent along with the status.
         */
        Dhcpv6Header::Option buildStatusOption(const ByteString& code, const std::string& message);
        
        // Enhanced timer management
        void scheduleTimers(const ByteString& duid, const ByteString& iaid, uint32_t t1, uint32_t t2);
        void handleTimerExpiry(const ByteString& duid, const ByteString& iaid, Dhcp::TimerType type);

        /**
         * @brief Validates Status Code option in responses
         * @param options Vector of DHCPv6 options
         * @return true if status code is valid
         */
        bool validateStatusCode(const std::vector<Dhcpv6Header::Option>& options);

        /**
         * @brief Implements Rapid Commit option handling
         * @param options Vector of DHCPv6 options
         * @return true if Rapid Commit is requested and supported
         */
        bool handleRapidCommit(const std::vector<Dhcpv6Header::Option>& options);

        /**
         * @brief Validates the relay message option
         * @param options Vector of DHCPv6 options
         * @return true if relay message is valid
         */
        bool validateRelayMessage(const std::vector<Dhcpv6Header::Option>& options);

        /**
         * @brief Implements preference option handling
         * @param config Network configuration
         * @return preference value to be sent to client
         */
        uint8_t getServerPreference(const Dhcp::DhcpNetworkConfig* config, const ByteString& duid);

        void buildReconfigPacket(const ByteString& clientID, const ByteString& leaseIp, const std::pair<ByteString, Interface*>& ifacePair, Dhcpv6::ReconfigReason reason, bool relay);

        bool trackElapsedTime(const Dhcpv6Header& header, const ByteString& duid);

        void addServerUnicast(Dhcpv6Header& header, const ByteString& leasedIp, Interface* iface);

        std::optional<Dhcpv6::AuthConfig::Key> getKey(uint64_t keyID);
        // Status code tracking
    };
}

#endif // DHCPV6_SERVER_H
