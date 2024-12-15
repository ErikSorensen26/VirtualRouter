#pragma once

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
#include <Interface.h>
#include <RoutingTable.h>
#include <TimeManager.h>
#include <Authentication.hpp>
#include <unordered_map>
#include <ByteString.hpp>

extern std::shared_mutex globalEigrpMutex;

// Config schemas for EIGRP class
namespace EigrpConfigs
{
    struct Network 
    {
        ByteString ip{};
        ByteString mask{};
    };
    struct RouterID
    {
        ByteString ID{};
        bool isStatic = false;
    };
    struct SummaryRoute 
    {
        ByteString network;
        int mask;
        bool isAuto = false;
    };
    struct KValue 
    {
        int k1_Bandwidth = 1;
        int k2_Load = 0;
        int k3_Delay = 1;
        int k4_Reliability = 0;
        int k5_MTU = 0;
        int k6_Power = 0;
    };
    struct AuthKey
    {
        int keyId;
        ByteString key;
    };
    struct Sequence 
    {
        bool init;
        bool conditionalReceive;
        bool endOfTable;

        Sequence() : init(false), conditionalReceive(false), endOfTable(false) {}

    };
    struct StubConfig
    {
        bool isStub = false;
        bool advertiseConnected = true;
        bool advertiseStatic = true;
        bool advertiseSummary = true;
        bool advertiseRedistributed = true;

        StubConfig() = default;

        StubConfig(bool stub, bool conn, bool stat, bool summ, bool redis)
            : isStub(stub), advertiseConnected(conn), advertiseStatic(stat),
              advertiseSummary(summ), advertiseRedistributed(redis) {}
    };
    enum class AuthType
    {
        NONE,
        MD5,
        SHA1
    };
    enum class TrafficShareMode
    {
        Balenced,
        Minimum
    };
    enum class Mode
    {
        POINT_TO_POINT,
        MULTIPOINT
    };
    enum class UpdateType
    {
        FULL,
        QUERY,
        RESPONSE_QUERY,
        PARTIAL,
        TRIGGERED,
        WITHDRAW,
    };
    enum class CommunicationMode
    {
        UNICAST,
        MULTICAST
    };
    enum class EigrpMode
    {
        NAMED,
        CLASSIC
    };
    enum class NeighborState
    {
        DOWN,
        INIT,
        TWOWAY,
        EXSTART,
        EXCHANGE,
        LOADING,
        ESTABLISHED
    };
    enum class InitRole
    {
        MASTER,
        SLAVE
    };
    struct NeighborInfo 
    {
        // Initialization
        std::mutex initializationMutex;
        NeighborState initialization = NeighborState::DOWN;
        bool initUpdateReceived = false;
        InitRole initRole = InitRole::MASTER;
        bool nullSent = false;
        bool slaveInit = false;
        bool masterInit = false;
        bool initComplete = false;
        bool processAcks = false;
        int initSequence{0};

        ByteString ipAddress;                                   // Neighbor's IP address
        ByteString macAddress;                                  // Neighbor's MAC address
        ByteString routerID;                                    // Neighbor's RouterID
        std::shared_mutex macMutex;                             // Neighbor's MAC mutex
        bool hasMac = false;                                    // Indicates if MAC address is known
        bool isInit = false;                                    // Initialization flag
        bool isGracfullyRestarting = false;                     // Gracefully restarting
        int lastReceivedSequenceNumber = 0;                     // Last received sequence number
        std::unordered_map<int, std::chrono::steady_clock::time_point> missingPacketTimestamps;
        CommunicationMode mode = CommunicationMode::MULTICAST;  // Communication mode
    
        int nullUpdateSequence;

        struct PacketBuffer { ByteString neighborIp; EigrpHeader eigrp; };
        std::map<int, PacketBuffer> packetBuffer;
    
        // Acks
        std::vector<int> pendingAcks;                           // Pending Acks
        
        // Synchronization primitives
        std::shared_mutex neighborDataMutex;                           // Protects neighbor-specific data
        std::condition_variable cv;                             // Condition variable for synchronization
    
        // RTT estimation
        double srtt = 1.0;                                      // Smoothed RTT
        double rttvar = 0.5;                                    // RTT variance
        double rto = 1.5;                                       // Retransmission timeout
    
        // Timers
        int holdTimerId = 0;                                    // Hold timer ID
        int holdTime;                                           // Hold time
        std::chrono::steady_clock::time_point lastHeard;        // Last heard time point
        std::unordered_map<int, int> retransmissionTimers;      // Map of sequenceNumber to timerId

