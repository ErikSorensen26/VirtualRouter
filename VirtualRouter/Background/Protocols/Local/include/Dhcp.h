// Dhcp.h

#ifndef DHCP_H
#define DHCP_H

#include <string>
#include <PacketStructure.h>
#include <Functions.h>
#include <Global.h>
#include <condition_variable>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <unordered_set>

double secondsSinceEpoch(); ///< Function to get the current time in seconds since the Unix epoch

// Forward declares interface class
class Interface;
class ProcessPacket;
class DhcpClientTest;
class DhcpRelayTest;
class DhcpServerTest;
class IPPoolTest;
enum class InterfaceType;

namespace Protocol 
{
 
    /**
     * @brief Represents a dhcp relay agent
     */
    class DhcpRelay
    {
    public:
        friend class ::DhcpRelayTest;

        /**
         * @brief Constructor for DHCP relay.
         * @param interface Reference to the associated interface.
         */
        explicit DhcpRelay(Interface* interface, AddressFamily af);

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
        Interface* associatedInterface; ///< Reference to the associated interface.
        std::vector<ByteString> helperAddresses; ///< List of helper addresses for this relay.
        std::mutex relayMutex; ///< Mutex for syncronizing access to helper addresses.

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

    /**
     * @brief Represents a dynamic sized IP pool for DHCP-managed networks.
     */
    class IPPool
    {
        ByteString baseAddress;         ///< First usable address (network + 1).
        ByteString broadcastAddress;    ///< Last usable address (network + pool size).
        uint32_t poolSize;              ///< Number of usable IPs in the subnet.
        ByteString currentAddress;      ///< Pointer to the next IP for dynamic allocations
        ByteString gateway;             ///< Current gateway to exclude

        std::unordered_map<ByteString, ByteString> allocatedIPs; ///< Tracks dynamically allocated IPs to MAC addresses.
        std::unordered_set<ByteString> excludedAddresses; ///< Maps excluded IPs
    public:
        friend class ::IPPoolTest;
        friend class ::DhcpServerTest;
        /**
         * @brief Default constructor with no parameters.
         */
        IPPool() {}

        /**
         * @brief Constructs an IPPool for a given network and subnet mask.
         *
         * @param network The network address as a ByteString.
         * @param subnetMask The subnet maske as a bytestring.
         */
        IPPool(const ByteString& network, const ByteString& subnetMask, const ByteString& gateway);

        /**
         * @brief Allocates the next available IP from the pool.
         *
         * @param macAddress The MAC address of the client.
         * @return The allocated UP address as a ByteString, or an empty ByteString if none are available.
         */
        ByteString allocateIP(const ByteString& macAddress);

        /**
         * @brief Reeleases an IP back to the pool.
         *
         * @param ip The IP address to release.
         */
        void releaseIP(const ByteString& ip);

        /**
         * @brief Excludes an IP for a specific MAC address.
         *
         * @param ip The IP address to reserve.
         * @return True if the reservation was successful, otherwise false.
         */
        bool excludeIP(const ByteString& ip);

        /**
         * @brief clears the mac address so it will not be allocated to any clients until the offer timeout expires
         * 
         * @param ip The IP address that is conflicted.
         * @return True if the ip exists in the pool and was set as conflicted, otherwise false.
         */
        bool setConflicted(const ByteString& ip);

        /**
         * @brief Removes an existing excluded IP.
         *
         * @param ip The IP address to unbind.
         * @return True if the reservation was removed, otherwise false.
         */
        bool removeExclusion(const ByteString& ip);

        /**
         * @brief Checks if an IP is currently allocated.
         *
         * @param ip The IP address to check.
         * @return True if the IP is allocated, otherwise false.
         */
        bool isAllocated(const ByteString& ip) const;

        /**
         * @brief Checks if an IP is currently reserved.
         *
         * @param ip The IP address to check.
         * @return True if the IP is reserved, otherwise false.
         */
        bool isExcluded(const ByteString& ip) const;

