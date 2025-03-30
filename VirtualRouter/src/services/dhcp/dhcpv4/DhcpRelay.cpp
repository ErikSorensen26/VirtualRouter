#include "DhcpRelay.h"
#include <Interface.h>

Protocol::DhcpRelay::DhcpRelay(Interface* interface)
    : associatedInterface(interface)
{}

Protocol::DhcpRelay::~DhcpRelay() {}

void Protocol::DhcpRelay::addHelperAddress(const ByteString& helperAddress)
{
    std::lock_guard<std::mutex> lock(relayMutex);
    helperAddresses.push_back(helperAddress);
}

void Protocol::DhcpRelay::removeHelperAddress(const ByteString& helperAddress)
{
    std::lock_guard<std::mutex> lock(relayMutex);
    helperAddresses.erase(std::remove(helperAddresses.begin(), helperAddresses.end(), helperAddress), helperAddresses.end());
}

void Protocol::DhcpRelay::forwardToHelper(PacketInfo& packet)
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

void Protocol::DhcpRelay::forwardToClient(PacketInfo& packet)
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

void Protocol::DhcpRelay::modifyGiaddr(PacketInfo& packet)
{
    if (packet.Layer5.empty() || !std::holds_alternative<DhcpHeader>(packet.Layer5[0]))
    {
        return; // Invalid DHCP packet.
    }

    auto& dhcpHeader = std::get<DhcpHeader>(packet.Layer5[0]);
    if (associatedInterface->Get())
    {
        std::shared_lock<std::shared_mutex> lock(associatedInterface->Get()->ipMutex);
        dhcpHeader.relayAgentIP = associatedInterface->Get()->ipv4.ipAddress;
    }
}

ByteString Protocol::DhcpRelay::extractAddress(PacketInfo& packet) const
{
    if (packet.Layer5.empty() || !std::holds_alternative<DhcpHeader>(packet.Layer5[0]))
    {
        return {}; // Invalid or unsupported packet structure.
    }

    const auto& dhcpHeader = std::get<DhcpHeader>(packet.Layer5[0]);

    // Prefer 'yourClientIP' if available, otherwise fall back to 'clientIP'
    return !dhcpHeader.yourClientIP.empty() ? dhcpHeader.yourClientIP : dhcpHeader.clientIP;
}
