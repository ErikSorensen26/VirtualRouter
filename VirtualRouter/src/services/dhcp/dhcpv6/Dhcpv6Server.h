/**
 * @file Dhcpv6Server.h
 * @brief DHCPv6 server: message processing, IPv6/prefix allocation, and lease management.
 */

// Dhcpv6Server.h

#ifndef DHCPV6_SERVER_H
#define DHCPV6_SERVER_H

#include "dhcp/DhcpInfo.hpp"
#include "PrefixLeaseManager.h"
#include "IPv6LeaseManager.h"
#include "Dhcpv6AuthManager.h"
#include "processing/PacketBuilder.hpp"
#include "Global.h"

#include <atomic>
#include <unordered_map>
#include <set>

namespace interface { enum class InterfaceType : uint8_t; }
class Dhcpv6ServerTest;

namespace services::dhcp
{

using Duid = ClientID;


struct DhcpNetwork;
struct PrefixPoolConfig;

struct InterfaceConfigs
{
    std::unordered_map<Duid, types::IPv6Address> clientReconfAccept;
    std::atomic<bool> automatic = false;
    std::atomic<bool> rapidCommit = false;
    std::atomic<bool> requireReconfigureAccept = false; // Require reconfigure-accept
    std::atomic<bool> reconfigureAll = false;
    std::string pool;
    std::atomic<uint8_t> preferenceValue;
};

/**
 * @brief Server-wide DHCPv6 configuration, shared by every network and interface the server serves.
 * @ingroup SERVICES_DHCP_V6
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

    std::atomic<bool> serverPing = false;
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
    std::unordered_map<IAKey, std::pair<types::IPv6Address, DhcpNetwork*>> staticNAs;
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

    std::vector<types::IPv6Address> dnsServers;           ///< A list of DNS servers provided with this network.
    std::vector<std::string> domainSearch;
    std::vector<types::IPv6Address> ntpServers;           ///< A list of DNS servers provided with this network.
    std::vector<types::IPv6Address> nisServers;
    std::vector<std::string> nisDomainName;
    std::vector<types::IPv6Address> nispServers;
    std::vector<std::string> nispDomainName;
    std::vector<types::IPv6Address> sipServers;
    std::vector<std::string> sipDomainname;
    std::vector<types::IPv6Address> sntpServers;

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
    std::string vrf = DEFAULT_VRF;

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
    bool updateNetwork(types::IPPrefix& prefix, uint32_t& gateway);
    types::IPPrefix getNetworkID() const;
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
    std::atomic<types::IPv6Address> linkAddress;

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

    PrefixPoolConfig(const types::IPv6Prefix& base, core::TimeManager& tm, Configs& cfgs, size_t deligationLength = 56)
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
    DhcpNetwork(core::TimeManager& tmgr, Configs& configs)
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
 * @ingroup SERVICES_DHCP_V6
 */
struct IANABlock
{
    const uint8_t* iaid = nullptr;
    Dhcpv6StatusMessage status;
    struct IANAEntry
    {
        types::IPv6Address address;
        Dhcpv6StatusMessage status;
        uint32_t staticPreferred;
        uint32_t staticValid;
    };
    std::vector<IANAEntry> addresses;
};

/**
 * @brief Represents a IA_TA identifier block
 * @ingroup SERVICES_DHCP_V6
 */
struct IATABlock
{
    const uint8_t* iaid = nullptr;
    Dhcpv6StatusMessage status = { Dhcpv6StatusCode::None };
    struct IATAEntry
    {
        types::IPv6Address address;
        Dhcpv6StatusMessage status;
    };
    std::vector<IATAEntry> addresses;
};

/**
 * @brief Represents a IA_PD identifier block
 * @ingroup SERVICES_DHCP_V6
 */
struct IAPDBlock 
{
    const uint8_t* iaid;
    Dhcpv6StatusMessage status = { Dhcpv6StatusCode::None };
    struct IAPDEntry
    {
        types::IPv6Prefix prefix;
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
    std::vector<IANABlock> ianaBlocks;
    std::vector<IATABlock> iataBlocks;
    std::vector<IAPDBlock> iapdBlocks;

