// DhcpClient.h

#ifndef DHCP_CLIENT_H
#define DHCP_CLIENT_H

#include <ByteString.hpp>
#include <PacketStructure.h>
#include <DhcpInfo.hpp>
#include <condition_variable>

// Forward declarations
class ProcessPacket;
class DhcpClientTest;
class Interface;

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

#endif // DHCP_CLIENT_H
