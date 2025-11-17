// EigrpTypes.hpp

#ifndef EIGRP_TYPES_HPP
#define EIGRP_TYPES_HPP

#include <shared_mutex>
#include <unordered_set>
#include <TimeManager.h>
#include <TopologyTable.h>

namespace Eigrp
{
struct RouteInfo;
}

/**
 * @namespace EigrpConfigs
 * Namespace containing configuration structs and enums for the EIGRP protocol.
 */
namespace EigrpConfigs
{
    struct Neighbor;
    struct KValue 
    {
        KValue(uint8_t k1 = 1, uint8_t k2 = 0, uint8_t k3 = 1, uint8_t k4 = 0, uint8_t k5 = 0, uint8_t k6 = 0)
            : k1_Bandwidth(k1), k2_Load(k2), k3_Delay(k3), k4_Reliability(k4), k5_MTU(k5), k6_Power(k6) {}

        uint8_t k1_Bandwidth;   ///< Weight for bandwidth.
        uint8_t k2_Load;        ///< Weight for load.
        uint8_t k3_Delay;      ///< Weight for delay.
        uint8_t k4_Reliability; ///< Weight for reliability.
        uint8_t k5_MTU;        ///< Weight for MTU.
        uint8_t k6_Power;       ///< Weight for power.
    };

    struct StubConfig
    {
        bool isStub = false;                ///< Indicates if Stub routing is enabled.
        bool advertiseConnected = true;     ///< Advertise connected routes.
        bool advertiseLeakMap = true;       ///< Advertise leak-map routes.
        bool advertiseStatic = true;        ///< Advertise static routes.
        bool advertiseSummary = true;       ///< Advertise summary routes.
        bool advertiseRedistributed = true; ///< Advertise redistributed routes.
        bool receiveOnly = true;            ///< Only receive routes.

        /**
         * @brief Default constructor.
         */
        StubConfig() = default;
        
        /**
         * @brief Parameterized constructor for StubConfig.
         * @param stub Indicates if Stub routing is enabled.
         * @param conn Advertise connected routes.
         * @param stat Advertise static routes.
         * @param summ Advertise summary routes.
         * @param redis Advertise redistributed routes.
         */
        StubConfig(bool stub, bool conn, bool stat, bool summ, bool redis)
            : isStub(stub), advertiseConnected(conn), advertiseStatic(stat),
              advertiseSummary(summ), advertiseRedistributed(redis) {}
    };

    /**
     * @enum AuthType
     * @brief Represents the type of authentication used.
     */
    enum class AuthType : uint8_t
    {
        NONE = 0x00,
        MD5 = 0x02,
        SHA1 = 0x03,
        SHA256 = 0x04,
        SHA384 = 0x05,
        SHA512 = 0x06
    };

    /**
     * @struct AuthKey
     * @brief Represents an authentication key.
     */
    struct AuthKey
    {
        std::atomic<bool> fullyEnabled = false; ///< Indicates if authentication is enabled.
        uint8_t keyId;              ///< Identifier for the authentication key.
        std::string key;             ///< The authentication key.
        AuthType authType = AuthType::NONE; ///< Type of authentication.
        std::atomic<uint32_t> replay;
        std::atomic<uint32_t> lastReplay;
    };

    /**
     * @enum TrafficShareMode
     * @brief Defines the traffic sharing mode in EIGRP.
     */
    enum class TrafficShareMode
    {
        Balanced, ///< Balanced traffic sharing.
        Minimum   ///< Minimum traffic sharing.
    };

    /**
     * @enum Mode
     * @brief Defines the interface mode for EIGRP.
     */
    enum class Mode
    {
        POINT_TO_POINT, ///< Point-to-point interface mode.
        MULTIPOINT      ///< Multipoint interface mode.
    };

    /**
     * @enum UpdateType
     * @brief Defines types of EIGRP updates.
     */
    enum class UpdateType
    {
        FULL,            ///< Full update.
        QUERY,           ///< Query update.
        RESPONSE_QUERY,  ///< Response to a query.
        PARTIAL,         ///< Partial update.
        TRIGGERED,       ///< Triggered update.
    };

    /**
     * @enum EigrpMode
     * @brief Defines the EIGRP operational mode.
     */
    enum class EigrpMode
    {
        NAMED,    ///< Named EIGRP mode.
        CLASSIC   ///< Classic EIGRP mode.
    };

    /**
     * @struct Network
     * @brief Represents a network with its IP address and subnet mask.
     */
    struct Network 
    {
        Network(AddressFamily family) : af(family) {}
        IPAddress ip;    ///< IP address of the network.
        uint8_t mask;  ///< Subnet mask of the network.
        AddressFamily af;

        bool operator==(const Network& other) const
        {
            return ip == other.ip && mask == other.mask;
        }
    };

