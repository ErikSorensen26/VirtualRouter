/**
 * @file DhcpClient.h
 * @brief DHCPv4 client: lease acquisition, renewal, and release for a single interface.
 */

// DhcpClient.h

#ifndef DHCP_CLIENT_H
#define DHCP_CLIENT_H

#include "dhcp/DhcpInfo.hpp"
#include "packet/headers/DhcpHeader.hpp"
#include "packet/TlvOptions.hpp"

namespace interface { class Interface; }
namespace processing { class PacketBuilder; }

class DhcpClientTest;

namespace services::dhcp
{
/**
 * @brief DHCPv4 client state machine for one interface: discover/request/renew/rebind/release.
 * @ingroup SERVICES_DHCP_V4
 *
 * Drives the DORA exchange and subsequent lease lifecycle (T1/T2 renewal, rebind, expiry)
 * via timers scheduled through TimeManager. @c dhcpMutex serializes access to lease state
 * shared with callers outside the client's own send/receive path.
 */
class DhcpClient
{
public:
    friend class ProcessPacket;
    friend class DhcpClientTest;

    /**
     * @brief Binds a DhcpClient to @p currentInterface without starting the DORA exchange.
     * @param currentInterface Interface this client requests a lease for.
     * @param reduced If true, skips setup steps not needed by unit tests.
     */
    DhcpClient(interface::Interface* currentInterface, bool reduced = false);

    ~DhcpClient();

    void initiate();
    void shutdown();

    /**
     * @brief Build DHCPDISCOVER packet header + options.
     * @param builder processing::PacketBuilder to use.
     * @param mac Hardware address of client.
     * @param hostname Hostname of client.
     * @return true if successful, false if reservation failed.
     */
    bool buildDhcpDiscover(processing::PacketBuilder& builder, const uint8_t* mac, const std::string& hostname);

    /**
     * @brief Build DHCPREQUEST packet.
     * @param builder processing::PacketBuilder to use.
     * @param transID Transaction ID (xid).
     * @param hostname Hostname of client.
     * @param requestedIP IP address being requested.
     * @param serverID Server Identifier.
     * @return true if successful, false otherwise.
     */
    bool buildDhcpRequest(processing::PacketBuilder& builder, uint32_t transID, const std::string& hostname,
                          uint32_t requestedIP, uint32_t serverID);

    /**
     * @brief Build DHCPRELEASE packet.
     * @param builder processing::PacketBuilder to write into.
     * @return true if successful, false otherwise.
     */
    bool buildDhcpRelease(processing::PacketBuilder& builder);

    /**
     * @brief Build DHCPINFORM packet (requests DNS, etc.).
     * @param builder processing::PacketBuilder to write into.
     * @param hostname Hostname.
     * @param mac MAC address.
     * @return true if successful, false otherwise.
     */
    bool buildDhcpInform(processing::PacketBuilder& builder, const std::string& hostname, const uint8_t* mac);

    /**
     * @brief Called by external logic when a DHCP packet is received.
     * @param dhcp Parsed DHCP header for the inbound packet.
     */
    void handleDhcpPacket(const packet::DhcpHeader& dhcp);

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
    
    uint8_t getOpcode(std::vector<packet::TLV8Option>& options);

    bool processDhcpOffer(const packet::DhcpHeader& dhcp, std::vector<packet::TLV8Option>& options);
    bool processDhcpAck(const packet::DhcpHeader& dhcp, std::vector<packet::TLV8Option>& options);
    bool processDhcpNak(const packet::DhcpHeader& dhcp, std::vector<packet::TLV8Option>& options, bool isDecline = false);
    bool processDhcpDecline(const packet::DhcpHeader& dhcp, std::vector<packet::TLV8Option>& option);
    bool processDhcpInformAck(const packet::DhcpHeader& dhcp, std::vector<packet::TLV8Option>& option);
    void processOptionalOption(const std::vector<packet::TLV8Option>& opts);

    void scheduleLeaseTimers(uint32_t t1, uint32_t t2, uint32_t lease);
    void cancelLeaseTimers();

    void expireLease();

    void appendAuthOptions(packet::TLV8BufferManager& tlv, const packet::DhcpHeader& dhcp);
    bool validateAuthentication(const packet::DhcpHeader& dhcp, const uint8_t* value);

    interface::Interface* currentInterface;
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
} // namespace services

#endif // DHCP_CLIENT_H

