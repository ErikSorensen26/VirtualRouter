// EigrpTypes.hpp

#ifndef EIGRP_TYPES_HPP
#define EIGRP_TYPES_HPP

#include <RoutingTable.h>
#include <shared_mutex>
#include <unordered_set>
#include <TimeManager.h>

/**
 * @namespace EigrpConfigs
 * Namespace containing configuration structs and enums for the EIGRP protocol.
 */
namespace EigrpConfigs
{
    struct NeighborInfo;
    
    /**
     * @struct RouterID
     * @brief Represents the Router ID configuration.
     */
    struct RouterID
    {
        uint8_t ID[4] = { 0x00, 0x00, 0x00, 0x00 }; ///< Router ID.
        bool isStatic = false;                 ///< Indicates if the Router ID is static.
    };

    /**
     * @struct RoutingUpdate
     * @brief Represents a routing update.
     */
    struct RoutingUpdate
    {
        RoutingTable::Eigrp* route;
        bool withdraw;
    };

    /**
     * @struct OutgoingQuery
     * @brief holds information on an active query.
     */ 
    struct OutgoingQuery
    {
        uint32_t sequenceNumber; ///< Sequence assigned for outgoing query.
        uint32_t siaTimerId = 0; ///< ID of the SIA timer for this neighbor.
        uint32_t retries = 0; ///< Number of SIA-Query retries if needed.
        std::chrono::steady_clock::time_point lastSIARefreshTime; ///< Time of last SIA activity.
    };

    /**
     * @struct ActiveRoute
     * @brief holds information on an active route.
     */
    struct ActiveRoute
    {
        RoutingTable::Eigrp* route; ///< The actual route.
        IPAddress originNeighbor; ///< Who started this query.
        uint32_t sequenceNumber; /// < Sequence assigned when you flooded query.
        std::unordered_map<IPAddress, OutgoingQuery> pendingQueries; ///< Per-neighbor tracking
        std::vector<std::tuple<NeighborInfo*, IPAddress, RoutingTable::Eigrp*, bool>> feasibleRoutes;
    };

    /**
     * @struct KValue
     * @brief Represents the K-values used in EIGRP metric calculation.
     */
    struct KValue 
    {
        KValue(uint8_t k1 = 1, uint8_t k2 = 0, uint32_t k3 = 1, uint8_t k4 = 0, uint16_t k5 = 0, uint8_t k6 = 0)
            : k1_Bandwidth(k1), k2_Load(k2), k3_Delay(k3), k4_Reliability(k4), k5_MTU(k5), k6_Power(k6) {}

        uint8_t k1_Bandwidth;   ///< Weight for bandwidth.
        uint8_t k2_Load;        ///< Weight for load.
        uint32_t k3_Delay;      ///< Weight for delay.
        uint8_t k4_Reliability; ///< Weight for reliability.
        uint16_t k5_MTU;        ///< Weight for MTU.
        uint8_t k6_Power;       ///< Weight for power.
    };

    /**
     * @brief Metric
     * @brief Represents the local metric along with all of the metric values.
     */
    struct Metric
    {
        Metric(uint32_t lm, uint32_t bw, uint8_t ld, uint32_t dl, uint8_t ry)
            : localMetric(lm), bandwidth(bw), load(ld), delay(dl), reliability(ry) {}

        uint32_t localMetric;
        uint32_t bandwidth;
        uint8_t load;
        uint32_t delay;
        uint8_t reliability;

        Metric() {}
    };

    /**
     * @struct SummaryRoute
     * @brief Represents a summary route configuration.
     */
    struct SummaryRoute 
    {
        RoutingTable::Eigrp* summary;
        bool isAuto = false;///< Indicates if the summary route is automatically generated.
    };

    /**
     * @struct SummaryMetric
     * @brief manual summary metrics for a route
     */
    struct SummaryMetric
    {
        uint8_t network[16];
        uint8_t mask;
        KValue kvalue;
        uint8_t adminDistance = 0;
    };

    /**
     * @struct StubConfig
     * @brief Configuration for Stub routing in EIGRP.
     */
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
     * @enum NeighborState
     * @brief Represents the state of an EIGRP neighbor.
     */
    enum class NeighborState
    {
        DOWN,       ///< Neighbor is down.
        INIT,       ///< Initialization state.
        TWOWAY,     ///< Two-Way communication established.
        EXSTART,    ///< Exchange start state.
        EXCHANGE,   ///< Exchange routing state.
        LOADING,    ///< Loading routing state.
        ESTABLISHED ///< Adjacency fully formed.
    };

