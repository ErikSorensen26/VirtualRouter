// TxQueueManager.cpp

#include "TxQueueManager.h"

#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <cmath>

#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <Egress.h>
#include <Interface.h>
#include "TxQueue.hpp"
#include "TxDistributor.h"

TxQueueManager::TxQueueManager() {}
TxQueueManager::~TxQueueManager()
{
    shutdown();
}

void TxQueueManager::setCorePool(std::vector<int> cpuIds)
{
    std::lock_guard<std::mutex> lk(mu);
    cores = std::move(cpuIds);
    reoptimize();
}

void TxQueueManager::setCpuPolicy(CpuPolicy p)
{
    std::lock_guard<std::mutex> lk(mu);
    cpuPolicy = p;
    reoptimize();
}

void TxQueueManager::setTxCoreBias(double bias)
{
    if (bias <= 0.0) bias = 0.01;
    std::lock_guard<std::mutex> lk(mu);
    txCoreBias = bias;
    reoptimize();
}

void TxQueueManager::addInterface(Interface& iface, const std::string& ifname, const TxIfacePolicy& policy)
{
    std::lock_guard<std::mutex> lk(mu);
    if (ifs.count(&iface)) return;

    uint32_t maxQ = (uint32_t)std::max(getHwTxQueues(ifname).maxTx, 1);

    IfState st;
    st.iface = &iface;
    st.ifname = ifname;
    st.policy = policy;
    st.policy.defaultQueueOpts.ifname = ifname;
    st.queues = new QueueState*[maxQ]();
    st.queueAmount = 0;

    auto q = ifs.emplace(&iface, std::move(st));
    iface.tx = new TxDistributor(q.first->second.queues, q.first->second.queueAmount);
    reoptimize();
}

void TxQueueManager::removeInterface(Interface& iface)
{
    std::lock_guard<std::mutex> lk(mu);

    auto it = ifs.find(&iface);
    if (it == ifs.end()) return;

    auto& st = it->second;

    setHwTxQueues(st.ifname, 1);

    for (int i = 0; i < st.queueAmount; ++i)
        stopAndDelete(st.queues[i]);

    delete[] it->second.queues;

    delete iface.tx;
    iface.tx = nullptr;

    ifs.erase(it);

    reoptimize();
}

void TxQueueManager::updateInterfacePolicy(Interface& iface, const TxIfacePolicy& policy)
{
    std::lock_guard<std::mutex> lk(mu);
    auto it = ifs.find(&iface);
    if (it == ifs.end()) return;
    auto& st = it->second;
    st.policy = policy;
    st.policy.defaultQueueOpts.ifname = st.ifname;
    reoptimize();
}

TxQueueManager::HwQueueCaps TxQueueManager::getHwTxQueues(const std::string& ifname)
{
    HwQueueCaps caps{};
    int s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return caps;

    ifreq ifr{};
    std::strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);

    ethtool_channels ec{};
    ec.cmd = ETHTOOL_GCHANNELS;
    ifr.ifr_data = reinterpret_cast<char*>(&ec);

    if (ioctl(s, SIOCETHTOOL, &ifr) == 0)
    {
        caps.maxTx = (int)(ec.max_tx ? ec.max_tx : ec.max_combined);
        caps.curTx = (int)(ec.tx_count ? ec.tx_count : ec.combined_count);
        if (caps.maxTx <= 0) caps.maxTx = 1;
        if (caps.curTx <= 0) caps.curTx = 1;
    }
    
    ::close(s);
    return caps;
}

bool TxQueueManager::setHwTxQueues(const std::string& ifname, int num)
{
    int s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return false;

    ifreq ifr{};
    std::strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);

    ethtool_channels ec{};
    ec.cmd = ETHTOOL_SCHANNELS;
    ec.tx_count = (__u32)num;
    // NOTE: Some drivers only accept combined_count; if so, adapt here.
    
    ifr.ifr_data = reinterpret_cast<char*>(&ec);
    bool ok = (ioctl(s, SIOCETHTOOL, &ifr) == 0);

    ::close(s);
    return ok;
}

