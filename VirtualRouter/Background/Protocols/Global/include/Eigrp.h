#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>
#include <PacketStructure.h>
#include <Functions.h>
#include <mutex>
#include <thread>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <atomic>
#include <Interface.h>
#include <RoutingTable.h>
#include <TimeManager.h>
#include <Authentication.hpp>
#include <unordered_map>
#include <shared_mutex>

using namespace std;

extern mutex globalEigrpMutex;

// Config schemas for EIGRP class
namespace EigrpConfigs
{
    struct network 
    {
        string ip{};
        string mask{};
    };
    struct SummaryRoute 
    {
        std::string network;
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
        std::string key;
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
        NeighborState initialization = NeighborState::INIT;
        bool initHelloReceived = false;
        bool neighborValidated = false;
        InitRole initRole;
        int lastFullUpdateSequence;
        std::mutex initializationMutex;

        int nullUpdateSequence;
        std::mutex nullMutex;

        std::string ipAddress;                                  // Neighbor's IP address
        std::string macAddress;                                 // Neighbor's MAC address
        std::string routerID;                                   // Neighbor's RouterID
        std::mutex macMutex;                                    // Neighbor's MAC mutex
        bool hasMac = false;                                    // Indicates if MAC address is known
        bool isInit = false;                                    // Initialization flag
        bool isGracfullyRestarting = false;                     // Gracefully restarting
        int conditionalReceive = 0;                             // Holds conditional receive sequence
        int lastReceivedSequenceNumber = 0;                     // Last received sequence number
        int nextSequenceNumber = 1;                             // Next sequence number
        CommunicationMode mode;                                 // Communication mode
    
        struct PacketBuffer { std::string neighborIp; eigrpHeader eigrp; };
        std::map<int, PacketBuffer> packetBuffer;
    
        // Acks
        vector<int> pendingAcks;                                // Pending Acks
        
        // Synchronization primitives
        std::mutex neighborDataMutex;                           // Protects neighbor-specific data
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
        std::string authKey;                                    // Authentication string
        bool authenticationEnabled = false;                     // Authentication enabled
        AuthType authType = AuthType::NONE;                     // Authentication type
    
        // Threads
        std::thread workerThread;                               // Worker thread
        std::atomic<bool> workerActive = false;                 // Worker thread active flag

        // Reliable delivery of packet tracking per route
        struct ReliablePacketInfo 
        {
            std::vector<std::string> packets;
            std::chrono::steady_clock::time_point sendTime;
            int retransmissionCount;
            int timerId;            
            
            // Default constructor
            ReliablePacketInfo() = default;

            // Default copy constructor and copy assignment operator
            ReliablePacketInfo(const ReliablePacketInfo&) = default;
            ReliablePacketInfo& operator=(const ReliablePacketInfo&) = default;

            // Default move constructor and move assignment operator
            ReliablePacketInfo(ReliablePacketInfo&&) = default;
            ReliablePacketInfo& operator=(ReliablePacketInfo&&) = default;
        };

