// DhcpServerBase.h

#ifndef DHCP_SERVER_BASE_H
#define DHCP_SERVER_BASE_H

#include <ByteString.hpp>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <condition_variable>
#include <LeaseManager.h>
#include <IPPool.h>
#include <PrefixLeaseManager.h>
#include <PrefixPool.h>
#include <PacketStructure.h>
#include <GlobalLeaseManager.h>
#include <DhcpInfo.hpp>

// Forward declarations
class Interface;
class Global;
class Internal_DhcpServerTest;
class Internal_Dhcpv6ServerTest;
class Internal_IPPoolTest;

namespace Protocol
{
    class DhcpServerBase;

    namespace Dhcp
    {
        struct DhcpNetwork;
        /**
         * @brief Global configs for DHCPv4 and DHCPv6
         */
        struct GlobalConfigs
        {
            std::shared_mutex configMutex;
            std::unordered_map<std::string, std::unordered_map<__uint128_t, std::set<__uint128_t>>> excludedAddresses;
        };
        /**
         * @brief Configuration details for a network managed by the DHCP server.
         *
         * Each network includes options such as the subnet, default gateway, DNS servers, domainName, and lease duration.
         */
        struct DhcpNetworkConfig
        {
            std::atomic<uint8_t> defaultSubnetPrefix;        ///< The default subnet Prefix for the DHCP pool.
            std::atomic<uint8_t> serverPreference = 255;     ///< Default server preference.
            std::atomic<double> leaseTime = 0.0;             ///< The default duration of a lease in seconds.
            std::atomic<double> t1Percentage = 0.5;          ///< Initial T1 percentage for calcualting renewal times.
            std::atomic<double> t2Percentage = 0.87;          ///< Initial T2 percentage for calcualting rebinding times.

            ByteString renewalTime;             ///< Renewal time in bytes for easy access.
            ByteString rebindingTime;           ///< Rebinding time in bytes for easy access.
            std::vector<ByteString> dnsServer;  ///< A list of DNS servers provided with this network.
            std::vector<std::string> domainName;///< The domain name associated with this network.
            std::vector<ByteString> netbiosName;///< The name of the NetBIOS server.

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
            std::atomic<uint16_t> mtu;                           ///< Maximum Transmission Unit (MTU) for the network.
            std::string bootfile;                   ///< Bootfile for pool.

            // Metadata
            std::string description;               ///< Description or label for this network configuration.
            std::atomic<bool> isPrivate;                        ///< Flag indicating whether this network is private or public.
            std::atomic<bool> isEnabled;                        ///< Flag indicating whether this network is currently active.
            
            mutable std::shared_mutex configMutex;
            // Methods (optional, if you want to add functions)
            bool updateNetwork(ByteString* newNetwork, uint8_t* newPrefix, ByteString* gateway, DhcpServerBase* server);
            ByteString getNetworkID() const { std::shared_lock<std::shared_mutex> lock(configMutex); return network + "/" + std::to_string(subnetPrefix.load(std::memory_order_relaxed)); }
            ByteString getNetwork() const { std::shared_lock<std::shared_mutex> lock(configMutex); return network; }
            ByteString getGateway() const { std::shared_lock<std::shared_mutex> lock(configMutex); return defaultGateway; }
            uint8_t getPrefixLen() const { return subnetPrefix.load(std::memory_order_relaxed); }

            std::string hostname;
        private:
            std::atomic<uint8_t> subnetPrefix;  ///< The subnet prefix for the DHCP pool.
            ByteString network;                 ///< The base address of the network. (e.g., "192.168.1.0").
            ByteString defaultGateway;          ///< The default gateway address for clients in this network.

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

        enum class TimerType
        {
            IP_OFFER_TIMEOUT,
            PREFIX_OFFER_TIMEOUT,
            CLIENT_REQUEST_TIMEOUT,
            DECLINE_HOLD,
            RELEASE_HOLD,
        };
            
        struct TrackedTimer
        {
            ByteString clientID;
            ByteString transactionID;
            ByteString resource;
            ByteString networkID;
            uint32_t timerID;
        };
    }

