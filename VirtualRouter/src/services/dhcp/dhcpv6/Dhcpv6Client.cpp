#include <Dhcpv6Client.h>
#include <Interface.h>
#include <InterfaceConfigs.h>

void Protocol::Dhcpv6Client::initiate()
{
    sendSolicit();
}

void Protocol::Dhcpv6Client::shutdown()
{
 
}

void Protocol::Dhcpv6Client::confirm(uint32_t iaid)
{

}

void Protocol::Dhcpv6Client::renew(uint32_t iaid)
{

}

void Protocol::Dhcpv6Client::rebind(uint32_t iaid)
{

}

void Protocol::Dhcpv6Client::release(uint32_t iaid)
{

}

void Protocol::Dhcpv6Client::decline(uint32_t iaid, const __uint128_t addr)
{

}

void Protocol::Dhcpv6Client::informationRequest(const std::vector<uint16_t>& oroOverride)
{

}

void Protocol::Dhcpv6Client::setDuid(ClientID& duid)
{

}

const ClientID& Protocol::Dhcpv6Client::getDuid() const
{

}

void Protocol::Dhcpv6Client::request(
    uint32_t iaid,
    const std::vector<Protocol::Dhcpv6::IAAddrRequest>& na,
    const std::vector<Protocol::Dhcpv6::IAAddrRequest>& ta,
    const std::vector<Protocol::Dhcpv6::IAPrefixRequest>& pd
)
{

}

void Protocol::Dhcpv6Client::sendSolicit(const Dhcpv6::IAOptions& ia)
{

}

void Protocol::Dhcpv6Client::sendRequest(const Dhcpv6::IAOptions& ia)
{

}

void Protocol::Dhcpv6Client::sendRenew(uint32_t iaid)
{

}

void Protocol::Dhcpv6Client::sendRebind(uint32_t iaid)
{

}

void Protocol::Dhcpv6Client::sendConfirm(uint32_t)
{

}

void Protocol::Dhcpv6Client::sendDecline(uint32_t iaid, __uint128_t addr)
{

}

void Protocol::Dhcpv6Client::sendRelease(uint32_t iaid)
{

}

void Protocol::Dhcpv6Client::sendInformationRequest(const std::vector<uint16_t>& oro)
{

}

void processAdvertisement(const Dhcpv6Header& dhcp, const TLV16BufferManager& options)
{

}

void processReply(const Dhcpv6Header& dhcp, const TLV16BufferManager& options)
{

}

void handleReconfigure(const Dhcpv6Header& dhcp, const TLV16BufferManager& options)
{

}

void verifyAuth(Dhcpv6Header& dhcp, TLV16Option* auth)
{

}

void buildAuthOption(Dhcpv6Header& dhcp, TLV16BufferManager& tlv)
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
