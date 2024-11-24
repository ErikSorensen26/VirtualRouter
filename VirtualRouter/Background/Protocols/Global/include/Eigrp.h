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

    struct NeighborInfo 
    {
        std::string ipAddress;                                  // Neighbor's IP address
        std::string macAddress;                                 // Neighbor's MAC address
        std::mutex macMutex;                                    // Neighbor's MAC mutex
        bool hasMac;                                            // Indicates if MAC address is known
        bool isInit;                                            // Initialization flag
        bool sendInitUpdate;                                    // Flag to send initial update
        int conditionalReceive;                                 // Holds conditional receive sequence
        bool receivedInitUpdate;                                // Flag for received initial update
        int globalSequenceNumber;                               // Sequence number for reliable delivery
        int lastReceivedSequenceNumber;                         // Last received sequence number
        int nextSequenceNumber;                                 // Next sequence number
        bool adjacency;                                         // Adjacency status
    
        // Acks
        vector<int> pendingAcks;                                // Pending Acks
    
        // Synchronization primitives
        std::mutex neighborDataMutex;                           // Protects neighbor-specific data
        std::condition_variable cv;                             // Condition variable for synchronization
    
        // RTT estimation
        double srtt;                                            // Smoothed RTT
        double rttvar;                                          // RTT variance
        double rto;                                             // Retransmission timeout
    
        // Timers
        int holdTimerId;                                        // Hold timer ID
        int holdTime;                                           // Hold time
        std::chrono::steady_clock::time_point lastHeard;        // Last heard time point
        std::unordered_map<int, int> retransmissionTimers;      // Map of sequenceNumber to timerId
        std::mutex retransmissionMutex;                         // Protects retransmissionTimers

        // Authentication
        int authKeyId;                                          // Authentication ID
        std::string authKey;                                    // Authentication string
        bool authenticationEnabled;                             // Authentication enabled
    
        // Threads
        std::thread workerThread;                               // Worker thread
        std::atomic<bool> workerActive;                         // Worker thread active flag

        // Reliable delivery of packet tracking per route
        struct ReliablePacketInfo 
        {
            std::string packet;
            std::chrono::steady_clock::time_point sendTime;
            int retransmissionCount;
            int timerId;
        };

        std::unordered_map<int, ReliablePacketInfo> reliablePackets;
        std::unordered_map<int, Sequence> sequenceList;
        std::unordered_map<int, std::vector<RoutingTable::Eigrp>> routingBuffers;
    
        NeighborInfo()
            : hasMac(false),
              isInit(false),
              sendInitUpdate(false),
              receivedInitUpdate(false),
              lastReceivedSequenceNumber(0),
              nextSequenceNumber(1),
              conditionalReceive(0),
              globalSequenceNumber(0),
              adjacency(false),
              srtt(1.0),
              rttvar(0.5),
              rto(1.5),
              holdTimerId(0),
              workerActive(false),
              authenticationEnabled(false),
              authKeyId(1)
        {}
    
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
}

namespace Protocol 
{
    class Eigrp;
    class EigrpInterface;
    class TopologyTable;

    struct EigrpInstance
    {
        // Address Families
        std::shared_ptr<Eigrp> IPv4;
        std::shared_ptr<Eigrp> IPv6;
    };

    struct EigrpInterfaceInstance
    {
        // Address Families
        std::shared_ptr<EigrpInterface> IPv4;
        std::shared_ptr<EigrpInterface> IPv6;
    };
    
    enum class AddressFamily 
    {
        IPv4,
        IPv6
    };