        std::unordered_map<int, ReliablePacketInfo> reliablePackets;
        std::unordered_map<int, Sequence> sequenceList;
        std::unordered_map<int, std::vector<RoutingTable::Eigrp>> routingBuffers;

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
        std::vector<network> networks;
        std::string routeID = std::string(4, '\x00');
        std::string defaultNetwork;
        int defaultMask = 0;
        TrafficShareMode trafficShareMode = TrafficShareMode::Balenced;
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
        // Sets up EIGRP packet headers
        PacketInfo EigrpBody(string unicastAddress = "");
        // Process Packet
        void ProcessPacket(eigrpHeader* eigrpPacket, const std::string& neighborIp);
        // Initialize neighbor
        void InitializeNeighbor(shared_ptr<EigrpConfigs::NeighborInfo> neighbor);
        // Changes the neighbor initialization state
        void ChangeNeighborState(shared_ptr<EigrpConfigs::NeighborInfo> neighbor, EigrpConfigs::NeighborState newState);
        // Processes Hello packets
        void ProcessHello(const eigrpHeader* receivedHello, const std::string neighborIp);
        // Processes Update packets
        void ProcessUpdate(eigrpHeader* receivedUpdate, const std::string& neighborIp);
        // Process Ack
        void ProcessAck(const std::string sequenceNumber, const std::string &neighborIp);
        // Process query
        void ProcessQuery(const eigrpHeader* receivedQuery, const std::string& neighborIp);
        // Process reply
        void ProcessReply(const eigrpHeader* recievedReply, const std::string& neighborIp);
        // Send Ack to neighbors
        void SendAckToNeighbor(const std::string& neighborIp, int sequenceNumber);
        // Send Update Packet
        void SendUpdateToNeighbor(const std::string &neighborIp, const std::vector<RoutingTable::Eigrp> &routes, EigrpConfigs::UpdateType updateType, bool restart = false, bool conditional = false, std::vector<string> conditionalNeighbors = {});
        // Send query to neighbors
        void SendQueryToNeighbors(const vector<RoutingTable::Eigrp>& failedRoutes, const std::string& originNeighborIp = "");
        // Send query to neighbor
        void SendQueryToNeighbor(const std::string&neighborIp, const vector<RoutingTable::Eigrp>& failedRoutes);
        // Send reply to neighbor
        void SendReplyToNeighbor(const std::string& neighborIp, const vector<RoutingTable::Eigrp>& routes);
        // Calculated the max amount of routes to be sent in a single update
        size_t CalculateMaxRoutesPerPacket(AddressFamily af, bool isExernal);
        // Encode query option
        std::string EncodeQueryOption(RoutingTable::Eigrp route);
        // Encode reply option
        std::string EncodeRouteOption(const RoutingTable::Eigrp& route, bool removed = false);
        // Encode external reply option
        std::string EncodeExternalRouteOption(const RoutingTable::Eigrp& route, bool removed = false);
        // Encode stub option
        std::string EncodeStubOption(const EigrpConfigs::StubConfig stub);
        // Finds an ip address for a querying router
        std::string FindQueryNeighbor(int queryId);
        // Decodes Routes
        RoutingTable::Eigrp DecodeRoute(string value, bool external, bool summary);
        // Flags an update that is apended
        void FlagPendingUpdate(const RoutingTable::Eigrp& route, const std::string& neighborIp);
        // Updates Routing Table
        void UpdateRoutingTable(const vector<RoutingTable::Eigrp> routes, bool init, const std::string& neighborIp);
        // Updates Routing Table for destination
        void UpdateRoutingTableForDestination(const std::string& destination);
        // Calculate Local Link Cost (LLC)
        double CalculateLocalLinkCost();
        // Helper function to get and increment the global sequence number
        int GetNextSequenceNumber(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor);
        // Handles stuck in active
        void HandleStuckInActive();
        // Handles stub route updates
        void HandleStubRouteUpdates();
        // Advertise a summary route to a neighbor
        void AdvertiseSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute);
        // Removes a summary route from a neighbor
        void WithdrawSummaryRoute(const std::string& network, int mask);
        // Encode summary route
        RoutingTable::Eigrp EncodeSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute);
        // Handles neighbor removal
        void HandleNeighborDown(const std::string& neighborIp);
        // Handles neihbor restart
        void HandleNeighborRestart(const std::string &neighborIp);
        // Set an interface to passive
        void SetPassive(bool passive);
        // Updates eigrp neighbors
        void addNeighbor(const std::string& ipAddress, const std::string& macAddress, EigrpConfigs::CommunicationMode mode);
        // Gets neighbor
        std::optional<std::shared_ptr<EigrpConfigs::NeighborInfo>> GetNeighborInfo(const std::string& neighborIp);
        // Resolved mac address
        void ResolveMacAddress(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor);

        // Authentication
        bool isNeighborAuthenticated(const std::string& neighborIp);
        void ConfigureAuthentication(const std::string& neighborIp, int keyId, const std::string& key, bool enable);
        std::string SerializeEigrpHeader(const eigrpHeader& eigrp, bool exclusiveAuthTLV);
        eigrpHeader::Option GenerateAuthenticatedTLV(const eigrpHeader& eigrp, const std::shared_ptr<EigrpConfigs::NeighborInfo>& neighbor);
        
        // Holds current interface
        std::shared_ptr<Interface> currentInterface;
    
        // Hello Timer
        void StartHello();
        void StartHelloHelper();
        void SendHelloPacket(string neighborIp = "", bool unicast = false, bool update = false, int sequenceNumber = 0, string routerID = "");
        void StopHello();
        // Active Timer
        void StartActiveTimer(const RoutingTable::Eigrp& route); 
        void HandleActiveTimeExpire(const RoutingTable::Eigrp& route);
        void CancelActiveTimer(const std::string &destination, int mask);
        // SIA Timer
        void StartStuckInActive();
        void CancelStuckInActive();
        // Hold Timer
        void StartHoldTimer(const std::string& neighborIp, int holdTime);
        void HandleHoldTimeExpire(const std::string& neighborIp);
        // Retransmission
        void SetupReliablePacket(std::shared_ptr <EigrpConfigs::NeighborInfo> &neighbor, const std::vector<std::string> packets, int sequenceNum);
        int StartRetransmissionTimer(const std::string &neighborIp, const int& sequenceNumber, double timeout);
        void HandleRetransmissionTimeout(const std::string &neighborIp, const int& sequenceNumber);
        double CalculateRTT(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor, int sequenceNumber);
        void UpdateRTTEstimate(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor, int sequenceNumber);

        inline std::unordered_map<std::string, RoutingTable::Eigrp>& getAdvertisedRoutes() { return advertisedRoutes; }
        inline std::shared_ptr<EigrpConfigs::InterfaceConfigs> getConfigs() { return std::make_shared<EigrpConfigs::InterfaceConfigs>(configs); }
        inline vector<shared_ptr<EigrpConfigs::NeighborInfo>> getNeighbors() { vector<shared_ptr<EigrpConfigs::NeighborInfo>> neighborsCp; 
        std::lock_guard<std::mutex> lock(neighborMutex); for (auto& neighbor : neighbors) {neighborsCp.emplace_back(neighbor.second);}return neighborsCp;}

    private:
        
        // Active Timers
        std::chrono::steady_clock::time_point helloStartTime;
        std::unordered_map<std::string, int> activeTimers;
        bool runTimers = true;
        bool helloTimerActive = false;

        // Timer IDs
        int helloTimerId = 0;
        int activeTimerId = 0;
        int stuckInActiveTimerId = 0;

        // Lists
        std::unordered_map<std::string, shared_ptr<EigrpConfigs::NeighborInfo>> neighbors;
        std::unordered_map<int, std::pair<std::string, int>> outstandingReplies;
        std::unordered_map<std::string, RoutingTable::Eigrp> advertisedRoutes;

        // Route Buffer
        vector<RoutingTable::Eigrp> routeBuffer;
        
        // Mutex
        std::mutex helloTimerMutex;
        std::mutex activeTimerMutex;
        std::mutex neighborMutex;

        Variable variable;
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
        virtual void InitializeEigrp();
        virtual void Shutdown();

        // Add network to configuration
        void AddNetwork(const EigrpConfigs::network& newNetwork);
        // Configures EIGRP Hello packet with specific settings
        void EigrpHello(eigrpHeader& eigrp, EigrpInterface* eigrpInt, std::string neighborIp, int sequenceNumber = 0, bool ack = false, bool update = false, string routeID = "");
        // Configures EIGRP update packet with specific settings
        void EigrpUpdate(eigrpHeader& eigrp, int sequenceNum, bool init = false, bool conditional = false, bool restart = false, bool endoftable = false, bool query = false, bool reply = false);
        // Updates list of EIGRP interfaces based on address matching
        void UpdateInterfaceList();
        // Tests if an IP address matches the configured networks
        bool TestAddress(const std::string& testIp);
        // Add EIGRP rouing entry
        double CalculateMetric(int bandwidth, int load, int delay, int reliability, int hopCount = 0);
        // Calculate Parameters
        string CalculateParameters(int holdTime);
        // Adds interface to routing table
        void UpdateRoutingTableForConnected(std::vector<std::string> routedToRemove = {});
        // Handles Interface change
        void OnInterfaceChange(Interface* interfacePtr, AddressFamily af);
        // Update from route change
        void NotifyRoutingChange(const vector<RoutingTable::Eigrp>& changedRoutes, bool isRemoval = false, bool init = false);
        // Redistribute routes
        void RedistributeRoute(const std::string &destination, int mask, const std::string &protocol);
        // Method to add a summary route to EIGRP
        void AddSummaryRoute(const std::string& network, int mask, bool isAuto = false);
        // Method to remove a summary route from EIGRP
        void RemoveSummaryRoute(const std::string& network, int mask);
        // Method to check if a route matches any summary route
        bool IsRouteSummarized(const std::string& network, int mask);
        // Updates interfaces when a summary route is applied
        void UpdateInterfacesWithSummaryRoute(const EigrpConfigs::SummaryRoute& summaryRoute);
        // Updates interfaces when a summary route is removed
        void UpdateInterfacesAfterRemovingSummaryRoute(const std::string& network, int mask);
        // Enabled auto summarization
        void EnableAutoSummary(bool enable);
        // Sets router to stub
        void SetStub(bool isStub, bool advertiseConnected = true, bool advertiseStatic = true, bool advertiseSummary = true, bool advertiseRedistributed = true);
        // Updates routes based on stub configuration
        void UpdateStubRoutes();
        // Finds the lowest bandwidth interface
        int GetLowestBandwidth();
        // Injects default route
        void AddDefaultRoute();
        // Removed default route
        void RemoveDefaultRoute();
        // Set variance
        void SetVariance(int var);
        // Recalculate routes when variance changes
        void RecalculateRoutes();
        // Graceful restart whole process
        void GracefulRestart();
        // Handles restart
        void Restart();
        // Cleans up EIGRP
        void Cleanup();
        // Preiodic prunes routes
        void PeriodicMaintenance();

        // Stub
        bool IsStub() const { return configs.stubConfig.isStub; }
        bool AdvertiseConnected() const { return configs.stubConfig.advertiseConnected; }
        bool AdvertiseStatic() const { return configs.stubConfig.advertiseStatic; }
        bool AdvertiseSummary() const { return configs.stubConfig.advertiseSummary; }
        bool AdvertiseRedistributed() const { return configs.stubConfig.advertiseRedistributed; }

        // Lists
        std::vector<EigrpConfigs::SummaryRoute> summaryRoutes;
        std::unordered_map<int, std::shared_ptr<EigrpInterface>> eigrpInterfaceList{};
        std::unordered_map<std::string, std::unordered_map<std::string, std::shared_ptr<EigrpConfigs::NeighborInfo>>> neighborBackup;

        // Eigrp proccess mutex
        mutex eigrpMutex;
        // Eigrp data mutex
        mutex eigrpDataMutex;
        
        // Configurations for EIGRP
        AddressFamily getAddressFamily() const { return addressFamily; }
        vector<vector<EigrpConfigs::NetworksDistributed>*> EigrpDistributionList;
        std::unordered_map<std::string, int> stuckInActiveTimers;
        std::unique_ptr<Protocol::TopologyTable> topologyTable;

        inline int getAsNumber() { std::lock_guard<std::mutex> lock(eigrpDataMutex); return asNumber; }
        inline AddressFamily getAddressFamily() { std::lock_guard<std::mutex> lock(eigrpDataMutex); return addressFamily; }
        inline string getVirtualRouterID() { std::lock_guard<std::mutex> lock(eigrpDataMutex); return virtualRouterID; }
        inline string getRouterID() { std::lock_guard<std::mutex> lock(eigrpDataMutex); return routeID; }
        inline std::shared_ptr<EigrpConfigs::EigrpConfigs> getConfigs() { return std::make_shared<EigrpConfigs::EigrpConfigs>(configs); }

    private:
        
        AddressFamily addressFamily;
        string virtualRouterID = std::string(2, '\x00');
        int asNumber;
        string routeID;
        Variable variable;
    };

    class ClassicEigrp : public Eigrp
    {
    public:
        ClassicEigrp(int& as, AddressFamily af) : Eigrp(as, af) {}
        void InitializeEigrp() override;
        void Shutdown() override;
    };

    class NamedEigrp : public Eigrp
    {
    private:
        std::string processName;
    public:
        NamedEigrp(int& as, AddressFamily af, const std::string& name);
        void InitializeEigrp() override;
        void Shutdown() override;
        void ConfigureInterface(const std::string& interfaceName, const EigrpConfigs::InterfaceConfigs& configs);
    };

    class TopologyTable {
    public:
        struct RouteInfo {
            int feasibleDistance;
            int reportedDistance;
            std::string nextHop;
            bool isSuccessor;
            bool isFeasibleSuccessor;
            int hopCount;
            int adminDistance = 90;
            std::chrono::steady_clock::time_point lastUpdate;
        };
    
        struct TopologyEntry {
            std::string destination;
            int prefixLength;
            std::unordered_map<std::string, RouteInfo> routesByNeighbor;
            bool isActive;
            // Timers for Active and Stuck-In-Active
            int activeTimerId = 0;
            int StuckInActiveTimerId = 0;

            double bestFD;

            vector<std::string> feasibleSuccessors;
            vector<std::string> successors;
        };

        TopologyTable(Eigrp* process);

        void AddOrUpdateRoute(const std::string& destination, int prefixLength, const RouteInfo& routeInfo, const std::string& neighborIp);
        void RemoveRoutesFromNeighbor(const std::string& neighborIp);
        TopologyEntry* FindBestRoute(const std::string& destination, int variance);
        void HandleRouteFailure(const std::string& destination, const std::string& failedNeighborIp);
        void MarkRouteAsPassive(const std::string& destination, EigrpInterface* eigrp);
        void RemoveEntry(const std::string& destination);
        void PruneStaleRoutes();
        void HandleNeighborDown(const std::string &neighborIp);

        std::unordered_map<std::string, TopologyEntry>& GetTopologyEntries() { std::lock_guard<std::mutex> lock(tableMutex); return topologyEntries; }

        int staleThreshold = 15;
    
    private:
        std::mutex tableMutex;
        std::unordered_map<std::string, TopologyEntry> topologyEntries;
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
extern std::map<std::string, std::shared_ptr<Protocol::EigrpInstance>> eigrpList;

// Map of all EIGRP autonomous systems
extern std::map<int, std::weak_ptr<Protocol::EigrpAutonomousSystems>> eigrpAutonomousSystems;

// Updates EIGRP interface list based on interface changes
void UpdateEigrpInterface(Interface* interface);
