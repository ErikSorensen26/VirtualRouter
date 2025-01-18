// Eigrp.h

#ifndef EIGRP_H
#define EIGRP_H

#include <map>
#include <memory>
#include <vector>
#include <PacketStructure.h>
#include <Functions.h>
#include <mutex>
#include <thread>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <atomic>
#include <shared_mutex>
#include <condition_variable>
#include <RoutingTable.h>
#include <TimeManager.h>
#include <Authentication.hpp>
#include <unordered_map>
#include <ByteString.hpp>

/**
 * @file Eigrp.h
 * @brief Header file for the EIGRP (Enhanced Interior Gateway Routing Protocol) implementation.
 */

extern std::shared_mutex globalEigrpMutex;
class Interface;

/**
 * @namespace EigrpConfigs
 * Namespace containing configuration structs and enums for the EIGRP protocol.
 */
namespace EigrpConfigs
{
    /**
     * @struct Network
     * @brief Represents a network with its IP address and subnet mask.
     */
    struct Network 
    {
        ByteString ip;    ///< IP address of the network.
        ByteString mask;  ///< Subnet mask of the network.
    };

    /**
     * @struct RouterID
     * @brief Represents the Router ID configuration.
     */
    struct RouterID
    {
        ByteString ID = ByteString(4, '\x00'); ///< Router ID.
        bool isStatic = false;                 ///< Indicates if the Router ID is static.
    };

    /**
     * @struct SummaryRoute
     * @brief Represents a summary route configuration.
     */
    struct SummaryRoute 
    {
        ByteString network; ///< Network address of the summary route.
        uint8_t mask;       ///< Subnet mask length of the summary route.
        bool isAuto = false;///< Indicates if the summary route is automatically generated.
    };

    /**
     * @struct KValue
     * @brief Represents the K-values used in EIGRP metric calculation.
     */
    struct KValue 
    {
        uint8_t k1_Bandwidth = 1; ///< Weight for bandwidth.
        uint8_t k2_Load = 0;       ///< Weight for load.
        uint8_t k3_Delay = 1;      ///< Weight for delay.
        uint8_t k4_Reliability = 0;///< Weight for reliability.
        uint8_t k5_MTU = 0;        ///< Weight for MTU.
        uint8_t k6_Power = 0;      ///< Weight for power.
    };

    /**
     * @struct Sequence
     * @brief Represents sequence-related flags for neighbor initialization.
     */
    struct Sequence 
    {
        bool init;                 ///< Initialization flag.
        bool conditionalReceive;   ///< Conditional receive flag.
        bool endOfTable;           ///< End of table flag.

        /**
         * @brief Default constructor initializing all flags to false.
         */
        Sequence() : init(false), conditionalReceive(false), endOfTable(false) {}

    };

    /**
     * @struct StubConfig
     * @brief Configuration for Stub routing in EIGRP.
     */
    struct StubConfig
    {
        bool isStub = false;                ///< Indicates if Stub routing is enabled.
        bool advertiseConnected = true;     ///< Advertise connected routes.
        bool advertiseStatic = true;        ///< Advertise static routes.
        bool advertiseSummary = true;       ///< Advertise summary routes.
        bool advertiseRedistributed = true; ///< Advertise redistributed routes.

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
    enum class AuthType
    {
        NONE, ///< No authentication.
        MD5,  ///< MD5 authentication.
        SHA1  ///< SHA1 authentication.
    };

    /**
     * @struct AuthKey
     * @brief Represents an authentication key.
     */
    struct AuthKey
    {
        uint8_t keyId;              ///< Identifier for the authentication key.
        ByteString key;             ///< The authentication key.
        AuthType authType = AuthType::NONE; ///< Type of authentication.
    };

    /**
     * @enum TrafficShareMode
     * @brief Defines the traffic sharing mode in EIGRP.
     */
    enum class TrafficShareMode
    {
        Balenced, ///< Balanced traffic sharing.
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
        WITHDRAW         ///< Withdraw update.
    };

    /**
     * @enum CommunicationMode
     * @brief Defines the communication mode for EIGRP neighbors.
     */
    enum class CommunicationMode
    {
        UNICAST,    ///< Unicast communication mode.
        MULTICAST   ///< Multicast communication mode.
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
     * @struct NeighborInfo
     * @brief Contains comprehensive information and state management for an EIGRP neighbor.
     *
     * This structure manages neighbor initialization, communication state, packet handling,
     * timers, acknowledgements, authentication, and thread operations required to maintain
     * EIGRP neighbor relationships.
     */
    struct NeighborInfo 
    {
        // Basic Neighbor Information
        ByteString ipAddress; ///< Neighbor's IP address.
        ByteString macAddress; ///< Neighbor's MAC address.
        ByteString routerID; ///< Neighbor's RouterID.
        CommunicationMode mode = CommunicationMode::MULTICAST; ///< Neighbors communication mode.

        // Initialization
        std::mutex initializationMutex; ///< Mutex for neighbor initialization
        NeighborState initialization = NeighborState::DOWN; ///< Current state of the neighbor.
        InitRole initRole = InitRole::MASTER; ///< Initialization role (MASTER/SLAVE).
        bool initUpdateReceived =   false; ///< Inidcates if an initialization update has been received
        bool nullSent =             false; ///< Indicates if a null update has been sent.
        bool slaveInit =            false; ///< Indicates if the neighbor is in slave initialization.
        bool masterInit =           false; ///< Indicates if the nieghbor is in master initialization.
        bool initComplete =         false; ///< Indicates if neighbor initialization is complete.
        bool processAcks =          false; ///< Indicates if ACKs should be processed.
        uint32_t initSequence{0}; ///< Initialization sequence number.
        uint32_t nullUpdateSequence{0}; ///< Last sequence number for received for null update.

        // Packet Handling
        /**
         * @struct PacketBuffer
         * @brief Represents a buffer for packets received from the neighbor.
         */
        struct PacketBuffer 
        {
            ByteString neighborIp;  ///< IP address of the neighbor.
            EigrpHeader eigrp;      ///< EIGRP packet header.
        };
        std::map<uint32_t, PacketBuffer> packetBuffer; ///< Buffer for packets from neighbors.

        // Acknowledgements
        std::vector<uint32_t> pendingAcks; ///< List of pending ACKs.

        // RTT (Route-Trip Time) Estimation
        double srtt = 1.0; ///< Smoothed RTT
        double rttvar = 0.5; //< RTT variance
        double rto = 1.5; ///< Retransmission timeout

        // Timers
        uint32_t holdTimerId = 0; ///< Hold timer ID.
        uint16_t holdTime; ///< Hold time in seconds.
        std::chrono::steady_clock::time_point lastHeard; ///< Last heard time point.
        std::unordered_map<uint32_t, uint32_t> retransmissionTimers; ///< Map of sequence numbers to retransmission timer IDs.

