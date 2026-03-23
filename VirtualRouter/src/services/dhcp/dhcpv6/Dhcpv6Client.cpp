// Dhcpv6Client.cpp

#include "Dhcpv6Client.h"
#include "interface/Interface.h"

namespace services::dhcp
{

void Dhcpv6Client::initiate()
{

}

void Dhcpv6Client::shutdown()
{
 
}

void Dhcpv6Client::confirm(uint32_t iaid)
{

}

void Dhcpv6Client::renew(uint32_t iaid)
{

}

void Dhcpv6Client::rebind(uint32_t iaid)
{

}

void Dhcpv6Client::release(uint32_t iaid)
{

}

void Dhcpv6Client::decline(uint32_t iaid, const __uint128_t addr)
{

}

void Dhcpv6Client::informationRequest(const std::vector<uint16_t>& oroOverride)
{

}

void Dhcpv6Client::setDuid(ClientID& duid)
{

}

const ClientID& Dhcpv6Client::getDuid() const
{

}

void Dhcpv6Client::request(
    uint32_t iaid,
    const std::vector<IAAddrRequest>& na,
    const std::vector<IAAddrRequest>& ta,
    const std::vector<IAPrefixRequest>& pd
)
{

}

void Dhcpv6Client::sendSolicit(const IAOptions& ia)
{

}

void Dhcpv6Client::sendRequest(const IAOptions& ia)
{

}

void Dhcpv6Client::sendRenew(uint32_t iaid)
{

}

void Dhcpv6Client::sendRebind(uint32_t iaid)
{

}

void Dhcpv6Client::sendConfirm(uint32_t)
{

}

void Dhcpv6Client::sendDecline(uint32_t iaid, __uint128_t addr)
{

}

void Dhcpv6Client::sendRelease(uint32_t iaid)
{

}

void Dhcpv6Client::sendInformationRequest(const std::vector<uint16_t>& oro)
{

}

void processAdvertisement(const packet::Dhcpv6Header& dhcp, const packet::TLV16BufferManager& options)
{

}

void processReply(const packet::Dhcpv6Header& dhcp, const packet::TLV16BufferManager& options)
{

}

void handleReconfigure(const packet::Dhcpv6Header& dhcp, const packet::TLV16BufferManager& options)
{

}

void verifyAuth(packet::Dhcpv6Header& dhcp, packet::TLV16Option* auth)
{

}

void buildAuthOption(packet::Dhcpv6Header& dhcp, packet::TLV16BufferManager& tlv)
{

}

void updateIaTimers()
{

}

void scheduleT1T2(uint32_t iaid, uint32_t t1, uint32_t t2)
{

}

void cancelTimers(uint32_t iaid)
{

}

} // namespace services::dhcp
