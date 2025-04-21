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
#include <atomic>
#include <shared_mutex>

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