        // Synchronization
        std::shared_mutex neighborDataMutex; ///< Protects neighbor-specific data.
        std::shared_mutex macMutex; ///< Neighbor's MAC mutex.
        std::condition_variable cv; ///< Condition variable for synchronization

        // Authentication
        uint8_t authKeyId = 1; ///< Authentication key ID.
        ByteString authKey; ///< Authentication key.
        bool authenticationEnabled = false; ///< Indicates if authentication is enabled.
        AuthType authType = AuthType::NONE; ///< Type of authentication.

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
                Packet(EigrpHeader eigrp, ByteString destination, std::vector<RoutingTable::Eigrp> routes = {}, bool isRemove = false)
                    : eigrp(eigrp), destination(destination), updatedRoutes(routes), remove(isRemove) {}
                Packet() = default;

                EigrpHeader eigrp = EigrpHeader(); ///< EIGRP packet header.
                ByteString destination{}; ///< Destination IP address.
                std::vector<RoutingTable::Eigrp> updatedRoutes{}; ///< Updated routes in the packet.
                bool remove = false; ///< Indicates if the packet is for route removal.
            };

            Packet packet; ///< Reliable packet information.
            std::chrono::steady_clock::time_point sendTime; ///< Time the packet was sent.
            uint8_t retransmissionCount; ///< Number of retransmissions.
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

        /**
         * @struct AdvertisedRoute
         * @brief Tracks advertised routes and their states.
         */
        struct AdvertisedRoute
        {
            RoutingTable::Eigrp route; ///< Advertised route information.
            bool active;               ///< Indicates if the route is currently active.
            bool pendingUpdate;        ///< Indicates if there is a pending update for the route.
            bool removePending;        ///< Indicates if the route is pending removal (Withdraw).
        };

        // Routing Updates
        std::unordered_map<uint32_t, Sequence> sequenceList; ///< Map of sequence numbers to sequence flags.
        std::unordered_map<uint32_t, std::vector<RoutingTable::Eigrp>> routingBuffers; ///< Buffer for routing updates.
        std::unordered_map<ByteString, AdvertisedRoute> advertisedRoutes; ///< Map of advertised routes.

        // Neighbor Flags
        bool hasMac = false; ///< Indicates if MAC address is known.
        bool isInit = false; ///< Initialization flag.
        bool isGracfullyRestarting = false; ///< Gracefully restarting.
        uint32_t lastReceivedSequenceNumber = 0; ///< Last received sequence number.
        std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> missingPacketTimestamps; ///< Timestamps for missing packets.
    
        // Threads and Worker Management
        std::thread workerThread; ///< Worker thread for neighbor operations.
        std::atomic<bool> workerActive = false; ///< Indicates if the worker thread is active.
    
        /**
         * @brief Default constructor.
         */
        NeighborInfo() = default;

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
        RoutingTable::Eigrp route; ///< Route information.
        bool distrubuted = false; ///< Indicates if the route has been distributed.
        
    };

    /**
     * @struct EigrpConfigs
     * @brief Configuration settings for the EIGRP process.
     */
    struct EigrpConfigs
    {
        uint8_t maxPaths = 4; ///< Maximum number of equal-cost paths.
        uint8_t adminDistance = 90; ///< Administrative distance for internal routes.
        uint8_t externalAdminDistance = 170; ///< Administrative distance for external routes.
        uint8_t summaryAdminDistance = 90; ///< Administrative distance for summary routes.
        uint8_t defaultAdminDistance = 90; ///< Administrative distance for default routes.
        uint8_t variance = 1; ///< Variance for unequal-cost load balancing.
        uint8_t defaultMask = 0; ///< Default network mask.
        uint8_t trafficShare = 0; ///< Traffic sharing mode.
        uint16_t activeTime = 180; ///< Active time in seconds.
        uint16_t stuckInActiveTime = 60; ///< Stuck-in-active time in seconds.
        uint32_t redistributionMetricOffset = 0.0; ///< Metric offset for redistribution.
        uint32_t wideMetric = 0; ///< Wide metric setting.
        bool logNeighborChanges = true; ///< Enable logging of neighbor changes.
        bool advertiseDefault = false; ///< Advertise default route.
        bool activeTimerEnabled = true; ///< Enable active timers.
        bool autoSummarizationEnabled = false; ///< Enable auto-summarization.
        std::vector<SummaryRoute> summaryRoutes; ///< List of summary routes.
        std::vector<Network> networks; ///< List of configured networks.
        ByteString defaultNetwork; ///< Default network.
        TrafficShareMode trafficShareMode = TrafficShareMode::Balenced; ///< Traffic sharing mode.
        KValue kvalue; ///< K-values for metric calculation.
        StubConfig stubConfig; ///< Stub routing configuration.
        AuthKey authKey; ///< Authentication key.
    };

    /**
     * @struct InterfaceConfigs
     * @brief Configuration settings for an EIGRP interface.
     */
    struct InterfaceConfigs
    {
        uint8_t reliability = 255; ///< Reliability value.
        uint8_t load = 1; ///< Load value.
        uint8_t DSCP = 0; ///< Differentiated Services Code Point.
        uint8_t interfaceMask; ///< Interface subnet mask.
        uint16_t helloTime = 5; ///< Hello interval in seconds.
        uint16_t holdTime = 15; ///< Hold time in seconds.
        uint16_t mtu = 1500; ///< Maximum Transmission Unit.
        bool splitHorizon = true; ///< Enable split horizon.
        bool isPassive = false; ///< Enable passive mode.
        std::unordered_map<uint32_t, uint32_t> retransmissionTimers; ///< Retransmission timers.
        Mode interfaceMode = Mode::MULTIPOINT; ///< Interface mode.
        ByteString interfaceAddress; ///< Interface IP address.
    };
}

struct IpInfo;

namespace Protocol 
{
    class Eigrp;
    class EigrpInterface;
    class TopologyTable;

    /**
     * @struct EigrpAutonomousSystems
     * @brief Manages multiple Autonomous Systems within the EIGRP process.
     */
    struct EigrpAutonomousSystems
    {
        std::unordered_map<AddressFamily, std::shared_ptr<Protocol::Eigrp>> addressFamilies; ///< Map of address families to EIGRP instances.
        EigrpConfigs::EigrpMode mode; ///< Operational mode of EIGRP (NAMED/CLASSIC).
    };

    /**
     * @struct EigrpInstance
     * @brief Represents an instance of the EIGRP process.
     */
    struct EigrpInstance
    {
        std::unordered_map<uint32_t, std::shared_ptr<EigrpAutonomousSystems>> autonomousSystems; ///< Map of Autonomous Systems by AS number.
        bool isShutdown = false; ///< Indicates if the EIGRP instance is shut down.
    };

    /**
     * @struct EigrpInterfaceInstance
     * @brief Represents EIGRP interfaces for IPv4 and IPv6.
     */
    struct EigrpInterfaceInstance
    {
        std::shared_ptr<Protocol::EigrpInterface> IPv4; ///< Shared pointer to the IPv4 EIGRP interface.
        std::shared_ptr<Protocol::EigrpInterface> IPv6; ///< Shared pointer to the IPv6 EIGRP interface.
    };

