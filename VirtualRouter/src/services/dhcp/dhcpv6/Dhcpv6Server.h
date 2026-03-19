// Dhcpv6Server.h

#ifndef DHCPV6_SERVER_H
#define DHCPV6_SERVER_H

#include "dhcp/DhcpInfo.hpp"
#include "PrefixLeaseManager.h"
#include "IPv6LeaseManager.h"
#include "Dhcpv6AuthManager.h"
#include "processing/PacketBuilder.hpp"

#include <atomic>
#include <unordered_map>
#include <set>

// Forward declarations
class NetworkConfigs;
class Dhcpv6ServerTest;
enum class InterfaceType : uint8_t;

using Duid = ClientID;

namespace Protocol 
{

    namespace Dhcpv6
    {
        struct DhcpNetwork;
        struct PrefixPoolConfig;

        struct InterfaceConfigs
        {
            std::unordered_map<Duid, IPv6Address> clientReconfAccept;
            std::atomic<bool> automatic = false;
            std::atomic<bool> rapidCommit = false;
            std::atomic<bool> requireReconfigureAccept = false; // Require reconfigure-accept
            std::atomic<bool> reconfigureAll = false;
            std::string pool;
            std::atomic<uint8_t> preferenceValue;
        };

        /**
         * @brief Contains dhcpv6 configuration parameters
         */
        struct Configs
        {
            std::mutex configsMutex;

            std::set<std::string> databases; // TODO later
            std::atomic<uint32_t> databaseTransferTimeout;
            std::atomic<uint32_t> databaseWriteDelay;

            std::atomic<bool> automatic = false;
            std::atomic<bool> rapidCommit = false;

            std::atomic<bool> addIANA = false;
            std::atomic<bool> addIAPD = false;

            std::atomic<bool> serverPing = false;;
            std::atomic<uint8_t> pingPackets;

            std::atomic<uint8_t> reconfigureTimeout = 100; ///< Initial Reconfigure timeout before retrying (used by server if initiating Reconfigure).
            std::atomic<bool> requireReconfigureAccept = false; // Require reconfigure-accept
            std::atomic<bool> reconfigureAll = false;

            std::atomic<uint8_t> hopCountLimit = 8; ///< Max hop count allowed in Relay-forward messages (server should discard beyond this).

            std::atomic<uint16_t> offerTimeout = 60; ///< How long to hold an offered lease before it's reclaimed (e.g., after ADVERTISE).
            std::atomic<uint16_t> clientRequestTimeout = 30; ///< Max time to wait for client to send REQUEST after ADVERTISE.
            std::atomic<uint16_t> confirmTimeout = 10; ///< Max time to wait for client to respond to Reconfigure with Confirm/Reply.
            std::atomic<uint16_t> expiredHoldTime = 0; ///< Optional short hold time after EXPIRE before making IP available again.
            std::atomic<uint16_t> declineHoldTime = 3600; ///< Hold time for declined IPs before they can be reallocated.

            std::atomic<double> t1Percentage = 0.5; ///< Initial T1 percentage for calcualting renewal times.
            std::atomic<double> t2Percentage = 0.8; ///< Initial T2 percentage for calcualting rebinding times.

            std::atomic<uint32_t> refreshTime = 86400; ///< Default Information Refresh Time (sent to clients in Reply to Info-Request).
            std::atomic<uint32_t> solMaxRt = 120;
            std::atomic<uint32_t> infMaxRt = 3600;

            std::atomic<bool> allowUnicast = false;     // Whether to allow unicast messages

            std::unordered_map<uint32_t, InterfaceConfigs*> interfaceConfigs;

            struct StaticConfigs
            {
                uint32_t preferred;
                uint32_t valid;
            };

            std::unordered_map<IALeaseKey, StaticConfigs> staticIANAConfigs;
            std::unordered_map<IAKey, std::pair<IPv6Address, DhcpNetwork*>> staticNAs;
            std::unordered_map<IAPrefixKey, StaticConfigs> staticIAPDConfigs;
            std::unordered_map<IAKey, std::pair<IAPrefixKey, PrefixPoolConfig*>> staticPDs;
        };

        struct AAAServer
        {
            uint32_t* validLifetime = nullptr;
            uint32_t* preferedLifetime = nullptr;
            std::chrono::steady_clock::time_point* expiry = nullptr;