        /**
         * @brief Checks if an IP is currently allocated or reserved.
         *
         * @param ip The IP address to check.
         * @return True if the IP is allocated or reserved, otherwise false.
         */
        bool isAllocatedOrExcluded(const ByteString& ip) const;

        /**
         * @brief Searches for a mac address against a IP address in the reserved addresses.
         * 
         * @param ip The IP address to search for
         * @param mac The MAC address looked for in the allocated ip
         */
        bool matchMacToIP(const ByteString ip, const ByteString& mac);

        /**
         * @brief Adjusts the pool to a new network or subnet mask configuration.
         *
         * @param network TThe new network address as a ByteString.
         * @param subnetMask The new subnet mask as a ByteString.
         * @param gatway The default gateway of the dhcp pool.
         */
        void adjustPool(const ByteString& network, const ByteString& subnetMask, const ByteString& gateway);

    private:
        /**
         * @brief Generates the next usable IP in the pool, skipping allocated IPs.
         *
         * @return the next usable IP as ByteString, or an empty Bytestring if no IPs are available.
         */
        ByteString findNextAvailableIP();
    };

    /**
     * @brief Represents a fully functional DHCP server
     * Cabable of managing IP address leases, handling dhcp packets, and supporting relay agents.
     */
    class DhcpServer
    {
    public:
        friend class ::DhcpServerTest;
        friend class ::IPPoolTest;

        /**
         * @brief Represents a lease assigned to a DHCP client.
         * 
         * Represents a DHCP client containing information about the assigned IP address,
         * the client MAC address, and the lease duration.
         */
        struct Lease
        {
            ByteString ipAddress;  ///< The IP address assigned to the client.
            ByteString macAddress; ///< The MAC address of the client requesting the lease.
            double leaseStart;      ///< The start time of the lease in seconds since the Unix epoch.
            double leaseDuration;   ///< The duration of the lease in seconds.
        };

        /**
         * @brief COnfiguration details for a network managed by the DHCP server.
         *
         * Each network includes options such as the subnet, default gateway, DNS servers, domainName, and lease duration.
         */
        struct NetworkConfig
        {
            ByteString network;                 ///< The base address of the network. (e.g., "192.168.1.0").
            ByteString subnetMask;              ///< The subnet mask defining the network size.
            ByteString defaultGateway;          ///< The default gateway address for clients in this network.
            ByteString renewalTime;             ///< Renewal time in bytes for easy access.
            ByteString rebindingTime;           ///< Rebinding time in bytes for easy access.
            std::vector<ByteString> dnsServer;  ///< A list of DNS servers provided with this network.
            std::string domainName;             ///< The domain name associated with this network.
            std::string netbiosName;            ///< The name of the NetBIOS server.
            double leaseTime = 0.0;             ///< The default duration of a lease in seconds.
            Interface* interface = nullptr;     ///< Pointer to the interface managing this network.

            // Additional fields
            ByteString ntpServer;                   ///< Network Time Protocol (NTP) server for this network.
            ByteString tftpServer;                  ///< TFTP server address for PXE booting.
            std::vector<ByteString> winsServer;     ///< A list of WINS (Windows Internet Name Service) servers.
            std::vector<ByteString> staticRoutes;   ///< Static routes provided to the network clients.
            std::vector<ByteString> helperAddresses;///< List of DHCP relay (helper) addresses.
            ByteString broadcastAddress;            ///< The broadcast address for this network.
            ByteString arpTimeout;                  ///< ARP timeout value for this network.
            std::optional<bool> allowDynamicUpdates; ///< Indicates whether dynamic updates (e.g., for DNS) are enabled.
            std::vector<std::string> allowedHostnames; ///< A list of hostnames allowed to operate on this network.
            uint16_t mtu;                           ///< Maximum Transmission Unit (MTU) for the network.
            std::string bootfile;                   ///< Bootfile for pool.

            // Metadata
            std::string description;               ///< Description or label for this network configuration.
            bool isPrivate;                        ///< Flag indicating whether this network is private or public.
            bool isEnabled;                        ///< Flag indicating whether this network is currently active.

