// DhcpServerBase.h

#ifndef DHCP_SERVER_BASE_H
#define DHCP_SERVER_BASE_H

#include <unordered_map>
#include <DhcpInfo.hpp>
#include <shared_mutex>
#include <PacketStructure.h>
#include <set>
#include <IPAddress.hpp>

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
            ClientID clientID;
            uint32_t transactionID;
            IPAddress resource;
            IPPrefix networkID;
            uint32_t timerID;
        };
    }
}


#endif // DHCP_SERVER_BASE_H
