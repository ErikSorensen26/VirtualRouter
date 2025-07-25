// DhcpServer.h

#ifndef DHCP_SERVER_H
#define DHCP_SERVER_H

#include <unordered_map>
#include <atomic>
#include <vector>
#include <mutex>
#include <IPv4LeaseManager.h>
#include <IPv4Pool.h>
#include <IPAddress.hpp>
#include <shared_mutex>
#include <DhcpTLVManager.hpp>
#include <set>

// Forward declarations
struct DhcpHeader;
class Global;
struct ClientID;
class Interface;
class TimeManager;

namespace Protocol
{
    namespace Dhcp
    {
        enum class Overload : uint8_t
        {
            NONE = 0,
            FILE = 128,
            SNAME = 64
        };

        struct SnoopingEntry
        {
            uint8_t mac[6];
            uint32_t ip;
            uint32_t interface;
            std::chrono::steady_clock::time_point expiration;
        };

        struct Configs
        {
            //TODO implement all of this
            std::shared_mutex configMutex;

            std::atomic<uint16_t> conflictResolution = 10; // minutes
            std::atomic<uint16_t> declineQuarintine = 3600;
            std::atomic<uint16_t> offerExpiration = 600;
            std::atomic<uint8_t> leaseExpirationOffset = 0;

            std::atomic<uint16_t> bindingCleanup = 3600;
            std::atomic<uint16_t> leasesPerInterface = 1;
            std::atomic<uint16_t> pingTimeout = 750; // milliseconds
            std::atomic<size_t> clientMaxSize = 576;

            std::unordered_map<std::string, uint16_t> databaseSaveInterval;
            std::unordered_map<std::string, std::pair<uint32_t, uint16_t>> writeDelay;
            std::vector<uint32_t> globalDnsServers;

            std::atomic<uint8_t> conflictInterval = 1;
            std::atomic<uint8_t> conflictRetry = 10;
            std::atomic<uint8_t> pingRetryCount = 2;
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
            std::atomic<bool> checkForConflict = false;
            std::atomic<bool> rapidCommit = false;
            std::atomic<bool> offerSearchDomain = false;

            struct DNS
            {
                std::atomic<bool> before = false;
                std::atomic<bool> both = false;
                std::atomic<bool> override = false;
            } updateDNS;

            struct Snooping
            {
                std::set<std::string> databases; // TODO later
                std::set<std::string> trustedCircuiteIDs;
                std::set<std::string> trustedRemoteIDs;
                std::unordered_map<uint16_t, std::set<uint16_t>> vlans;
                std::atomic<bool> informationOption = false; //TODO
                std::atomic<bool> allowUntrusted = false;
                std::atomic<bool> verifyMac = false;
                std::atomic<bool> verifyGiaddr = false;
                std::atomic<bool> verifyRelay = false;
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

        struct DhcpNetworkConfig
        {
            std::atomic<uint8_t> defaultSubnetPrefix;       ///< The default subnet Prefix for the DHCP pool.
            std::atomic<uint8_t> serverPreference = 255;    ///< Default server preference.
            std::atomic<uint32_t> leaseTime = 0;            ///< The default duration of a lease in seconds.
            std::atomic<double> t1Percentage = 0.5;         ///< Initial T1 percentage for calcualting renewal times.
            std::atomic<double> t2Percentage = 0.87;        ///< Initial T2 percentage for calcualting rebinding times.

            std::vector<uint32_t> dnsServers;           ///< A list of DNS servers provided with this network.
            std::vector<std::string> searchDomains;     ///< The domain name associated with this network.
            std::string domainName;

            Interface* interface = nullptr;             ///< Pointer to the interface managing this network.
            
            // Vendor
            std::string vendorClassID;
            std::string vendorSpecificData;

            // PXE
            std::optional<uint8_t> pxeDiscoveryControl;
            std::vector<uint32_t> pxeBootServers;

            // Boot
            std::string bootFileName;
            std::string tftpServerName;

            // Additional fields
            std::vector<uint32_t> ntpServers;           ///< Network Time Protocol (NTP) server for this network.
            std::vector<uint32_t> tftpServers;          ///< TFTP server address for PXE booting.
            std::vector<uint32_t> winsServers;          ///< A list of WINS (Windows Internet Name Service) servers.
            std::vector<uint32_t> staticRoutes;         ///< Static routes provided to the network clients.
            std::vector<uint32_t> helperAddresses;      ///< List of DHCP relay (helper) addresses.
            uint32_t broadcastAddress;                  ///< The broadcast address for this network.
            uint32_t arpTimeout;                        ///< ARP timeout value for this network.
            std::optional<bool> allowDynamicUpdates;    ///< Indicates whether dynamic updates (e.g., for DNS) are enabled.
            std::vector<std::string> allowedHostnames;  ///< A list of hostnames allowed to operate on this network.
            std::atomic<uint16_t> mtu;                  ///< Maximum Transmission Unit (MTU) for the network.