            // Methods (optional, if you want to add functions)
            //void reset();                          ///< Resets all network configuration fields to default values.
            //void printConfig() const;              ///< Prints the current network configuration.
        };

        /**
         * @brief COnstructs a new instance of the DhcpServer class and initialized internal structures for lease and network management.
         */
        DhcpServer();

        /**
         * @brief Destructor that cleans up resources, stops the server, and releases any threads or mutexes in use.
         */
        ~DhcpServer();

        /**
         * @brief Starts the DHCP server, enabling it to proces DHCP packets and manage leases for confugured networks.
         */
        void startServer();

        /**
         * @brief Stops the DHCP server and ensures that any ongoing operations or threads are safely terminalted.
         */
        void stopServer();

        /**
         * @brief Configuraes a network for DHCP management
         *
         * Allows the server to provide leases and handle DHCP packets for
         * the specified network.
         *
         * @param ByteString& network The network address of the configuration to update.
         * @param newConfig A network config object containing details about the network.
         */
        void addNetwork(const NetworkConfig& newConfig);

        /**
         * @brief Updates the configuration of an existing network.
         * 
         * @param network The network address of the configuration to update.
         * @param newConfig The new network configuration.
         * @param dnsToRemove Optional vector of dns servers to remove.
         * @param winsToRemove Optional vector of wins to remove.
         * @param helperAddressToRemove Optional vector of helper addresses to remove.
         */
        void updateNetworkConfig(const ByteString& network, const NetworkConfig& newConfig, const std::optional<std::vector<ByteString>>& dnsToRemove = std::nullopt, const std::optional<std::vector<ByteString>>& winsToRemove = std::nullopt, const std::optional<std::vector<ByteString>>& helperAddressesToRemove = std::nullopt);

        /**
         * @brief Processes an incoming DHCP packet.
         *
         * determines its type, and preforms the appropriate actions based on the DHCP message.
         *
         * @param packet The received PacketInfo object containing the header and payload.
         */
        void handleDhcpPacket(const PacketInfo& packet);

        ByteString generateTransactionID();

    private:
        
        struct OfferTimeout
        {
            std::condition_variable cv; ///< Per-offer condition variable
            std::mutex mutex; ///< Mutex to syncronize this offer's timeout
            bool requestReceived = false; ///< Flag indicating if a request was received
        };
        
        std::mutex configMutex; ///< Mutex for syncronizing access to network ocnfigurations.
        std::mutex dhcpMutex; ///< Mutex for syncronizing access to lease management resources.
        std::atomic<bool> stopFlag; ///< Atomic flag indicating whether the server is stopping.
        std::thread dhcpThread; ///< Background thread for lease management and periodic tasks.

        std::unordered_map<ByteString, NetworkConfig> networkConfigs; ///< Maps network addresses to their configuration.
        std::unordered_map<ByteString, Lease> leases; ///< Tracks active leases by assigned IP addresses.
        std::unordered_map<ByteString, IPPool> ipPools; ///< IP allocation pools per network.
        std::unordered_map<ByteString, std::shared_ptr<OfferTimeout>> offerTimeouts;

        /**
         * @brief The main handler thread for managing DHCP server operations.
         *
         * Including cleanup up expired leases and processing queued requests.
         */
        void dhcpHandler();

        /**
         * @brief Cleans up excess offer timeouts.
         */
        void cleanupTimeouts();

        /**
         * @brief Cleans up expired leases by checking all active leases and removing those that exceed their lease duration.
         */
        void cleanupExpiredLeases();

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
         * @brief Offer timeout waiting for a reply to make sure IP gets released if there is no reply
         *
         * @param ipAddress The IP address the timeout is on.
         * @param macAddress The mac address of the client that is being waited on.
         * @param network Network that the leased IP is on.
         * @param timeoutSeconds The timeout length.
         */
        void startOfferTimeout(const ByteString& ipAddress, const ByteString& macAddress, const ByteString& network, double timeoutSeconds);