        // Authentication
        int authKeyId = 1;                                      // Authentication ID
        ByteString authKey;                                     // Authentication ByteString
        bool authenticationEnabled = false;                     // Authentication enabled
        AuthType authType = AuthType::NONE;                     // Authentication type
    
        // Threads
        std::thread workerThread;                               // Worker thread
        std::atomic<bool> workerActive = false;                 // Worker thread active flag

        // Reliable delivery of packet tracking per route
        struct ReliablePacketInfo 
        {
            struct Packet {
                Packet(EigrpHeader eigrp, ByteString destination, std::vector<RoutingTable::Eigrp> routes = {}, bool isRemove = false)
                    : eigrp(eigrp), destination(destination), updatedRoutes(routes), remove(isRemove) {}
                Packet() = default;
                EigrpHeader eigrp = EigrpHeader();
                ByteString destination{};
                std::vector<RoutingTable::Eigrp> updatedRoutes{};
                bool remove = false;
            };

            Packet packet;
            std::chrono::steady_clock::time_point sendTime;
            int retransmissionCount;
            int timerId;

            // Default constructor
            ReliablePacketInfo() = default;
            ReliablePacketInfo(Packet packet) : packet(packet) {}

            // Default copy constructor and copy assignment operator
            ReliablePacketInfo(const ReliablePacketInfo&) = default;
            ReliablePacketInfo& operator=(const ReliablePacketInfo&) = default;

            // Default move constructor and move assignment operator
            ReliablePacketInfo(ReliablePacketInfo&&) = default;
            ReliablePacketInfo& operator=(ReliablePacketInfo&&) = default;
        };
        
        // Track advertised routes and their state
        struct AdvertisedRoute
        {
            RoutingTable::Eigrp route;
            bool active;            // Is this route currently activated?
            bool pendingUpdate;     // is there a pending update for this route?
            bool removePending;     // Is this route pending removal (Withdraw)?
        };

        std::unordered_map<int, ReliablePacketInfo> reliablePackets;
        std::unordered_map<int, Sequence> sequenceList;
        std::unordered_map<int, std::vector<RoutingTable::Eigrp>> routingBuffers;
        std::unordered_map<ByteString, AdvertisedRoute> advertisedRoutes;

        NeighborInfo() = default;

        // Delete copy constructor and copy assignment operator
        NeighborInfo(const NeighborInfo&) = delete;
        NeighborInfo& operator=(const NeighborInfo&) = delete;
    
        // Delete move constructor and move assignment operator
        NeighborInfo(NeighborInfo&&) = delete;
        NeighborInfo& operator=(NeighborInfo&&) = delete;
    };
    struct NetworksDistributed
    {
        RoutingTable::Eigrp route;
        bool distrubuted = false;
        
    };
    struct EigrpConfigs
    {
        int maxPaths = 4;
        int activeTime = 180;
        int stuckInActiveTime = 60;
        int adminDistance = 90;
        int externalAdminDistance = 170;
        int summaryAdminDistance = 90;
        int defaultAdminDistance = 90;
        int variance = 1;
        bool logNeighborChanges = true;
        bool advertiseDefault = false;
        bool activeTimerEnabled = true;
        int trafficShare = 0;
        KValue kvalue;
        StubConfig stubConfig;
        std::vector<SummaryRoute> summaryRoutes;
        AuthKey authKey;
        bool autoSummarizationEnabled = false;
        double redistributionMetricOffset = 0.0;
        double wideMetric = 0.0;
        std::vector<Network> networks;
        ByteString routeID = ByteString(4, '\x00');
        ByteString defaultNetwork;
        int defaultMask = 0;
        TrafficShareMode trafficShareMode = TrafficShareMode::Balenced;
        RouterID routerID;
    };
    struct InterfaceConfigs
    {
        int helloTime = 5;
        int holdTime = 15;
        int bandwidth = 100000;
        int delay = 10;
        int reliability = 255;
        int load = 1;
        int mtu = 1500;
        bool splitHorizon = true;
        bool isPassive = false;
        int DSCP = 0;
        std::unordered_map<int, int> retransmissionTimers;
        Mode interfaceMode = Mode::MULTIPOINT;
    };
}

namespace Protocol 
{
    class Eigrp;
    class EigrpInterface;
    class TopologyTable;

