// DhcpServerBase.h

#ifndef DHCP_SERVER_BASE_H
#define DHCP_SERVER_BASE_H

#include <ByteString.hpp>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <LeaseManager.h>
#include <IPPool.h>
#include <PrefixLeaseManager.h>
#include <PrefixPool.h>
#include <PacketStructure.h>
#include <GlobalLeaseManager.h>
#include <DhcpInfo.hpp>

// Forward declarations
class Interface;
class DhcpServerTest;
class Dhcpv6ServerTest;
class IPPoolTest;

namespace Protocol 
{
    namespace Dhcp
    {
        /**
         * @brief Structure representing a network configuration for DHCP.
         *
         * For DHCPv4, subnetMask and defaultGateway are used.
         * For DHCPv6, these fields are ignored.
         */
        struct networkConfig
        {
            ByteString network;         // Network address
            ByteString subnetMask;      // For DHCPv4 (ignored in v6)
            ByteString defaultGateway;  // For DHCPv4 (ignored in v6)
            double leaseTime = 3600.0;  // Lease duration (in seconds)
            Interface* interface = nullptr; // Network interface for sending packets
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
        friend class ::DhcpServerTest;
        friend class ::Dhcpv6ServerTest;
        friend class ::IPPoolTest;

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
        void addNetwork(DhcpNetworkConfig* config);

        /**
         * @brief Removes a network configuration.
         * @param networkID Network identifier.
         */
        void removeNetwork(const ByteString& networkID);

        /**
         * @brief Updates an existing network configuration.
         *
         * @param network The network address idetnfier.
         * @param config The updated network configuration.
         * @param dnsToRemove DNS servers to remove from the config.
         * @param winsToRemove WINS servers to remove from the config.
         * @param helperAddressesToRemove Helper addresses to remove.
         */
        bool updateNetworkConfig(const ByteString& network, const DhcpNetworkConfig& newConfig, const std::vector<ByteString>& dnsToRemove, const std::vector<ByteString>& winsToRemove, const std::vector<ByteString>& helperAddressesToRemove);

        /**
         * @brief Processes an incoming DHCP packet.
         * @param packet The incoming packet information.
         */
        virtual void handleDhcpPacket(const PacketInfo& packet) = 0;

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

    private:

        std::map<Dhcp::TimerType, std::vector<Dhcp::TrackedTimer>> activeTimers;
        std::mutex timerMutex;

        std::set<ByteString> outgoingRequests;

        std::unordered_map<ByteString, DhcpNetwork*> dhcpNetworks;   ///< Map of network configurations.

        GlobalLeaseManager globalLeaseManager; ///< Global lease manager aggregating all leases.

        std::mutex configMutex;     ///< Mutex for synchronizing network configuration access.
        std::atomic<bool> stopFlag { false }; ///< Flag to signal server thread to stop.
        std::thread serverThread;   ///< Server thread for handling DHCP processing.

        /**
         * @brief Abstract handler loop for DHCP processing.
         */
        virtual void dhcpHandler() = 0;
    };
}

#endif // DHCP_SERVER_BASE_H