    /**
     * @class EigrpInterface
     * @brief Represents an interface participating in the EIGRP process.
     *
     * The EigrpInterface class manages EIGRP operations specific to a network interface,
     * including sending and receiving EIGRP packets, maintaining neighbor relationships,
     * handling routing updates, and managing timers and retransmissions.
     */
    class EigrpInterface : public std::enable_shared_from_this<Protocol::EigrpInterface> 
    {
    protected:
        EigrpConfigs::InterfaceConfigs configs; ///< Configuration settings for the interface.
    public:
        Eigrp* eigrpProcess; ///< Reference to the EIGRP process.

        /**
         * @brief Constructs an EigrpInterface instance.
         *
         * Initializes the EigrpInterface with the provided EIGRP process and network interface.
         * Sets up necessary configurations and starts the Hello timer.
         *
         * @param eigrpSystem Reference to the EIGRP process.
         * @param interface Shared pointer to the network interface.
         */
        EigrpInterface(Eigrp& eigrpSystem, std::shared_ptr<Interface> interface);

        /**
         * @brief Destructor for EigrpInterface.
         *
         * Cancels all active timers, stops the worker thread, and performs necessary cleanup
         * to gracefully terminate the EIGRP interface operations.
         */
        ~EigrpInterface();

        /**
         * @brief Processes an incoming EIGRP packet.
         *
         * Determines the type of the received EIGRP packet and dispatches it to the appropriate
         * handler function (e.g., Hello, Update, Query, Reply, ACK).
         *
         * @param eigrpPacket Pointer to the received EIGRP packet header.
         * @param neighborIp IP address of the neighbor that sent the packet.
         */
        void processPacket(const EigrpHeader* eigrpPacket, const ByteString neighborIp);

        /**
         * @brief Initializes a neighbor's information.
         *
         * Sets up the initial state for a new neighbor, including starting the initialization
         * sequence, setting roles, and preparing for communication.
         *
         * @param neighbor Shared pointer to the neighbor's information.
         */
        void initializeNeighbor(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor);

        /**
         * @brief Changes the state of a neighbor during initialization.
         *
         * Transitions the neighbor's state machine to a new state, ensuring proper synchronization
         * and handling of any necessary actions during the state change.
         *
         * @param neighbor Shared pointer to the neighbor's information.
         * @param newState New state to transition to.
         */
        void changeNeighborState(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor, EigrpConfigs::NeighborState newState);

        /**
         * @brief Processes incoming Hello packets from a neighbor.
         *
         * Handles the reception of Hello packets, updating neighbor states, exchanging Router IDs,
         * and maintaining the neighbor relationship.
         *
         * @param receivedHello Pointer to the received Hello packet header.
         * @param neighborIp IP address of the neighbor.
         */
        void processHello(const EigrpHeader* receivedHello, const ByteString neighborIp);

        /**
         * @brief Processes incoming Update packets from a neighbor.
         *
         * Parses the Update packet, extracts routing information, updates the topology table,
         * and sends acknowledgements as necessary.
         *
         * @param receivedUpdate Pointer to the received Update packet header.
         * @param neighborIp IP address of the neighbor.
         */
        void processUpdate(const EigrpHeader* receivedUpdate, const ByteString neighborIp);

        /**
         * @brief Processes buffered Update packets for a neighbor.
         *
         * Sends any buffered Update packets that were previously held due to pending acknowledgements
         * or state conditions.
         *
         * @param neighbor Shared pointer to the neighbor's information.
         */
        void processBufferedPackets(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor);

        /**
         * @brief Processes an incoming ACK from a neighbor.
         *
         * Validates the acknowledgement, removes the corresponding packet from the retransmission queue,
         * and updates RTT estimates.
         *
         * @param sequenceNumber Sequence number being acknowledged.
         * @param neighborIp IP address of the neighbor.
         */
        void processAck(const ByteString sequenceNumber, const ByteString neighborIp);

        /**
         * @brief Processes an incoming Query packet from a neighbor.
         *
         * Handles the Query by checking the feasibility of the routes in question and responding
         * with appropriate Reply packets.
         *
         * @param receivedQuery Pointer to the received Query packet header.
         * @param neighborIp IP address of the neighbor.
         */
        void processQuery(const EigrpHeader* receivedQuery, const ByteString neighborIp);

        /**
         * @brief Processes an incoming Reply packet from a neighbor.
         *
         * Updates the topology table based on the Reply, recalculates the best routes,
         * and resolves any pending queries.
         *
         * @param recievedReply Pointer to the received Reply packet header.
         * @param neighborIp IP address of the neighbor.
         */
        void processReply(const EigrpHeader* recievedReply, const ByteString neighborIp);

        /**
         * @brief Sends an ACK to a neighbor.
         *
         * Constructs and sends an ACK packet to confirm the receipt of a specific Update or Query packet.
         *
         * @param neighborIp IP address of the neighbor.
         * @param sequenceNumber Sequence number to acknowledge.
         */
        void sendAckToNeighbor(const ByteString neighborIp, uint32_t sequenceNumber);

        /**
         * @brief Sends an Update packet to a neighbor.
         *
         * Constructs and sends an Update packet containing routing information to the specified neighbor.
         * Handles different types of updates based on the updateType parameter.
         *
         * @param neighborIp IP address of the neighbor.
         * @param routes Routes to include in the update.
         * @param updateType Type of the update (FULL/QUERY/RESPONSE_QUERY/PARTIAL/TRIGGERED/WITHDRAW).
         * @param restart Indicates if this update is part of a restart.
         * @param conditional Indicates if this update is conditional.
         * @param conditionalNeighbors List of neighbors for conditional updates.
         */
        void sendUpdateToNeighbor(const ByteString neighborIp, const std::vector<RoutingTable::Eigrp> &routes, EigrpConfigs::UpdateType updateType, bool restart = false, bool conditional = false, std::vector<ByteString> conditionalNeighbors = {});

        /**
         * @brief Sends a Query packet to all neighbors except the originator.
         *
         * Initiates the process of querying other neighbors about the failed routes to determine
         * alternate paths or confirm route removal.
         *
         * @param failedRoutes Routes that have failed and need to be queried.
         * @param originNeighborIp IP address of the neighbor that originated the query.
         */
        void sendQueryToNeighbors(const std::vector<RoutingTable::Eigrp>& failedRoutes, const ByteString& originNeighborIp = "");

        /**
         * @brief Sends a Query packet to a specific neighbor.
         *
         * Directly queries a single neighbor about specific failed routes to ascertain their status
         * and potential alternatives.
         *
         * @param neighborIp IP address of the neighbor.
         * @param failedRoutes Routes that have failed and need to be queried.
         */
        void sendQueryToNeighbor(const ByteString neighborIp, const std::vector<RoutingTable::Eigrp>& failedRoutes);