    struct EigrpAutonomousSystems
    {
        std::unordered_map<AddressFamily, std::shared_ptr<Protocol::Eigrp>> addressFamilies;
        EigrpConfigs::EigrpMode mode;
    };

    struct EigrpInstance
    {
        // Address Families
        std::unordered_map<int, std::shared_ptr<EigrpAutonomousSystems>> autonomousSystems;
        bool isShutdown = false;
    };

    struct EigrpInterfaceInstance
    {
        // Address Families
        std::shared_ptr<EigrpInterface> IPv4;
        std::shared_ptr<EigrpInterface> IPv6;
    };
    
    class EigrpInterface : public std::enable_shared_from_this<Protocol::EigrpInterface> 
    {
    protected:
        EigrpConfigs::InterfaceConfigs configs;
    public:
        // Holds EIGRP process
        Eigrp* eigrpProcess;

        // Construct:or initializing AS number and starting Hello timer
        EigrpInterface(Eigrp& eigrpSystem, std::shared_ptr<Interface> interface);
        // Destructor stopping all timers
        ~EigrpInterface();
        // Process Packet
        void processPacket(EigrpHeader* eigrpPacket, const ByteString neighborIp);
        // Initialize neighbor
        void initializeNeighbor(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor);
        // Changes the neighbor initialization state
        void changeNeighborState(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor, EigrpConfigs::NeighborState newState);
        // Processes Hello packets
        void processHello(const EigrpHeader* receivedHello, const ByteString neighborIp);
        // Processes Update packets
        void processUpdate(EigrpHeader* receivedUpdate, const ByteString neighborIp);
        // Processes buffered update packets
        void processBufferedPackets(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor);
        // Process Ack
        void processAck(const ByteString sequenceNumber, const ByteString neighborIp);
        // Process query
        void processQuery(const EigrpHeader* receivedQuery, const ByteString neighborIp);
        // Process reply
        void processReply(const EigrpHeader* recievedReply, const ByteString neighborIp);
        // Send Ack to neighbors
        void sendAckToNeighbor(const ByteString neighborIp, int sequenceNumber);
        // Send Update Packet
        void sendUpdateToNeighbor(const ByteString neighborIp, const std::vector<RoutingTable::Eigrp> &routes, EigrpConfigs::UpdateType updateType, bool restart = false, bool conditional = false, std::vector<ByteString> conditionalNeighbors = {}, int ack = 0);
        // Send query to neighbors
        void sendQueryToNeighbors(const std::vector<RoutingTable::Eigrp>& failedRoutes, const ByteString& originNeighborIp = "");
        // Send query to neighbor
        void sendQueryToNeighbor(const ByteString neighborIp, const std::vector<RoutingTable::Eigrp>& failedRoutes);
        // Send reply to neighbor
        void sendReplyToNeighbor(const ByteString neighborIp, const std::vector<RoutingTable::Eigrp>& routes);
        // Tracks missing packet timeout
        bool isTimeoutForMissing(int sequenceNumber, std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor);
        // Calculated the max amount of routes to be sent in a single update
        size_t calculateMaxRoutesPerPacket(AddressFamily af, bool isExernal);
        // Encode query option
        ByteString encodeQueryOption(RoutingTable::Eigrp route);
        // Encode reply option
        ByteString encodeRouteOption(const RoutingTable::Eigrp& route, bool removed = false);
        // Encode external reply option
        ByteString encodeExternalRouteOption(const RoutingTable::Eigrp& route, bool removed = false);
        // Encode stub option
        ByteString encodeStubOption(const EigrpConfigs::StubConfig stub);
        // Finds an ip address for a querying router
        ByteString findQueryNeighbor(int queryId);
        // Decodes Routes
        RoutingTable::Eigrp decodeRoute(ByteString value, bool external, bool summary);
        // Flags an update that is apended
        void flagPendingUpdate(const RoutingTable::Eigrp& route, const ByteString neighborIp);
        // Updates Routing Table
        void updateRoutingTable(const std::vector<RoutingTable::Eigrp> routes, bool init, const ByteString neighborIp);
        // Updates Routing Table for destination
        void updateRoutingTableForDestination(const ByteString& destination);
        // Calculate Local Link Cost (LLC)
        double calculateLocalLinkCost();
        // Helper function to get and increment the global sequence number
        int getNextSequenceNumber();
        // Handles stuck in active
        void handleStuckInActive();
        // Handles stub route updates
        void handleStubRouteUpdates();
        // Advertise a summary route to a neighbor
        void advertiseSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute);
        // Removes a summary route from a neighbor
        void withdrawSummaryRoute(const ByteString& network, int mask);
        // Encode summary route
        RoutingTable::Eigrp encodeSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute);
        // Handles neighbor removal
        void handleNeighborDown(const ByteString neighborIp);
        // Handles neihbor restart
        void handleNeighborRestart(const ByteString neighborIp);
        // Set an interface to passive
        void setPassive(bool passive);
        // Updates eigrp neighbors
        void addNeighbor(const ByteString& ipAddress, const ByteString& macAddress, EigrpConfigs::CommunicationMode mode);
        // Gets neighbor
        std::optional<std::shared_ptr<EigrpConfigs::NeighborInfo>> getNeighborInfo(const ByteString neighborIp);
        // Resolved mac address
        void resolveMacAddress(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor);

        // Authentication
        bool isNeighborAuthenticated(const ByteString neighborIp);
        void configureAuthentication(const ByteString neighborIp, int keyId, const ByteString& key, bool enable);
        ByteString serializeEigrpHeader(const EigrpHeader& eigrp, bool exclusiveAuthTLV);
        EigrpHeader::Option generateAuthenticatedTLV(const EigrpHeader& eigrp, const std::shared_ptr<EigrpConfigs::NeighborInfo>& neighbor);
        
        // Holds current interface
        std::shared_ptr<Interface> currentInterface;

        // Neighbor Advertised routes
        bool isRouteAdvertised(ByteString& network, int mask);
        void advertiseRouteToNeighbor(const RoutingTable::Eigrp& route, bool conditional = false);
        void withdrawRouteFromNeighbor(const RoutingTable::Eigrp& route);

    
        // Hello Timer
        void startHello();
        void startHelloHelper();
        void sendHelloPacket(ByteString neighborIp = "", bool unicast = false, bool update = false, int sequenceNumber = 0, ByteString routerID = "");
        void stopHello();
        // Active Timer
        void startActiveTimer(const RoutingTable::Eigrp& route); 
        void handleActiveTimeExpire(const RoutingTable::Eigrp& route);
        void cancelActiveTimer(const ByteString &destination, int mask);
        // SIA Timer
        void startStuckInActive();
        void cancelStuckInActive();
        // Hold Timer
        void startHoldTimer(const ByteString neighborIp, int holdTime);
        void handleHoldTimeExpire(const ByteString neighborIp);
        // Retransmission
        void setupReliablePacket(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor, const EigrpConfigs::NeighborInfo::ReliablePacketInfo::Packet packet, int sequenceNum);
        int startRetransmissionTimer(const ByteString neighborIp, const int& sequenceNumber, double timeout);
        void handleRetransmissionTimeout(const ByteString neighborIp, const int& sequenceNumber);
        double calculateRTT(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor, int sequenceNumber);
        void updateRTTEstimate(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor, int sequenceNumber);

        ByteString getMulticast();

