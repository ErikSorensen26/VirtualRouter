#pragma once

#include <map>
#include <memory>
#include <algorithm>
#include <string>
#include <vector>
#include <PacketStructure.h>
#include <Functions.h>
#include <random>
#include <mutex>
#include <thread>
#include <chrono>
#include <climits>
#include <time.h>
#include <condition_variable>
#include <atomic>
#include <Interface.h>
#include <RoutingTable.h>
#include <TimeManager.h>

using namespace std;

extern mutex globalEigrpMutex;

// Config schemas for EIGRP class
namespace EigrpConfigs {
    struct network {
        string ip{};
        string mask{};
    };
    struct KValue {
        int k1_Bandwidth = 1;
        int k2_Load = 0;
        int k3_Delay = 1;
        int k4_Reliability = 0;
        int k5_MTU = 0;
        int k6_Power = 0;
    };
    struct InternalRoute {
        string nexthop{"00000000"};
        string prefixLength{"10"};
        string destination{"00000000"};
        struct metric {
            string scaledDelay{"00000000"};
            string scaledBw{"00000000"};
            string mtu{"000000"};
            string hopCount{"00"};
            string reliability{"00"};
            string load{"00"};
            string routeTag{"00"};
            string flags{"00"};
        } metric;
    };
    struct NeighborInfo {
        // Arp info
        bool hasMac = false;

        // Neighbor identification
        std::string ipAddress;
        std::string macAddress;

        // Timing and sequence information
        int holdTime;
        std::chrono::steady_clock::time_point lastHeard;

        // Sequence Numbers for reliabile delivery
        int sequenceNumber;
        int lastReceivedSequenceNumber;

        // Reliable transport data
        std::map<int, std::string> reliablePackets;
        std::map<int, int> retransmissionTimers;
        std::map<int, std::chrono::steady_clock::time_point> packetSendTimes;
        

        // RTT and RTO estimations
        double srtt;
        double rttvar;
        double rto;

        // Retransmission data
        int retransmissions;

        // Hold timer management
        int holdTimerId = 0;


        std::thread holdTimerThread;
        std::atomic<bool> holdTimerRunning;
        std::mutex holdMutex;
        std::condition_variable holdCV;
        std::atomic<bool> holdStop;
        // Other neighbor-specific data


        NeighborInfo()
            : holdTime(0), sequenceNumber(0), lastReceivedSequenceNumber(0),
              srtt(0.0), rttvar(0.0), rto(1.0), retransmissions(0) {}
    };
    struct NetworksDistributed {
        RoutingTable::Eigrp route;
        bool distrubuted = false;
        
    }; 
}

namespace Protocol 
{
    class Eigrp;
    class TopologyTable;

    class EigrpInterface : public std::enable_shared_from_this<Protocol::EigrpInterface> {
    public:
        // Holds EIGRP process
        Eigrp* eigrpProcess;

        // Constructor initializing AS number and starting Hello timer
        EigrpInterface(Eigrp& eigrpSystem, std::shared_ptr<Interface> interface);
        // Destructor stopping all timers
        ~EigrpInterface();
        
        // Sets up EIGRP packet headers
        void EigrpBody(ethernetHeader& eth, ipv4Header& ip, string mac);
        // Process Packet
        void ProcessPacket(const eigrpHeader* eigrpPacket, const ipv4Header* ipPacket);
        // Processes Hello packets
        void ProcessHello(const eigrpHeader* receivedHello, const ipv4Header* recievedIP);
        // Processes Update packets
        void ProcessUpdate(const eigrpHeader* receivedUpdate, const std::string& neighborIp);
        // Process Ack
        void ProcessAck(const eigrpHeader* recievedAck, const std::string& neighborIp);
        // Process query
        void ProcessQuery(const eigrpHeader* receivedQuery, const std::string& neighborIp);
        // Process reply
        void ProcessReply(const eigrpHeader* recievedReply, const std::string& neighborIp);
        // Send Ack to neighbors
        void SendAckToNeighbor(const std::string& neighborIp, int sequenceNumber);
        // Send Update Packet
        void SendUpdateToNeighbor(const std::string& neighborIp, const RoutingTable::Eigrp& route, bool removal);
        // Send full Update Packet
        void SendFullUpdateToNeighbor(const std::string& neighborIp);
        // Send query to neighbor
        void SendQueryToNeighbor(const std::string& neighborIp, const RoutingTable::Eigrp& route);
        // Send reply to neighbor
        void SendReplyToNeighbor(const std::string& neighborIp, const RoutingTable::Eigrp& route);

        // Updates Routing Table
        void UpdateRoutingTable(const RoutingTable::Eigrp& route);
        
        // Decodes Routes
        RoutingTable::Eigrp DecodeRoute(string ip, string value, bool external);
    
        mutex eigrpMutex;
        PacketInfo eigrpHello;

        int helloTime = 6;
        int holdTime = 15;
        int activeTime = 180;
        int stuckInActiveTime = 60; 
        int adminDistance = 90;
        int load{};
        bool passive = false;

        // Holds current interface
        std::shared_ptr<Interface> currentInterface;
    