        /**
         * @brief Sends a Reply packet to a neighbor in response to a Query.
         *
         * Responds to a neighbor's Query packet by providing detailed routing information about
         * the requested routes.
         *
         * @param neighborIp IP address of the neighbor.
         * @param routes Routes to include in the reply.
         */
        void sendReplyToNeighbor(const ByteString neighborIp, const std::vector<RoutingTable::Eigrp>& routes);

        /**
         * @brief Checks if a timeout has occurred for missing packets.
         *
         * Determines whether a retransmission timeout has been reached for a specific packet.
         * If a timeout is detected, appropriate actions such as retransmission or neighbor
         * down status updates are triggered.
         *
         * @param sequenceNumber Sequence number of the packet.
         * @param neighbor Shared pointer to the neighbor's information.
         * @return True if a timeout has occurred, false otherwise.
         */
        bool isTimeoutForMissing(uint32_t sequenceNumber, std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor);

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
        size_t calculateMaxRoutesPerPacket(AddressFamily af, bool isExernal);

        /**
         * @brief Encodes a Query option for a specific route.
         *
         * Serializes the Query option for a given route into a ByteString suitable for
         * inclusion in an EIGRP packet.
         *
         * @param route Route information.
         * @return Encoded Query option as ByteString.
         */
        ByteString encodeQueryOption(RoutingTable::Eigrp route);

        /**
         * @brief Encodes a Route option for a specific route.
         *
         * Serializes the Route option, including route metrics and other relevant data,
         * into a ByteString for transmission.
         *
         * @param route Route information.
         * @param removed Indicates if the route is being removed.
         * @return Encoded Route option as ByteString.
         */
        ByteString encodeRouteOption(const RoutingTable::Eigrp& route, bool removed = false);

        /**
         * @brief Encodes an External Route option for a specific route.
         *
         * Serializes the External Route option, which includes additional metrics for external
         * routes, into a ByteString for inclusion in an EIGRP packet.
         *
         * @param route External route information.
         * @param removed Indicates if the route is being removed.
         * @return Encoded External Route option as ByteString.
         */
        ByteString encodeExternalRouteOption(const RoutingTable::Eigrp& route, bool removed = false);

        /**
         * @brief Encodes a Stub option based on stub configuration.
         *
         * Constructs the Stub option TLV (Type-Length-Value) based on the provided
         * stub configuration settings.
         *
         * @param stub Stub configuration.
         * @return Encoded Stub option as ByteString.
         */
        ByteString encodeStubOption(const EigrpConfigs::StubConfig stub);

        /**
         * @brief Finds the neighbor IP address associated with a specific Query ID.
         *
         * Searches for the neighbor that originated a specific Query based on the Query ID.
         * Useful for correlating Replies to their corresponding Queries.
         *
         * @param queryId Query identifier.
         * @return Neighbor IP address if found, empty ByteString otherwise.
         */
        ByteString findQueryNeighbor(uint32_t queryId);

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
        RoutingTable::Eigrp decodeRoute(ByteString value, bool external, bool summary);

        /**
         * @brief Flags a pending update for a specific route.
         *
         * Marks a route as pending an update, indicating that further actions or acknowledgements
         * are required before the route can be fully processed or advertised.
         *
         * @param route Route information.
         * @param neighborIp IP address of the neighbor.
         */
        void flagPendingUpdate(const RoutingTable::Eigrp& route, const ByteString neighborIp);

        /**
         * @brief Updates the routing table based on received routes.
         *
         * Integrates the received routes into the local routing table, recalculates metrics,
         * and determines the best paths based on EIGRP's metric calculations and policies.
         *
         * @param routes Vector of received routes.
         * @param init Indicates if the update is part of initialization.
         * @param neighborIp IP address of the neighbor who sent the routes.
         */
        void updateRoutingTable(const std::vector<RoutingTable::Eigrp> routes, bool init, const ByteString neighborIp);

        /**
         * @brief Updates the routing table for a specific destination.
         *
         * Re-evaluates and updates the routing entry for a single destination network,
         * ensuring that the best available route is selected and maintained.
         *
         * @param destination Destination network.
         */
        void updateRoutingTableForDestination(const ByteString& destination);

        /**
         * @brief Calculates the Local Link Cost (LLC) for the interface.
         *
         * Computes the LLC based on interface metrics such as bandwidth, delay, reliability,
         * and load, contributing to the overall EIGRP metric calculation.
         *
         * @return Calculated LLC value.
         */
        uint32_t calculateLocalLinkCost();

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
         * @brief Handles the expiration of the Stuck-In-Active timer.
         *
         * Detects routes that are stuck in the active state due to unresponsive neighbors
         * and initiates necessary actions such as query retransmissions or neighbor down
         * status updates.
         */
        void handleStuckInActive();

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
        void advertiseSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute);

        /**
         * @brief Withdraws a summary route from a neighbor.
         *
         * Sends a Withdraw (WITHDRAW) packet to the neighbor to remove the previously advertised
         * summary route, ensuring that outdated or no longer valid summary routes are cleaned up.
         *
         * @param network Network address of the summary route.
         * @param mask Subnet mask of the summary route.
         */
        void withdrawSummaryRoute(const ByteString& network, uint8_t mask);

        /**
         * @brief Encodes a Summary Route for advertisement.
         *
         * Serializes the Summary Route into a format suitable for inclusion in an EIGRP
         * Update packet, consolidating route information for efficient transmission.
         *
         * @param summaryRoute Summary route information.
         * @return Encoded Summary Route as RoutingTable::Eigrp.
         */
        RoutingTable::Eigrp encodeSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute);

        /**
         * @brief Handles the removal of a neighbor by cleaning up associated routes and timers.
         *
         * Performs cleanup operations when a neighbor is removed, including removing
         * routes learned from the neighbor, cancelling active timers, and updating the
         * topology table to reflect the neighbor's departure.
         *
         * @param neighborIp IP address of the neighbor being removed.
         */
        void handleNeighborDown(const ByteString neighborIp);

        /**
         * @brief Handles the restart of a neighbor by reinitializing its state.
         *
         * Resets the neighbor's state machine and re-establishes the neighbor relationship
         * after a graceful restart, ensuring continuity in routing operations.
         *
         * @param neighborIp IP address of the neighbor restarting.
         */
        void handleNeighborRestart(const ByteString neighborIp);

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
         * @param mode Communication mode (UNICAST/MULTICAST).
         */
        void addNeighbor(const ByteString& ipAddress, const ByteString& macAddress, EigrpConfigs::CommunicationMode mode);

        /**
         * @brief Retrieves information about a specific neighbor.
         *
         * Searches for and returns the NeighborInfo structure associated with the given
         * neighbor IP address, allowing for inspection or modification of the neighbor's state.
         *
         * @param neighborIp IP address of the neighbor.
         * @return Optional shared pointer to the neighbor's information if found.
         */
        std::optional<std::shared_ptr<EigrpConfigs::NeighborInfo>> getNeighborInfo(const ByteString neighborIp);