//         inline std::unordered_map<ByteString, RoutingTable::Eigrp>& getAdvertisedRoutes() { return advertisedRoutes; }
        inline std::shared_ptr<EigrpConfigs::InterfaceConfigs> getConfigs() { return std::make_shared<EigrpConfigs::InterfaceConfigs>(configs); }
        
        // Neighbors
        std::mutex initMutex;
        std::mutex ackMutex;
        std::shared_mutex neighborMutex;
        std::unordered_map<ByteString, std::shared_ptr<EigrpConfigs::NeighborInfo>> neighbors;

    private:
        
        // Active Timers
        std::chrono::steady_clock::time_point helloStartTime;
        std::unordered_map<ByteString, int> activeTimers;
        bool runTimers = true;
        bool helloTimerActive = false;

        // Timer IDs
        int helloTimerId = 0;
        int activeTimerId = 0;
        int stuckInActiveTimerId = 0;

        // Lists
        std::unordered_map<int, std::pair<ByteString, int>> outstandingReplies;

        // Route Buffer
        std::vector<RoutingTable::Eigrp> routeBuffer = {};
        
        // Mutex
        std::mutex helloTimerMutex;
        std::mutex activeTimerMutex;

        // Sequence number
        int nextSequenceNumber = 1;                             // Next sequence number
        int conditionalReceive = 0;                             // Holds conditional receive sequence
        std::shared_mutex seqMutex;
    };


    // EIGRP class managing EIGRP protocol operations
    class Eigrp {
    protected:
        EigrpConfigs::EigrpConfigs configs;
        std::unordered_map<std::shared_ptr<Interface>, EigrpConfigs::InterfaceConfigs> interfaceConfigs;
    public:
        // Constructor initializing AS number and starting Hello timer
        Eigrp(int& as, AddressFamily af);
        // Destructor stopping all timers
        virtual ~Eigrp();
        virtual void initializeEigrp();
        virtual void shutdown();

        // Add network to configuration
        void addNetwork(const EigrpConfigs::Network& newNetwork);
        // Configures EIGRP Hello packet with specific settings
        void eigrpHello(EigrpHeader& eigrp, EigrpInterface* eigrpInt, ByteString neighborIp, int sequenceNumber = 0, bool ack = false, bool update = false, ByteString routeID = ""); // Configures EIGRP update packet with specific settings
        // Configures EIGRP Hello packet with specific settings
        void eigrpUpdate(EigrpHeader& eigrp, int sequenceNum, bool init = false, bool conditional = false, bool restart = false, bool endoftable = false, bool query = false, bool reply = false);
        // Updates list of EIGRP interfaces based on address matching
        void updateInterfaceList();
        // Tests if an IP address matches the configured networks
        bool testAddress(const ByteString& testIp);
        // Add EIGRP rouing entry
        double calculateMetric(int bandwidth, int load, int delay, int reliability, int hopCount = 0);
        // Calculate Parameters
        ByteString calculateParameters(int holdTime);
        // Adds interface to routing table
        void updateRoutingTableForConnected(const std::shared_ptr<EigrpInterface> eigrpInterface = nullptr);
        // Handles Interface change
        void onInterfaceChange(Interface* interfacePtr, AddressFamily af);
        // Update from route change
        void notifyRoutingChange(const std::vector<RoutingTable::Eigrp>& changedRoutes, bool isRemoval = false, bool init = false);
        // Redistribute routes
        void redistributeRoute(const ByteString &destination, int mask, const ByteString &protocol);
        // Method to add a summary route to EIGRP
        void addSummaryRoute(const ByteString& network, int mask, bool isAuto = false);
        // Method to remove a summary route from EIGRP
        void removeSummaryRoute(const ByteString& network, int mask);
        // Method to check if a route matches any summary route
        bool isRouteSummarized(const ByteString& network, int mask);
        // Updates interfaces when a summary route is applied
        void updateInterfacesWithSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute);
        // Updates interfaces when a summary route is removed
        void updateInterfacesAfterRemovingSummaryRoute(const ByteString& network, int mask);
        // Enabled auto summarization
        void enableAutoSummary(bool enable);
        // Sets router to stub
        void setStub(bool isStub, bool advertiseConnected = true, bool advertiseStatic = true, bool advertiseSummary = true, bool advertiseRedistributed = true);
        // Updates routes based on stub configuration
        void updateStubRoutes();
        // Finds the lowest bandwidth interface
        int getLowestBandwidth();
        // Injects default route
        void addDefaultRoute();
        // Removed default route
        void removeDefaultRoute();
        // Set variance
        void setVariance(int var);
        // Recalculate routes when variance changes
        void recalculateRoutes();
        // Graceful restart whole process
        void gracefulRestart();
        // Handles restart
        void restart();
        // Cleans up EIGRP
        void cleanup();
        // Periodic prunes routes
        void periodicMaintenance();
        // Calculates routerID during startup
        void calculateRouterID();

        // Stub
        bool isStub() const { return configs.stubConfig.isStub; }
        bool advertiseConnected() const { return configs.stubConfig.advertiseConnected; }
        bool advertiseStatic() const { return configs.stubConfig.advertiseStatic; }
        bool advertiseSummary() const { return configs.stubConfig.advertiseSummary; }
        bool advertiseRedistributed() const { return configs.stubConfig.advertiseRedistributed; }

        // Lists
        std::vector<EigrpConfigs::SummaryRoute> summaryRoutes;
        std::unordered_map<int, std::shared_ptr<EigrpInterface>> eigrpInterfaceList{};
        std::unordered_map<ByteString, std::unordered_map<ByteString, std::shared_ptr<EigrpConfigs::NeighborInfo>>> neighborBackup;

        // Eigrp proccess mutex
        std::shared_mutex eigrpMutex;
        // Eigrp data mutex
        std::shared_mutex eigrpDataMutex;
        
        // Configurations for EIGRP
        AddressFamily getAddressFamily() const { return addressFamily; }
