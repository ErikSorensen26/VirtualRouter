/*#include "DhcpRelay.h"
#include <Interface.h>

namespace services
{

protocol::DhcpRelay::DhcpRelay(interface::Interface* interface)
    : associatedInterface(interface)
{}

protocol::DhcpRelay::~DhcpRelay() {}

void protocol::DhcpRelay::addHelperAddress(const ByteString& helperAddress)
{
    std::lock_guard<std::mutex> lock(relayMutex);
    helperAddresses.push_back(helperAddress);
}

void protocol::DhcpRelay::removeHelperAddress(const ByteString& helperAddress)
{
    std::lock_guard<std::mutex> lock(relayMutex);
    helperAddresses.erase(std::remove(helperAddresses.begin(), helperAddresses.end(), helperAddress), helperAddresses.end());
}

void protocol::DhcpRelay::forwardToHelper(PacketInfo& packet)
{
    std::lock_guard<std::mutex> lock(relayMutex);
    
    if (helperAddresses.empty())
    {
        return; // No helper addresses configured.
    }

    modifyGiaddr(packet);

    for (const auto& helperAddress : helperAddresses)
    {
        if (std::holds_alternative<IPv4Header>(packet.Layer3.front()))
        {
            if (auto *ipv4Header = &(std::get<IPv4Header>(packet.Layer3.front())))
            {
                ipv4Header->destinationAddress = helperAddress;
                associatedInterface->enqueuePacket(packet);
            }
        }
    }
}

void protocol::DhcpRelay::forwardToClient(PacketInfo& packet)
{
    if (std::holds_alternative<IPv4Header>(packet.Layer3.front()))
    {
        if (auto *ipv4Header = &(std::get<IPv4Header>(packet.Layer3.front())))
        {
            ipv4Header->destinationAddress = extractAddress(packet);
            associatedInterface->enqueuePacket(packet);
        }
    }
}

void protocol::DhcpRelay::modifyGiaddr(PacketInfo& packet)
{
    if (packet.Layer5.empty() || !std::holds_alternative<DhcpHeader>(packet.Layer5[0]))
    {
        return; // Invalid DHCP packet.
    }

    auto& dhcpHeader = std::get<DhcpHeader>(packet.Layer5[0]);
    dhcpHeader.relayAgentIP = associatedInterface->configs.ipv4.getAddressInt();
}

ByteString protocol::DhcpRelay::extractAddress(PacketInfo& packet) const
{
    if (packet.Layer5.empty() || !std::holds_alternative<DhcpHeader>(packet.Layer5[0]))
    {
        return {}; // Invalid or unsupported packet structure.
    }

    const auto& dhcpHeader = std::get<DhcpHeader>(packet.Layer5[0]);

    // Prefer 'yourClientIP' if available, otherwise fall back to 'clientIP'
    return !dhcpHeader.yourClientIP.empty() ? dhcpHeader.yourClientIP : dhcpHeader.clientIP;
}*/
