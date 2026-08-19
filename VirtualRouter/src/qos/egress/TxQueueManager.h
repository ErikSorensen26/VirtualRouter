/**
 * @file TxQueueManager.h
 * @brief Manages TX queue creation, distribution, and per-interface egress policy.
 */

/**
 * @defgroup QOS Quality of Service
 * @brief Per-VRF TX/RX queue management: policies, distribution, and egress/ingress handling.
 */

/**
 * @defgroup QOS_EGRESS QoS Egress
 * @ingroup QOS
 * @brief TX queue manager, distributor, queue types, and per-queue options.
 */

#ifndef TX_QUEUE_MANAGER_H
#define TX_QUEUE_MANAGER_H

#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>
#include "TxQueueOpts.hpp"

namespace interface { class Interface; }
namespace hardware::egress { class EgressBase; }

namespace qos::egress
{
class BaseQueue;
class TxDistributor;

/**
 * @enum CpuPolicy
 * @brief CPU allocation policy for TX queue distribution across cores.
 * @ingroup QOS
 */
enum class CpuPolicy { EqualShare, Weighted };

/**
 * @brief Per-interface TX egress policy and queue configuration.
 * @ingroup QOS
 *
 * Defines how many TX queues should be created for an interface, whether to distribute
 * traffic uniformly across CPUs or use weighted allocation, and default options for
 * each queue instance.
 */
struct TxIfacePolicy
{
    int minQueues = 1;           ///< Minimum number of queues for this interface.
    int maxQueues = 0;           ///< Maximum number of queues (0 = unlimited).
    int weight    = 1;           ///< Weight for CPU allocation if CpuPolicy::Weighted.
    TxQueueOpts defaultQueueOpts{}; ///< Default options applied to queues on this interface.
};

/**
 * @brief TX queue state: egress backend and queue instance.
 * @ingroup QOS
 */
struct QueueState
{
    TxQueueOpts              opts;    ///< Queue configuration options.
    hardware::egress::EgressBase* egress = nullptr; ///< Egress (TX) backend for frame transmission.
    BaseQueue*               queue  = nullptr; ///< TX queue instance.
};

/**
 * @brief Manages TX queue lifecycle and distribution across interfaces and CPU cores.
 * @ingroup QOS
 *
 * Creates, configures, and destroys TX queues for each interface. Distributes queues
 * across available CPU cores according to policy (equal share or weighted). Coordinates
 * with TxDistributor to multiplex traffic from multiple cores to physical TX rings.
 *
 * ## Architectural Role
 * Sits between the interface manager and hardware egress layer. On interface startup,
 * allocates queues; on shutdown, deallocates them. Acts as registry for locating queues
 * by interface.
 *
 * ## Lifecycle & Ownership
 * Owned by the global system. Queues are created dynamically as interfaces come up and
 * destroyed as they go down. All queue memory is managed internally.
 *
 * ## Concurrency Model
 * Thread-safe for start/stop operations via internal mutex. Each queue handles its own
 * serialization internally (assumes single producer per queue).
 */
class TxQueueManager
{
public:
    /**
     * @brief Per-interface policy for queue allocation and parameters.
     * @ingroup QOS
     */
    struct IfacePolicy
    {
        int minQueues = 1;           ///< Minimum queues for this interface.
        int maxQueues = 0;           ///< Maximum queues (0 = unlimited).
        int weight = 1;              ///< Weight for weighted CPU allocation.
        TxQueueOpts defaultQueueOpts{}; ///< Default options per queue.
    };

    /**
     * @brief Constructs the TX queue manager.
     */
    TxQueueManager();

    /**
     * @brief Destructs and deallocates all TX queues.
     */
    ~TxQueueManager();

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
     * @brief Sets the CPU core processing bias compared to rx.
     */
    void setTxCoreBias(double bias);

    /**
     * @brief Sets the process-wide default for TxQueueOpts::qdiscBypass.
     *
     * Applied to every queue's opts as they're (re)created, unless an interface's
     * policy.defaultQueueOpts already opts in explicitly. See TxQueueOpts::qdiscBypass
     * for the tradeoff: faster egress vs. invisible to tcpdump/Wireshark.
     *
     * @param enable True to bypass the qdisc layer on TX sockets by default.
     */
    void setQdiscBypass(bool enable);

    /**
     * @brief Starts TX queues for an interface.
     *
     * Allocates queues according to policy, pins to CPUs, and initializes egress backends.
     * Called when interface is brought up.
     *
     * @param iface Interface to start TX queues for.
     */
    void start(interface::Interface* iface);

    /**
     * @brief Stops TX queues for an interface.
     *
     * Stops packet transmission, deallocates queues, and releases resources.
     * Called when interface is brought down.
     *
     * @param iface Interface to stop TX queues for.
     */
    void stop(interface::Interface* iface);

    void addInterface(interface::Interface& iface,
                      const std::string& ifname,
                      const TxIfacePolicy& policy);
    void removeInterface(interface::Interface& iface);
    void updateInterfacePolicy(interface::Interface& iface,
                               const TxIfacePolicy& policy);

    void shutdown();

private:
    struct HwQueueCaps
    {
        int maxTx = 1;
        int curTx = 1;
    };

    struct IfState
    {
        interface::Interface* iface      = nullptr;
        std::string           ifname;
        TxIfacePolicy         policy;
        int                   hwTxQueues = 0;

        // Fixed-capacity array of queue pointers.  Allocated once in
        // addInterface with size == hardware maxTx; never reallocated so the
        // raw pointer handed to TxDistributor remains stable.
        std::unique_ptr<QueueState*[]> queues;
        uint32_t queueCap    = 0; // allocated capacity
        uint32_t queueAmount = 0; // active queues [0, queueCap)
    };

    std::mutex           mu;
    std::vector<int>     cores;
    CpuPolicy            cpuPolicy  = CpuPolicy::EqualShare;
    double               txCoreBias = 0.5;
    bool                 qdiscBypassDefault = false;

    std::unordered_map<interface::Interface*, IfState> ifs;

    void reoptimize();

    HwQueueCaps getHwTxQueues(const std::string& ifname);
    bool        setHwTxQueues(const std::string& ifname, int num);

    int              computeTargetFor(const IfState& st, int totalCores) const;
    std::vector<int> buildCoreOrder(const IfState& st) const;

    void ensureQueueCount(IfState& st, int target, const std::vector<int>& coreOrder);
    void startOne(IfState& st, TxQueueOpts qopts);
    void stopLast(IfState& st);
    void stopAndDelete(QueueState* qs);
};

} // namespace qos::egress

#endif // TX_QUEUE_MANAGER_H