        /**
         * @brief Resolves the MAC address of a neighbor using ARP.
         *
         * Initiates an ARP request to determine the MAC address associated with the neighbor's
         * IP address, updating the NeighborInfo structure upon successful resolution.
         *
         * @param neighbor Shared pointer to the neighbor's information.
         */
        void resolveMacAddress(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor);

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
        bool isRouteAdvertised(ByteString& network, uint8_t mask);

        /**
         * @brief Advertises a route to a neighbor.
         *
         * Sends a routing update to the specified neighbor, optionally based on certain
         * conditions or triggers, to inform them of the new or updated route.
         *
         * @param route Route information.
         * @param conditional Indicates if the advertisement is conditional.
         */
        void advertiseRouteToNeighbor(const RoutingTable::Eigrp& route, bool conditional = false);

        /**
         * @brief Withdraws a route from a neighbor.
         *
         * Sends a Withdraw (WITHDRAW) packet to the specified neighbor to inform them that
         * the route is no longer valid or has been removed from the local routing table.
         *
         * @param route Route information.
         */
        void withdrawRouteFromNeighbor(const RoutingTable::Eigrp& route);

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
         * @param neighborIp IP address of the neighbor (empty for multicast).
         * @param unicast True to send a unicast Hello, false for multicast.
         * @param update Indicates if this Hello is part of an update.
         * @param sequenceNumber Sequence number for the Hello packet.
         */
        void sendHelloPacket(ByteString neighborIp = "", bool unicast = false, bool update = false, uint32_t sequenceNumber = 0);

        /**
         * @brief Stops the Hello timer.
         *
         * Cancels the active Hello timer thread, ceasing the periodic transmission of Hello packets
         * and effectively pausing neighbor relationship maintenance.
         */
        void stopHello();

        /**
         * @brief Starts an Active timer for a failed route.
         *
         * Initiates a timer to monitor the duration a route remains in the active state,
         * prompting retries or fallback mechanisms if the route is not resolved within the
         * configured timeframe.
         *
         * @param route Failed route information.
         */
        void startActiveTimer(const RoutingTable::Eigrp& route); 

        /**
         * @brief Handles the expiration of an Active timer for a failed route.
         *
         * Responds to the expiration of an Active timer by marking the route as inactive,
         * initiating queries to neighbors, or removing the route from the routing table if
         * no viable alternatives are found.
         *
         * @param route Failed route information.
         */
        void handleActiveTimeExpire(const RoutingTable::Eigrp& route);

        /**
         * @brief Cancels an Active timer for a specific route.
         *
         * Stops and removes the Active timer associated with the specified route,
         * preventing further timeout actions for that route.
         *
         * @param destination Destination network.
         * @param mask Subnet mask of the destination.
         */
        void cancelActiveTimer(const ByteString &destination, uint8_t mask);

        /**
         * @brief Starts the Stuck-In-Active timer.
         *
         * Initiates a timer to detect routes that remain perpetually in the active state,
         * indicating potential issues with route resolution or neighbor communication.
         */
        void startStuckInActive();

        /**
         * @brief Cancels the Stuck-In-Active timer.
         *
         * Stops the Stuck-In-Active timer, preventing the protocol from flagging routes
         * as stuck in the active state.
         */
        void cancelStuckInActive();

        /**
         * @brief Starts a Hold timer for a specific neighbor.
         *
         * Initiates a Hold timer to monitor the neighbor's responsiveness. If the neighbor
         * fails to send a Hello or any EIGRP packet within the hold time, the neighbor is
         * considered down, triggering route recalculations and neighbor cleanup.
         *
         * @param neighborIp IP address of the neighbor.
         * @param holdTime Hold time in seconds.
         */
        void startHoldTimer(const ByteString neighborIp, uint16_t holdTime);

        /**
         * @brief Handles the expiration of a Hold timer for a neighbor.
         *
         * Marks the neighbor as down due to inactivity, removes associated routes, and
         * cleans up any related state information to maintain accurate routing tables.
         * 
         * @param neighborIp IP address of the neighbor.
         */
        void handleHoldTimeExpire(const ByteString neighborIp);

        /**
         * @brief Sets up a reliable packet for retransmission if needed.
         *
         * Registers a packet in the retransmission queue, ensuring that it is resent
         * if an acknowledgement is not received within the timeout period.
         *
         * @param neighbor Shared pointer to the neighbor's information.
         * @param packet Packet information.
         * @param sequenceNum Sequence number of the packet.
         */
        void setupReliablePacket(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor, const EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet packet, uint32_t sequenceNum);

        /**
         * @brief Starts a retransmission timer for reliable packet delivery.
         *
         * Initiates a timer that triggers a retransmission of a packet if an ACK is not
         * received within the specified timeout period, enhancing reliability in packet delivery.
         *
         * @param neighborIp IP address of the neighbor.
         * @param sequenceNumber Sequence number of the packet.
         * @param timeout Timeout duration in seconds.
         * @return Timer ID of the retransmission timer.
         */
        uint32_t startRetransmissionTimer(const ByteString neighborIp, const uint32_t& sequenceNumber, double timeout);

        /**
         * @brief Handles the expiration of a retransmission timer by resending the packet or marking the neighbor down.
         *
         * Responds to retransmission timeouts by either resending the packet for another attempt
         * or marking the neighbor as down if repeated failures occur, ensuring robust neighbor management.
         *
         * @param neighborIp IP address of the neighbor.
         * @param sequenceNumber Sequence number of the packet.
         */
        void handleRetransmissionTimeout(const ByteString neighborIp, const uint32_t& sequenceNumber);

        /**
         * @brief Calculates the Round-Trip Time (RTT) for a packet.
         *
         * Measures the time taken for a packet to be sent and acknowledged, updating RTT estimates
         * to inform retransmission timeouts and network performance metrics.
         * 
         * @param neighbor Shared pointer to the neighbor's information.
         * @param sequenceNumber Sequence number of the packet.
         * @return Calculated RTT in seconds.
         */
        double calculateRTT(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor, uint32_t sequenceNumber);

        /**
         * @brief Updates RTT estimates based on received ACKs.
         *
         * Refines the RTT and RTT variance calculations upon receiving an ACK, allowing for
         * more accurate retransmission timeouts and improved protocol responsiveness.
         *
         * @param neighbor Shared pointer to the neighbor's information.
         * @param sequenceNumber Sequence number of the acknowledged packet.
         */
        void updateRTTEstimate(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor, uint32_t sequenceNumber);

        /**
         * @brief Retrieves the multicast address based on the address family.
         *
         * Determines and returns the appropriate multicast address for EIGRP packet
         * transmission based on whether IPv4 or IPv6 is being used.
         *
         * @return ByteString representing the multicast address.
         */
        ByteString getMulticast();