    /**
     * @enum InitRole
     * @brief Represents the role (MASTER/SLAVE) during neighbor initialization.
     */
    enum class InitRole
    {
        MASTER, ///< Master role.
        SLAVE   ///< Slave role.
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
     * @struct NeighborInfo
     * @brief Contains comprehensive information and state management for an EIGRP neighbor.
     *
     * This structure manages neighbor initialization, communication state, packet handling,
     * timers, acknowledgements, authentication, and thread operations required to maintain
     * EIGRP neighbor relationships.
     */
    struct NeighborInfo 
    {
        ~NeighborInfo()
        {
            clearTimers();
        }

        TimeManager& timeManager;

        void clearTimers()
        {
            // Cancel timers
            if (stuckInInitCheckActive)
            {
                timeManager.cancelTimer(stuckInInitTimerId);
            }

            if (twoWayThreadID != 0)
            {
                timeManager.cancelTimer(twoWayThreadID.load(std::memory_order_relaxed));
            }

            if (holdTimerId != 0)
            {
                timeManager.cancelTimer(holdTimerId);
            }

            if (gracefulRestartTimerId != 0)
            {
                timeManager.cancelTimer(gracefulRestartTimerId);
            }

            for (auto& relPkt : reliablePackets)
            {
                if (relPkt.second.timerId != 0)
                {
                    timeManager.cancelTimer(relPkt.second.timerId);
                }
            }
        }

        void restart()
        {
            {
                std::lock_guard<std::mutex> lock(initFlagMutex);
                initFlags.initUpdateReceived = false;
                initFlags.nullSent = false;
                initFlags.slaveInit = false;
                initFlags.masterInit = false;
                initFlags.initRole = InitRole::MASTER;
            }
            neighborState.store(NeighborState::DOWN);
            initComplete.store(false, std::memory_order_release);
            initSequence.store(0, std::memory_order_release);
            nullUpdateSequence.store(0, std::memory_order_release);
            stuckInInitCheckActive.store(false, std::memory_order_release);
            secondHelloReceived.store(false, std::memory_order_release);
        }

        // Basic Neighbor Information
        IPAddress ipAddress; ///< Neighbor's IP address.
        uint8_t macAddress[6]; ///< Neighbor's MAC address.
        uint32_t routerID; ///< Neighbor's RouterID.

        // Initialization
        std::atomic<NeighborState> neighborState = NeighborState::DOWN; ///< Current state of the neighbor.

        std::mutex initFlagMutex;
        struct InitFlags {
            InitRole initRole = InitRole::MASTER; ///< Initialization role (MASTER/SLAVE).
            bool initUpdateReceived =   false; ///< Inidcates if an initialization update has been received
            bool nullSent =             false; ///< Indicates if a null update has been sent.
            bool slaveInit =            false; ///< Indicates if the neighbor is in slave initialization.
            bool masterInit =           false; ///< Indicates if the nieghbor is in master initialization.
        } initFlags;

        std::atomic<bool> initComplete = false; ///< Indicates if neighbor initialization is complete.
        std::atomic<bool> processAcks = false; ///< Indicates if ACKs should be processed.

        std::atomic<uint32_t> initSequence{0}; ///< Initialization sequence number.
        std::atomic<uint32_t> nullUpdateSequence{0}; ///< Last sequence number for received for null update.


        std::chrono::steady_clock::time_point initStartTime; ///< Init start time.
        std::atomic<bool> stuckInInitCheckActive = false; ///< Indicating if neighbor is stuck initializing.
        std::atomic<uint32_t> stuckInInitTimerId;

        std::condition_variable twoWayCV; ///< TWOWAY conditional variable for managing state transition.
        std::atomic<uint32_t> twoWayThreadID; ///< Thread used for TWOWAY state transition.
        std::atomic<bool> secondHelloReceived = false; ///< Indicates when a second hello is received.

        // Packet Handling
        /**
         * @struct PacketBuffer
         * @brief Represents a buffer for packets received from the neighbor.
         */
        struct PacketBuffer 
        {
            IPAddress neighborIp;  ///< IP address of the neighbor.
            EigrpHeader eigrp;      ///< EIGRP packet header. //TODO handle packet buffer
        };
        std::unordered_map<uint32_t, PacketBuffer> packetBuffer; ///< Buffer for packets from neighbors.
        std::mutex bufferMutex;