        /**
         * @brief Allocates an IP address from the specified network for a client based on its MAC address.
         *
         * Ensure that the allocated UP is not already in use.
         *
         * @param network The network from which to allocate the IP address.
         * @param macAddress The MAC address of the client requesing the lease.
         * @return The allocated IP address, or an empty string if no address is available.
         */
        ByteString allocateIPAddress(const ByteString& network, const ByteString& macAddress);

        /**
         * @brief Releases an IP address back to the pool.
         *
         * @param network The network containing the pool.
         * @param ipAddress The IP address to release.
         */
        void releaseIPAddress(const ByteString& network, const ByteString& ipAddress);

        /**
         * @brief Finds a network configuration matching the provided DHCP header, based
         *        on the client's request or relay agent information.
         * 
         * @param dhcpHeader The DHCP header containing client request details.
         * @return The matching network address, or an empty string if none is found.
         */
        ByteString findMatchingNetwork(const DhcpHeader& dhcpHeader);

        /**
         * @brief Finds a network configuration matching the IP address.
         * 
         * @param ip The IP address to find match a pool network to.
         * @return The matching network address, or an empty string if none is found.
         */
        ByteString findMatchingNetworkAgainstIP(const ByteString& ip);

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

        /**
         * @brief Builds a DHCPOFFER packet to respond to a DHCPDISCOVER message, including
         *        the offered IP address, lease time, and other options.
         * 
         * @param dhcpHeader The DHCP header of the request.
         * @param config The network configuration for the offer.
         * @param ipAddress The IP address being offered.
         * @return A PacketInfo object representing the constructed DHCPOFFER.
         */
        PacketInfo buildDhcpOffer(const DhcpHeader& dhcpHeader, const NetworkConfig& config, const ByteString& ipAddress);

        /**
         * @brief Builds a DHCPACK packet to acknowledge a successful lease request, providing
         *        the client with its IP address and other configuration details.
         * 
         * @param dhcpHeader The DHCP header of the request.
         * @param config The network configuration for the acknowledgment.
         * @param ipAddress The IP address being acknowledged.
         * @return A PacketInfo object representing the constructed DHCPACK.
         */
        PacketInfo buildDhcpAck(const DhcpHeader& dhcpHeader, const NetworkConfig& config, const ByteString& ipAddress);

        /**
         * @brief Builds a DHCPACK packet to send back data requested by a clinet from a infomr message.
         *
         * @param dhcpHeader The DHCP header of the request.
         * @param config The network configuration for the acknowledgment.
         * @param requestedOptions A vector of Options requested in ByteStrings.
         * @return A PacketInfo object representing the constructed DHCPACK.
         */
        PacketInfo buildDhcpAckForInform(const DhcpHeader& dhcpHeader, const NetworkConfig& config, const std::vector<ByteString>& requestedOptions);

        /**
         * @brief Builds all requested options from an inform request
         *
         * @param requestedOptions Vector holding all requested options.
         * @param config NetworkConfig object holding the information to fill in.
         * @return A vector of fully made and ready options.
         */
        std::vector<DhcpHeader::Option> buildRequestedOptions(const std::vector<ByteString>& requestedOptions, const NetworkConfig& config);

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
         * @brief Gets the current time in seconds since the Unix epoch.
         * 
         * @return The current time in seconds.
         */
        double secondsSinceEpoch();
    };

    /**
     * @struct Dhcp
     * @brief Stores DHCP configuration information.
     */
    struct DhcpInfo {
        ByteString dhcpServer{};        ///< DHCP server address
        ByteString broadcast{};         ///< Broadcast address
        ByteString router{};            ///< Router address
        std::vector<ByteString> dnsServer{}; ///< List of DNS servers
        ByteString leaseTime{};         ///< Lease time for DHCP
        ByteString renewalTime{};       ///< Renewal time for DHCP
        ByteString rebindingTime{};     ///< Rebinding time for DHCP
        uint8_t subnetMask{};           ///< Subnet mask

        std::vector<ByteString> helperAddresses; ///< List of DHCP helper addresses (relay agents)