            ~AAAServer()
            {
                if (validLifetime) delete validLifetime;
                if (preferedLifetime) delete validLifetime;
                if (expiry) delete expiry;
            }
        };

        struct DhcpNetworkConfig
        {
            DhcpNetworkConfig(Configs& cfgs)
                : configs(cfgs) {}

            uint32_t interfaceKey;

            std::unordered_map<std::string, AAAServer> prefixAAAServers;

            std::string accountingList;

            std::vector<IPv6Address> dnsServers;           ///< A list of DNS servers provided with this network.
            std::vector<std::string> domainSearch;
            std::vector<IPv6Address> ntpServers;           ///< A list of DNS servers provided with this network.
            std::vector<IPv6Address> nisServers;
            std::vector<std::string> nisDomainName;
            std::vector<IPv6Address> nispServers;
            std::vector<std::string> nispDomainName;
            std::vector<IPv6Address> sipServers;
            std::vector<std::string> sipDomainname;
            std::vector<IPv6Address> sntpServers;

            std::string fqdn;

            // Commands to add
            std::atomic<bool> reconfigureAll = false;
            
            std::atomic<bool> importDNS = false;
            std::atomic<bool> importDomainName = false;
            std::atomic<bool> importRefreshInfo = false;
            std::atomic<bool> importNISAddress = false;
            std::atomic<bool> importNISDomainName = false;
            std::atomic<bool> importNISPAddress = false;
            std::atomic<bool> importNISPDomainName = false;
            std::atomic<bool> importSIPAddress = false;
            std::atomic<bool> importSIPDomainName = false;
            std::atomic<bool> importSNTPAddress = false;

            std::atomic<uint32_t> vendorEnterpriseId;
            std::string vrf = "default";

            std::atomic<uint32_t> preferredLifetime = 86400;
            std::atomic<uint32_t> validLifetime = 172800;

            std::atomic<uint32_t> refreshTime;
            std::atomic<bool> isRefreshTime = false;
            std::atomic<uint32_t> solMaxRt;
            std::atomic<bool> isSolMaxRt = false;
            std::atomic<uint32_t> infMaxRt;
            std::atomic<bool> isInfMaxRt = false;

            mutable std::shared_mutex configMutex;
            // Methods (optional, if you want to add functions)
            bool updateNetwork(IPPrefix& prefix, uint32_t& gateway);
            IPPrefix getNetworkID() const;
            uint8_t* getNetwork(uint8_t* out) const;
            uint32_t getNetwork() const;
            uint8_t* getGateway(uint8_t* out) const;
            uint32_t getGateway() const;
            uint8_t getPrefixLen() const;
            uint8_t* getSubnetMask(uint8_t* out);
            uint32_t getSubnetMask();

            std::string hostname;
        private:
            std::atomic<uint8_t> linkPrefixLength;
            std::atomic<IPv6Address> linkAddress;

            Configs& configs;
        };

        struct PrefixPoolConfig
        {
            enum class DelegationMode {
                Ignore,   // Always use delegationLength
                Prefer,   // Try requested length, fall back to delegationLength
                Exact,    // Only assign if requested length is available
                Minimum   // Assign smallest prefix >= requested length
            };

            PrefixPoolConfig(const IPv6Prefix& base, TimeManager& tm, Dhcpv6::Configs& cfgs, size_t deligationLength = 56)
                : pool(new PrefixPool(base, tm, deligationLength)), leaseManager(new PrefixLeaseManager(*pool, tm, cfgs)) {}

            PrefixPool* pool = nullptr;
            PrefixLeaseManager* leaseManager = nullptr;

            std::atomic<DelegationMode> mode = DelegationMode::Ignore;
            std::atomic<uint32_t> validLifetime = 7200;
            std::atomic<uint32_t> preferedLifetime = 3600;
            std::chrono::steady_clock::time_point expiry;

            ~PrefixPoolConfig()
            {
                if (pool) delete pool;
                if (leaseManager) delete leaseManager;
            }
        };

        struct CachedPrefixPool
        {
            ClientID& clientID;
            PrefixPoolConfig& pool;
            uint32_t validLifetime;
            uint32_t preferredLifetime;
        };

        struct DhcpNetwork
        {
            DhcpNetwork(TimeManager& tmgr, Dhcpv6::Configs& configs)
                : configs(configs)
            {
                pool = new IPv6Pool(tmgr);
                leaseManager = new IPv6LeaseManager(*pool, configs);
                pool->setLeaseManager(leaseManager);
            }

