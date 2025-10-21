// EigrpInterfaceTimerManager.h

#ifndef EIGRP_INTERFACE_TIMER_MANAGER_H
#define EIGRP_INTERFACE_TIMER_MANAGER_H

#include <cstdint>
#include <chrono>
#include <unordered_map>
#include <atomic>
#include <deque>
#include <IPAddress.hpp>

class RoutingTable { public: struct Eigrp; };
namespace EigrpConfigs
{
struct NeighborInfo { struct ReliablePacketInfo {struct Packet; }; };
struct OutgoingQuery;
}

namespace Protocol
{
class EigrpInterfaceTimerManager
{
public:

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
     * @brief Stops the Hello timer.
     *
     * Cancels the active Hello timer thread, ceasing the periodic transmission of Hello packets
     * and effectively pausing neighbor relationship maintenance.
     */
    virtual void stopHello();

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

    // Hello timer
    std::atomic<uint32_t> helloTimerId = 0; ///< Timer ID for the Hello timer.
    std::atomic<bool> helloDone{true};
    std::atomic<bool>helloTimerActive = false; ///< Indicates if the Hello timer is active.
    std::chrono::steady_clock::time_point helloStartTime; ///< Start time for the Hello timer.

    // SIA timers
    std::unordered_map<IPPrefix, uint32_t> activeTimers; ///< Map of active timers for routes.
    std::unordered_map<IPPrefix, uint32_t> siaTimers; ///< Map of SIA timers for routes.

    // Mutexes
    std::mutex helloTimerMutex; ///< Mutex for Hello timer operations.
    std::mutex activeTimerMutex; ///< Mutex for Active timer operations.

    // Other options/tracking
    std::deque<std::chrono::steady_clock::time_point> routeChangeTimes;
    std::chrono::steady_clock::time_point supressedUntil;
    uint8_t restartCounter = 0;
    std::atomic<bool> isSupressed = false;
    std::atomic<uint32_t> dampeningTimerId = 0;

    std::atomic<bool>runTimers = true; ///< Flag to indicate if timers should continue running.
};
}

#endif // EIGRP_INTERFACE_TIMER_MANAGER_H