        // Additional variables
        ByteString domainName{};             ///< Domain name provided by the DHCP server
        ByteString hostName{};               ///< Hostname of the client
        ByteString clientIdentifier{};       ///< Client Identifier option (e.g., MAC address or custom ID)
        ByteString requestedIpAddress{};     ///< IP address requested by the client
        ByteString serverIdentifier{};       ///< Server Identifier from the DHCP server
        std::vector<ByteString> ntpServers{}; ///< List of NTP (Network Time Protocol) servers
        ByteString mtu{};                    ///< Maximum Transmission Unit (MTU) size
        ByteString tftpServer{};             ///< TFTP server for booting (commonly used in PXE environments)
        ByteString bootFile{};               ///< Boot file name (commonly used in PXE environments)
        std::vector<ByteString> staticRoutes{}; ///< List of static routes provided by the DHCP server
        ByteString arpTimeout{};             ///< ARP timeout value (if provided by the DHCP server)
        std::vector<ByteString> winsServer{}; ///< List of WINS servers
        ByteString vendorSpecificOptions{};  ///< Vendor-specific options (Option 43 in DHCP)
        ByteString parameterRequestList{};   ///< Parameter request list sent by the client
        ByteString clientIpAddress{};        ///< The client’s IP address (set if the client has already obtained a lease)
        ByteString nextServerIp{};           ///< The next server IP address (used in booting scenarios)
    };

    /**
     * @brief Represents a DHCP client that handles DHCP operations
     * DHCP client able to hanble operations such as discovery, offers,
     * acknowledgments, and lease renewals.
     */
    class DhcpClient 
    {
    public:
        friend class ::ProcessPacket;
        friend class ::DhcpClientTest;

        /**
         * @brief Constructs a DhcpClient with the specific interface.
         *
         * @param CurrentInterface Reference ot the interface object
         * @param reduced Mode to reduce functions in the constructor for testing.
         */
        DhcpClient(Interface* CurrentInterface, bool reduced = false);

        /**
         * @brief Destructor to clean up threads and resources.
         */
        ~DhcpClient();
    
        /**
         * @brief Creates a DHCP packet body with Ethernet, IP, and UDP headers.
         *
         * @param hardwareAddress The hardware (MAC) address of the client.
         * @return PacketInfo The constructued DHCP packet.
         */
        PacketInfo dhcpBody(ByteString& hardwareAddress);

        /**
         * @brief Creates a DHCP Discover packet with the provided hostname and hardware address.
         *
         * @param packet The base packet structure.
         * @param hostname The hostname of the cient.
         * @param hardwareAddress The hardware (MAC) address of the cient.
         * #preturn PacketInfo The DHCP Discover packet.
         */
        PacketInfo dhcpDiscover(PacketInfo packet, const std::string& hostname, ByteString& hardwareAddress);

        /**
         * @brief Creates a DHCP Request packet with the given header, hostname, hardware address, requested IP, and server ID.
         * 
         * @param packet The base packet structure.
         * @param header The DHCP header containing transaction ID and client IP.
         * @param hostname The hostname of the client.
         * @param hardwareAddress The hardware (MAC) address of the client.
         * @param requestedIP The IP address being requested.
         * @param serverID The DHCP server identifier.
         * @return PacketInfo The DHCP Request packet.
         */
        PacketInfo dhcpRequest(PacketInfo packet, DhcpHeader& header, const std::string& hostname, 
                               ByteString hardwareAddress, ByteString requestedIP, ByteString serverID);

        /**
         * @brief Creates a DHCP Release packet to release the leased IP address.
         * 
         * @param packet The base packet structure.
         * @param hardwareAddress The hardware (MAC) address of the client.
         * @return PacketInfo The DHCP Release packet.
         */
        PacketInfo dhcpRelease(PacketInfo packet, const ByteString& hardwareAddress);