            ~DhcpNetwork()
            {
                delete pool;
                delete leaseManager;
            }

            IPv6Pool* pool = nullptr;
            IPv6LeaseManager* leaseManager = nullptr;
            std::vector<PrefixPoolConfig*> prefixPools;
            DhcpNetworkConfig configs;
        };

        struct CachedDhcpNetwork
        {
            ClientID& clientID;
            DhcpNetwork& pool;
            uint32_t validLifetime;
            uint32_t preferredLifetime;
        };

        /**
         * @brief Represents a IA_NA identifier block
         */
        struct IANABlock
        {
            const uint8_t* iaid = nullptr;
            Dhcpv6StatusMessage status;
            struct IANAEntry
            {
                IPv6Address address;
                Dhcpv6StatusMessage status;
                uint32_t staticPreferred;
                uint32_t staticValid;
            };
            std::vector<IANAEntry> addresses;
        };

        /**
         * @brief Represents a IA_TA identifier block
         */
        struct IATABlock
        {
            const uint8_t* iaid = nullptr;
            Dhcpv6StatusMessage status = { Dhcpv6StatusCode::None };
            struct IATAEntry
            {
                IPv6Address address;
                Dhcpv6StatusMessage status;
            };
            std::vector<IATAEntry> addresses;
        };

        /**
         * @brief Represents a IA_PD identifier block
         */
        struct IAPDBlock 
        {
            const uint8_t* iaid;
            Dhcpv6StatusMessage status = { Dhcpv6StatusCode::None };
            struct IAPDEntry
            {
                IPv6Prefix prefix;
                Dhcpv6StatusMessage status;
                PrefixPoolConfig* pool = nullptr;
                uint32_t staticPreferred;
                uint32_t staticValid;
            };
            std::vector<IAPDEntry> prefixes;
        };

        struct Dhcpv6IAOptions
        {
            DhcpNetwork* network = nullptr;
            std::vector<Dhcpv6::IANABlock> ianaBlocks;
            std::vector<Dhcpv6::IATABlock> iataBlocks;
            std::vector<Dhcpv6::IAPDBlock> iapdBlocks;

            bool empty() { return ianaBlocks.empty() || iataBlocks.empty() || iapdBlocks.empty(); };
        };

        struct Dhcpv6PacketBuild
        {
            PacketBuilder builder;
            Dhcpv6Header dhcp;
            size_t maxSize;
        };

        struct Dhcpv6PacketSend
        {
            Interface& iface;
            ClientID clientID;
            const uint8_t* oro = nullptr;
            size_t oroSize = 0;
            bool multicast = false;
            bool relay = false;
            const uint8_t* clientAddress = nullptr;
            Dhcpv6PacketBuild& build;
        };

        struct Dhcpv6PacketReceive
        {
            Dhcpv6Header& dhcpHeader;
            std::vector<TLV16Option> options;
            Dhcpv6PacketSend& send;
        };

        struct Dhcpv6SendType
        {
            uint8_t type;
            bool property;
        };

        /**
         * @enum ReconfigReason
         *
         * This holds resonds for the reconfiguration.
         */
        enum class ReconfigReason : uint8_t
        {
            RENEW = 5,
            INFORMATION = 11
        };

        struct ReconfigureAccepts
        {
            IPv6Address clientAddress;
            uint32_t interfaceKey;
        };

        /**
         * @struct ReconfigureState
         *
         * This object holds reconfiguration states for certain clients.
         */
        struct ReconfigureState
        {
            ReconfigReason reason;
            __uint128_t secret;
            uint32_t interfaceKey;
            uint32_t timerID = 0;
            uint32_t transactionID;
        };

        struct RelayLink
        {
            Dhcpv6RelayHeader relay;
            std::vector<TLV16Option> options;
        };
    }

    /**
     * @brief Represents a DHCPv6 server.
     *
     * Handles DHCPv6 message processing (SOLICIT, REQUEST, RELEASE, DECLINE),
     * IPv6 address allocation, and lease management.
     */
    class Dhcpv6Server
    {
    public:
        friend class ::Dhcpv6ServerTest;

        Dhcpv6Server(Global& global, TimeManager& timeManager);
        ~Dhcpv6Server();