        /**
         * @brief Retrieves a shared pointer to the interface configurations.
         *
         * Provides access to the current interface's configuration settings, allowing
         * for inspection or modification as needed.
         *
         * @return Shared pointer to InterfaceConfigs.
         */
        inline std::shared_ptr<EigrpConfigs::InterfaceConfigs> getConfigs() { return std::make_shared<EigrpConfigs::InterfaceConfigs>(configs); }

        // Authentication
        bool isNeighborAuthenticated(const ByteString neighborIp);
        void configureAuthentication(const ByteString neighborIp, uint8_t keyId, const ByteString& key, bool enable);
        ByteString serializeEigrpHeader(const EigrpHeader& eigrp, bool exclusiveAuthTLV);
        EigrpHeader::Option generateAuthenticatedTLV(const EigrpHeader& eigrp, const std::shared_ptr<EigrpConfigs::NeighborInfo>& neighbor);
        
        std::weak_ptr<Interface> currentInterface; ///< Weak pointer to the current network interface.
        IpInfo* currentInterfaceInfo; ///< Weak pointer to the current interface's IP information.
        
        // Neighbor management
        std::mutex initMutex; ///< Mutex for initialization operations.
        std::mutex ackMutex; ///< Mutex for ACK operations.
        std::shared_mutex neighborMutex; ///< Shared mutex for neighbor operations.
        std::unordered_map<ByteString, std::shared_ptr<EigrpConfigs::NeighborInfo>> neighbors; ///< Map of neighbor IPs to their information.

    private:

        // Active Timers
        std::chrono::steady_clock::time_point helloStartTime; ///< Start time for the Hello timer.
        std::unordered_map<ByteString, uint32_t> activeTimers; ///< Map of active timers for routes.
        bool runTimers = true; ///< Flag to indicate if timers should continue running.
        bool helloTimerActive = false; ///< Indicates if the Hello timer is active.

        // Timer IDs
        uint32_t helloTimerId = 0; ///< Timer ID for the Hello timer.
        uint32_t activeTimerId = 0; ///< Timer ID for Active timers.
        uint32_t stuckInActiveTimerId = 0; ///< Timer ID for Stuck-In-Active timer.

        // Lists
        std::unordered_map<uint32_t, std::pair<ByteString, uint32_t>> outstandingReplies; ///< Map of outstanding query IDs to neighbor IPs and timer IDs.

        // Route Buffer
        std::vector<RoutingTable::Eigrp> routeBuffer = {}; ///< Buffer for routing updates.

        // Mutexes
        std::mutex helloTimerMutex; ///< Mutex for Hello timer operations.
        std::mutex activeTimerMutex; ///< Mutex for Active timer operations.

