// Eigrp.h

#ifndef EIGRP_H
#define EIGRP_H

#include <vector>
#include <PacketStructure.h>
#include <Functions.h>
#include <mutex>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <atomic>
#include <shared_mutex>
#include <condition_variable>
#include <RoutingTable.h>
#include <TimeManager.h>
#include <unordered_map>
#include <unordered_set>
#include <PacketBuilder.hpp>
#include <tuple>

#define MAX_RETRANSMISSIONS 16
#define PACKET_TIMEOUT_MS 5000

/**
 * @file Eigrp.h
 * @brief Header file for the EIGRP (Enhanced Interior Gateway Routing Protocol) implementation.
 */

class Interface;
class VirtualRouter;
class Internal_EigrpTest;
enum class InterfaceType : uint8_t;

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
    struct EigrpConfigs
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

class InterfaceConfigs;

namespace Protocol 
{
    class Eigrp;
    class EigrpInterface;
    class TopologyTable;

    /**
     * @class EigrpInterface
     * @brief Represents an interface participating in the EIGRP process.
     *
     * The EigrpInterface class manages EIGRP operations specific to a network interface,
     * including sending and receiving EIGRP packets, maintaining neighbor relationships,
     * handling routing updates, and managing timers and retransmissions.
     */
    class EigrpInterface
    {
    public:
        friend class ::Internal_EigrpTest;

        Eigrp& eigrpProcess; ///< Pointer to the EIGRP process.
        EigrpConfigs::InterfaceConfigs* configs; ///< Configuration settings for the interface.

        /**
         * @brief Constructs an EigrpInterface instance.
         *
         * Initializes the EigrpInterface with the provided EIGRP process and network interface.
         * Sets up necessary configurations and starts the Hello timer.
         * 
         * @param eigrpSystem Reference to the EIGRP process.
         * @param interface Shared pointer to the network interface.
         */
        EigrpInterface(Eigrp& eigrpSystem, EigrpConfigs::InterfaceConfigs* intConfigs, Interface* interface);

        /**
         * @brief Destructor for EigrpInterface.
         *
         * Cancels all active timers, stops the worker thread, and performs necessary cleanup
         * to gracefully terminate the EIGRP interface operations.
         */
        virtual ~EigrpInterface();

        /**
         * @brief enables multicast on the interface.
         *
         * Handles enabling multicast on the interface if it was initiated without multicast.
         */
        void enableMulticast();

        /**
         * @brief disables multicast on the interface.
         *
         * Handles disabling multicast on the interface if it no longer matches
         * conditions but unicast is still present.
         */
        void disableMulticast();

        /**
         * @brief Processes an incoming EIGRP packet.
         *
         * Determines the type of the received EIGRP packet and dispatches it to the appropriate
         * handler function (e.g., Hello, Update, Query, Reply, ACK).
         *
         * @param eigrpPacket Pointer to the received EIGRP packet header.
         * @param neighborIp IP address of the neighbor that sent the packet.
         */
        void processPacket(const EigrpHeader& eigrpPacket, const uint8_t* neighborIp, bool multicast);

        /**
         * @brief Initializes a neighbor's information.
         *
         * Sets up the initial state for a new neighbor, including starting the initialization
         * sequence, setting roles, and preparing for communication.
         *
         * @param neighbor reference to the neighbor's information.
         */
        void initializeNeighbor(EigrpConfigs::NeighborInfo* neighbor);

        /**
         * @brief Changes the state of a neighbor during initialization.
         *
         * Transitions the neighbor's state machine to a new state, ensuring proper synchronization
         * and handling of any necessary actions during the state change.
         *
         * @param neighbor pointer to the neighbor's information.
         * @param newState New state to transition to.
         */
        void changeNeighborState(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, EigrpConfigs::NeighborState newState);

        /**
         * @brief Processes incoming Hello packets from a neighbor.
         *
         * Handles the reception of Hello packets, updating neighbor states, exchanging Router IDs,
         * and maintaining the neighbor relationship.
         *
         * @param neighbor Pointer to the neighbor information.
         * @param receivedHello Pointer to the received Hello packet header.
         * @param neighborIp IP address of the neighbor.
         */
        void processHello(EigrpConfigs::NeighborInfo* neighbor, const EigrpHeader& receivedHello, const IPAddress& neighborIp, bool unicast);

        /**
         * @brief Processes incoming Update packets from a neighbor.
         *
         * Parses the Update packet, extracts routing information, updates the topology table,
         * and sends acknowledgements as necessary.
         *
         * @param neighbor Pointer to the neighbor information.
         * @param receivedUpdate Pointer to the received Update packet header.
         */
        void processUpdate(EigrpConfigs::NeighborInfo* neighbor, const EigrpHeader& receivedUpdate);

        /**
         * @brief Processes buffered Update packets for a neighbor.
         *
         * Sends any buffered Update packets that were previously held due to pending acknowledgements
         * or state conditions.
         *
         * @param neighbor pointer to the neighbor's information.
         */
        void processBufferedPackets(EigrpConfigs::NeighborInfo* neighbor);

        /**
         * @brief Processes an incoming ACK from a neighbor.
         *
         * Validates the acknowledgement, removes the corresponding packet from the retransmission queue,
         * and updates RTT estimates.
         *
         * @param neighbor Pointer to the neighbor information.
         * @param sequenceNumber Sequence number being acknowledged.
         */
        void processAck(EigrpConfigs::NeighborInfo* neighbor, const uint32_t sequenceNumber);

        /**
         * @brief Processes an incoming Query packet from a neighbor.
         *
         * Handles the Query by checking the feasibility of the routes in question and responding
         * with appropriate Reply packets.
         *
         * @param neighbor Pointer to the neighbor information.
         * @param receivedQuery Pointer to the received Query packet header.
         * @param neighborIp IP of the neighbor that sent the query.
         */
        void processQuery(EigrpConfigs::NeighborInfo* neighbor, const EigrpHeader& receivedQuery, const IPAddress& neighborIp);

        /**
         * @brief Processes an incoming SIAQuery packet from a neighbor
         * 
         * Handles the SIAQuery by checking the feasibility of the routes in question and immedietly
         * responsing with appropriate Reply packets.Arp
         * 
         * @param neighbor Pointer to the neighbor information.
         * @param receivedQuery Pointer to the received Query packet header.
         * @param neighborIp IP of the neighbor that sent the query.
         */
        void processSIAQuery(EigrpConfigs::NeighborInfo* neighbor, const EigrpHeader& receivedQuery, const IPAddress& neighborIp);

        /**
         * @brief Processes an incoming Reply packet from a neighbor.
         *
         * Updates the topology table based on the Reply, recalculates the best routes,
         * and resolves any pending queries.
         *
         * @param neighbor Pointer to the neighbor information.
         * @param neighborIp Reference to neighbors IP.
         * @param recievedReply Pointer to the received Reply packet header.
         */
        void processReply(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, const EigrpHeader& recievedReply);

        /**
         * @brief Processes an incoming SIAReply packet from a neighbor.
         *
         * Updates the topology table based on the SIAReply, recalculates the best routes,
         * and resolves any pending queries.
         *
         * @param neighbor Pointer to the neighbor information.
         * @param neighborIp Reference to neighbors IP.
         * @param recievedReply Pointer to the received Reply packet header.
         */
        void processSIAReply(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, const EigrpHeader& recievedReply);

        /**
         * @brief Sends an ACK to a neighbor.
         *
         * Constructs and sends an ACK packet to confirm the receipt of a specific Update or Query packet.
         *
         * @param neighbor Pointer to the neighbor information.
         * @param sequenceNumber Sequence number to acknowledge.
         */
        void sendAckToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, uint32_t sequenceNumber);

