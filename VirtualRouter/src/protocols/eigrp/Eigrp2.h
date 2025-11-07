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


class InterfaceConfigs;

namespace Protocol 
{
    class Eigrp;
    class EigrpInterface;
    class TopologyTable;

    /**
     * @struct EigrpAutonomousSystems
     * @brief Manages multiple Autonomous Systems within the EIGRP process.
     */

}


#endif // EIGRP_H