    class EigrpInterface : public std::enable_shared_from_this<Protocol::EigrpInterface> 
    {
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
        void ProcessPacket(const eigrpHeader* eigrpPacket, const std::string& neighborIp);
        // Processes Hello packets
        void ProcessHello(const eigrpHeader* receivedHello, const std::string& neighborIp);
        // Processes Update packets
        void ProcessUpdate(const eigrpHeader* receivedUpdate, const std::string& neighborIp);
        // Process Ack
        void ProcessAck(const std::string sequenceNumber, const std::string &neighborIp);
        // Process query
        void ProcessQuery(const eigrpHeader* receivedQuery, const std::string& neighborIp);
        // Process reply
        void ProcessReply(const eigrpHeader* recievedReply, const std::string& neighborIp);
        // Send Ack to neighbors
        void SendAckToNeighbor(const std::string& neighborIp, int sequenceNumber);
        // Send Update Packet
        void SendUpdateToNeighbor(const std::string& neighborIp, const vector<RoutingTable::Eigrp>& routes, bool removal);
        // Send full Update Packet
        void SendFullUpdateToNeighbor(const std::string& neighborIp);
        // Send empty Update Packet to neighbor
        void SendEmptyUpdateToNeighbor(const std::string& neighborIp);
        // Send query to neighbors
        void SendQueryToNeighbors(const vector<RoutingTable::Eigrp>& failedRoutes, const std::string& originNeighborIp = "");
        // Send query to neighbor
        void SendQueryToNeighbor(const std::string&neighborIp, const vector<RoutingTable::Eigrp>& failedRoutes);
        // Encode query option
        std::string EncodeQueryOption(RoutingTable::Eigrp route);
        // Send reply to neighbor
        void SendReplyToNeighbor(const std::string& neighborIp, const vector<RoutingTable::Eigrp>& routes);
        // Encode reply option
        std::string EncodeRouteOption(const RoutingTable::Eigrp& routes);
        // Encode stub option
        std::string EncodeStubOption(const EigrpConfigs::StubConfig stub);
        // Calculate Local Link Cost (LLC)
        double CalculateLocalLinkCost();
        // Helper function to get and increment the global sequence number
        int GetNextSequenceNumber(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor);
        // Updates Routing Table
        void UpdateRoutingTable(const vector<RoutingTable::Eigrp> routes, bool init, const std::string& neighborIp);
        // Decodes Routes
        RoutingTable::Eigrp DecodeRoute(string value, bool external, bool summary);
        // Finds an ip address for a querying router
        std::string FindQueryNeighbor(int queryId);
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

        void SetupReliablePacket(std::shared_ptr<EigrpConfigs::NeighborInfo> &neighbor, const std::string &packet, int sequenceNum);
    
        mutex eigrpMutex;
        
        int helloTime = 5;
        int holdTime = 15;
        int activeTime = 180;
        int stuckInActiveTime = 60; 
        int adminDistance = 90;
        int variance = 1;
        bool passive = false;
        int bandwidth;
        int reliability = 255;
        int load = 1;

        // Qos
        int DSCP = 0;

        // Split horizon
        bool splitHorizon = false;

        // Holds current interface
        std::shared_ptr<Interface> currentInterface;
    
        // Hello Timer
        void StartHello();
        void StartHelloHelper();
        void SendHelloPacket(bool update = false, int sequenceNum = 0, string neighborIp = "00000000");
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
        int StartRetransmissionTimer(const std::string &neighborIp, const int& sequenceNumber, double timeout);
        void HandleRetransmissionTimeout(const std::string &neighborIp, const int& sequenceNumber);
        double CalculateRTT(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor, int sequenceNumber);
        void UpdateRTTEstimate(std::shared_ptr<EigrpConfigs::NeighborInfo> neighbor, int sequenceNumber);

        void UpdateRoutingTableForDestination(const std::string& destination);

        // Neighbor went down
        void HandleNeighborDown(const std::string& neighborIp);
        void HandleNeighborRestart(const std::string &neighborIp);

        // Holds EIGRP neighbors
        std::map<std::string, shared_ptr<EigrpConfigs::NeighborInfo>> neighbors;
        // Protects access to neighbors
        std::mutex neighborMutex;

        // Advertised route mutex
        std::mutex advertisedRouteMutex;
        // List of advertised routes
        std::unordered_map<std::string, RoutingTable::Eigrp> advertisedRoutes;
        // Outstanding replies
        std::unordered_map<int, std::pair<std::string, int>> outstandingReplies;
        std::mutex replyTrackingMutex;

    private:

        std::chrono::steady_clock::time_point helloStartTime;

        // List of active timers
        std::unordered_map<std::string, int> activeTimers;

        // Timer IDs
        int helloTimerId = 0;
        int activeTimerId = 0;
        int stuckInActiveTimerId = 0;

        Variable variable;
        bool runTimers = true;
        bool helloTimerActive = false;

        // Route Buffer
        vector<RoutingTable::Eigrp> routeBuffer;

        std::mutex helloTimerMutex;
        std::mutex activeTimerMutex;
        std::mutex retransmissionMutex;
    };


    // EIGRP class managing EIGRP protocol operations
    class Eigrp {
    public:
        // Constructor initializing AS number and starting Hello timer
        Eigrp(int& as, AddressFamily af);
        // Destructor stopping all timers
        ~Eigrp();