        // Acknowledgements
        std::unordered_set<uint32_t> pendingAcks; ///< List of pending ACKs.

        // RTT (Route-Trip Time) Estimation
        double srtt = 1.0; ///< Smoothed RTT
        double rttvar = 0.5; //< RTT variance
        double rto = 1.5; ///< Retransmission timeout

        std::atomic<uint64_t> siaFailures = 0; ///< Amount of stuck-in-active failures.

        // Timers
        std::atomic<uint32_t> holdTimerId = 0; ///< Hold timer ID.
        std::atomic<uint16_t> holdTime; ///< Hold time in seconds.
        std::chrono::steady_clock::time_point lastHeard; ///< Last heard time point.
        std::unordered_map<uint32_t, uint32_t> retransmissionTimers; ///< Map of sequence numbers to retransmission timer IDs.

        // Synchronization
        std::shared_mutex neighborDataMutex; ///< Protects neighbor-specific data.
        std::condition_variable cv; ///< Condition variable for synchronization

        /**
         * @struct ReliablePacketInfo
         * @brief Information about reliable packets sent to the neighbor.
         */
        struct ReliablePacketInfo 
        {
            /**
             * @struct Packet
             * @brief Represents a reliable packet.
             */
            struct Packet {
                /**
                 * @brief Constructs a Packet with specified parameters.
                 * @param eigrp EIGRP packet header.
                 * @param destination Destination IP address.
                 * @param routes Updated routes included in the packet.
                 * @param isRemove Indicates if the packet is for route removal.
                 */
                Packet(AddressFamily af, const uint8_t* eigrp, size_t eigrpSize, const IPAddress dest, std::vector<RoutingUpdate> routes = {}, bool isRemove = false)
                    : updatedRoutes(routes) 
                {
                    destination = dest;
                }
                Packet() = default;

                uint8_t headerBuffer[1540];
                size_t headerSize;
                IPAddress destination; ///< Destination IP address.
                std::vector<RoutingUpdate> updatedRoutes{}; ///< Updated routes in the packet.
            };

            Packet packet; ///< Reliable packet information.
            std::chrono::steady_clock::time_point sendTime; ///< Time the packet was sent.
            uint8_t retransmissionCount = 0; ///< Number of retransmissions.
            uint32_t timerId; ///< Timer ID for retransmission.

            /**
             * @brief Default constructor.
             */
            ReliablePacketInfo() = default;

            /**
             * @brief Constructs a ReliablePacketInfo with a specified packet.
             * @param packet Reliable packet information.
             */
            ReliablePacketInfo(Packet packet) : packet(packet) {}

            // Default copy constructor and copy assignment operator
            ReliablePacketInfo(const ReliablePacketInfo&) = default;
            ReliablePacketInfo& operator=(const ReliablePacketInfo&) = default;

            // Default move constructor and move assignment operator
            ReliablePacketInfo(ReliablePacketInfo&&) = default;
            ReliablePacketInfo& operator=(ReliablePacketInfo&&) = default;
        };
        std::unordered_map<uint32_t, ReliablePacketInfo> reliablePackets; ///< Map of sequence numbers to reliable packets.
        std::mutex reliableMutex;

        /**
         * @struct AdvertisedRoute
         * @brief Tracks advertised routes and their states.
         */
        struct AdvertisedRoute
        {
            RoutingTable::Eigrp* route;///< Advertised route information.
            bool active;               ///< Indicates if the route is currently active.
            bool pendingUpdate;        ///< Indicates if there is a pending update for the route.
            bool removePending;        ///< Indicates if the route is pending removal (Withdraw).
        };

        // Routing Updates
        std::unordered_map<IPPrefix, AdvertisedRoute> advertisedRoutes; ///< Map of advertised routes.

        // Neighbor Flags
        bool unicast = false; ///< Indicates if the neighbor is unicast.
        std::atomic<bool> hasMac = false; ///< Indicates if MAC address is known.
        std::atomic<bool> isInit = false; ///< Initialization flag.
        std::atomic<bool> isGracfullyRestarting = false; ///< Gracefully restarting.
        std::atomic<uint32_t> gracefulRestartTimerId = 0; ///< Graceful restart timer ID.
        std::atomic<uint32_t> lastReceivedSequenceNumber = 0; ///< Last received sequence number.
        std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> missingPacketTimestamps; ///< Timestamps for missing packets.
    