    /**
     * @brief Base class for DHCP server implementation.
     *
     * Provides common members and abstract interfaces for starting/stopping the
     * server, adding/updating networks, and processing incoming DHCP packets.
     */
    class DhcpServerBase
    {
    public:
        friend class DhcpServer;
        friend class Dhcpv6Server;
        friend class ::Internal_DhcpServerTest;
        friend class ::Internal_Dhcpv6ServerTest;
        friend class ::Internal_IPPoolTest;

        friend struct Dhcp::DhcpNetworkConfig;

        DhcpServerBase(Global& global);

        virtual ~DhcpServerBase() = default;

        /**
         * @brief Starts the DHCP server.
         */
        virtual void startServer() = 0;

        /**
         * @brief Stops the DHCP server.
         */
        virtual void stopServer() = 0;

        /**
         * @brief Adds a network configuration.
         *
         * Allows the server to provide leases and handle DHCP packets for
         * the specified network.
         *
         * @param config The network configuration to add.
         */
        void addNetwork(Dhcp::DhcpNetworkConfig* config);

        /**
         * @brief Removes a network configuration.
         * @param networkID Network identifier.
         */
        void removeNetwork(const ByteString& networkID);

        /**
         * @brief moves a NetworkConfig to a different key.
         *
         * @param oldKey Old networkID.
         * @param newKey New networkID.
         * @return true if successful, otherwise false.
         */
        virtual bool moveConfig(const ByteString& oldKey, const ByteString& newKey);

        /**
         * @brief Schedules a timeout for a temporary pooled item.
         *
         * @param type Timer type being created.
         * @param id Clients ID.
         * @param offer Temporary offered item.
         * @param networkID Network identifier aligning with the pool.
         * @param timeout Timeout in seconds that the server will wait for a response for.
         * @param prefixLen option integer value stored in timer (created for prefix).
         */
        void scheduleTimeout(Dhcp::TimerType type, const ByteString& id, const ByteString& offer, const ByteString& networkID, uint32_t timeout);
        
        /**
         * @brief Cancels a temporary pooled item timeout
         *
         * @param type Timer type being canceled.
         * @param id Clients id tied to the temp item.
         * @param offer Temporary offered item.
         * @param prefixLen option integer value stored in timer (created for prefix).
         */
        void cancelTimeout(Dhcp::TimerType type, const ByteString& id, const ByteString& offer);
        void clearOfferTimeouts();

        ByteString encodeDnsName(const std::string& name) 
        {
            ByteString out;
            size_t start = 0;
            while (start < name.size()) 
            {
                size_t end = name.find('.', start);
                if (end == std::string::npos) end = name.size();
                size_t len = end - start;
                out.push_back(static_cast<uint8_t>(len));
                out.append(ByteString(name.substr(start, len)));
                start = end + 1;
            }
            out.push_back(0x00);
            return out;
        }

        Dhcp::GlobalConfigs vrfConfigs;

        std::unordered_map<std::string, Dhcp::DhcpNetworkConfig*> poolConfigs;

    private:

        std::map<Dhcp::TimerType, std::vector<Dhcp::TrackedTimer>> activeTimers;
        std::mutex timerMutex;

        std::set<ByteString> outgoingRequests;

        std::unordered_map<ByteString, Dhcp::DhcpNetwork*> dhcpNetworks;   ///< Map of network configurations.

        GlobalLeaseManager globalLeaseManager; ///< Global lease manager aggregating all leases.

        std::mutex configMutex;     ///< Mutex for synchronizing network configuration access.
        std::atomic<bool> stopFlag { false }; ///< Flag to signal server thread to stop.
        std::thread serverThread;   ///< Server thread for handling DHCP processing.
        std::mutex serverThreadMutex;
        std::condition_variable serverCV;

        /**
         * @brief Abstract handler loop for DHCP processing.
         */
        virtual void dhcpHandler() = 0;

        Global& global;
    };
}

#endif // DHCP_SERVER_BASE_H