        void handlePacket(Dhcpv6Header& dhcp, Interface& iface, bool multicast, const uint8_t* clientIp);
        bool handleDhcpPacket(Dhcpv6::Dhcpv6PacketReceive& receive, IPv6Address networkAddress);

        void expireClient(const ClientID& clientID);

        Dhcpv6::Configs configs;

    private:

        Global& global;
        TimeManager& timeManager;
        Dhcpv6::AuthManager authManager;

        std::unordered_map<std::string, Dhcpv6::DhcpNetwork*> stringToPool;
        std::unordered_map<IPv6Prefix, Dhcpv6::DhcpNetwork*> prefixToPool;

        Duid dhcpUniqueIdentifier; ///< Unique Identifier for DHCP server.

        std::mutex serverMutex;

        std::unordered_map<ClientID, Dhcpv6::ReconfigureAccepts> reconfigAccepts;
        std::unordered_map<ClientID, Dhcpv6::ReconfigureState> activeReconfigs;

        // Process Functions

        std::optional<Dhcpv6::Dhcpv6SendType> processSolicit(Dhcpv6::Dhcpv6PacketReceive& packet, Dhcpv6::Dhcpv6IAOptions& ia);

        std::optional<Dhcpv6::Dhcpv6SendType> processRequest(Dhcpv6::Dhcpv6PacketReceive& packet, Dhcpv6::Dhcpv6IAOptions& ia, Duid& serverID);

        std::optional<Dhcpv6::Dhcpv6SendType> processConfirm(Dhcpv6::Dhcpv6PacketReceive& packet, Dhcpv6::Dhcpv6IAOptions& ia);

        std::optional<Dhcpv6::Dhcpv6SendType> processRenew(Dhcpv6::Dhcpv6PacketReceive& packet, Dhcpv6::Dhcpv6IAOptions& ia, Duid& serverID);
        
        std::optional<Dhcpv6::Dhcpv6SendType> processRebind(Dhcpv6::Dhcpv6PacketReceive& packet, Dhcpv6::Dhcpv6IAOptions& ia);

        std::optional<Dhcpv6::Dhcpv6SendType> processRelease(Dhcpv6::Dhcpv6PacketReceive& packet, Dhcpv6::Dhcpv6IAOptions& ia, Duid& serverID);

        std::optional<Dhcpv6::Dhcpv6SendType> processDecline(Dhcpv6::Dhcpv6PacketReceive& packet, Dhcpv6::Dhcpv6IAOptions& ia, Duid& serverID);

        std::optional<Dhcpv6::Dhcpv6SendType> processInformationRequest(Dhcpv6::Dhcpv6PacketReceive& packet);

        bool processRelayForward(const Dhcpv6RelayHeader& relay, Interface& iface, const uint8_t* sourceAddress, const uint8_t* relayIp);

        size_t processRelayChain(const Dhcpv6RelayHeader& relay, Dhcpv6Header& dhcp, std::vector<Dhcpv6::RelayLink>& chain);

        // Packet Creation

        bool sendAdvertise(Dhcpv6::Dhcpv6PacketSend& send, Dhcpv6::Dhcpv6IAOptions& ia, const uint8_t* transId);

        bool sendReply(Dhcpv6::Dhcpv6PacketSend& send, Dhcpv6::Dhcpv6IAOptions* ia, const uint8_t* transId, bool rapidCommit);

        bool sendConfirmReply(Dhcpv6::Dhcpv6PacketSend& send, Dhcpv6::Dhcpv6IAOptions& ia, const uint8_t* transId, bool success);

        bool sendReconfigure(Dhcpv6::Dhcpv6PacketSend& send, Dhcpv6::ReconfigReason reason);

        // Packet Builders

        bool buildAdvertise(Dhcpv6::Dhcpv6PacketBuild& build, Dhcpv6::Dhcpv6PacketSend& send, Dhcpv6::Dhcpv6IAOptions& ia, const uint8_t* transId);

        bool buildReply(Dhcpv6::Dhcpv6PacketBuild& build, Dhcpv6::Dhcpv6PacketSend& send, Dhcpv6::Dhcpv6IAOptions* ia, const uint8_t* transId, bool rapidCommit);

        bool buildConfirmReply(Dhcpv6::Dhcpv6PacketBuild& build, Dhcpv6::Dhcpv6PacketSend& send, Dhcpv6::Dhcpv6IAOptions& ia, const uint8_t* transId, bool success);

