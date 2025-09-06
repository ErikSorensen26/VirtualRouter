// RxQueueManager.h

#ifndef RX_QUEUE_MANAGER_H
#define RX_QUEUE_MANAGER_H

#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <string>
#include <Interface.h>
#include <Ingress.h>
#include "RxQueueOpts.hpp"

class RxQueueManager
{
public:
    struct IfacePolicy
    {
        int minQueues = 1;
        int maxQueues = 0;
        int weight = 1;
        RxQueueOpts defaultQueueOpts{};
    };

    enum class CpuPolicy { EqualShare, Weighted };

    RxQueueManager();
    ~RxQueueManager();

    void setCorePool(std::vector<int> cpuIds);
    void setCpuPolicy(CpuPolicy p);

    void addInterface(Interface& iface, const std::string& ifname, const IfacePolicy& policy);
    void removeInterface(Interface& iface);
    void updateInterfacePolicy(Interface& iface, const IfacePolicy& policy);

    void shutdown();

//private:
    struct QueueState
    {
        RxQueueOpts opts;
        IngressBase* ingress = nullptr;
    };
    struct  IfState
    {
        Interface* iface = nullptr;
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

    std::unordered_map<Interface*, IfState> ifs;
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

#endif // RX_QUEUE_MANAGER_H