        // Sequence number
        uint32_t nextSequenceNumber = 1; ///< Next sequence number for packets.
        uint32_t conditionalReceive = 0; ///< Holds conditional receive sequence number.
        std::shared_mutex seqMutex; ///< Mutex for sequence number operations.
    };


    /**
     * @class Eigrp
     * @brief Manages EIGRP protocol operations including initialization, shutdown, network configuration, and route calculations.
     *
     * The Eigrp class is responsible for overseeing the overall EIGRP operations, handling
     * network configurations, managing EIGRP interfaces, processing routing updates, and
     * maintaining the routing table. It supports both Classic and Named EIGRP modes.
     */
    class Eigrp {
    protected:
        EigrpConfigs::EigrpConfigs configs; ///< Configuration settings for EIGRP.
        std::unordered_map<std::shared_ptr<Interface>, EigrpConfigs::InterfaceConfigs> interfaceConfigs; ///< Map of interfaces to their configurations.
    public:

        /**
         * @brief Constructs an Eigrp instance.
         *
         * Initializes the EIGRP process with the specified Autonomous System number and
         * address family, setting up necessary configurations and preparing for network operations.
         *
         * @param as Autonomous System number.
         * @param af Address family (IPv4/IPv6).
         */
        Eigrp(uint32_t& as, AddressFamily af);

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
        void eigrpHello(EigrpHeader& eigrp, EigrpInterface* eigrpInt, ByteString neighborIp, uint32_t sequenceNumber = 0, bool ack = false, bool update = false);

        /**
         * @brief Configures an EIGRP Update packet with specific settings.
         *
         * Constructs and sends an Update packet containing routing information to neighbors.
         * The Update packet can carry various types of routing information based on the
         * specified parameters, facilitating route advertisement and query responses.
         *
         * @param eigrp Reference to the EIGRP header.
         * @param sequenceNum Sequence number for the Update packet.
         * @param init Indicates if this Update is part of initialization.
         * @param conditional Indicates if this Update is conditional.
         * @param restart Indicates if this Update is part of a restart.
         * @param endoftable Indicates if this Update marks the end of the table.
         * @param query Indicates if this Update is a Query.
         * @param reply Indicates if this Update is a Reply to a Query.
         */
        void eigrpUpdate(EigrpHeader& eigrp, uint32_t sequenceNum, bool init = false, bool conditional = false, bool restart = false, bool endoftable = false, bool query = false, bool reply = false);

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
        bool testAddress(const ByteString& testIp);

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
        uint32_t calculateMetric(uint32_t bandwidth, uint8_t load, uint32_t delay, uint8_t reliability, uint8_t hopCount = 0);

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
        ByteString calculateParameters(uint16_t holdTime);

        /**
         * @brief Updates the routing table with connected routes.
         *
         * Integrates connected network routes into the EIGRP routing table, allowing
         * EIGRP to advertise and route traffic through these directly connected networks.
         *
         * @param eigrpInterface Shared pointer to the EIGRP interface (optional).
         */
        void updateRoutingTableForConnected(const std::shared_ptr<EigrpInterface> eigrpInterface = nullptr);

        /**
         * @brief Handles interface changes by updating EIGRP configurations.
         *
         * Responds to network interface modifications (e.g., addition/removal, IP changes)
         * by re-evaluating EIGRP configurations and adjusting routing operations to maintain
         * accurate and efficient routing paths.
         *
         * @param interfacePtr Pointer to the interface that changed.
         * @param af Address family of the interface.
         */
        void onInterfaceChange(Interface* interfacePtr, AddressFamily af);

        /**
         * @brief Notifies all EIGRP interfaces about routing changes.
         *
         * Broadcasts routing updates to all active EIGRP interfaces, informing neighbors
         * of new, updated, or removed routes to ensure consistent and synchronized routing
         * information across the network.
         *
         * @param changedRoutes Vector of routes that have changed.
         * @param isRemoval Indicates if the routes are being removed.
         * @param init Indicates if this notification is part of initialization.
         */
        void notifyRoutingChange(const std::vector<RoutingTable::Eigrp>& changedRoutes, bool isRemoval = false, bool init = false);

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
        void redistributeRoute(const ByteString &destination, uint8_t mask, const ByteString &protocol);

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
        void addSummaryRoute(const ByteString& network, uint8_t mask, bool isAuto = false);

        /**
         * @brief Removes a summary route from the EIGRP configuration.
         *
         * Deletes a previously configured summary route, ensuring that outdated or
         * unnecessary summary routes are no longer advertised or maintained.
         *
         * @param network Network address of the summary route.
         * @param mask Subnet mask of the summary route.
         */
        void removeSummaryRoute(const ByteString& network, uint8_t mask);

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
        bool isRouteSummarized(const ByteString& network, uint8_t mask);

        /**
         * @brief Updates interfaces with a newly added summary route.
         *
         * Propagates the addition of a summary route across all active EIGRP interfaces,
         * ensuring that neighbors receive the updated summary information.
         *
         * @param summaryRoute Summary route information.
         */
        void updateInterfacesWithSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute);

        /**
         * @brief Updates interfaces after removing a summary route.
         *
         * Notifies all active EIGRP interfaces of the removal of a summary route, ensuring
         * that neighbors cease advertising the outdated summary information.
         *
         * @param network Network address of the removed summary route.
         * @param mask Subnet mask of the removed summary route.
         */
        void updateInterfacesAfterRemovingSummaryRoute(const ByteString& network, uint8_t mask);

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
         * @brief Sets the EIGRP process as a stub.
         *
         * Configures the EIGRP process to operate in stub mode, limiting the types of routes
         * advertised to reduce routing protocol complexity and overhead, especially in hub-and-spoke
         * network topologies.
         * 
         * @param isStub True to set as stub, false otherwise.
         * @param advertiseConnected Advertise connected routes.
         * @param advertiseStatic Advertise static routes.
         * @param advertiseSummary Advertise summary routes.
         * @param advertiseRedistributed Advertise redistributed routes.
         */
        void setStub(bool isStub, bool advertiseConnected = true, bool advertiseStatic = true, bool advertiseSummary = true, bool advertiseRedistributed = true);

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
         * @brief Adds a default route to the routing table.
         *
         * Inserts a default route (0.0.0.0/0 for IPv4 or ::/0 for IPv6) into the EIGRP
         * routing table, allowing for the forwarding of traffic to destinations not explicitly
         * listed in the routing table.
         */
        void addDefaultRoute();

        /**
         * @brief Removes the default route from the routing table.
         *
         * Deletes the default route from the EIGRP routing table, ceasing the forwarding
         * of unspecified traffic through the default path.
         */
        void removeDefaultRoute();

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
         * @brief Initiates a graceful restart of the EIGRP process.
         *
         * Performs a controlled restart of the EIGRP process, maintaining neighbor relationships
         * and minimizing routing disruptions by retaining routing information during the restart.
         */
        void gracefulRestart();

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
        std::vector<EigrpConfigs::SummaryRoute> summaryRoutes; ///< List of configured summary routes.
        std::unordered_map<uint8_t, std::shared_ptr<EigrpInterface>> eigrpInterfaceList{}; ///< Map of EIGRP interfaces by identifier.
        std::unordered_map<ByteString, std::unordered_map<ByteString, std::shared_ptr<EigrpConfigs::NeighborInfo>>> neighborBackup; ///< Backup information for neighbors.

        // Eigrp process mutex
        std::shared_mutex eigrpMutex; ///< Mutex for synchronizing EIGRP process operations.

        // Eigrp data mutex
        std::shared_mutex eigrpDataMutex; ///< Mutex for synchronizing access to EIGRP data structures.

        // Configurations for EIGRP
        /**
         * @brief Retrieves the Address Family used by the EIGRP process.
         *
         * Returns the address family (e.g., IPv4 or IPv6) that the EIGRP process is
         * operating under, affecting packet formats and routing behaviors.
         *
         * @return AddressFamily enum value.
         */
        AddressFamily getAddressFamily() const { return addressFamily; }

        /**
         * @brief Retrieves the Autonomous System number.
         *
         * Returns the AS number assigned to this EIGRP process, used to distinguish
         * EIGRP instances and manage route distribution.
         *
         * @return Autonomous System number.
         */
        inline uint32_t getAsNumber() { std::shared_lock<std::shared_mutex> lock(eigrpDataMutex); return asNumber; }

        /**
         * @brief Retrieves the Virtual Router ID.
         *
         * Provides the virtual Router ID assigned to the EIGRP process, used in
         * routing advertisements and neighbor identification.
         *
         * @return ByteString representing the virtual Router ID.
         */
        inline ByteString getVirtualRouterID() { std::shared_lock<std::shared_mutex> lock(eigrpDataMutex); return virtualRouterID; }

        /**
         * @brief Retrieves the Router ID.
         *
         * Returns the Router ID configured for the EIGRP process, serving as a unique
         * identifier within the EIGRP routing domain.
         *
         * @return ByteString representing the Router ID.
         */
        inline ByteString getRouterID() { std::shared_lock<std::shared_mutex> lock(eigrpDataMutex); return routerID.ID; }

        /**
         * @brief Retrieves the EIGRP configurations.
         *
         * Provides access to the current EIGRP configuration settings, allowing for
         * inspection and modification as needed.
         *
         * @return Shared pointer to the EIGRP configuration settings.
         */
        inline std::shared_ptr<EigrpConfigs::EigrpConfigs> getConfigs() { return std::make_shared<EigrpConfigs::EigrpConfigs>(configs); }

        // Topology Table
        std::unique_ptr<Protocol::TopologyTable> topologyTable; ///< Unique pointer to the EIGRP topology table.

    private:
        AddressFamily addressFamily; ///< Address family (IPv4/IPv6).
        ByteString virtualRouterID = ByteString(2, '\x00'); ///< Virtual Router ID.
        uint32_t asNumber; ///< Autonomous System number.
        EigrpConfigs::RouterID routerID; ///< Router ID configuration.
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
        ClassicEigrp(uint32_t& as, AddressFamily af) : Eigrp(as, af) {}

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
        ByteString processName; ///< Name of the Named EIGRP process.

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
         */
        NamedEigrp(uint32_t& as, AddressFamily af, const ByteString& name);

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
        void configureInterface(const ByteString& interfaceName, const EigrpConfigs::InterfaceConfigs& configs);
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
            uint32_t feasibleDistance; ///< Feasible distance of the route.
            uint32_t reportedDistance; ///< Reported distance from the neighbor.
            uint8_t hopCount; ///< Number of hops to the destination.
            uint8_t adminDistance = 90; ///< Administrative distance.
            ByteString nextHop; ///< Next hop IP address.
            bool isSuccessor; ///< Indicates if this route is a successor.
            bool isFeasibleSuccessor; ///< Indicates if this route is a feasible successor.
            std::chrono::steady_clock::time_point lastUpdate; ///< Timestamp of the last update.
        };

        /**
         * @struct TopologyEntry
         * @brief Represents an entry in the topology table for a specific destination.
         */
        struct TopologyEntry {
            ByteString destination; ///< Destination network.
            uint8_t prefixLength; ///< Prefix length of the destination.
            std::unordered_map<ByteString, RouteInfo> routesByNeighbor; ///< Routes learned from each neighbor.
            bool isActive; ///< Indicates if the route is active.

            // Timers for Active and Stuck-In-Active
            uint32_t activeTimerId = 0; ///< Timer ID for active routes.
            uint32_t StuckInActiveTimerId = 0; ///< Timer ID for stuck-in-active routes.

            uint32_t bestFD; ///< Best feasible distance for the route.

            std::vector<ByteString> feasibleSuccessors; ///< List of feasible successor neighbors.
            std::vector<ByteString> successors; ///< List of successor neighbors.
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

        /**
         * @brief Adds or updates a route in the topology table.
         *
         * Inserts a new route or updates an existing route in the topology table based
         * on information received from a neighbor, adjusting route metrics and statuses
         * as necessary.
         *
         * @param destination Destination network.
         * @param prefixLength Prefix length of the destination.
         * @param routeInfo Information about the route.
         * @param neighborIp IP address of the neighbor.
         */
        void addOrUpdateRoute(const ByteString destination, uint8_t prefixLength, const RouteInfo& routeInfo, const ByteString neighborIp);

        /**
         * @brief Removes all routes associated with a specific neighbor.
         *
         * Deletes all routes learned from the specified neighbor, ensuring that stale
         * or invalid routes are no longer present in the topology table.
         *
         * @param neighborIp IP address of the neighbor.
         */
        void removeRoutesFromNeighbor(const ByteString neighborIp);

        /**
         * @brief Finds the best route for a given destination considering variance.
         *
         * Evaluates all available routes to a destination, considering EIGRP's
         * variance setting to allow unequal-cost load balancing, and identifies the
         * optimal route based on feasible distance and other metrics.
         *
         * @param destination Destination network.
         * @param variance Variance factor for route selection.
         * @return Optional RouteInfo if a best route is found.
         */
        std::optional<RouteInfo> findBestRoute(const ByteString& destination, uint8_t variance);

        /**
         * @brief Updates successors and feasible successors for a topology entry.
         *
         * Recalculates and assigns successor and feasible successor routes for the
         * specified topology entry, ensuring optimal route selection and redundancy.
         *
         * @param entry Shared pointer to the TopologyEntry.
         */
        void updateSuccessorAndFeasibleSuccessors(std::shared_ptr<TopologyEntry>& entry);

        /**
         * @brief Retrieves the topology entry for a specific route.
         *
         * Searches for and returns the topology entry associated with the given destination
         * network, facilitating detailed route inspections and modifications.
         *
         * @param destination Destination network.
         * @return Shared pointer to the TopologyEntry or nullptr if not found.
         */
        std::shared_ptr<TopologyEntry> getEntryForRoute(const ByteString& destination);

        /**
         * @brief Handles the failure of a route by removing it from the topology table.
         *
         * Removes the specified route from the topology table due to neighbor failure,
         * triggering route recalculations and potential advertisements to other neighbors.
         *
         * @param destination Destination network.
         * @param failedNeighborIp IP address of the failed neighbor.
         */
        void handleRouteFailure(const ByteString& destination, const ByteString& failedNeighborIp);

        /**
         * @brief Marks a route as passive, disabling further updates.
         *
         * Sets the specified route to a passive state, preventing it from being updated
         * or advertised further, often used during route maintenance or controlled shutdowns.
         *
         * @param destination Destination network.
         * @param eigrp Pointer to the EIGRP interface.
         */
        void markRouteAsPassive(const ByteString& destination, EigrpInterface* eigrp);

        /**
         * @brief Removes a topology entry for a specific destination.
         *
         * Deletes the entire topology entry for the given destination network,
         * effectively removing all associated routing information.
         *
         * @param destination Destination network.
         */
        void removeEntry(const ByteString& destination);

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
        void handleNeighborDown(const ByteString neighborIp);

        /**
         * @brief Retrieves all topology entries.
         *
         * Provides access to the entire topology table, allowing for comprehensive
         * inspections, exports, or modifications of routing information.
         *
         * @return Reference to the map of topology entries.
         */
        std::unordered_map<ByteString, std::shared_ptr<TopologyEntry>>& getTopologyEntries() { std::lock_guard<std::mutex> lock(tableMutex); return topologyEntries; }

        uint8_t staleThreshold = 15; ///< Threshold in seconds to consider a route stale.
    
    private:
        std::mutex tableMutex; ///< Mutex for synchronizing access to the topology table.
        std::unordered_map<ByteString, std::shared_ptr<TopologyEntry>> topologyEntries; ///< Map of destination networks to their topology entries.
        std::shared_ptr<Eigrp> eigrpProcess; ///< Shared pointer to the EIGRP process.
    };
}