    bool empty() { return ianaBlocks.empty() || iataBlocks.empty() || iapdBlocks.empty(); };
};

struct Dhcpv6PacketBuild
{
    processing::PacketBuilder builder;
    packet::Dhcpv6Header dhcp;
    size_t maxSize;
};

struct Dhcpv6PacketSend
{
    interface::Interface& iface;
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
    packet::Dhcpv6Header& dhcpHeader;
    std::vector<packet::TLV16Option> options;
    Dhcpv6PacketSend& send;
};

struct Dhcpv6SendType
{
    uint8_t type;
    bool property;
};

/**
 * @ingroup SERVICES_DHCP_V6
 * @enum ReconfigReason
 * @brief Reasons a server-initiated Reconfigure message can carry (RFC 8415 SS21.19).
 */
enum class ReconfigReason : uint8_t
{
    RENEW = 5,
    INFORMATION = 11
};

struct ReconfigureAccepts
{
    types::IPv6Address clientAddress;
    uint32_t interfaceKey;
};

/**
 * @struct ReconfigureState
 * @brief Per-client state tracking an in-flight Reconfigure exchange.
 * @ingroup SERVICES_DHCP_V6
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
    packet::Dhcpv6RelayHeader relay;
    std::vector<packet::TLV16Option> options;
};

/**
* @brief Owns every DHCPv6 network and address/prefix pool the router serves.
* @ingroup SERVICES_DHCP_V6
*
* Handles DHCPv6 message processing (SOLICIT, REQUEST, RELEASE, DECLINE),
* IPv6 address allocation, and lease management.
*/
class Dhcpv6Server
{
    public:
    friend class Dhcpv6ServerTest;

    Dhcpv6Server(core::Global& global, core::TimeManager& timeManager);
    ~Dhcpv6Server();

    void handlePacket(packet::Dhcpv6Header& dhcp, interface::Interface& iface, bool multicast, const uint8_t* clientIp);
    bool handleDhcpPacket(Dhcpv6PacketReceive& receive, types::IPv6Address networkAddress);

    void expireClient(const ClientID& clientID);

    Configs configs;

    private:

    core::Global& global;
    core::TimeManager& timeManager;
    AuthManager authManager;

    std::unordered_map<std::string, DhcpNetwork*> stringToPool;
    std::unordered_map<types::IPv6Prefix, DhcpNetwork*> prefixToPool;

    Duid dhcpUniqueIdentifier; ///< Unique Identifier for DHCP server.

    std::mutex serverMutex;

    std::unordered_map<ClientID, ReconfigureAccepts> reconfigAccepts;
    std::unordered_map<ClientID, ReconfigureState> activeReconfigs;

    // Process Functions

    std::optional<Dhcpv6SendType> processSolicit(Dhcpv6PacketReceive& packet, Dhcpv6IAOptions& ia);

    std::optional<Dhcpv6SendType> processRequest(Dhcpv6PacketReceive& packet, Dhcpv6IAOptions& ia, Duid& serverID);

    std::optional<Dhcpv6SendType> processConfirm(Dhcpv6PacketReceive& packet, Dhcpv6IAOptions& ia);

    std::optional<Dhcpv6SendType> processRenew(Dhcpv6PacketReceive& packet, Dhcpv6IAOptions& ia, Duid& serverID);

    std::optional<Dhcpv6SendType> processRebind(Dhcpv6PacketReceive& packet, Dhcpv6IAOptions& ia);

    std::optional<Dhcpv6SendType> processRelease(Dhcpv6PacketReceive& packet, Dhcpv6IAOptions& ia, Duid& serverID);

    std::optional<Dhcpv6SendType> processDecline(Dhcpv6PacketReceive& packet, Dhcpv6IAOptions& ia, Duid& serverID);

    std::optional<Dhcpv6SendType> processInformationRequest(Dhcpv6PacketReceive& packet);

    bool processRelayForward(const packet::Dhcpv6RelayHeader& relay, interface::Interface& iface, const uint8_t* sourceAddress, const uint8_t* relayIp);

    size_t processRelayChain(const packet::Dhcpv6RelayHeader& relay, packet::Dhcpv6Header& dhcp, std::vector<RelayLink>& chain);

    // Packet Creation

    bool sendAdvertise(Dhcpv6PacketSend& send, Dhcpv6IAOptions& ia, const uint8_t* transId);

    bool sendReply(Dhcpv6PacketSend& send, Dhcpv6IAOptions* ia, const uint8_t* transId, bool rapidCommit);

    bool sendConfirmReply(Dhcpv6PacketSend& send, Dhcpv6IAOptions& ia, const uint8_t* transId, bool success);

    bool sendReconfigure(Dhcpv6PacketSend& send, ReconfigReason reason);

    // Packet Builders

    bool buildAdvertise(Dhcpv6PacketBuild& build, Dhcpv6PacketSend& send, Dhcpv6IAOptions& ia, const uint8_t* transId);

    bool buildReply(Dhcpv6PacketBuild& build, Dhcpv6PacketSend& send, Dhcpv6IAOptions* ia, const uint8_t* transId, bool rapidCommit);

    bool buildConfirmReply(Dhcpv6PacketBuild& build, Dhcpv6PacketSend& send, Dhcpv6IAOptions& ia, const uint8_t* transId, bool success);

    bool buildReconfigure(Dhcpv6PacketBuild& build, Dhcpv6PacketSend& send, ReconfigReason reason);

