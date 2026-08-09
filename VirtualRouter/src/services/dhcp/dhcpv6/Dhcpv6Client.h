/**
 * @file Dhcpv6Client.h
 */

// Dhcpv6Client.h

#ifndef DHCPV6_CLIENT_H
#define DHCPV6_CLIENT_H

#include "Dhcpv6.h"
#include "dhcp/DhcpInfo.hpp"

namespace interface { class Interface; }
namespace processing { class PacketBuilder; }
namespace packet { struct TLV16Option; struct Dhcpv6Header; class TLV16BufferManager; class ProcessPacket; }

namespace services::dhcp
{
struct ClientConfigs
{
    std::unordered_map<uint32_t, std::string> authKeys;
    std::atomic<uint16_t> minimumRefreshTime;

    struct ClientPrefix
    {
        bool rapidCommit = false;
        types::IPv6Prefix hint;
    };
    std::unordered_map<std::string, ClientPrefix> prefixes;

    std::atomic<bool> requestVendorOpt;

    std::vector<uint16_t> oro;

    std::atomic<uint8_t> leaseQueryRetryCount;
    std::atomic<uint8_t> leaseQueryRetryTime;
    std::atomic<__uint128_t> leaseQueryServer;
};

struct IAAddress
{
    __uint128_t address;
    uint32_t preferredLifetime;
    uint32_t validLifetime;
    bool declined = false;
    bool requested = false;
};

struct IAPrefix
{
    types::IPv6Prefix prefix;
    uint32_t preferredLifetime;
    uint32_t validLifetime;
    bool requested = false;
};

struct IdentityAssociation
{
    IAType type;
    uint32_t iaid;
    uint32_t t1 = 0;
    uint32_t t2 = 0;
    std::vector<IAAddress> addresses;
    std::vector<IAPrefix> prefixes;
    std::chrono::steady_clock::time_point expiration;
    bool requested = false;

    ClientID serverDuid;
};

struct IAAddrRequest
{
    uint32_t iaid;
    std::vector<IAAddress> addrs;
};

struct IAPrefixRequest
{
    uint32_t iaid;
    std::vector<IAPrefix> prefixes;
};

struct IAOptions
{
    uint32_t iaid;
    std::vector<IAAddrRequest> nas;
    std::vector<IAAddrRequest> tas;
    std::vector<IAPrefixRequest> pds;
};

    /**
     * @brief Represents a DHCPv6 client.
     *
     * Handles sending DHCPv6 SOLICIT and REQUEST messages and processing responses.
     */
class Dhcpv6Client 
{
public:
    friend class ProcessPacket;

    /**
     * @brief Constructs a Dhcpv6Client bound to a specific interface.
     *
     * @param currentInterface Interface this client requests addresses for.
     */
    Dhcpv6Client(interface::Interface* currentInterface);

    /**
     * @brief Destructor to clean up threads and resources.
     */
    ~Dhcpv6Client();

    void initiate();
    void shutdown();

    void confirm(uint32_t iaid);
    void renew(uint32_t iaid);
    void rebind(uint32_t iaid);
    void release(uint32_t iaid);
    void decline(uint32_t iaid, const __uint128_t addr);

    void informationRequest(const std::vector<uint16_t>& oroOverride = {});

    void setDuid(ClientID& duid);
    const ClientID& getDuid() const;

    void request(
        uint32_t iaid,
        const std::vector<IAAddrRequest>& na,
        const std::vector<IAAddrRequest>& ta,
        const std::vector<IAPrefixRequest>& pd
    );

private:
    interface::Interface& iface;
    std::mutex dhcpMutex;

    DhcpInfo configs;

    std::unordered_map<uint32_t, IdentityAssociation> iaMap;
    std::unordered_map<uint32_t, uint32_t> activeTimers;

    void sendSolicit(const IAOptions& ia);
    void sendRequest(const IAOptions& ia);
    void sendRenew(uint32_t iaid);
    void sendRebind(uint32_t iaid);
    void sendConfirm(uint32_t);
    void sendDecline(uint32_t iaid, __uint128_t addr);
    void sendRelease(uint32_t iaid);
    void sendInformationRequest(const std::vector<uint16_t>& oro);

    void processAdvertisement(const packet::Dhcpv6Header& dhcp, const packet::TLV16BufferManager& options);
    void processReply(const packet::Dhcpv6Header& dhcp, const packet::TLV16BufferManager& options);
    void handleReconfigure(const packet::Dhcpv6Header& dhcp, const packet::TLV16BufferManager& options);

    void verifyAuth(packet::Dhcpv6Header& dhcp, packet::TLV16Option* auth);
    void buildAuthOption(packet::Dhcpv6Header& dhcp, packet::TLV16BufferManager& tlv);

    void updateIaTimers();
    void scheduleT1T2(uint32_t iaid, uint32_t t1, uint32_t t2);
    void cancelTimers(uint32_t iaid);

    IdentityAssociation getOrCreateIA(uint32_t iaid, IAType type);
};
} // namespace services::dhcp

#endif //DHCPV6_CLIENT_H