void TxQueueManager::reoptimize()
{
    if (cores.empty()) return;

    for (auto& kv : ifs)
    {
        auto& st = kv.second;
        auto order = buildCoreOrder(st);

        int desired = computeTargetFor(st, (int)order.size());

        desired = std::max(1, (int)std::lround(desired * txCoreBias));

        HwQueueCaps caps = getHwTxQueues(st.ifname);
        if (desired > caps.maxTx) desired = caps.maxTx;

        int applied = caps.curTx;
        if (desired != caps.curTx)
            if (setHwTxQueues(st.ifname, desired))
                applied = desired;

        st.hwTxQueues = applied;
        ensureQueueCount(st, applied, order);
    }
}

int TxQueueManager::computeTargetFor(const IfState& st, int totalCores) const
{
    int minQ = std::max(1, st.policy.minQueues);
    int maxQ = st.policy.maxQueues > 0 ? st.policy.maxQueues : 1 << 30;

    if (cpuPolicy == CpuPolicy::EqualShare)
    {
        int nIf = (int)ifs.size();
        int base = std::max(1, totalCores / std::max(1, nIf));
        return std::clamp(base, minQ, maxQ);
    }
    else
    {
        int sumW = 0;
        for (auto& kv : ifs) sumW += std::max(1, kv.second.policy.weight);
        int want = (int)((double)totalCores * (double)std::max(1, st.policy.weight) / (double)std::max(1, sumW));
        return std::clamp(std::max(1, want), minQ, maxQ);
    }
}

std::vector<int> TxQueueManager::buildCoreOrder(const IfState& st) const
{
    std::vector<int> out = cores;
    if (out.empty()) out.push_back(0);
    return out;
}

void TxQueueManager::ensureQueueCount(IfState& st, int target, const std::vector<int>& coreOrder)
{
    int cur = (int)st.queueAmount;
    if (target > cur)
    {
        size_t rr = 0;
        for (int i = 0; i < target - cur; ++i)
        {
            TxQueueOpts q = st.policy.defaultQueueOpts;
            q.ifname = st.ifname;

            q.cpuId = coreOrder[rr % coreOrder.size()];
            rr = (rr + 1) % coreOrder.size();

            startOne(st, q);
        }
    }
    else if (target < cur)
    {
        for (int i = 0; i < cur - target; ++i) stopLast(st);
    }
}

void TxQueueManager::startOne(IfState& st, TxQueueOpts qopts)
{
    int idx = (int)st.queueAmount;
    auto* qs = new QueueState{};
    qs->opts = qopts;
    qs->egress = EgressFactory::create(st.iface, qs->opts);
    if (!qs->egress) { delete qs; throw std::runtime_error("egress factory returned null"); }
    qs->queue = TxQueueFactory::create(TxQueueType::FIFO, qopts.frameCount, *qs->egress);
    if (!qs->queue) { delete qs->egress; delete qs; throw std::runtime_error("queue factory returned null"); }

    st.queues[idx] = qs;
    qs->queue->start();
    ++st.queueAmount;

    if (st.iface && st.iface->tx)
        st.iface->tx->appendQueue();
}

void TxQueueManager::stopLast(IfState& st)
{
    if (st.queueAmount == 0) return;

    int idx = (int)st.queueAmount - 1;
    auto* qs = st.queues[idx];

    stopAndDelete(qs);

    st.queues[idx] = nullptr;
    --st.queueAmount;

    if (st.iface && st.iface->tx)
        st.iface->tx->popQueue();
}

void TxQueueManager::stopAndDelete(QueueState* qs)
{
    if (!qs) return;

    if (qs->queue)
    {
        qs->queue->stop();
        delete qs->queue;
        qs->queue = nullptr;
    }

    if (qs->egress)
    {
        delete qs->egress;
        qs->egress = nullptr;
    }
    delete qs;
    qs = nullptr;
}

void TxQueueManager::shutdown()
{
    std::lock_guard<std::mutex> lk(mu);
    for (auto& kv : ifs)
    {
        for (int i = 0; i < kv.second.queueAmount; ++i)
            stopAndDelete(kv.second.queues[i]);

        delete[] kv.second.queues;
        kv.second.queues = nullptr;
        kv.second.queueAmount = 0;
    }
    ifs.clear();
}
