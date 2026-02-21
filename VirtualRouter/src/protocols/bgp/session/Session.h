// Session.h

#ifndef BGP_SESSION_H
#define BGP_SESSION_H

#include <span>
#include <cstdint>

#include "tcp/Connection.h"
#include "configs/registry/router/BgpRegistry.h"

struct BgpHeader;
class PacketBuilder;

namespace BGP
{
class Neighbor;
class BgpProcess;

struct Capabilities
{
    struct MpFamily
    {
        uint16_t afi;
        uint8_t safi;
    };
    std::vector<MpFamily> mpFamilies;

    bool routeRefresh = false;
    bool enhancedRouteRefresh = false;
    bool asn32bit = false;
    uint32_t asn;
    bool extendedMessage = false;

    struct GracefulRestartFamily
    {
        uint16_t afi;
        uint8_t safi;
        bool forwardingStatePreserved;
    };

    bool gracefulRestart = false;
    bool restarting = false;
    uint16_t restartTime = 0;
    std::vector<GracefulRestartFamily> gracefulFamilies;

    struct LlgrFamily
    {
        uint16_t afi;
        uint8_t safi;
        uint32_t staleTime;
    };

    bool llgr = false;
    std::vector<LlgrFamily> llgrFamilies;

    struct AddPathFamily
    {
        uint16_t afi;
        uint8_t safi;
        uint8_t sendReceive;
    };

    bool addPath = false;
    std::vector<AddPathFamily> addPathFamilies;

    struct OrfEntry
    {
        uint16_t afi;
        uint8_t safi;
        uint8_t orfType;
        uint8_t sendReceive;
    };

    bool outboundRouteFiltering = false;
    std::vector<OrfEntry> orfEntries;

    struct ExtendedNextHop
    {
        uint16_t nlriAfi;
        uint8_t nlriSafi;
        uint16_t nextHopAfi;
    };

    bool extendedNextHop = false;
    std::vector<ExtendedNextHop> extendedNextHopEntries;

    struct LabeledUnicastFamily
    {
        uint16_t afi;
        uint8_t safi;
    };

    bool multipleLabels = false;
    std::vector<LabeledUnicastFamily> labeledFamilies;

    struct RtConstraintFamily
    {
        uint16_t afi;
        uint8_t safi;
    };

    bool routeTargetConstraint = false;
    std::vector<RtConstraintFamily> RtConstraintFamily;

    struct BgpsecFamily
    {
        uint16_t afi;
        uint8_t safi;
    };

    bool bgpsec = false;
    std::vector<BgpsecFamily> bgpsecFamilies;
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

    // Getters
    Neighbor& getNeighbor() { return neighbor; }
    const Neighbor& getNeighbor() const { return neighbor; }
    TCP::Connection& getConnection() { return connection; }
    const TCP::Connection& getConnection() const { return connection; }
    Config::BgpBaseRegistry& getConfigs() { return base; }
    const Config::BgpBaseRegistry& getConfigs() const { return base; }
    
    uint16_t holdTime;

private:

    Config::BgpBaseRegistry& base;
    Neighbor& neighbor;
    TCP::Connection connection;
};
}

#endif // BGP_TRANSMISSION_H
