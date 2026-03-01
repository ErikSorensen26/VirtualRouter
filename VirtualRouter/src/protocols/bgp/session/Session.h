// Session.h

#ifndef BGP_SESSION_H
#define BGP_SESSION_H

#include <span>
#include <cstdint>
#include <unordered_set>

#include "tcp/Connection.h"
#include "bgp/BgpTypes.hpp"
#include "configs/registry/router/BgpRegistry.h"

struct BgpHeader;
class PacketBuilder;

namespace BGP
{
class Neighbor;
class BgpProcess;

struct Capabilities
{
    std::unordered_set<AfiSafi> families;

    bool routeRefresh = false;
    bool enhancedRouteRefresh = false;
    bool asn32bit = false;
    uint32_t asn;
    bool extendedMessage = false;

    struct GracefulRestartFamily
    {
        AfiSafi family;
        bool forwardingStatePreserved;
    };

    bool gracefulRestart = false;
    bool restarting = false;
    uint16_t restartTime = 0;
    std::vector<GracefulRestartFamily> gracefulFamilies;

    struct LlgrFamily
    {
        AfiSafi family;
        uint32_t staleTime;
    };

    bool llgr = false;
    std::vector<LlgrFamily> llgrFamilies;

    struct AddPathFamily
    {
        AfiSafi family;
        uint8_t sendReceive;
    };

    bool addPath = false;
    std::vector<AddPathFamily> addPathFamilies;

    struct OrfEntry
    {
        AfiSafi family;
        uint8_t orfType;
        uint8_t sendReceive;
    };

    bool outboundRouteFiltering = false;
    std::vector<OrfEntry> orfEntries;

    struct ExtendedNextHop
    {
        AfiSafi family;
        uint16_t nextHopAfi;
    };

    bool extendedNextHop = false;
    std::vector<ExtendedNextHop> extendedNextHopEntries;

    bool multipleLabels = false;
    std::vector<AfiSafi> labeledFamilies;

    bool routeTargetConstraint = false;
    std::vector<AfiSafi> RtConstraintFamily;

    bool bgpsec = false;
    std::vector<AfiSafi> bgpsecFamilies;
};

class Session
{
public:
    Session(Neighbor& nbr, TCP::Connection& c) noexcept;
    Session(Neighbor& nbr) noexcept;

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    Session(Session&&) noexcept = delete;
    Session& operator=(Session&&) noexcept = delete;

    ~Session();

    void handleIncoming(std::span<uint8_t> data);

    bool established(); // TODO
    void setRid(uint32_t rid); // TODO
    uint32_t getRid(); // TODO
    Capabilities& capabilities(); // TODO
    bool isEbgp(); // TODO

    // Getters
    TCP::Connection& getConnection() { return connection; }
    const TCP::Connection& getConnection() const { return connection; }
    Config::BgpBaseRegistry& getConfigs() { return base; }
    const Config::BgpBaseRegistry& getConfigs() const { return base; }
    
    uint16_t holdTime;

private:

    Config::BgpBaseRegistry& base;
    TCP::Connection connection;
};
}

#endif // BGP_TRANSMISSION_H