            // Metadata
            std::string description;                    ///< Description or label for this network configuration.
            std::atomic<bool> isPrivate;                ///< Flag indicating whether this network is private or public.
            std::atomic<bool> isEnabled;                ///< Flag indicating whether this network is currently active.
            
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
            std::atomic<uint8_t> subnetPrefix;  ///< The subnet prefix for the DHCP pool.
            uint32_t subnetMask;                ///< The subnet mask for the DHCP pool.
            uint32_t network;                 ///< The base address of the network. (e.g., "192.168.1.0").
            uint32_t defaultGateway;          ///< The default gateway address for clients in this network.

        };

        struct DhcpNetwork
        {
            DhcpNetwork(TimeManager& tmgr, Dhcp::Configs& configs)
            {
                pool = new IPv4Pool(tmgr);
                leaseManager = new IPv4LeaseManager(*pool, configs);
                pool->setLeaseManager(leaseManager);
            }

            ~DhcpNetwork()
            {
                delete pool;
                delete leaseManager;
            }

            IPv4Pool* pool = nullptr;
            IPv4LeaseManager* leaseManager = nullptr;
            DhcpNetworkConfig configs;
        };

        struct DhcpAuthState
        {
            std::string sharedKey;
            uint64_t lastReplayCounter = 0;
            bool enforced = false;
        };

        class DhcpAuthManager
        {
        public:
            void addClientKey(const ClientID& clientID, const std::string& key, bool enforced = true)
            {
                std::unique_lock<std::shared_mutex> lock(authMutex);
                authTable[clientID] = DhcpAuthState{key, 0, enforced};
            }

            void setDefaultKey(const std::string& key)
            {
                std::unique_lock<std::shared_mutex> lock(authMutex);
                defaultKey = key;
            }

            const DhcpAuthState* getClientState(const ClientID& clientID) const
            {
                std::shared_lock<std::shared_mutex> lock(authMutex);
                auto it = authTable.find(clientID);
                return it != authTable.end() ? &it->second : nullptr;
            }

            const std::string* getKeyForClient(const ClientID& clientID) const
            {
                std::shared_lock<std::shared_mutex> lock(authMutex);
                auto it = authTable.find(clientID);
                if (it != authTable.end())
                    return &it->second.sharedKey;

                return defaultKey.empty() ? nullptr : &defaultKey;
            }

            uint64_t& getReplayCounter(const ClientID& clientId)
            {
                std::unique_lock<std::shared_mutex> lock(authMutex);
                return authTable[clientId].lastReplayCounter;
            }

            bool shouldEnforce(const ClientID& clientID) const
            {
                std::shared_lock<std::shared_mutex> lock(authMutex);
                auto it = authTable.find(clientID);
                return it != authTable.end() ? it->second.enforced : false;
            }

        private:
            mutable std::shared_mutex authMutex;
            std::unordered_map<ClientID, DhcpAuthState> authTable;
            std::string defaultKey;
        };
    }


    /**
     * @brief Represents a fully functional DHCP server
     * Cabable of managing IP address leases, handling dhcp packets, and supporting relay agents.
     */
    class DhcpServer {
    public:
        DhcpServer(Global& global, TimeManager& timeManager);
        ~DhcpServer();

        void start();
        void stop();

        void handlePacket(const DhcpHeader& dhcp, const uint8_t* sourceMac, Interface& iface);

        Dhcp::Configs configs;

        std::mutex serverMutex;

        std::unordered_map<std::string, Dhcp::DhcpNetwork*> networks;
        Dhcp::DhcpNetwork* addPool(const std::string& poolName);

    private:

        Global& global;
        TimeManager& timeManager;
        Dhcp::DhcpAuthManager authManager;

        std::unordered_map<uint64_t, Dhcp::SnoopingEntry> snoopingTable;

        void removePool(std::string& poolName);
        bool removeConfig(IPPrefix& prefix);


