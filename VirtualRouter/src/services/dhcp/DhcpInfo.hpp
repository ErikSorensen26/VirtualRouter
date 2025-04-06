// DhcpInfo.hpp

#ifndef DHCP_INFO_HPP
#define DHCP_INFO_HPP

#include <ByteString.hpp>
#include <vector>
#include <optional>
#include <IPPool.h>
#include <LeaseManager.h>
#include <PrefixPool.h>
#include <PrefixLeaseManager.h>
#include <random>

// Forward declarations
class Interface;

namespace Protocol 
{
    // Initialize a static random generator for transaction IDs
    static std::mt19937& getTransidGenerator()
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        return gen;
    }

    // Generates a random DHCP transaction ID
    static ByteString generateDhcpTransid() 
    {
        static std::uniform_int_distribution<uint32_t> dis(0, UINT32_MAX);
        uint32_t transId = dis(getTransidGenerator());
        ByteString transIdBytes(4, 0);
        transIdBytes[0] = static_cast<unsigned char>((transId >> 24) & 0xFF);
        transIdBytes[1] = static_cast<unsigned char>((transId >> 16) & 0xFF);
        transIdBytes[2] = static_cast<unsigned char>((transId >> 8) & 0xFF);
        transIdBytes[3] = static_cast<unsigned char>(transId & 0xFF);
        return transIdBytes;
    }
    /**
     * @brief Configuration details for a network managed by the DHCP server.
     *
     * Each network includes options such as the subnet, default gateway, DNS servers, domainName, and lease duration.
     */
    struct DhcpNetworkConfig
    {
        uint8_t subnetPrefix;               ///< The subnet prefix for the DHCP pool.
        uint8_t defaultSubnetPrefix;        ///< The default subnet Prefix for the DHCP pool.
        uint8_t serverPreference = 255;     ///< Default server preference.
        ByteString network;                 ///< The base address of the network. (e.g., "192.168.1.0").
        ByteString defaultGateway;          ///< The default gateway address for clients in this network.
        ByteString renewalTime;             ///< Renewal time in bytes for easy access.
        ByteString rebindingTime;           ///< Rebinding time in bytes for easy access.
        std::vector<ByteString> dnsServer;  ///< A list of DNS servers provided with this network.
        std::string domainName;             ///< The domain name associated with this network.
        std::string netbiosName;            ///< The name of the NetBIOS server.
        double leaseTime = 0.0;             ///< The default duration of a lease in seconds.
        double t1Percentage = 0.5;          ///< Initial T1 percentage for calcualting renewal times.
        double t2Percentage = 0.87;          ///< Initial T2 percentage for calcualting rebinding times.

        Interface* interface = nullptr;     ///< Pointer to the interface managing this network.

        // Additional fields
        std::vector<ByteString> ntpServer;      ///< Network Time Protocol (NTP) server for this network.
        std::vector<ByteString> tftpServer;     ///< TFTP server address for PXE booting.
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
        ByteString getNetworkID() { return network + "/" + std::to_string(subnetPrefix); }
    };
    /**
     * @struct DhcpNetwork
     * @brief Holds a IPPool, LeaseManager, and a DhcpNetworkConfig.
     */
    struct DhcpNetwork
    {
        DhcpNetwork() = default;

        IPPool* pool = nullptr;
        LeaseManager* lease = nullptr;
        PrefixLeaseManager* prefixLease = nullptr;
        PrefixPool* prefixPool = nullptr;
        DhcpNetworkConfig* config = nullptr;

        ~DhcpNetwork() 
        {
            if (pool) delete pool;
            if (lease) delete lease;
            if (prefixLease) delete prefixLease;
            if (prefixPool) delete prefixPool;
            if (config) delete config;
        }
    };
    /**
     * @struct Dhcp
     * @brief Stores DHCP configuration information.
     */
    struct DhcpInfo 
    {
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
}

#endif //DHCP_INFO_HPP
