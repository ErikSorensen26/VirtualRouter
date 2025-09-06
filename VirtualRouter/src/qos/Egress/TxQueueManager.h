// RxQueueManager.h

#ifndef TX_QUEUE_MANAGER_H
#define TX_QUEUE_MANAGER_H

#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include "TxQueueOpts.hpp"

class Interface;
class EgressBase;
class BaseQueue;
class TxDistributor;

enum class CpuPolicy { EqualShare, Weighted };

struct TxIfacePolicy
{
    int minQueues = 1;
    int maxQueues = 0;
    int weight = 1;
    TxQueueOpts defaultQueueOpts = {};
};

    struct QueueState
    {
        TxQueueOpts opts;
        EgressBase* egress = nullptr;
        BaseQueue* queue = nullptr;
    };

class TxQueueManager
{
public:
    TxQueueManager();
    ~TxQueueManager();
    
    void setCorePool(std::vector<int> cpuIds);
    void setCpuPolicy(CpuPolicy p);

    void setTxCoreBias(double bias);

    void addInterface(Interface& iface, const std::string& ifname, const TxIfacePolicy& policy);
    void removeInterface(Interface& iface);
    void updateInterfacePolicy(Interface& iface, const TxIfacePolicy& policy);

    void shutdown();

private:
    struct HwQueueCaps
    {
        int maxTx = 1;
        int curTx = 1;
    };

    struct IfState
    {
        Interface* iface = nullptr;
        std::string ifname;
        TxIfacePolicy policy;

        int hwTxQueues = 0;

        QueueState** queues = nullptr;
        uint32_t queueAmount = 0;
        TxDistributor* distro = nullptr;
    };

    std::mutex mu;
    std::vector<int> cores;
    CpuPolicy cpuPolicy = CpuPolicy::EqualShare;
    double txCoreBias = 0.5;

    std::unordered_map<Interface*, IfState> ifs;

private:
    void reoptimize();

    HwQueueCaps getHwTxQueues(const std::string& ifname);
    bool setHwTxQueues(const std::string& ifname, int mun);

    int computeTargetFor(const IfState& st, int totalCores) const;
    std::vector<int> buildCoreOrder(const IfState& st) const;

    void ensureQueueCount(IfState& st, int target, const std::vector<int>& coreOrder);
    void startOne(IfState& st, TxQueueOpts qopts);
    void stopLast(IfState& st);
    void stopAndDelete(QueueState* qs);
};


#endif // TX_QUEUE_MANAGER_H
