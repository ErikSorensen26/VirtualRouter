// DhcpClient.h

#ifndef DHCP_CLIENT_H
#define DHCP_CLIENT_H

#include <DhcpInfo.hpp>

// Forward declarations
struct TLV8Option;
struct DhcpHeader;
class TLV8BufferManager;
class ProcessPacket;
class DhcpClientTest;
class Interface;
class PacketBuilder;

namespace Protocol
{
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
        DhcpClient(Interface* currentInterface, bool reduced = false);

        /**
         * @brief Destructor to clean up threads and resources.
         */
        ~DhcpClient();

        void initiate();
        void shutdown();

        /**
         * @brief Build DHCPDISCOVER packet header + options.
         * @param builder PacketBuilder to use.
         * @param mac Hardware address of client.
         * @param hostname Hostname of client.
         * @return true if successful, false if reservation failed.
         */
        bool buildDhcpDiscover(PacketBuilder& builder, const uint8_t* mac, const std::string& hostname);

        /**
         * @brief Build DHCPREQUEST packet.
         * @param builder PacketBuilder to use.
         * @param transID Transaction ID (xid).
         * @param hostname Hostname of client.
         * @param mac Hardware address.
         * @param requestedIP IP address being requested.
         * @param serverID Server Identifier.
         * @return true if successful, false otherwise.
         */
        bool buildDhcpRequest(PacketBuilder& builder, uint32_t transID, const std::string& hostname,
                              uint32_t requestedIP, uint32_t serverID);

        /**
         * @brief Build DHCPRELEASE packet.
         * @param builder PacketBuilder to write into.
         * @param mac MAC address to identify client.
         * @return true if successful, false otherwise.
         */
        bool buildDhcpRelease(PacketBuilder& builder);

        /**
         * @brief Build DHCPINFORM packet (requests DNS, etc.).
         * @param builder PacketBuilder to write into.
         * @param hostname Hostname.
         * @param mac MAC address.
         * @return true if successful, false otherwise.
         */
        bool buildDhcpInform(PacketBuilder& builder, const std::string& hostname, const uint8_t* mac);

        /**
         * @brief Called by external logic when a DHCP packet is received.
         * @param header Pointer to parsed DHCPHeader object.
         */
        void handleDhcpPacket(const DhcpHeader& dhcp);

        /**
         * @brief Called by external logic when a DHCP IP is to be released.
         */
        void sendDhcpRelease();

        DhcpInfo configs; ///< Parsed DHCP lease/config state
        std::mutex dhcpMutex;

    private:
        void sendDhcpDiscover(const std::string& hostname, const uint8_t* mac);
        void sendDhcpRequest(uint32_t transID, const std::string& hostname, uint32_t requestedIp, uint32_t serverId);
        void sendRenew();
        void sendRebind();
        
        uint8_t getOpcode(std::vector<TLV8Option>& options);

        bool processDhcpOffer(const DhcpHeader& dhcp, std::vector<TLV8Option>& options);
        bool processDhcpAck(const DhcpHeader& dhcp, std::vector<TLV8Option>& options);
        bool processDhcpNak(const DhcpHeader& dhcp, std::vector<TLV8Option>& options, bool isDecline = false);
        bool processDhcpDecline(const DhcpHeader& dhcp, std::vector<TLV8Option>& option);
        bool processDhcpInformAck(const DhcpHeader& dhcp, std::vector<TLV8Option>& option);
        void processOptionalOption(const std::vector<TLV8Option>& opts);

        void scheduleLeaseTimers(uint32_t t1, uint32_t t2, uint32_t lease);
        void cancelLeaseTimers();

        void expireLease();

        void appendAuthOptions(TLV8BufferManager& tlv, const DhcpHeader& dhcp);
        bool validateAuthentication(const DhcpHeader& dhcp, const uint8_t* value);

        Interface* currentInterface;
        std::atomic<bool> stopFlag;

        std::atomic<uint32_t> discoveryRetryTimerId = 0;
        std::atomic<uint32_t> requestRetryTimerId = 0;
        uint32_t renewTimerId = 0;
        uint32_t rebindTimerId = 0;
        uint32_t expireTimerId = 0;

        std::atomic<bool> offered = false;
        std::atomic<bool> acked = false;
        std::atomic<bool> naked = false;
        double leaseStart = 0;
    };
}

#endif // DHCP_CLIENT_H