        /**
         * @brief Sends an Update packet to a neighbor.
         *
         * Constructs and sends an Update packet containing routing information to the specified neighbor.
         * Handles different types of updates based on the updateType parameter.
         *
         * @param neighbor Pointer to the neighbor information.
         * @param routes Routes to include in the update.
         * @param updateType Type of the update (FULL/QUERY/RESPONSE_QUERY/PARTIAL/TRIGGERED/WITHDRAW).
         * @param restart Indicates if this update is part of a restart.
         * @param conditional Indicates if this update is conditional.
         * @param conditionalNeighbors List of neighbors for conditional updates.
         */
        virtual void sendUpdateToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const std::vector<EigrpConfigs::RoutingUpdate>& routes, EigrpConfigs::UpdateType updateType, bool restart = false, bool conditional = false, std::vector<IPAddress> conditionalNeighbors = {});

        /**
         * @brief Sends a Query packet to a specific neighbor.
         *
         * Directly queries a single neighbor about specific failed routes to ascertain their status
         * and potential alternatives.
         *
         * @param neighbor Pointer to the neighbor information.
         * @param neighborIp Reference to neighbors IP address.
         * @param failedRoutes Routes that have failed and need to be queried.
         * @return the sequence number for the query.
         */
        virtual void sendQueryToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, std::vector<RoutingTable::Eigrp*> failedRoutes);

        /**
         * @brief Sends a query to all connected neighbors
         *
         * Finds all connected neighbors and sends a query to all of them.
         *
         * @param failedRoutes routes that will be included in the queries.
         */
        void sendQueryToNeighbors(std::vector<RoutingTable::Eigrp*> failedRoutes);

        /**
         * @brief Sends a SIAQuery packet to a specific neighbor.
         *
         * Directly queries a single neighbor about specific failed routes to ascertain their status
         * and potential alternatives.
         *
         * @param neighbor Pointer to the neighbor information.
         * @param neighborIp Reference to neighbors IP address.
         */
        void sendSIAQueryToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp);

        /**
         * @brief Sends a Reply packet to a neighbor in response to a Query.
         *
         * Responds to a neighbor's Query packet by providing detailed routing information about
         * the requested routes.
         *
         * @param neighbor Pointer to the neighbor information.
         * @param neighborIp Reference to the neighbors IP.
         * @param queryRoutes Routes queried from the neighbor.
         * @param existingRoutes existing route to send to neighbor.
         * @param querySequence Outgoing query configs.
         */
        virtual void sendReplyToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, std::vector<RoutingTable::Eigrp*> queryRoutes, std::vector<RoutingTable::Eigrp*> existingRoutes, uint32_t sequenceNumber);

        /**
         * @brief Sends a SIAReply packet to a neighbor in response to a Query.
         *
         * Responds to a neighbor's Query packet by providing detailed routing information about
         * the requested routes.
         *
         * @param neighbor Pointer to the neighbor information.
         * @param neighborIp Reference to the neighbors IP.
         * @param querySequence Query sequence number that the reply needs to match to.
         */
        void sendSIAReplyToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, uint32_t querySequence);

        /**
         * @brief Checks if a timeout has occurred for missing packets.
         *
         * Determines whether a retransmission timeout has been reached for a specific packet.
         * If a timeout is detected, appropriate actions such as retransmission or neighbor
         * down status updates are triggered.
         *
         * @param neighbor Pointer to the neighbor's information.
         * @param sequenceNumber Sequence number of the packet.
         * @return True if a timeout has occurred, false otherwise.
         */
        bool isTimeoutForMissing(EigrpConfigs::NeighborInfo* neighbor, uint32_t sequenceNumber);

        /**
         * @brief Calculates the maximum number of routes to be sent in a single Update packet.
         *
         * Determines the upper limit on the number of routing entries that can be included
         * in a single Update packet based on the address family and whether the routes are
         * external.
         *
         * @param af Address family (IPv4/IPv6).
         * @param isExternal Indicates if the route is external.
         * @return Maximum number of routes per packet.
         */
        size_t calculateMaxRoutesPerPacket(size_t baseSize, AddressFamily af, bool isExernal);

        /**
         * @brief Encodes a Route option for a specific route.
         *
         * Serializes the Route option, including route metrics and other relevant data,
         * into a ByteString for transmission.
         *
         * @param route Route information.
         * @param currentBandwidth Bandwidth of the current interface.
         * @param currentDelay Delay of the current Interface.
         * @param removed Indicates if the route is being removed.
         * @return Encoded Route option as ByteString.
         */
        uint8_t encodeRouteOption(uint8_t* out, RoutingTable::Eigrp* route, uint32_t currentBandwidth, uint32_t currentDelay, bool removed = false);

        /**
         * @brief Encodes an External Route option for a specific route.
         *
         * Serializes the External Route option, which includes additional metrics for external
         * routes, into a ByteString for inclusion in an EIGRP packet.
         *
         * @param route External route information.
         * @param currentBandwidth Bandwidth of the current interface.
         * @param currentDelay Delay of the current Interface.
         * @param removed Indicates if the route is being removed.
         * @return Encoded External Route option as ByteString.
         */
        uint8_t encodeExternalRouteOption(uint8_t* out, RoutingTable::Eigrp* route, uint32_t currentBandwidth, uint32_t currentDelay, bool removed = false);

        /**
         * @brief Encodes a Stub option based on stub configuration.
         *
         * Constructs the Stub option TLV (Type-Length-Value) based on the provided
         * stub configuration settings.
         *
         * @param stub Stub configuration.
         * @return Encoded Stub option as ByteString.
         */
        uint8_t* encodeStubOption(uint8_t* out, const EigrpConfigs::StubConfig& stub);

        /**
         * @brief Decodes a Route from a ByteString.
         *
         * Deserializes the Route option from its ByteString representation into a RoutingTable::Eigrp
         * structure, extracting all relevant routing metrics and information.
         *
         * @param value ByteString containing the encoded route.
         * @param external Indicates if the route is external.
         * @param summary Indicates if the route is a summary route.
         * @return Decoded EIGRP route.
         */
        RoutingTable::Eigrp* decodeRoute(const uint8_t* value, size_t valueSize, bool external, bool summary);

        /**
         * @brief Flags a pending update for a specific route.
         *
         * Marks a route as pending an update, indicating that further actions or acknowledgements
         * are required before the route can be fully processed or advertised.
         *
         * @param neighbor Pointer to the neighbor's information.
         * @param route Route information.
         * @param neighborIp IP address of the neighbor.
         */
        void flagPendingUpdate(EigrpConfigs::NeighborInfo* neighbor, RoutingTable::Eigrp* route);

        /**
         * @brief Updates the routing table based on received routes.
         *
         * Integrates the received routes into the local routing table, recalculates metrics,
         * and determines the best paths based on EIGRP's metric calculations and policies.
         *
         * @param neighbor Pointer to the neighbor's information.
         * @param neighborIp Reference to neighbor's IP address.
         * @param routes Vector of received routes.
         * @param init Indicates if the update is part of initialization.
         * @param remove Indicates if routes are being removed.
         */
        void updateRoutingTable(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, const std::vector<EigrpConfigs::RoutingUpdate>& routes);

        /**
         * @brief Updates the routing table for a specific destination.
         *
         * Re-evaluates and updates the routing entry for a single destination network,
         * ensuring that the best available route is selected and maintained.
         *
         * @param destination Destination network.
         */
        void updateRoutingTableForDestination(const IPPrefix& prefix);

        /**
         * @brief Retrieves the next sequence number for packet identification.
         *
         * Generates and returns the next available sequence number for EIGRP packet tracking,
         * ensuring uniqueness and proper sequencing of packets.
         *
         * @return Next sequence number.
         */
        uint32_t getNextSequenceNumber();

        /**
         * @brief Handles updates related to Stub routing.
         *
         * Applies or retracts routes based on the current Stub configuration, ensuring that
         * only permitted routes are advertised or maintained.
         */
        void handleStubRouteUpdates();

        /**
         * @brief Advertises a summary route to a neighbor.
         *
         * Constructs and sends a summary route advertisement to the specified neighbor,
         * consolidating multiple routes into a single summary route as per EIGRP's
         * summarization policies.
         *
         * @param summaryRoute Summary route information.
         */
        void advertiseSummaryRoute(RoutingTable::Eigrp* summaryRoute);

        /**
         * @brief Withdraws a summary route from a neighbor.
         *
         * Sends a Withdraw (WITHDRAW) packet to the neighbor to remove the previously advertised
         * summary route, ensuring that outdated or no longer valid summary routes are cleaned up.
         *
         * @param route Summary route information.
         */
        void withdrawSummaryRoute(RoutingTable::Eigrp* route);

        /**
         * @brief Removes all auto summarized route on an interface
         *
         * Finds all summarized routes and removes all automatically assigned summarizations
         * for the specific interface.
         */
        void removeAllAutoSummaries();

        /**
         * @brief Encodes a Summary Route for advertisement.
         *
         * Serializes the Summary Route into a format suitable for inclusion in an EIGRP
         * Update packet, consolidating route information for efficient transmission.
         *
         * @param summaryRoute Summary route information.
         * @return Encoded Summary Route as RoutingTable::Eigrp.
         */
        RoutingTable::Eigrp* encodeSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute);

        /**
         * @brief Handles the removal of a neighbor by cleaning up associated routes and timers.
         *
         * Performs cleanup operations when a neighbor is removed, including removing
         * routes learned from the neighbor, cancelling active timers, and updating the
         * topology table to reflect the neighbor's departure.
         *
         * @param neighbor Pointer to the neighbor's information.
         * @param neighborIp Reference to neighbors IP
         */
        void handleNeighborDown(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp);

        /**
         * @brief Handles the restart of a neighbor by reinitializing its state.
         *
         * Resets the neighbor's state machine and re-establishes the neighbor relationship
         * after a graceful restart, ensuring continuity in routing operations.
         *
         * @param neighbor Pointer to the neighbor's information.
         * @param neighborIp Reference to neighbors ip address.
         */
        void handleNeighborRestart(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp);

        /**
         * @brief Initiates a graceful restart of the EIGRP process.
         *
         * Performs a controlled restart of the EIGRP process, maintaining neighbor relationships
         * and minimizing routing disruptions by retaining routing information during the restart.
         *
         * @param neighbor Pointer to the neighbor's information.
         * @param neighborIp Reference to neighbors ip address.
         */
        void gracefulRestart(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp);

        /**
         * @brief Sets the interface to passive or active mode.
         *
         * Configures the interface's operational mode, determining whether it actively
         * sends and receives EIGRP packets or remains passive, only responding to received packets.
         *
         * @param passive True to set the interface to passive, false to make it active.
         */
        void setPassive(bool passive);

        /**
         * @brief Adds a neighbor to the EIGRP interface.
         *
         * Registers a new neighbor with the specified IP and MAC addresses, setting the
         * communication mode and initializing necessary state information.
         *
         * @param ipAddress IP address of the neighbor.
         * @param macAddress MAC address of the neighbor.
         * @param unicast Indicating whether the neighbor is unicast or multicast.
         */
        void addNeighbor(const IPAddress& ipAddress, const uint8_t* macAddress, bool unicast = false);

        /**
         * @brief Adds a unicast neighbor to the specified interface.
         *
         * Adds a unicast neighbor to a specified interface. Will convert a
         * multicast neighbor to a unicast neighbor if needed.
         *
         * @param neighborIp IP of the static neighbor.
         * @param interfaceType The type of interface this neighbor is being added to.
         * @param id The interface ID of the interface that this neighbor is being added to.
         */
        void addUnicastNeighbor(const IPAddress& neighborIp);

        /**
         * @brief Removes a unicast neighbor to the specified interface.
         *
         * Removes a unicast neighbor to a specified interface. Will enable multicast
         * if no unicast neighbors are present.
         *
         * @param neighborIp IP of the static neighbor.
         * @param interfaceType The type of interface this neighbor is being added to.
         * @param id The interface ID of the interface that this neighbor is being added to.
         */
        void removeUnicastNeighbor(const IPAddress& neighborIp);

        /**
         * @brief Retrieves information about a specific neighbor.
         *
         * Searches for and returns the NeighborInfo structure associated with the given
         * neighbor IP address, allowing for inspection or modification of the neighbor's state.
         *
         * @param neighborIp IP address of the neighbor.
         * @return Optional reference to the neighbor's information if found.
         */
        EigrpConfigs::NeighborInfo* getNeighborInfo(const IPAddress& neighborIp);

        /**
         * @brief Determines if a route is already advertised to all neighbors.
         *
         * Checks whether the specified route has been successfully advertised to all currently
         * known neighbors, preventing redundant advertisements and ensuring efficient network
         * utilization.
         *
         * @param network Network address.
         * @param mask Subnet mask.
         * @return True if the route is advertised to all neighbors, false otherwise.
         */
        bool isRouteAdvertised(const uint8_t* network, uint8_t mask);

        /**
         * @brief Advertises a route to a neighbor.
         *
         * Sends a routing update to the specified neighbor, optionally based on certain
         * conditions or triggers, to inform them of the new or updated route.
         *
         * @param route Route information.
         * @param conditional Indicates if the advertisement is conditional.
         */
        void advertiseRouteToNeighbor(const RoutingTable::Eigrp* route, bool conditional = false);

        /**
         * @brief Withdraws a route from a neighbor.
         *
         * Sends a Withdraw (WITHDRAW) packet to the specified neighbor to inform them that
         * the route is no longer valid or has been removed from the local routing table.
         *
         * @param route Route information.
         */
        void withdrawRouteFromNeighbor(const RoutingTable::Eigrp* route);

        /**
         * @brief Adds a summary route to the EIGRP configuration.
         *
         * Configures a summary route, aggregating multiple routes into a single summary
         * entry to reduce routing table size and improve network efficiency.
         *
         * @param network Network address of the summary route.
         * @param mask Subnet mask of the summary route.
         * @param isAuto Indicates if the summary route is auto-generated.
         */
        void addSummaryRoute(const uint8_t* network, uint8_t mask, bool isAuto = false);

        /**
         * @brief Removes a summary route from the EIGRP configuration.
         *
         * Deletes a previously configured summary route, ensuring that outdated or
         * unnecessary summary routes are no longer advertised or maintained.
         *
         * @param network Network address of the summary route.
         * @param mask Subnet mask of the summary route.
         */
        void removeSummaryRoute(const IPAddress& network, uint8_t mask);

        /**
         * @brief 
         */
        void restoreSummaryRoutes(const IPAddress& summaryNetwork, uint8_t summaryMask);

        /**
         * @brief Checks if a route matches any configured summary route.
         *
         * Verifies whether the specified route falls under any of the configured summary
         * routes, aiding in route aggregation and advertisement decisions.
         *
         * @param network Network address.
         * @param mask Subnet mask.
         * @return True if the route is summarized, false otherwise.
         */
        EigrpConfigs::SummaryRoute* isRouteSummarized(const uint8_t* network, uint8_t mask);

        /**
         * @brief Starts the Hello timer.
         *
         * Initiates the periodic sending of Hello packets to maintain and verify
         * neighbor relationships, ensuring ongoing connectivity and protocol operations.
         */
        void startHello();

        /**
         * @brief Starts the Hello timer helper function.
         *
         * Continuously sends Hello packets at configured intervals in a separate thread,
         * facilitating ongoing neighbor communication without blocking main protocol operations.
         */
        void startHelloHelper();

        /**
         * @brief Sends a Hello packet to a neighbor or multicast group.
         *
         * Constructs and dispatches a Hello packet to either a specific neighbor or the multicast
         * address, depending on the parameters. The sequence number aids in tracking and acknowledging
         * Hello packets.
         *
         * @param neighbor Pointer to the neighbor's information.
         * @param unicast True to send a unicast Hello, false for multicast.
         * @param update Indicates if this Hello is part of an update.
         * @param sequenceNumber Sequence number for the Hello packet.
         */
        virtual void sendHelloPacket(EigrpConfigs::NeighborInfo* neighbor = nullptr, bool unicast = false, bool update = false, uint32_t sequenceNumber = 0);

        /**
         * @brief Stops the Hello timer.
         *
         * Cancels the active Hello timer thread, ceasing the periodic transmission of Hello packets
         * and effectively pausing neighbor relationship maintenance.
         */
        virtual void stopHello();

        /**
         * @brief Starts an Active timer for a failed route.
         *
         * Initiates a timer to monitor the duration a route remains in the active state,
         * prompting retries or fallback mechanisms if the route is not resolved within the
         * configured timeframe.
         *
         * @param route Failed route information.
         */
        void startActiveTimer(RoutingTable::Eigrp* route); 

        /**
         * @brief Starts an SIA timer for query tracking
         *
         * Initiates a timer to monitor the duration a route remains in the active state,
         * prompting retries or fallback mechanisms if the route is not resolved within the
         * configured timeframe.
         *
         * @param route Failed route information.
         * @returns the timer ID.
         */
        uint32_t startSIATimer(RoutingTable::Eigrp* route, const IPAddress& neighborIp, EigrpConfigs::OutgoingQuery& outgoing);

        /**
         * @brief Handles a query resend when the SIA time runs out.
         *
         * @param route Route that is in transitioning to Stuck-In-Active
         * @param queryKey Key corresponding to the query information.
         */
        void handleSIATimeout(RoutingTable::Eigrp* route, const IPAddress& neighborIp);

        /**
         * @brief Handles the expiration of an Active timer for a failed route.
         *
         * Responds to the expiration of an Active timer by marking the route as inactive,
         * initiating queries to neighbors, or removing the route from the routing table if
         * no viable alternatives are found.
         *
         * @param route Failed route information.
         */
        void handleActiveTimeExpire(RoutingTable::Eigrp* route);

        /**
         * @brief Handles the stuck in active for a local route
         *
         * Starts and handles the active route condition and timers for a certain destination.
         *
         * @param destination destination of the active route.
         */
        void handleActiveStateForRoute(const IPAddress& destination);

        /**
         * @brief Cancels an Active timer for a specific route.
         *
         * Stops and removes the Active timer associated with the specified route,
         * preventing further timeout actions for that route.
         *
         * @param destination Destination network.
         * @param mask Subnet mask of the destination.
         */
        void cancelActiveTimer(const IPAddress& destination, uint8_t mask);

        /**
         * @brief Starts a Hold timer for a specific neighbor.
         *
         * Initiates a Hold timer to monitor the neighbor's responsiveness. If the neighbor
         * fails to send a Hello or any EIGRP packet within the hold time, the neighbor is
         * considered down, triggering route recalculations and neighbor cleanup.
         *
         * @param neighbor Pointer to the neighbor's information.
         * @param holdTime Hold time in seconds.
         */
        virtual void startHoldTimer(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, uint16_t holdTime);

        /**
         * @brief Handles the expiration of a Hold timer for a neighbor.
         *
         * Marks the neighbor as down due to inactivity, removes associated routes, and
         * cleans up any related state information to maintain accurate routing tables.
         * 
         * @param neighbor Pointer to the neighbor's information.
         * @param neighborIp Reference ip for neighbor.
         */
        void handleHoldTimeExpire(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp);

        /**
         * @brief Sets up a reliable packet for retransmission if needed.
         *
         * Registers a packet in the retransmission queue, ensuring that it is resent
         * if an acknowledgement is not received within the timeout period.
         *
         * @param neighbor Pointer to the neighbor's information.
         * @param packet Reference to packet information.
         * @param sequenceNum Sequence number of the packet.
         */
        void setupReliablePacket(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, const EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet& packet, uint32_t sequenceNum);

        /**
         * @brief Starts a retransmission timer for reliable packet delivery.
         *
         * Initiates a timer that triggers a retransmission of a packet if an ACK is not
         * received within the specified timeout period, enhancing reliability in packet delivery.
         *
         * @param neighbor Pointer to the neighbor's information.
         * @param sequenceNumber Sequence number of the packet.
         * @param timeout Timeout duration in seconds.
         * @return Timer ID of the retransmission timer.
         */
        uint32_t startRetransmissionTimer(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, const uint32_t sequenceNumber, double timeout);

        /**
         * @brief Handles the expiration of a retransmission timer by resending the packet or marking the neighbor down.
         *
         * Responds to retransmission timeouts by either resending the packet for another attempt
         * or marking the neighbor as down if repeated failures occur, ensuring robust neighbor management.
         *
         * @param neighbor Pointer to the neighbor's information.
         * @param sequenceNumber Sequence number of the packet.
         */
        void handleRetransmissionTimeout(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, const uint32_t sequenceNumber);

        /**
         * @brief Calculates the Round-Trip Time (RTT) for a packet.
         *
         * Measures the time taken for a packet to be sent and acknowledged, updating RTT estimates
         * to inform retransmission timeouts and network performance metrics.
         * 
         * @param neighbor Neighbor.
         * @param sequenceNumber Sequence number of the packet.
         * @return Calculated RTT in seconds.
         */
        double calculateRTT(EigrpConfigs::NeighborInfo* neighbor, uint32_t sequenceNumber);

        /**
         * @brief Updates RTT estimates based on received ACKs.
         *
         * Refines the RTT and RTT variance calculations upon receiving an ACK, allowing for
         * more accurate retransmission timeouts and improved protocol responsiveness.
         *
         * @param neighbor Neighbor.
         * @param sequenceNumber Sequence number of the acknowledged packet.
         */
        void updateRTTEstimate(EigrpConfigs::NeighborInfo* neighbor, uint32_t sequenceNumber);

        /**
         * @brief Updates dampening timestamps for the current route change.
         *
         * Updates tracking timestamps and counters for this AS if dampening is enabled and tracking
         */
        void recordRouteChange();

        /**
         * @brief Checks suppression status every couple of seconds.
         *
         * This is ran when dampening detects too many route changes in order
         * to supress changes to not overwhelm the system.
         */
        void checkSuppressionStatus();

        /**
         * @brief counts prefixes for route dampening.
         *
         * This is ran when a prefix is learned in order to cap a maximum amount of routes
         * if route dampening is enabled.
         */
        void onPrefixLearned();

        /**
         * @brief Retrieves the multicast address based on the address family.
         *
         * Determines and returns the appropriate multicast address for EIGRP packet
         * transmission based on whether IPv4 or IPv6 is being used.
         *
         * @return ByteString representing the multicast address.
         */
        const uint8_t* getMulticast();

        // Get the ip address of the interaface
        inline IPAddress getInterfaceIp();

        // Authentication
        void configureAuthentication(uint8_t* keyId = nullptr, const std::string* key = nullptr, EigrpConfigs::AuthType* type = nullptr, bool enable = false);
        uint8_t* serializeEigrpHeader(uint8_t* out, const EigrpHeader& eigrp, bool exclusiveAuthTLV);
        virtual uint8_t generateAuthenticatedTLV(uint8_t* out);
        static void appendAuthHMAC(uint8_t* packetStart);
        
        Interface* currentInterface; ///< Pointer to the current network interface.
        InterfaceConfigs* currentInterfaceInfo; ///< Pointer to the current interface's IP information.
        
        // Neighbor management
        std::shared_mutex neighborMutex; ///< Shared mutex for neighbor operations.
        std::unordered_map<IPAddress, EigrpConfigs::NeighborInfo*> neighbors; ///< Map of neighbor IPs to their information.

        uint32_t interfaceKey;

        std::atomic<bool> destroy{false}; ///< Destroy boolean for destruction of eigrp class.

    private:

        // Active Timers
        std::chrono::steady_clock::time_point helloStartTime; ///< Start time for the Hello timer.
        std::unordered_map<IPPrefix, uint32_t> activeTimers; ///< Map of active timers for routes.
        std::unordered_map<IPPrefix, uint32_t> siaTimers; ///< Map of SIA timers for routes.
        bool runTimers = true; ///< Flag to indicate if timers should continue running.
        std::atomic<bool>helloTimerActive = false; ///< Indicates if the Hello timer is active.

        // Timer IDs
        std::atomic<uint32_t> helloTimerId = 0; ///< Timer ID for the Hello timer.
        std::atomic<bool> helloDone{true};

        // Route Buffer
        std::vector<EigrpConfigs::RoutingUpdate> routeBuffer; ///< Buffer for routing updates.
        std::mutex bufferMutex;

        // Mutexes
        std::mutex helloTimerMutex; ///< Mutex for Hello timer operations.
        std::mutex activeTimerMutex; ///< Mutex for Active timer operations.

        // Sequence number
        std::atomic<uint32_t> nextSequenceNumber = 1; ///< Next sequence number for packets.

        std::deque<std::chrono::steady_clock::time_point> routeChangeTimes;
        std::chrono::steady_clock::time_point supressedUntil;
        uint8_t restartCounter = 0;
        std::atomic<bool> isSupressed = false;
        std::atomic<uint32_t> dampeningTimerId = 0;
        std::atomic<uint32_t> prefixCount = 0;
    };


    /**
     * @class Eigrp
     * @brief Manages EIGRP protocol operations including initialization, shutdown, network configuration, and route calculations.
     *
     * The Eigrp class is responsible for overseeing the overall EIGRP operations, handling
     * network configurations, managing EIGRP interfaces, processing routing updates, and
     * maintaining the routing table. It supports both Classic and Named EIGRP modes.
     */
    class Eigrp 
    {
    public:
        using InterfaceKey = std::pair<InterfaceType, float>;
        friend class ::Internal_EigrpTest;

        EigrpConfigs::EigrpConfigs configs; ///< Configuration settings for EIGRP.

        /**
         * @brief Constructs an Eigrp instance.
         *
         * Initializes the EIGRP process with the specified Autonomous System number and
         * address family, setting up necessary configurations and preparing for network operations.
         *
         * @param as Autonomous System number.
         * @param af Address family (IPv4/IPv6).
         */
        Eigrp(uint32_t as, AddressFamily af, VirtualRouter* vrf, bool named = false);

        /**
         * @brief Destructor for Eigrp.
         *
         * Shuts down the EIGRP process gracefully, ensuring that all interfaces are properly
         * closed, timers are canceled, and resources are cleaned up to prevent memory leaks.
         */
        virtual ~Eigrp();

        /**
         * @brief Initializes the EIGRP process.
         *
         * Sets up necessary configurations, initializes interfaces, starts Hello timers,
         * and begins the process of establishing neighbor relationships.
         */
        virtual void initializeEigrp();

        /**
         * @brief Shuts down the EIGRP process gracefully.
         *
         * Terminates all EIGRP operations, cancels active timers, removes routes from
         * the routing table, and cleans up any allocated resources to ensure a clean shutdown.
         */
        virtual void shutdown();

        /**
         * @brief Adds a network to the EIGRP configuration.
         *
         * Registers a new network with EIGRP, allowing the protocol to advertise and
         * route traffic through the specified network.
         *
         * @param newNetwork Network configuration to add.
         */
        void addNetwork(const EigrpConfigs::Network& newNetwork);

        /**
         * @brief Adds common TLVs to an EIGRP header
         *
         * Adds common TLVs to an EIGRP header such as stub, version, sequence,
         * auth, parameter.
         *
         * @param hdr Header that TLVs are heing added to.
         * @param cfg EigrpInterface object with needed interface configs.
         * @param isUpdate Indicates if this is for a UPDATE header.
         * @param isAck Indicates if this is for a ACK header
         * @param neighborIp IP of the neighbor that this packet is being sent to.
         * @param sequence Sequence number of the packet being sent.
         */
        size_t addCommonTlvs(EigrpHeader& hdr, EigrpInterface& cfg, bool isUpdate, bool isAck, const uint8_t* neighborIp, uint32_t sequence);

        /**
         * @brief Configures an EIGRP Hello packet with specific settings.
         *
         * Constructs and sends a Hello packet to a neighbor, initiating or maintaining
         * the neighbor relationship. The Hello packet includes necessary information for
         * synchronization and state management between peers.
         *
         * @param eigrp Reference to the EIGRP header.
         * @param eigrpInt Pointer to the EIGRP interface.
         * @param neighborIp IP address of the neighbor.
         * @param sequenceNumber Sequence number for the Hello packet.
         * @param ack Indicates if this Hello is an ACK.
         * @param update Indicates if this Hello is part of an update.
         */
        void eigrpHello(PacketBuilder& eigrp, EigrpInterface& eigrpInt, const uint8_t* neighborIp, uint32_t sequenceNumber = 0, bool ack = false, bool update = false);

        /**
         * @brief Configures an EIGRP Update packet with specific settings.
         *
         * Constructs and sends an Update packet containing routing information to neighbors.
         * The Update packet can carry various types of routing information based on the
         * specified parameters, facilitating route advertisement and query responses.
         *
         * @param eigrp Reference to the EIGRP header.
         * @param sequenceNum Sequence number for the Update packet.
         * @param neighbork Pointer to the neighbor that the packet is being sent to.
         * @param init Indicates if this Update is part of initialization.
         * @param conditional Indicates if this Update is conditional.
         * @param restart Indicates if this Update is part of a restart.
         * @param endoftable Indicates if this Update marks the end of the table.
         * @param query Indicates if this Update is a Query.
         * @param reply Indicates if this Update is a Reply to a Query.
         */
        void eigrpUpdate(EigrpHeader& eigrp, uint32_t sequenceNum, bool init = false, bool conditional = false, bool restart = false, bool endoftable = false, bool query = false, bool reply = false);

        /**
         * @brief Updates the list of EIGRP interfaces manually for multicast.
         * 
         * @param interface Pointer to the interface you want added.
         * @return The newly made EigrpInterface.
         */
        EigrpInterface* addEigrpInterface(Interface* interface);
        
        /**
         * @brief Updates the list of EIGRP interfaces based on address matching.
         *
         * Scans the network interfaces, matches them against configured EIGRP networks,
         * and updates the internal list of active EIGRP interfaces accordingly.
         */
        void updateInterfaceList();

        /**
         * @brief Tests if an IP address matches any of the configured EIGRP networks.
         *
         * Checks whether the provided IP address falls within any of the networks
         * configured for EIGRP, determining if EIGRP operations should be active
         * on that interface.
         *
         * @param testIp IP address to test.
         * @return True if the IP address matches a configured network, false otherwise.
         */
        bool testAddress(const uint8_t* testIp);

        /**
         * @brief Calculates the EIGRP metric for a route.
         *
         * Computes the EIGRP metric based on the provided parameters, adhering to
         * EIGRP's metric calculation formula which considers bandwidth, load, delay,
         * reliability, and optionally hop count for unequal-cost load balancing.
         *
         * @param bandwidth Bandwidth value in Kbps.
         * @param load Load value (0-255).
         * @param delay Delay value in tens of microseconds.
         * @param reliability Reliability value (0-255).
         * @param hopCount Number of hops (default is 0).
         * @return Calculated metric value.
         */
        uint64_t calculateMetric(uint32_t bandwidth, uint8_t load, uint32_t delay, uint8_t reliability, uint8_t hopCount = 0);

        /**
         * @brief Calculates the Local Link Cost (LLC) for the interface.
         *
         * Computes the LLC based on interface metrics such as bandwidth, delay, reliability,
         * and load, contributing to the overall EIGRP metric calculation.
         *
         * @param load Load of the interface used.
         * @param delay Delay of the interface used.
         * @return Calculated LLC value.
         */
        uint32_t calculateLocalLinkCost(uint8_t load, uint32_t delay, uint8_t reliability);

        /**
         * @brief Calculates EIGRP parameters based on the hold time.
         *
         * Derives EIGRP parameter values such as retransmission timeout and others
         * based on the configured hold time, ensuring synchronization with neighbor
         * timers and state management.
         *
         * @param holdTime Hold time in seconds.
         * @return ByteString representing calculated parameters.
         */
        uint8_t* calculateParameters(uint8_t* out, uint16_t holdTime);

        /**
         * @brief Updates the routing table with connected routes.
         *
         * Integrates connected network routes into the EIGRP routing table, allowing
         * EIGRP to advertise and route traffic through these directly connected networks.
         *
         * @param eigrpInterface Shared pointer to the EIGRP interface (optional).
         */
        void updateRoutingTableForConnected(EigrpInterface* eigrpInterface = nullptr);

        /**
         * @brief Notifies all EIGRP interfaces about routing changes.
         *
         * Broadcasts routing updates to all active EIGRP interfaces, informing neighbors
         * of new, updated, or removed routes to ensure consistent and synchronized routing
         * information across the network.
         *
         * @param changedRoutes Vector of routes that have changed.
         */
        void notifyRoutingChange(const std::vector<EigrpConfigs::RoutingUpdate>& changedRoutes);

        /**
         * @brief Redistributes a route from another protocol into EIGRP.
         *
         * Injects routes learned from external routing protocols (e.g., OSPF, BGP) into
         * the EIGRP routing table, allowing for route redistribution and integration of
         * diverse routing information.
         *
         * @param destination Destination network.
         * @param mask Subnet mask of the destination.
         * @param protocol Protocol identifier.
         */
        void redistributeRoute(const uint8_t* destination, uint8_t mask, const uint16_t protocol);

        /**
         * @brief Enables or disables auto-summarization.
         *
         * Toggles the auto-summarization feature, allowing EIGRP to automatically summarize
         * routes at major network boundaries, simplifying routing tables and reducing
         * routing protocol overhead.
         *
         * @param enable True to enable, false to disable.
         */
        void enableAutoSummary(bool enable);

        /**
         * @brief Recomputes auto summaries when a new routes is learned
         *
         * Takes all existing auto summarized routes and recalculates the summarized routes
         */
        void recomputeAutoSummaries();

        /**
         * @brief Sets the EIGRP process as a stub.
         *
         * Configures the EIGRP process to operate in stub mode, limiting the types of routes
         * advertised to reduce routing protocol complexity and overhead, especially in hub-and-spoke
         * network topologies.
         * 
         * @param isStub True to set as stub, false otherwise.
         * @param advertiseConnected Advertise connected routes.
         * @param advertiseLeakMap Advertise leak-map routes.
         * @param advertiseStatic Advertise static routes.
         * @param advertiseSummary Advertise summary routes.
         * @param advertiseRedistributed Advertise redistributed routes.
         */
        void setStub(bool isStub, bool advertiseConnected = true, bool advertiseLeakMap = true, bool advertiseStatic = true, bool advertiseSummary = true, bool advertiseRedistributed = true);

        /**
         * @brief Updates routes based on stub configuration.
         *
         * Applies or retracts route advertisements based on the current stub settings,
         * ensuring that only permitted route types are advertised to neighbors.
         */
        void updateStubRoutes();

        /**
         * @brief Retrieves the lowest bandwidth among all configured interfaces.
         *
         * Scans all EIGRP-configured interfaces to determine the minimum bandwidth value,
         * which is crucial for metric calculations and route selection processes.
         *
         * @return Lowest bandwidth value in Kbps.
         */
        uint32_t getLowestBandwidth();

        /**
         * @brief Adds or removes a passive interface.
         *
         * Will store the interface in a list as passive and if the interface
         * is active, it will set the interface as passive.
         *
         * @param type Type of interface (e.g. GigabitEthernet, Ethernet)
         * @param id ID of the passive interface.
         * @param add optional boolean that tells whether to add or remove the passive interface.
         */
        void addPassiveInterface(uint32_t key, bool add = true);

        /**
         * @brief Sets the variance for unequal-cost load balancing.
         *
         * Configures the variance multiplier, allowing EIGRP to utilize multiple routes
         * with feasible distances within the specified variance factor, enabling unequal-cost
         * load balancing across multiple paths.
         *
         * @param var Variance value.
         */
        void setVariance(uint8_t var);

        /**
         * @brief Recalculates routes based on updated metrics and variance.
         *
         * Initiates a recalculation of the routing table to account for changes in route
         * metrics or variance settings, ensuring optimal route selection and load balancing.
         */
        void recalculateRoutes();

        /**
         * @brief Recalculates all metrics for all routes
         *
         * Recalculates metrics for all new routes, mostly used when
         * a new interface is added.
         */
        void recalculateRouteMetrics();

        /**
         * @brief calculates updates the route metric, feasible distance, and reported distance.
         *
         * Calculates a new metric, feasuble distance, and reported distance for the route provided
         * 
         * @param localCost Local link cost calculated seperately in the 
         * @param route Eigrp route that needs a metric added.
         */
        void addRouteMetric(uint32_t localCost, RoutingTable::Eigrp* route);

        /**
         * @brief Enables a specific unicast neighbor on a specific interface.
         *
         * Will attempt to add the unciast interface if able to, if able to it will
         * disable multicast on the interface removing all of the multicast neighobrs.
         * Will then store the unicast neighbor for the future.
         *
         * @param neighborIp IP of the static neighbor.
         * @param interfaceType The type of interface this neighbor is being added to.
         * @param id The interface ID of the interface that this neighbor is being added to.
         */
        void enableUnicastNeighbor(const IPAddress& neighborIp, uint32_t key);

        /**
         * @brief Disables a unicast neighbor to the specified interface.
         *
         * Will remove the unicast neighbor if the nieghbor is currently present
         * on the specified interface.
         *
         * @param neighborIp IP of the static neighbor.
         * @param interfaceType The type of interface this neighbor is being added to.
         * @param id The interface ID of the interface that this neighbor is being added to.
         */
        void disableUnicastNeighbor(const IPAddress& neighborIp, uint32_t key);

        /**
         * @brief Restarts the EIGRP process.
         *
         * Completely restarts the EIGRP process, resetting configurations, clearing routing tables,
         * and re-establishing neighbor relationships from scratch.
         */
        void restart();

        /**
         * @brief Cleans up all EIGRP configurations and state.
         *
         * Removes all EIGRP configurations, clears routing tables, cancels timers,
         * and frees allocated resources to ensure a complete cleanup of the EIGRP process.
         */
        void cleanup();

        /**
         * @brief Performs periodic maintenance tasks.
         *
         * Executes routine maintenance operations such as pruning stale routes,
         * updating neighbor states, and managing timers to ensure the EIGRP process
         * remains healthy and up-to-date with the network state.
         */
        void periodicMaintenance();

        /**
         * @brief Calculates the Router ID based on interface addresses.
         *
         * Determines the Router ID by selecting the highest IP address among all configured
         * interfaces or using a manually configured static Router ID, ensuring a unique identifier
         * for the EIGRP process.
         */
        void calculateRouterID();

        /**
         * @brief Checks if the EIGRP process is configured as a stub.
         *
         * Indicates whether the EIGRP process is operating in stub mode, affecting route
         * advertisements and protocol behavior accordingly.
         *
         * @return True if stub is enabled, false otherwise.
         */
        bool isStub() const { return configs.stubConfig.isStub; }

        /**
         * @brief Indicates whether connected routes are being advertised.
         * @return True if connected routes are advertised, false otherwise.
         */
        bool advertiseConnected() const { return configs.stubConfig.advertiseConnected; }

        /**
         * @brief Indecates whether leak-map routes are being advertised.
         * @return True if leak-map routes are advertised, false otherwise.
         */
        bool advertisedLeakMap() const { return configs.stubConfig.advertiseLeakMap; }

        /**
         * @brief Indicates whether static routes are being advertised.
         * @return True if static routes are advertised, false otherwise.
         */
        bool advertiseStatic() const { return configs.stubConfig.advertiseStatic; }

        /**
         * @brief Indicates whether summary routes are being advertised.
         * @return True if summary routes are advertised, false otherwise.
         */
        bool advertiseSummary() const { return configs.stubConfig.advertiseSummary; }

        /**
         * @brief Indicates whether redistributed routes are being advertised.
         * @return True if redistributed routes are advertised, false otherwise.
         */
        bool advertiseRedistributed() const { return configs.stubConfig.advertiseRedistributed; }

        // Lists
        std::unordered_map<uint32_t, EigrpInterface*> eigrpInterfaceList; ///< Map of EIGRP interfaces by identifier.
        std::unordered_map<uint32_t, EigrpConfigs::InterfaceConfigs*> eigrpInterfaceConfigList; ///< Map of EIGRP interface config by identifier.

        // Eigrp data mutex
        std::shared_mutex eigrpDataMutex; ///< Mutex for synchronizing access to EIGRP data structures.

        // Configurations for EIGRP

        /**
         * @brief Retrieves the Virtual Router ID.
         *
         * Provides the virtual Router ID assigned to the EIGRP process, used in
         * routing advertisements and neighbor identification.
         *
         * @return ByteString representing the virtual Router ID.
         */
        inline uint32_t getVirtualRouterID() { std::shared_lock<std::shared_mutex> lock(eigrpDataMutex); return virtualRouterID; }

        /**
         * @brief Retrieves the Router ID.
         *
         * Returns the Router ID configured for the EIGRP process, serving as a unique
         * identifier within the EIGRP routing domain.
         * @return ByteString representing the Router ID.
         */
        inline uint8_t* getRouterID(uint8_t* out) { std::shared_lock<std::shared_mutex> lock(eigrpDataMutex); return routerID.ID; }
        inline uint32_t getRouterID() { std::shared_lock<std::shared_mutex> lock(eigrpDataMutex); return readU32(routerID.ID); }

        // Topology TablE
        Protocol::TopologyTable* topologyTable; ///< Unique pointer to the EIGRP topology table.

        // Lists
        std::unordered_map<IPPrefix, EigrpConfigs::ActiveRoute> outstandingReplies; ///< Map of outstanding query IDs to neighbor IPs and timer IDs.
        VirtualRouter* routingInstance; ///< Routing instance coorsponding with the current process.

        void setRouterID(const uint8_t* routerId) { std::memcpy(routerID.ID, routerId, 4); routerID.isStatic = true;}
        void clearRouterID() { routerID.isStatic = false; calculateRouterID(); }

        const AddressFamily addressFamily; ///< Address family (IPv4/IPv6).
        const uint32_t asNumber; ///< Autonomous System number.
        std::shared_mutex interfaceMutex;
        std::unordered_map<IPAddress, EigrpConfigs::NeighborInfo*> allNeighbors;
        std::mutex neighborMutex;
        bool namedMode = false;

    private:
        uint16_t virtualRouterID = 0x0000; ///< Virtual Router ID.
        EigrpConfigs::RouterID routerID; ///< Router ID configuration.
    };

    /**
     * @struct EigrpAutonomousSystems
     * @brief Manages multiple Autonomous Systems within the EIGRP process.
     */
    struct EigrpAutonomousSystem
    {
        Eigrp* ipv4 = nullptr;
        Eigrp* ipv6 = nullptr;
        bool ipv4Named = false;
        bool ipv6Named = false;
    };

    /**
     * @struct EigrpNamed
     * @brief Represents an instance of the EIGRP process.
     */
    struct EigrpNamed
    {
        Eigrp* ipv4 = nullptr;
        Eigrp* ipv6 = nullptr;
    };

    /**
     * @struct EigrpInterfaceInstance
     * @brief Represents EIGRP interfaces for IPv4 and IPv6.
     */
    struct EigrpInterfaceInstance
    {
        EigrpInterface* IPv4; ///< Pointer to the IPv4 EIGRP interface.
        EigrpInterface* IPv6; ///< Pointer to the IPv6 EIGRP interface.
    };

    /**
     * @class ClassicEigrp
     * @brief Represents a Classic EIGRP process.
     *
     * The ClassicEigrp class inherits from the base Eigrp class and implements
     * Classic-specific functionalities such as auto-summarization.
     */
    class ClassicEigrp : public Eigrp
    {
    public:
        /**
         * @brief Constructs a ClassicEigrp instance.
         *
         * Initializes the ClassicEigrp process with the specified AS number and
         * address family, enabling Classic-specific features like auto-summarization.
         *
         * @param as Autonomous System number.
         * @param af Address family.
         */
        ClassicEigrp(uint32_t& as, AddressFamily af, VirtualRouter* vrf) : Eigrp(as, af, vrf) {}

        /**
         * @brief Initializes the Classic EIGRP process.
         *
         * Sets up Classic-specific settings such as enabling auto-summarization,
         * configuring K-values, and preparing the routing table for Classic operations.
         */
        void initializeEigrp() override;

        /**
         * @brief Shuts down the Classic EIGRP process gracefully.
         *
         * Disables auto-summarization, removes all routes associated with Classic EIGRP,
         * and cleans up resources specific to Classic operations.
         */
        void shutdown() override;
    };

    /**
     * @class NamedEigrp
     * @brief Represents a Named EIGRP process.
     *
     * The NamedEigrp class inherits from the base Eigrp class and implements
     * Named-specific functionalities, allowing for multiple named EIGRP processes
     * within the same routing domain.
     */
    class NamedEigrp : public Eigrp
    {
    private:
        std::string processName; ///< Name of the Named EIGRP process.

    public:
        /**
         * @brief Constructs a NamedEigrp instance.
         *
         * Initializes the NamedEigrp process with the specified AS number, address
         * family, and a unique process name, enabling Named-specific features and
         * multiple concurrent EIGRP instances.
         *
         * @param as Autonomous System number.
         * @param af Address family.
         * @param name Name of the EIGRP process.
         * @param vrf Pointer to the routing instance being used.
         * @param multicast Indicates if this instance routes multicast routes.
         */
        NamedEigrp(uint32_t& as, AddressFamily af, const std::string& name, VirtualRouter* vrf, bool multicast);

        /**
         * @brief Initializes the Named EIGRP process.
         *
         * Sets up Named-specific settings such as disabling auto-summarization,
         * configuring unique Router IDs, and preparing the routing table for Named operations.
         */
        void initializeEigrp() override;

        /**
         * @brief Shuts down the Named EIGRP process gracefully.
         *
         * Removes all routes associated with the Named EIGRP process, disables features
         * specific to Named operations, and cleans up allocated resources.
         */
        void shutdown() override;

        /**
         * @brief Configures an interface with specific EIGRP settings.
         *
         * Applies EIGRP configurations to a specified network interface, enabling
         * or modifying EIGRP operations on that interface based on the provided settings.
         *
         * @param interfaceName Name of the interface.
         * @param configs Interface configuration settings.
         */
        void configureInterface(uint32_t interfaceId, const EigrpConfigs::InterfaceConfigs& configs);
    };

    /**
     * @class TopologyTable
     * @brief Manages the EIGRP topology table.
     *
     * The TopologyTable class maintains information about all known routes,
     * including their feasibility and successor statuses, based on information
     * received from neighbors. It supports adding, updating, and removing routes,
     * as well as determining the best available paths considering EIGRP's metric
     * calculations and variance settings.
     */
    class TopologyTable {
    public:

        /**
         * @struct RouteInfo
         * @brief Contains information about a specific route in the topology table.
         */
        struct RouteInfo {
            EigrpInterface* eigrpInterface; ///< Pointer the the interface this was learned on.
            uint32_t bandwidthMetric;
            uint32_t delayMetric;
            uint32_t feasibleDistance; ///< Feasible distance of the route.
            uint32_t reportedDistance; ///< Reported distance from the neighbor.
            uint8_t hopCount; ///< Number of hops to the destination.
            uint8_t adminDistance = 90; ///< Administrative distance.
            IPAddress nextHop; ///< Next hop IP address.
            bool isSuccessor = false; ///< Indicates if this route is a successor.
            bool isFeasibleSuccessor = false; ///< Indicates if this route is a feasible successor.
            bool notFeasible = false; ///< Indicates if this route is feasible or not.
            std::chrono::steady_clock::time_point lastUpdate; ///< Timestamp of the last update.
            RoutingTable::Eigrp::RouteType routeType;
        };

        /**
         * @struct TopologyEntry
         * @brief Represents an entry in the topology table for a specific destination.
         */
        struct TopologyEntry {
            IPAddress destination; ///< Destination network.
            uint8_t prefixLength; ///< Prefix length of the destination.
            std::unordered_map<IPAddress, RouteInfo> routesByNeighbor; ///< Routes learned from each neighbor.
            bool isActive; ///< Indicates if the route is active.

            // Timers for Active and Stuck-In-Active
            uint32_t activeTimerId = 0; ///< Timer ID for active routes.
            uint32_t StuckInActiveTimerId = 0; ///< Timer ID for stuck-in-active routes.

            uint32_t bestFD; ///< Best feasible distance for the route.

            std::vector<IPAddress> feasibleSuccessors; ///< List of feasible successor neighbors.
            std::vector<IPAddress> successors; ///< List of successor neighbors.
        };

        /**
         * @brief Constructs a TopologyTable instance.
         *
         * Initializes the TopologyTable, associating it with the given EIGRP process
         * to enable route management and synchronization with routing updates.
         *
         * @param process Pointer to the EIGRP process.
         */
        TopologyTable(Eigrp* process);

        ~TopologyTable();

        /**
         * @brief gathers all of the best routes in the routing table
         *
         * @param network Network that you want successors for.
         * @param mask Prefix length for the network you want successors for.
         * @return Returns a vector of all successors for a specific route.
         */
        std::vector<RoutingTable::Eigrp*> getSuccessorsForRoute(const IPAddress& network, uint8_t mask);

        /**
         * @brief Adds or updates a route in the topology table.
         *
         * Inserts a new route or updates an existing route in the topology table based
         * on information received from a neighbor, adjusting route metrics and statuses
         * as necessary.
         *
         * @param neighbor Reference to neighbors IP.
         * @param destination Destination network.
         * @param prefixLength Prefix length of the destination.
         * @param routeInfo Information about the route.
         */
        void addOrUpdateRoute(const IPAddress& neighborIp, const IPAddress& destination, uint8_t prefixLength, const RouteInfo& routeInfo);

        /**
         * @brief Removes all routes associated with a specific neighbor.
         *
         * Deletes all routes learned from the specified neighbor, ensuring that stale
         * or invalid routes are no longer present in the topology table.
         *
         * @param neighborIp IP address of the neighbor.
         */
        void removeRoutesFromNeighbor(const IPAddress& neighborIp);

        /**
         * @brief Finds the best route for a given destination considering variance.
         *
         * Evaluates all available routes to a destination, considering EIGRP's
         * variance setting to allow unequal-cost load balancing, and identifies the
         * optimal route based on feasible distance and other metrics.
         *
         * @param prefix Destination prefix.
         * @param mask Mask of prefix.
         * @return Optional RouteInfo if a best route is found.
         */
        std::optional<RouteInfo> findBestRoute(const IPPrefix& prefix);

        /**
         * @brief Updates successors and feasible successors for a topology entry.
         *
         * Recalculates and assigns successor and feasible successor routes for the
         * specified topology entry, ensuring optimal route selection and redundancy.
         *
         * @param entry Reference to the TopologyEntry.
         */
        void updateSuccessorAndFeasibleSuccessors(TopologyEntry* entry);

        /**
         * @brief Retrieves the topology entry for a specific route.
         *
         * Searches for and returns the topology entry associated with the given destination
         * network, facilitating detailed route inspections and modifications.
         *
         * @param prefix Destination prefix.
         * @param mask Mask of prefix.
         * @return Shared pointer to the TopologyEntry or nullptr if not found.
         */
        TopologyEntry* getEntryForRoute(const IPPrefix& prefix);

        /**
         * @brief Handles the failure of a route by removing it from the topology table.
         *
         * Removes the specified route from the topology table due to neighbor failure,
         * triggering route recalculations and potential advertisements to other neighbors.
         *
         * @param prefix Destination prefix.
         * @param mask Mask of prefix.
         * @param failedNeighborIp IP address of the failed neighbor.
         */
        void handleRouteFailure(const IPPrefix& prefix, const IPAddress& failedNeighborIp);

        /**
         * @brief Marks a route as passive, disabling further updates.
         *
         * Sets the specified route to a passive state, preventing it from being updated
         * or advertised further, often used during route maintenance or controlled shutdowns.
         *
         * @param prefix Destination prefix.
         * @param mask Mask of prefix.
         * @param eigrp Pointer to the EIGRP interface.
         */
        void markRouteAsPassive(const IPPrefix& prefix, EigrpInterface* eigrp);

        /**
         * @brief Removes a route from a specific neighbor for a specific destination.
         *
         * Deletes the entire topology entry for the given destination network,
         * effectively removing all associated routing information.
         *
         * @param prefix Destination prefix.
         * @param mask Mask of prefix.
         */
        bool removeRoute(const IPPrefix& prefix, const IPAddress& neighbor);

        /**
         * @brief Prunes stale routes that have not been updated within the threshold.
         *
         * Scans the topology table for routes that have not received updates within
         * a specified stale threshold and removes them to maintain an accurate and
         * efficient routing table.
         */
        void pruneStaleRoutes();

        /**
         * @brief Handles the removal of a neighbor by cleaning up associated routes.
         *
         * Executes cleanup procedures when a neighbor is removed, including
         * deleting routes learned from the neighbor and updating the topology table.
         *
         * @param neighborIp IP address of the neighbor being removed.
         */
        void handleNeighborDown(const IPAddress& neighborIp);

        /**
         * @brief Retrieves all topology entries.
         *
         * Provides access to the entire topology table, allowing for comprehensive
         * inspections, exports, or modifications of routing information.
         *
         * @return Reference to the map of topology entries.
         */
        std::unordered_map<IPPrefix, TopologyEntry*>& getTopologyEntries() { std::lock_guard<std::mutex> lock(tableMutex); return topologyEntries; }

        uint8_t staleThreshold = 15; ///< Threshold in seconds to consider a route stale.
        std::mutex tableMutex; ///< Mutex for synchronizing access to the topology table.
    
    private:
        std::unordered_map<IPPrefix, TopologyEntry*> topologyEntries; ///< Map of destination networks to their topology entries.
        Eigrp* eigrpProcess; ///< Shared pointer to the EIGRP process.
    };
}

inline bool isFeasibleSuccessor(RoutingTable::Eigrp* candidate, RoutingTable::Eigrp* currentSuccessor)
{
    if (!currentSuccessor) return true;
    return candidate->reportedDistance < currentSuccessor->feasibleDistance;
}

/**
 * @var currentEigrp
 * @brief Global pointer to the current EIGRP autonomous system.
 *
 * Maintains a pointer to the active EIGRP autonomous system, allowing
 * for global access without ownership concerns.
 */
extern Protocol::Eigrp* currentEigrp;

/**
 * @var currentEigrpNamed
 * @brief Global pointer to the current Named EIGRP system.
 *
 * Maintains a pointer to the current active EIGRP named system,
 * allowing for global access without ownership concerns.
 */
extern Protocol::EigrpNamed* currentEigrpNamed;

/**
 * @var currentEigrpInterface
 * @brief Global pointer to the current EIGRP interface
 *
 * Maintains a pointer to the current EIGRP interface being configured,
 * allowing for global access without ownership concerns.
 */
extern Protocol::EigrpInterface* currentEigrpInterface;

#endif // EIGRP_H