        /**
         * @brief Creates a DHCP Inform packet to request local configuration parameters.
         * 
         * @param packet The base packet structure.
         * @param hostname The hostname of the client.
         * @param hardwareAddress The hardware (MAC) address of the client.
         * @return PacketInfo The DHCP Inform packet.
         */
        PacketInfo dhcpInform(PacketInfo packet, std::string& hostname, ByteString& hardwareAddress);

        /**
         * @brief Initializes the DHCP client, handling discovery, offers, requests, acknowledgments, and lease renewals.
         * 
         * @param hardwareAddress The hardware (MAC) address of the client.
         */
        void InitializeDhcp(ByteString& hardwareAddress);

        /**
         * @brief Extracts DHCP options from the provided list of options and updates the interface's DHCP configuration accordingly.
         * 
         * @param options The list of DHCP options received from the server.
         */
        void ExtractOptions(std::vector<DhcpHeader::Option> options);

        /**
         * @brief Processes a DHCP packet with the given header and type.
         * 
         * @param header Pointer to the DHCP header.
         * @param type The type of DHCP message.
         */
        void DhcpPacket(const DhcpHeader* header, ByteString& type);

        /**
         * @brief Generates a unique DHCP transaction ID.
         * 
         * @return ByteString The generated transaction ID as a byte string.
         */
        ByteString generateDhcpTransid();
        void sendDhcpRelease();

        DhcpInfo configs; ///< Dhcp Configurations.

    private:
        /**
         * @brief Sends a DHCP Discover message.
         * 
         * @param hostname The hostname of the client.
         * @param hardwareAddress The hardware (MAC) address of the client.
         */
        void sendDhcpDiscover(const std::string& hostname, ByteString& hardwareAddress);

        /**
         * @brief Sends a DHCP Request message.
         * 
         * @param requestPacket Request packet to be reliably transported.
         */
        void sendDhcpRequest(PacketInfo& requestPacket);

        /**
         * @brief Processes received DHCP Offer, ACK, NAK, DECLINE, and INFORM messages.
         * 
         * @param hostname The hostname of the client.
         * @param hardwareAddress The hardware (MAC) address of the client.
         */
        void processDhcpResponses(const std::string& hostname, ByteString& hardwareAddress);


        /**
         * @brief Processes received DHCP Offer.
         * 
         * @param hostname The hostname of the client.
         * @param hardwareAddress The hardware (MAC) address of the client.
         */
        void processDhcpOffer(const std::string& hostname, ByteString& hardwareAddress);
        /**
         * @brief Handles DHCP Lease Renewal.
         * 
         * @param hardwareAddress The hardware (MAC) address of the client.
         * @param hostname The hostname of the client.
         */
        void handleLeaseRenewal(ByteString& hardwareAddress, const std::string& hostname);

        /**
         * @brief Resets the DHCP client state upon receiving a NAK or DECLINE.
         */
        void resetDhcpState();

        /**
         * @brief The main DHCP handling thread.
         */
        void dhcpHandler(std::string hostname, ByteString hardwareAddress);

        Interface* currentInterface; ///< Pointer to the interface associated with this DHCP client

        std::mutex dhcpMutex; ///< Mutex for syncronizing access to DHCP-related resources.
        std::condition_variable cv; ///< Condition variable to syncronize DHCP state changes.
        std::thread dhcpThread; ///< Thread handling DHCP operations.
        std::atomic<bool> stopFlag; ///< Atomic flag to signal thread termination.

        PacketInfo dhcpOffer{}; ///< Packet information for DHCP Offer.
        PacketInfo dhcpAck{}; ///< Packet information for DHCP Acknowledgment.
        PacketInfo dhcpNak{}; ///< Packet information for DHCP NAK;
        PacketInfo dhcpDecline{}; ///< Packet information for DHCP Decline.
        PacketInfo dhcpInformPacket{};

        bool offered; ///< Flag indicating if a DHCP offer has been received.
        bool acked; ///< Flag indicating if a DHCP acknowledgment has been received.
        bool naked; ///< Flag indicating if a DHCP NAK has been received.
        double leaseStart; ///< Time when the lease started, measured in seconds since epoch.
    };

}

#endif // DHCP_H