        // Configures EIGRP Hello packet with specific settings
        void EigrpHello(eigrpHeader& eigrp, EigrpInterface* eigrpInt, bool ack = false, bool update = false, int sequenceNumber = 0, string neighborIp = "00000000");
        // Configures EIGRP update packet with specific settings
        void EigrpUpdate(eigrpHeader& eigrp, int sequenceNum, vector<EigrpConfigs::NetworksDistributed>& internalRoutes, bool init = false, bool conditional = false, bool restart = false, bool endoftable = false, bool query = false, bool reply = false);
        // Updates list of EIGRP interfaces based on address matching
        void UpdateInterfaceList();
        // Tests if an IP address matches the configured networks
        bool TestAddress(const std::string& testIp);
        // Add EIGRP rouing entry
        double CalculateMetric(int bandwidth, int load, int delay, int reliability, int hopCount = 0);
        // Calculate Parameters
        string CalculateParameters(int holdTime);
        // Adds interface to routing table
        void UpdateRoutingTableForConnected();
        // Handles Interface change
        void OnInterfaceChange(Interface* interfacePtr, AddressFamily af);
        // Update from route change
        void NotifyRoutingChange(const vector<RoutingTable::Eigrp>& changedRoutes, bool isRemoval = false, bool init = false);
        // Graceful instance shutdown
        void Shutdown();
        // Redistribute routes
        void RedistributeRoute(const std::string &destination, int mask, const std::string &protocol);
        // Adds network to the network table
        void AddNetwork(const EigrpConfigs::network newNetwork);
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

        // Gets neighbor
        std::shared_ptr<EigrpConfigs::NeighborInfo> GetNeighborInfo(const std::string& neighborIp);


        // Authentication
        void ConfigureAuthentication(const std::string& neighborIp, int keyId, const std::string& key, bool enable);
        std::string SerializeEigrpHeader(const eigrpHeader& eigrp, bool exclusiveAuthTLV);
        eigrpHeader::Option GenerateAuthenticatedTLV(const eigrpHeader& eigrp, const std::shared_ptr<EigrpConfigs::NeighborInfo>& neighbor);
        
        // Stub Checks
        bool IsStub() const { return stubConfig.isStub; }
        bool AdvertiseConnected() const { return stubConfig.advertiseConnected; }
        bool AdvertiseStatic() const { return stubConfig.advertiseStatic; }
        bool AdvertiseSummary() const { return stubConfig.advertiseSummary; }
        bool AdvertiseRedistributed() const { return stubConfig.advertiseRedistributed; }

        // List of summary routes
        std::vector<EigrpConfigs::SummaryRoute> summaryRoutes;
    
        // List of EIGRP interfaces
        std::map<int, std::shared_ptr<EigrpInterface>> eigrpInterfaceList{};

        // Eigrp proccess mutex
        mutex eigrpMutex;
        
        // Configurations for EIGRP
        vector<EigrpConfigs::network> networks;
        EigrpConfigs::KValue kvalue;
        string virtualRouterID = std::string("\x00\x00", 2);
        int asNumber;
        double wideMetric;
        double redistributionMetricOffset = 0.0;
        bool autoSummarizationEnabled = false;
        AddressFamily getAddressFamily() const { return addressFamily; }
        vector<vector<EigrpConfigs::NetworksDistributed>*> EigrpDistributionList;
        std::unordered_map<std::string, int> stuckInActiveTimers;
        std::unique_ptr<Protocol::TopologyTable> topologyTable;
        EigrpConfigs::StubConfig stubConfig;

    private:

        Variable variable;
        AddressFamily addressFamily;
        bool runTimers = true;
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
        };
    
        struct TopologyEntry {
            std::string destination;
            int prefixLength;
            std::map<std::string, RouteInfo> routesByNeighbor;
            bool isActive;
            // Timers for Active and Stuck-In-Active
            int activeTimerId = 0;
            int StuckInActiveTimerId = 0;

            vector<std::string> feasibleSuccessors;
            vector<std::string> successors;
        };

        void AddOrUpdateRoute(const std::string& destination, int prefixLength, const RouteInfo& routeInfo, const std::string& neighborIp);
        void RemoveRoutesFromNeighbor(const std::string& neighborIp);
        TopologyEntry* FindBestRoute(const std::string& destination);
        void HandleRouteFailure(const std::string& destination);
        void MarkRouteAsPassive(const std::string& destination, EigrpInterface* eigrp);
        void RemoveEntry(const std::string& destination);
        void SetVariance(int var);

        std::map<std::string, TopologyEntry>& GetTopologyEntries() {return topologyEntries; }
    
    private:
        std::mutex tableMutex;
        std::map<std::string, TopologyEntry> topologyEntries;
        int variance = 1;
    };
}




// Global pointer to the current EIGRP instance
extern Protocol::Eigrp* currentEigrp;

// Map of EIGRP instances by AS number
extern map<int, std::shared_ptr<Protocol::EigrpInstance>> eigrpList;

// Updates EIGRP interface list based on interface changes
void UpdateEigrpInterface(Interface* interface);