        // === PACKET LOGIC ===
        void processDiscover(
            const DhcpHeader& dhcp,
            std::vector<TLV8Option>& options,
            ClientID& client,
            Interface& iface,
            size_t clientMaxSize,
            const TLV8Option* relayInfo
        );
        void processRequest(
            const DhcpHeader& dhcp,
            std::vector<TLV8Option>& options,
            ClientID& client,
            Interface& iface,
            size_t clientMaxSize,
            const TLV8Option* relayInfo
        );
        void processDecline(
            const DhcpHeader& dhcp,
            std::vector<TLV8Option>& options,
            ClientID& client,
            Interface& iface
        );
        void processRelease(
            const DhcpHeader& dhcp,
            std::vector<TLV8Option>& options,
            ClientID& client,
            Interface& iface
        );
        void processInform(
            const DhcpHeader& dhcp,
            std::vector<TLV8Option>& options,
            ClientID& client,
            Interface& iface,
            size_t clientMaxSize,
            const TLV8Option* relayInfo
        );
        void processLeaseQuery(
            const DhcpHeader& dhcp,
            std::vector<TLV8Option>& options,
            ClientID& client,
            Interface& iface,
            size_t clientMaxSize,
            const TLV8Option* relayInfo
        );

        // === PACKET SENDING ===
        void sendOffer(
            Interface& iface,
            const uint8_t* transID,
            const ClientID& client,
            const uint8_t* chaddr,
            const uint8_t* giaddr,
            uint32_t ip,
            const uint8_t* destination,
            uint8_t* requests,
            size_t reqiestsSize,
            size_t clientMaxSize,
            const TLV8Option* relayInfo
        );
        void sendAck(
            Interface& iface,
            const uint8_t* transID,
            const ClientID& client,
            const uint8_t* chaddr,
            const uint8_t* giaddr,
            uint32_t ip,
            const uint8_t* destination,
            uint8_t* requests,
            size_t requestsSize,
            bool isRC,
            size_t clientMaxSize,
            const TLV8Option* relayInfo
        );
        void sendNak(
            Interface& iface,
            const uint8_t* transID,
            const ClientID& client,
            const uint8_t* chaddr,
            const uint8_t* giaddr,
            size_t clientMaxSize,
            const TLV8Option* relayInfo
        );
        void sendInformReply(
            Interface& iface,
            const uint8_t* transID,
            const ClientID& client,
            const uint8_t* chaddr, 
            const uint8_t* giaddr,
            const uint8_t* destination,
            uint8_t* requests,
            size_t requestsSize,
            size_t clientMaxSize,
            const TLV8Option* relayInfo
        );
        void sendForceRenew(
            Interface& iface,
            const ClientID& client,
            const uint8_t* chaddr,
            uint32_t ciaddr,
            const uint8_t* destination,
            size_t clientMaxSize,
            const TLV8Option* relayInfo
        );
        void sendLeaseQueryReply(
            Interface& iface,
            const uint8_t* transID,
            const ClientID& client,
            uint32_t clientIP,
            const uint8_t* chaddr,
            const uint8_t* giaddr,
            const TLV8Option* relayInfo,
            const IPv4LeaseManager::Lease* lease,
            uint8_t prefixLen,
            uint8_t responseType,
            size_t clientMaxSize
        );

        void addRequestedOptions(Dhcp::DhcpTLVManager& tlv, Dhcp::DhcpNetwork& network, const uint8_t* requests, size_t requestsSize);
        void appendDnsServers(Dhcp::DhcpTLVManager& tlv, const Dhcp::DhcpNetworkConfig& network);
        void appendNtpServers(Dhcp::DhcpTLVManager& tlv, const Dhcp::DhcpNetworkConfig& network);
        void appendDomainSearchList(Dhcp::DhcpTLVManager& tlv, const std::vector<std::string>& domains);
        void appendVendorOptions(Dhcp::DhcpTLVManager& tlv, const ClientID& client, const Interface& iface, const Dhcp::DhcpNetworkConfig& configs);
        void appendBootOptions(Dhcp::DhcpTLVManager& tlv, const Dhcp::DhcpNetworkConfig& configs);
        void appendAuthOptions(TLV8BufferManager& tlv, const DhcpHeader& dhcp, const ClientID& clientID);

        bool validateAuthentication(const DhcpHeader& dhcp, const ClientID& clientID, const uint8_t* value);

        void snoopingAllowed(const DhcpHeader& dhcp, Interface& iface);

        // === TIMER-BASED TASKS ===
        void expireOffer(uint32_t ip);
        void cleanupBinding(uint32_t ip);
        void clearDecline(uint32_t ip);
        void clearForceRenew(uint32_t ip);

        // === ENFORCEMENT ===
        bool enforceTrust(const Interface& iface, const ClientID& client, const Dhcp::Configs& config);
        bool verifySnooping(const Interface& iface, const ClientID& client, const Dhcp::Configs& config);
        void applyDNSRules(std::vector<IPAddress>& dnsOut, const Dhcp::Configs::DNS& dnsCfg);
        void applyOptionRules(std::vector<uint8_t>& optOut, const Dhcp::Configs& config);

        Dhcp::DhcpNetwork* matchingNetwork(const Interface& iface, const DhcpHeader& dhcp) const;
    };
}

#endif // DHCP_SERVER_H
