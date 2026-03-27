/**
 * @file RxQueueManager.h
 * @brief Manages RX queue creation and distribution across CPU cores for ingress processing.
 */

/**
 * @defgroup QOS_INGRESS QoS Ingress
 * @ingroup QOS
 * @brief RX queue manager and per-queue ingress options.
 */

#ifndef RX_QUEUE_MANAGER_H
#define RX_QUEUE_MANAGER_H

#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <string>

#include "interface/Interface.h"
#include "hardware/ingress/Ingress.h"
#include "RxQueueOpts.hpp"

namespace qos::ingress
{

/**
 * @brief Manages RX queue lifecycle and distribution across CPU cores.
 * @ingroup QOS_INGRESS
 *
 * Creates, configures, and destroys RX queues for each interface. Distributes queues
 * across available CPU cores according to policy (equal share or weighted). Coordinates
 * with the ingress hardware layer to deliver packets.
 *
 * ## Architectural Role
 * Sits between the interface manager and hardware ingress layer. On interface startup,
 * allocates queues; on shutdown, deallocates them. Acts as registry for locating queues
 * by interface.
 *
 * ## Lifecycle & Ownership
 * Owned by the global system. Queues are created dynamically as interfaces come up and
 * destroyed as they go down. All queue memory is managed internally.
 *
 * ## Concurrency Model
 * Thread-safe for start/stop operations via internal mutex. Each queue handles its own
 * serialization internally (assumes single consumer per queue).
 */
class RxQueueManager
{
public:
    /**
     * @brief Per-interface policy for RX queue allocation and parameters.
     * @ingroup QOS_INGRESS
     */
    struct IfacePolicy
    {
        int minQueues = 1;           ///< Minimum queues for this interface.
        int maxQueues = 0;           ///< Maximum queues (0 = unlimited).
        int weight = 1;              ///< Weight for weighted CPU allocation.
        RxQueueOpts defaultQueueOpts{}; ///< Default options per queue.
    };

    /**
     * @enum CpuPolicy
     * @brief CPU allocation policy for RX queue distribution across cores.
     * @ingroup QOS_INGRESS
     */
    enum class CpuPolicy { EqualShare, Weighted };

    /**
     * @brief Constructs the RX queue manager.
     */
    RxQueueManager();

    /**
     * @brief Destructs and deallocates all RX queues.
     */
    ~RxQueueManager();

    /**
     * @brief Sets the available CPU cores for queue distribution.
     *
     * Queues will be pinned to these cores according to policy (EqualShare or Weighted).
     *
     * @param cpuIds Vector of CPU core IDs.
     */
    void setCorePool(std::vector<int> cpuIds);

    /**
     * @brief Sets the CPU allocation policy for queue distribution.
     *
     * @param p Policy: EqualShare (uniform across cores) or Weighted (per-interface weight).
     */
    void setCpuPolicy(CpuPolicy p);

    /**
     * @brief Starts RX queues for an interface.
     *
     * Allocates queues according to policy, pins to CPUs, and initializes ingress backends.
     * Called when interface is brought up.
     *
     * @param iface Interface to start RX queues for.
     */
    void start(interface::Interface* iface);

    /**
     * @brief Stops RX queues for an interface.
     *
     * Stops packet reception, deallocates queues, and releases resources.
     * Called when interface is brought down.
     *
     * @param iface Interface to stop RX queues for.
     */
    void stop(interface::Interface* iface);

    /**
     * @brief Adds an interface with policy to the queue manager.
     *
     * @param iface Interface to add.
     * @param ifname Interface name (e.g., "eth0").
     * @param policy Queue allocation and configuration policy.
     */
    void addInterface(interface::Interface& iface, const std::string& ifname, const IfacePolicy& policy);

    /**
     * @brief Removes an interface from queue management.
     *
     * @param iface Interface to remove.
     */
    void removeInterface(interface::Interface& iface);

    /**
     * @brief Updates the policy for an interface.
     *
     * @param iface Interface to update.
     * @param policy New policy.
     */
    void updateInterfacePolicy(interface::Interface& iface, const IfacePolicy& policy);

    void shutdown();

private:
    struct QueueState
    {
        RxQueueOpts opts;
        hardware::ingress::IngressBase* ingress = nullptr;
    };
    struct  IfState
    {
        interface::Interface* iface = nullptr;
        std::string ifname;
        IfacePolicy policy;
        uint16_t fanoutGroup = 0;

        int hwRxQueues = 1;
        std::vector<QueueState> queues;
    };
    struct HwQueueCaps
    {
        int maxRx = 1;
        int curRx = 1;
    };

    std::vector<int> cores;
    CpuPolicy cpuPolicy = CpuPolicy::EqualShare;

    std::unordered_map<interface::Interface*, IfState> ifs;
    std::mutex mu;
    std::atomic<uint32_t> fanoutSeed = 0xCAFE;

    void reoptimize();
    HwQueueCaps getHwRxQueues(const std::string& ifnames);
    bool setHwRxQueues(const std::string& ifname, int num);
    void ensureQueueCount(IfState& st, int target, const std::vector<int>& coreOrder);

    void startOne(IfState& st, RxQueueOpts qopts);
    void stopLast(IfState& st);
    static void stopAndDelete(QueueState& qs);

    uint16_t assignFanoutGroup();
    int computeTargetFor(const IfState& st, int totalCores) const;
    std::vector<int> buildCoreOrder(const IfState& st) const;
};

} // namespace qos

#endif // RX_QUEUE_MANAGER_H