        // Timers and their management methods
        void StartHello();
        void StartHelloHelper();
        void StopHello();
    
        void StartActiveTimer(const std::string& destinationF); 
        void HandleActiveTimeExpire(const std::string& destination); 
    
        void StartStuckInActive();
        void StopStuckInActive();

        void StartHoldTimer(const std::string& neighborIp, int holdTime);
        void HandleHoldTimeExpire(const std::string& neighborIp);

        void StartRetransmissionTimer(const std::string& neighborIp, int sequenceNumber, double timeout);
        void HandleRetransmissionTimeout(const std::string& neighborIp, int sequenceNumber);
        void UpdateRTTEstimate(EigrpConfigs::NeighborInfo& neighbor, int sequenceNumber);

        void UpdateRoutingTableForDestination(const std::string& destination);

        // Neighbor went down
        void HandleNeighborDown(const std::string& neighborIp);

        // Holds EIGRP neighbors
        std::map<std::string, EigrpConfigs::NeighborInfo> neighbors;
        // Protects access to neighbors
        std::mutex neighborMutex;

        int bandwidth;
        int delay{0};

        // Advertized route mutex
        std::mutex advertizedRouteMutex;
        // List of advertized routes
        std::unordered_map<std::string, RoutingTable::Eigrp> advertisedRoutes;

    private:

        std::chrono::steady_clock::time_point helloStartTime;

        // Topology table
        std::unique_ptr<Protocol::TopologyTable> topologyTable;

        // Timer IDs
        int helloTimerId = 0;
        int activeTimerId = 0;
        int stuckInActiveTimerId = 0;

        Functions* function = Functions::getInstance();
        Variable variable;
        bool runTimers = true;
        bool helloTimerActive = false;

        // Route Buffer
        vector<RoutingTable::Eigrp> routeBuffer;

        std::mutex helloTimerMutex;
        std::mutex retransmissionMutex;
    };


    // EIGRP class managing EIGRP protocol operations
    class Eigrp {
    public:
        // Constructor initializing AS number and starting Hello timer
        Eigrp(int& as);
        // Destructor stopping all timers
        ~Eigrp();

        // Configures EIGRP Hello packet with specific settings
        void EigrpHello(eigrpHeader& eigrp, string virtualRouterID, EigrpInterface* eigrpInt, bool ack = false, bool update = false, int sequenceNum = 0);
        // Configures EIGRP update packet with specific settings
        void EigrpUpdate(eigrpHeader& eigrp, string virtualRouterID, int sequenceNum, vector<EigrpConfigs::NetworksDistributed>& internalRoutes, bool init = false, bool conditional = false, bool restart = false, bool endoftable = false, bool query = false, bool reply = false);
        // Updates list of EIGRP interfaces based on address matching
        void UpdateInterfaceList();
        // Tests if an IP address matches the configured networks
        bool TestAddress(const std::string& testIp);
        // Add EIGRP rouing entry
        double CalculateMetric(EigrpConfigs::KValue k, int bandwidth, int load, int delay, int reliability);
        // Calculate Parameters
        string CalculateParameters(int holdTime);
        // Updates Distribution Lists
        void UpdateRoutingTable(const RoutingTable::Eigrp& route, const std::string& neighborIp);
        // Adds interface to routing table
        void UpdateRoutingTableForConnected();
        // Handles Interface change
        void OnInterfaceChange(Interface* interfacePtr);
        // Update from route change
        void NotifyRoutingChange(const RoutingTable::Eigrp& changeRoute, bool isRemoval);
    
        // List of EIGRP interfaces
        std::map<int, std::shared_ptr<EigrpInterface>> eigrpInterfaceList{};

        // Eigrp proccess mutex
        mutex eigrpMutex;
        
        // Configurations for EIGRP
        vector<EigrpConfigs::network> networks;
        EigrpConfigs::KValue kvalue;
        string virtualRouterID = "0000";
        int asNumber;

        // Eigrp Distribution List
        vector<vector<EigrpConfigs::NetworksDistributed>*> EigrpDistributionList;

    private:

        Functions* function = Functions::getInstance();
        Variable variable;
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
        };
    
        void AddOrUpdateRoute(const std::string& destination, int prefixLength, const RouteInfo& routeInfo, const std::string& neighborIp);
        void RemoveRoutesFromNeighbor(const std::string& neighborIp);
        TopologyEntry* FindBestRoute(const std::string& destination);
        void HandleRouteFailure(const std::string& destination);
        void MarkRouteAsPassive(const std::string& destination, EigrpInterface* eigrp);
        void RemoveEntry(const std::string& destination);
    
        std::map<std::string, TopologyEntry>& GetTopologyEntries() {return topologyEntries; }
    
    
    private:
        std::map<std::string, TopologyEntry> topologyEntries;
        std::mutex tableMutex;
    };
}




// Global pointer to the current EIGRP instance
extern Protocol::Eigrp* currentEigrp;

// Map of EIGRP instances by AS number
extern map<int, std::shared_ptr<Protocol::Eigrp>> eigrpList;

// Updates EIGRP interface list based on interface changes
void UpdateEigrpInterface(Interface* interface);
