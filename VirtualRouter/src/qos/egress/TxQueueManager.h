// TxQueueManager.h (was RxQueueManager.h — note: file kept its original name)

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

enum class CpuPolicy { EqualShare, Weighted };

struct TxIfacePolicy
{
    int minQueues = 1;
    int maxQueues = 0;
    int weight    = 1;
    TxQueueOpts defaultQueueOpts{};
};

struct QueueState
{
    TxQueueOpts              opts;
    hardware::egress::EgressBase* egress = nullptr;
    BaseQueue*               queue  = nullptr;
};

class TxQueueManager
{
public:
    TxQueueManager();
    ~TxQueueManager();

    void setCorePool(std::vector<int> cpuIds);
    void setCpuPolicy(CpuPolicy p);
    void setTxCoreBias(double bias);

    void start(interface::Interface* iface);
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