        bool buildReconfigure(Dhcpv6::Dhcpv6PacketBuild& build, Dhcpv6::Dhcpv6PacketSend& send, Dhcpv6::ReconfigReason reason);

        /**
         * @brief Extracts the client IA_NA data: IAID, T1, T2, and IAADDR.
         *
         * @param header DHCPv6 message header.
         * @param iface Interface the packet was received on.
         * @return dhcpv6 ia optiohs
         */
        std::optional<Dhcpv6::IANABlock> extractIA_NA(const TLV16Option& option, Dhcpv6::DhcpNetwork* network, Duid& clientID, bool isBinding);

        /**
         * @brief Extracts IA_TA and TEMP address for temporary address support.
         *
         * @param header DHCPv6 message header.
         * @param iface Interface the packet was received on.
         * @return True if IA_TA and IAADDR were found and parsed
         */
        std::optional<Dhcpv6::IATABlock> extractIA_TA(const TLV16Option& option, Dhcpv6::DhcpNetwork* network, bool isBinding);

        /**
         * @brief Extracts IA_PD and IAPREFIX for prefix deligation support.
         *
         * @param header DHCPv6 message header.
         * @param iface Interface the packet was received on.
         * @return True if IA_NA and IAADDR were found and parsed.
         */
        std::optional<Dhcpv6::IAPDBlock> extractIA_PD(const TLV16Option& option, Dhcpv6::DhcpNetwork* network, Duid& clientID, bool isBinding);

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
        void buildResponse(
            Dhcpv6Header& dhcp,
            ClientID& clientID,
            TLV16BufferManager& tlv,
            Dhcpv6::Dhcpv6IAOptions* ia,
            const uint8_t* oro,
            size_t oroSize
        );

        void handleNoPool(Dhcpv6::Dhcpv6IAOptions& ia);

        Dhcpv6::DhcpNetwork* matchAddressToPool(const IPv6Address& addr, uint32_t interfaceKey);

        Dhcpv6::PrefixPoolConfig* selectPrefixPool(const Dhcpv6::DhcpNetwork* network, const IPv6Prefix* requestedPrefix);

        bool addServerUnicast(TLV16BufferManager& tlv, IPv6Address leasedIp, Interface& iface);


        bool addStaticLease(Dhcpv6::IANABlock& block, const IAKey& key);
        bool addStaticPrefix(Dhcpv6::IAPDBlock& block, const IAKey& key);
        bool addStaticAdvertisedLease(Dhcpv6::IANABlock& block, const IAKey& key);
        bool addStaticAdvertisedPrefix(Dhcpv6::IAPDBlock& block, const IAKey& key);











#if 0
        /**
         * @brief Constructs DHCPv6 options to send in response.
         *
         * @param config DHCP network configuration.
         * @param oroOptions Options requested by client (via ORO).
         * @return Vector of DHCPv6 options.
         */
        void buildOptions(TLV16BufferManager& tlv, Dhcpv6::DhcpNetworkConfig* config, const std::vector<uint16_t>& oroOptions);

        // UTILS---------------------------------------------------

        /**
         * @brief Finds the matching network configuration for a DHCPv6 header.
         *
         * @param dhcpHeader The DHCP header.
         * @return The network identifier if found, or an empty ByteString otherwise.
         */
        IPPrefix findMatchingNetwork(const uint8_t* ip);

        /**
         * @brief Generates a unique identifier for the DHCP server to use.
         */
        Duid generateUniqueIdentifier();

        
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

        /**
         * @brief Validates the Authentication on a incoming dhcpv6 packet
         *
         * @param header The incoming DHCPv6 header.
         * @param duid The Client ID of the incoming packet.
         */

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
        uint8_t getServerPreference(const Dhcpv6::DhcpNetworkConfig* config, const ByteString& duid);

        void buildReconfigPacket(const ByteString& clientID, const ByteString& leaseIp, const std::pair<ByteString, Interface*>& ifacePair, Dhcpv6::ReconfigReason reason, bool relay);

        bool trackElapsedTime(const Dhcpv6Header& header, const ByteString& duid);


        std::optional<Dhcpv6::AuthConfig::Key> getKey(uint64_t keyID);
        // Status code tracking
#endif
    };
}

#endif // DHCPV6_SERVER_H