    /**
     * @struct EigrpConfigs
     * @brief Configuration settings for the EIGRP process.
     */
    struct EigrpConfigs
    {
        std::shared_mutex configsMutex;
        std::atomic<uint8_t> maxPaths = 4; ///< Maximum number of equal-cost paths.
        std::atomic<uint8_t> maxHops = 100; ///< Maximum hops for path.
        std::atomic<uint8_t> TOS = 0; ///< Type of service, should remain 0.
        std::atomic<uint8_t> adminDistance = 90; ///< Administrative distance for internal routes.
        std::atomic<uint8_t> externalAdminDistance = 170; ///< Administrative distance for external routes.
        std::atomic<uint8_t> variance = 1; ///< Variance for unequal-cost load balancing.
        std::atomic<uint8_t> trafficShare = 0; ///< Traffic sharing mode.
        std::atomic<uint8_t> ribScale = 128; ///< Rib scale for metric when adding to RIB. //TODO
        std::atomic<uint8_t> dampeningInterval = 75; ///< Dampening interval for route dampening.
        std::atomic<uint16_t> dampeningResetTime = 0; ///< Reset time for dampening.
        std::atomic<uint16_t> dampeningRestart = 0; ///< Restart time for dampening.
        std::atomic<uint16_t> dampeningRestartCount = 1; ///< Restart count for dampening.
        std::atomic<uint16_t> routeDelTimer = 120; ///< Route unreachable hold timer before deletion.
        std::atomic<uint16_t> stuckInActiveTime = 90; ///< Stuck-in-active time in seconds.
        std::atomic<uint16_t> purgeTime = 240; ///< Purge time for nsf mode with graceful restarts.
        std::atomic<uint32_t> redistributionMetricOffset = 0; ///< Metric offset for redistribution. //TODO
        std::atomic<uint32_t> wideMetric = 10000000; ///< Wide metric setting.
        std::atomic<uint32_t> eventLogSize = 500; //< Event log size for eigrp.
        std::atomic<uint32_t> maximumPrefix = 0; ///< Max number of prefixes that will be accepted.
        std::atomic<bool> logNeighborChanges = true; ///< Enable logging of neighbor changes.
        std::atomic<bool> logNeighborWarnings = false; ///< Enable logging of neighbor warnings.
        std::atomic<bool> advertiseDefault = false; ///< Advertise default route.
        std::atomic<bool> activeTimerEnabled = true; ///< Enable active timers.
        std::atomic<bool> autoSummarizationEnabled = false; ///< Enable auto-summarization.
        std::atomic<bool> nonStopForwarding = false; ///< Enable non-stop-forwarding.
        std::atomic<bool> activeDisabled = false; ///< Disables active routes from becoming stuck in active.
        std::atomic<bool> routingMulticast = false; ///< Indicates if multicast is being routed.
        std::atomic<bool> dampening = false; ///< Indicates that dampening is enabled.
        std::atomic<bool> dampeningWarnings = false; ///< Show warning when dampening limit is reached.
        std::vector<Network> networks; ///< List of configured networks.
        std::unordered_set<uint32_t> passiveInterfaces; ///< List of all passive interfaces.
        std::unordered_map<uint32_t, std::unordered_set<IPAddress>> unicastNeighbors; ///< Manually defined unicast neighbors.
        std::atomic<TrafficShareMode> trafficShareMode = TrafficShareMode::Balanced; ///< Traffic sharing mode.
        KValue kvalue; ///< K-values for metric calculation.
        StubConfig stubConfig; ///< Stub routing configuration.
        KValue defaultMetrics; ///< Default metrics.
    };

    /**
     * @struct InterfaceConfigs
     * @brief Configuration settings for an EIGRP interface.
     */
    struct InterfaceConfigs
    {
    private:
        InterfaceConfigs() = default;
    public:
        ~InterfaceConfigs() = default;

        InterfaceConfigs(uint32_t key) : key(key) {}
        uint32_t key;
        bool shutdown = false;
        bool userMade = false;
        mutable std::shared_mutex configsMutex;
        std::vector<IPPrefix> pendingSummaryRoutes;
        std::atomic<uint8_t> DSCP = 0; ///< Differentiated Services Code Point.
        std::atomic<uint8_t> interfaceMask; ///< Interface subnet mask.
        std::atomic<uint8_t> dampeningChange = 1; ///< Number of prefix changes that triggers dampening.
        std::atomic<uint16_t> dampeningInterval = 5; /// Interval the interface will check for changed routes.
        std::atomic<uint16_t> helloTime = 5; ///< Hello interval in seconds.
        std::atomic<uint16_t> holdTime = 15; ///< Hold time in seconds.
        std::atomic<uint32_t> bandwidthPercentage = 50; ///< Bandwidth percentage to use.
        std::atomic<bool> splitHorizon = true; ///< Enable split horizon.
        std::atomic<bool> nextHopSelf = false; ///< Enable next hop self.
        std::atomic<bool> isPassive = false; ///< Enable passive mode.
        std::atomic<bool> multicastEnabled = true; ///< Indicates if multicast is enabled on this interface.
        std::atomic<Mode> interfaceMode = Mode::MULTIPOINT; ///< Interface mode.
        std::atomic<bool> dampeningIntervalConfigured = false; ///< Indicates whether dampening interval is configured on the interface.
        std::atomic<uint64_t> localMetric; ///< Local metric of the interface.
        std::atomic<bool> noEcmpMode = false; ///< No ECMP mode used for VPNs. //TODO
        AuthKey authKey; ///< Authentication key.

        /**
         * @brief used to see if configs are defaulted
         *
         * Used for classic mode to detect default configurations so
         * the configuration object can be removed if its not being used.
         */
        bool isDefault() const {
            InterfaceConfigs other;
            std::shared_lock<std::shared_mutex> lock(configsMutex);
            return 
                pendingSummaryRoutes.empty() &&
                helloTime.load() == other.helloTime.load() &&
                holdTime.load() == other.holdTime.load() &&
                bandwidthPercentage.load() == other.bandwidthPercentage.load() &&
                splitHorizon.load() == other.splitHorizon.load() &&
                nextHopSelf.load() == other.nextHopSelf.load() &&
                dampeningChange.load() == other.dampeningChange.load() &&
                dampeningInterval.load() == other.dampeningInterval.load() &&
                authKey.authType == other.authKey.authType && 
                authKey.fullyEnabled.load() == other.authKey.fullyEnabled.load() &&
                authKey.key == other.authKey.key &&
                authKey.keyId == other.authKey.keyId;
        }
    };
}

#endif // EIGRP_TYPES_HPP