//         std::vector<std::vector<EigrpConfigs::NetworksDistributed>*> eigrpDistributionList;
        std::unordered_map<ByteString, int> stuckInActiveTimers;
        std::unique_ptr<Protocol::TopologyTable> topologyTable;

        inline int getAsNumber() { std::shared_lock<std::shared_mutex> lock(eigrpDataMutex); return asNumber; }
        inline AddressFamily getAddressFamily() { std::shared_lock<std::shared_mutex> lock(eigrpDataMutex); return addressFamily; }
        inline ByteString getVirtualRouterID() { std::shared_lock<std::shared_mutex> lock(eigrpDataMutex); return virtualRouterID; }
        inline ByteString getRouterID() { std::shared_lock<std::shared_mutex> lock(eigrpDataMutex); return routeID; }
        inline std::shared_ptr<EigrpConfigs::EigrpConfigs> getConfigs() { return std::make_shared<EigrpConfigs::EigrpConfigs>(configs); }

    private:
        
        AddressFamily addressFamily;
        ByteString virtualRouterID = ByteString(2, '\x00');
        int asNumber;
        ByteString routeID;
    };

    class ClassicEigrp : public Eigrp
    {
    public:
        ClassicEigrp(int& as, AddressFamily af) : Eigrp(as, af) {}
        void initializeEigrp() override;
        void shutdown() override;
    };

    class NamedEigrp : public Eigrp
    {
    private:
        ByteString processName;
    public:
        NamedEigrp(int& as, AddressFamily af, const ByteString& name);
        void initializeEigrp() override;
        void shutdown() override;
        void configureInterface(const ByteString& interfaceName, const EigrpConfigs::InterfaceConfigs& configs);
    };

    class TopologyTable {
    public:
        struct RouteInfo {
            unsigned int feasibleDistance;
            unsigned int reportedDistance;
            ByteString nextHop;
            bool isSuccessor;
            bool isFeasibleSuccessor;
            int hopCount;
            int adminDistance = 90;
            std::chrono::steady_clock::time_point lastUpdate;
        };
    
        struct TopologyEntry {
            ByteString destination;
            int prefixLength;
            std::unordered_map<ByteString, RouteInfo> routesByNeighbor;
            bool isActive;
            // Timers for Active and Stuck-In-Active
            int activeTimerId = 0;
            int StuckInActiveTimerId = 0;

            double bestFD;

            std::vector<ByteString> feasibleSuccessors;
            std::vector<ByteString> successors;
        };

        TopologyTable(Eigrp* process);

        void addOrUpdateRoute(const ByteString destination, int prefixLength, const RouteInfo& routeInfo, const ByteString neighborIp);
        void removeRoutesFromNeighbor(const ByteString neighborIp);
        std::optional<RouteInfo> findBestRoute(const ByteString& destination, int variance);
        void updateSuccessorAndFeasibleSuccessors(std::shared_ptr<TopologyEntry>& entry);
        std::shared_ptr<TopologyTable::TopologyEntry> getEntryForRoute(const ByteString& destination);
        void handleRouteFailure(const ByteString& destination, const ByteString& failedNeighborIp);
        void markRouteAsPassive(const ByteString& destination, EigrpInterface* eigrp);
        void removeEntry(const ByteString& destination);
        void pruneStaleRoutes();
        void handleNeighborDown(const ByteString neighborIp);

        std::unordered_map<ByteString, std::shared_ptr<TopologyEntry>>& getTopologyEntries() { std::lock_guard<std::mutex> lock(tableMutex); return topologyEntries; }

        int staleThreshold = 15;
    
    private:
        std::mutex tableMutex;
        std::unordered_map<ByteString, std::shared_ptr<TopologyEntry>> topologyEntries;
        std::shared_ptr<Eigrp> eigrpProcess;
    };
}


// Communication Mode
extern EigrpConfigs::CommunicationMode* currentCommunicationMode;

// Global pointer to the current EIGRP autonomous system
extern std::shared_ptr<Protocol::Eigrp> currentEigrp;

// Global pointer to the cirrent EIGRP instance
extern std::shared_ptr<Protocol::EigrpInstance> currentEigrpInstance;

// Map of EIGRP instances by AS number
extern std::map<ByteString, std::shared_ptr<Protocol::EigrpInstance>> eigrpList;

// Map of all EIGRP autonomous systems
extern std::map<int, std::weak_ptr<Protocol::EigrpAutonomousSystems>> eigrpAutonomousSystems;

// Updates EIGRP interface list based on interface changes
void updateEigrpInterface(Interface* interface);