/**
 * @var currentCommunicationMode
 * @brief Current communication mode for EIGRP neighbors.
 *
 * Points to the active communication mode (UNICAST or MULTICAST) used by EIGRP
 * interfaces for neighbor communications and packet transmissions.
 */
extern EigrpConfigs::CommunicationMode* currentCommunicationMode;

/**
 * @var currentEigrp
 * @brief Global pointer to the current EIGRP autonomous system.
 *
 * Maintains a weak reference to the active EIGRP autonomous system, allowing
 * for global access without ownership concerns.
 */
extern std::weak_ptr<Protocol::Eigrp> currentEigrp;

/**
 * @var currentEigrpInstance
 * @brief Global pointer to the current EIGRP instance.
 *
 * Holds a weak reference to the active EIGRP instance, facilitating access to
 * EIGRP operations and configurations from various parts of the program.
 */
extern std::weak_ptr<Protocol::EigrpInstance> currentEigrpInstance;

/**
 * @var eigrpList
 * @brief Map of EIGRP instances by AS number.
 *
 * Associates each Autonomous System number with its corresponding EIGRP instance,
 * enabling management and retrieval of multiple EIGRP processes within the same
 * routing domain.
 */
extern std::map<ByteString, std::shared_ptr<Protocol::EigrpInstance>> eigrpList;

/**
 * @var eigrpAutonomousSystems
 * @brief Map of all EIGRP autonomous systems.
 *
 * Tracks all autonomous systems configured in the EIGRP process, allowing for
 * efficient management of routing domains and inter-AS route advertisements.
 */
extern std::map<uint32_t, std::weak_ptr<Protocol::EigrpAutonomousSystems>> eigrpAutonomousSystems;

/**
 * @brief Updates the EIGRP interface list based on interface changes.
 * @param interface Pointer to the interface that has changed.
 *
 * Responds to changes in network interfaces (such as additions, removals, or IP
 * address updates) by re-evaluating EIGRP configurations and updating the active
 * EIGRP interface list accordingly.
 */
void updateEigrpInterface(Interface* interface);

#endif // EIGRP_H