    /**
     * @brief Extracts the client IA_NA data: IAID, T1, T2, and IAADDR.
     *
     * @param option    The IA_NA option to parse.
     * @param network   Network the client was matched to.
     * @param clientID  Client DUID, used to look up an existing binding.
     * @param isBinding True when the request should update the lease binding.
     * @return The parsed IA_NA block, or nullopt if the option is malformed.
     */
    std::optional<IANABlock> extractIA_NA(const packet::TLV16Option& option, DhcpNetwork* network, Duid& clientID, bool isBinding);

    /**
     * @brief Extracts IA_TA and TEMP address for temporary address support.
     *
     * @param option    The IA_TA option to parse.
     * @param network   Network the client was matched to.
     * @param isBinding True when the request should update the lease binding.
     * @return The parsed IA_TA block, or nullopt if the option is malformed.
     */
    std::optional<IATABlock> extractIA_TA(const packet::TLV16Option& option, DhcpNetwork* network, bool isBinding);

    /**
     * @brief Extracts IA_PD and IAPREFIX for prefix delegation support.
     *
     * @param option    The IA_PD option to parse.
     * @param network   Network the client was matched to.
     * @param clientID  Client DUID, used to look up an existing binding.
     * @param isBinding True when the request should update the lease binding.
     * @return The parsed IA_PD block, or nullopt if the option is malformed.
     */
    std::optional<IAPDBlock> extractIA_PD(const packet::TLV16Option& option, DhcpNetwork* network, Duid& clientID, bool isBinding);

    // BUILDING -------------------------------------------------

    /**
     * @brief Builds a standard DHCPv6 reply header with all configured options.
     *
     * @param dhcp     Reply header to populate.
     * @param clientID Client identifier echoed back in the reply.
     * @param tlv      Option buffer the response options are written into.
     * @param ia       IA options to include, or nullptr for none.
     * @param oro      Option Request Option list from the client.
     * @param oroSize  Number of entries in @p oro.
     */
    void buildResponse(
        packet::Dhcpv6Header& dhcp,
        ClientID& clientID,
        packet::TLV16BufferManager& tlv,
        Dhcpv6IAOptions* ia,
        const uint8_t* oro,
        size_t oroSize
    );

    void handleNoPool(Dhcpv6IAOptions& ia);

    DhcpNetwork* matchAddressToPool(const types::IPv6Address& addr, uint32_t interfaceKey);

    PrefixPoolConfig* selectPrefixPool(const DhcpNetwork* network, const types::IPv6Prefix* requestedPrefix);

    bool addServerUnicast(packet::TLV16BufferManager& tlv, types::IPv6Address leasedIp, interface::Interface& iface);


    bool addStaticLease(IANABlock& block, const IAKey& key);
    bool addStaticPrefix(IAPDBlock& block, const IAKey& key);
    bool addStaticAdvertisedLease(IANABlock& block, const IAKey& key);
    bool addStaticAdvertisedPrefix(IAPDBlock& block, const IAKey& key);

#if 0
    /**
     * @brief Constructs DHCPv6 options to send in response.
     *
     * @param config DHCP network configuration.
     * @param oroOptions Options requested by client (via ORO).
     * @return Vector of DHCPv6 options.
     */
    void buildOptions(TLV16BufferManager& tlv, DhcpNetworkConfig* config, const std::vector<uint16_t>& oroOptions);

    // UTILS---------------------------------------------------

    /**
     * @brief Finds the network configuration whose prefix covers an address.
     *
     * @param ip The client address to match against configured networks.
     * @return The matching prefix, or an empty prefix if none covers @p ip.
     */
    types::IPPrefix findMatchingNetwork(const uint8_t* ip);

    /**
     * @brief Generates a unique identifier for the DHCP server to use.
     */
    Duid generateUniqueIdentifier();


    void sendRelayPacket(Dhcpv6RelayHeader& header, interface::Interface* iface);

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
     * @brief Builds a status option to send back to the client indicating status of request.
     *
     * @param code The status code to place in the option.
     * @param message The message to be sent along with the status.
     */
    Dhcpv6Header::Option buildStatusOption(const ByteString& code, const std::string& message);

    // Enhanced timer management
    void scheduleTimers(const ByteString& duid, const ByteString& iaid, uint32_t t1, uint32_t t2);
    void handleTimerExpiry(const ByteString& duid, const ByteString& iaid, dhcp::TimerType type);

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
    uint8_t getServerPreference(const DhcpNetworkConfig* config, const ByteString& duid);

    void buildReconfigPacket(const ByteString& clientID, const ByteString& leaseIp, const std::pair<ByteString, interface::Interface*>& ifacePair, ReconfigReason reason, bool relay);

    bool trackElapsedTime(const Dhcpv6Header& header, const ByteString& duid);


    std::optional<AuthConfig::Key> getKey(uint64_t keyID);
    // Status code tracking
#endif
};
} // namespace services::dhcp

#endif // DHCPV6_SERVER_H