        /**
         * @brief Default constructor.
         * 
         * @param unicast 
         */
        NeighborInfo(AddressFamily af, TimeManager& manager, const IPAddress& neighborIp, bool unicast = false) : timeManager(manager), unicast(unicast)
        {
            ipAddress = neighborIp;
        }

        // Delete copy constructor and copy assignment operator
        NeighborInfo(const NeighborInfo&) = delete;
        NeighborInfo& operator=(const NeighborInfo&) = delete;
    
        // Delete move constructor and move assignment operator
        NeighborInfo(NeighborInfo&&) = delete;
        NeighborInfo& operator=(NeighborInfo&&) = delete;
    };

    /**
     * @struct NetworksDistributed
     * @brief Represents distributed networks in EIGRP.
     */
    struct NetworksDistributed
    {
        RoutingTable::Eigrp* route; ///< Route information.
        bool distrubuted = false; ///< Indicates if the route has been distributed.
        
    };

    /**
     * @struct EigrpConfigs
     * @brief Configuration settings for the EIGRP process.
     */
    struct nextHopSelf
    {
        std::shared_mutex configsMutex;
        std::atomic<uint8_t> maxPaths = 4; ///< Maximum number of equal-cost paths.
        std::atomic<uint8_t> maxHops = 100; ///< Maximum hops for path. // TODO
        std::atomic<uint8_t> TOS = 0; ///< Type of service, should remain 0.
        std::atomic<uint8_t> adminDistance = 90; ///< Administrative distance for internal routes.
        std::atomic<uint8_t> externalAdminDistance = 170; ///< Administrative distance for external routes.
        std::atomic<uint8_t> variance = 1; ///< Variance for unequal-cost load balancing.
        std::atomic<uint8_t> trafficShare = 0; ///< Traffic sharing mode.
        std::atomic<uint8_t> dampeningInterval = 75; ///< Dampening interval for route dampening.
        std::atomic<uint8_t> ribScale = 128; ///< Rib scale for metric when adding to RIB. //TODO
        std::atomic<uint16_t> dampeningResetTime = 0; ///< Reset time for dampening.
        std::atomic<uint16_t> dampeningRestart = 0; ///< Restart time for dampening.
        std::atomic<uint16_t> dampeningRestartCount = 1; ///< Restart count for dampening.
        std::atomic<uint16_t> activeTime = 180; ///< Active time in seconds.
        std::atomic<uint16_t> stuckInActiveTime = 90; ///< Stuck-in-active time in seconds.
        std::atomic<uint16_t> purgeTime = 240; ///< Purge time for nsf mode with graceful restarts.
        std::atomic<uint32_t> redistributionMetricOffset = 0; ///< Metric offset for redistribution. //TODO
        std::atomic<uint32_t> wideMetric = 10000000; ///< Wide metric setting.
        std::atomic<uint32_t> eventLogSize = 500; //< Event log size for eigrp.
        std::atomic<uint32_t> lowestBandwidth = std::numeric_limits<uint32_t>::max(); ///< Holds the lowest bandwidth on all interfaces.
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
        std::vector<std::pair<IPAddress, uint8_t>> pendingSummaryRoutes;
        std::vector<SummaryRoute> summaryRoutes; ///< List of summary routes.
        std::atomic<uint8_t> DSCP = 0; ///< Differentiated Services Code Point.
        std::atomic<uint8_t> interfaceMask; ///< Interface subnet mask.
        std::atomic<uint8_t> dampeningChange = 1; ///< Number of prefix changes that triggers dampening. //TODO
        std::atomic<uint16_t> dampeningInterval = 5; /// Interval the interface will check for changed routes.
        std::atomic<uint16_t> helloTime = 5; ///< Hello interval in seconds.
        std::atomic<uint16_t> holdTime = 15; ///< Hold time in seconds.
        std::atomic<uint32_t> bandwidthPercentage = 50; ///< Bandwidth percentage to use.
        std::atomic<bool> splitHorizon = true; ///< Enable split horizon.
        std::atomic<bool> nextHopSelf = false; ///< Enable next hop self.
        std::atomic<bool> isPassive = false; ///< Enable passive mode.
        std::atomic<bool> multicastEnabled = true; ///< Indicates if multicast is enabled on this interface.
        std::unordered_map<uint32_t, uint32_t> retransmissionTimers; ///< Retransmission timers.
        std::atomic<Mode> interfaceMode = Mode::MULTIPOINT; ///< Interface mode.
        std::atomic<uint32_t> localMetric; ///< Local metric of the interface.
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
