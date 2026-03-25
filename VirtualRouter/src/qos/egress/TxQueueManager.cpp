// TxQueueManager.cpp

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

#include "TxQueueManager.h"
#include "hardware/egress/Egress.h"
#include "hardware/egress/EgressBase.h"
#include "interface/Interface.h"
#include "TxQueue.hpp"
#include "TxDistributor.h"

namespace qos::egress
{

TxQueueManager::TxQueueManager()  = default;
TxQueueManager::~TxQueueManager() { shutdown(); }

// ---- configuration -------------------------------------------------------

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

// ---- interface management -----------------------------------------------

void TxQueueManager::addInterface(interface::Interface& iface,
                                   const std::string& ifname,
                                   const TxIfacePolicy& policy)
{
    std::lock_guard<std::mutex> lk(mu);
    if (ifs.count(&iface)) return;

    HwQueueCaps caps = getHwTxQueues(ifname);
    uint32_t maxQ = static_cast<uint32_t>(std::max(caps.maxTx, 1));

    IfState st;
    st.iface       = &iface;
    st.ifname      = ifname;
    st.policy      = policy;
    st.policy.defaultQueueOpts.ifname = ifname;
    st.queues      = std::make_unique<QueueState*[]>(maxQ);
    st.queueCap    = maxQ;
    st.queueAmount = 0;
    std::fill(st.queues.get(), st.queues.get() + maxQ, nullptr);

    auto [it, ok] = ifs.emplace(&iface, std::move(st));

    // TxDistributor holds a raw pointer into the stable queue array.
    iface.tx = new TxDistributor(it->second.queues.get(), 0);
    reoptimize();
}

void TxQueueManager::removeInterface(interface::Interface& iface)
{
    std::lock_guard<std::mutex> lk(mu);

    auto it = ifs.find(&iface);
    if (it == ifs.end()) return;

    auto& st = it->second;
    setHwTxQueues(st.ifname, 1);

    for (uint32_t i = 0; i < st.queueAmount; ++i)
        stopAndDelete(st.queues[i]);

    // unique_ptr releases the array automatically when IfState is destroyed.

    delete iface.tx;
    iface.tx = nullptr;

    ifs.erase(it);
    reoptimize();
}

void TxQueueManager::updateInterfacePolicy(interface::Interface& iface,
                                            const TxIfacePolicy& policy)
{
    std::lock_guard<std::mutex> lk(mu);
    auto it = ifs.find(&iface);
    if (it == ifs.end()) return;
    auto& st = it->second;
    st.policy = policy;
    st.policy.defaultQueueOpts.ifname = st.ifname;
    reoptimize();
}

// ---- start / stop --------------------------------------------------------

void TxQueueManager::start(interface::Interface* iface)
{
    std::lock_guard<std::mutex> lk(mu);
    auto it = ifs.find(iface);
    if (it == ifs.end()) return;
    for (uint32_t i = 0; i < it->second.queueAmount; ++i)
        it->second.queues[i]->queue->start();
}

void TxQueueManager::stop(interface::Interface* iface)
{
    std::lock_guard<std::mutex> lk(mu);
    auto it = ifs.find(iface);
    if (it == ifs.end()) return;
    for (uint32_t i = 0; i < it->second.queueAmount; ++i)
        it->second.queues[i]->queue->stop();
}

void TxQueueManager::shutdown()
{
    std::lock_guard<std::mutex> lk(mu);
    for (auto& [iface, st] : ifs)
    {
        for (uint32_t i = 0; i < st.queueAmount; ++i)
            stopAndDelete(st.queues[i]);
        st.queueAmount = 0;
        // unique_ptr cleans up array storage when ifs is cleared below.
    }
    ifs.clear();
}

// ---- hardware queue count ------------------------------------------------

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
        caps.maxTx = static_cast<int>(ec.max_tx ? ec.max_tx : ec.max_combined);
        caps.curTx = static_cast<int>(ec.tx_count ? ec.tx_count : ec.combined_count);
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
    ec.cmd      = ETHTOOL_SCHANNELS;
    ec.tx_count = static_cast<__u32>(num);
    ifr.ifr_data = reinterpret_cast<char*>(&ec);

    bool ok = (ioctl(s, SIOCETHTOOL, &ifr) == 0);
    ::close(s);
    return ok;
}

// ---- reoptimize ----------------------------------------------------------

void TxQueueManager::reoptimize()
{
    if (cores.empty()) return;

    for (auto& [iface, st] : ifs)
    {
        auto  order   = buildCoreOrder(st);
        int   desired = computeTargetFor(st, static_cast<int>(order.size()));

        desired = std::max(1, static_cast<int>(std::lround(desired * txCoreBias)));

        HwQueueCaps caps = getHwTxQueues(st.ifname);
        if (desired > caps.maxTx) desired = caps.maxTx;
        // Also respect the pre-allocated array capacity.
        desired = std::min(desired, static_cast<int>(st.queueCap));

        int applied = caps.curTx;
        if (desired != caps.curTx && setHwTxQueues(st.ifname, desired))
            applied = desired;

        st.hwTxQueues = applied;
        ensureQueueCount(st, applied, order);
    }
}

int TxQueueManager::computeTargetFor(const IfState& st, int totalCores) const
{
    int minQ = std::max(1, st.policy.minQueues);
    int maxQ = st.policy.maxQueues > 0 ? st.policy.maxQueues : (1 << 30);

    if (cpuPolicy == CpuPolicy::EqualShare)
    {
        int nIf = static_cast<int>(ifs.size());
        int base = std::max(1, totalCores / std::max(1, nIf));
        return std::clamp(base, minQ, maxQ);
    }
    else
    {
        int sumW = 0;
        for (auto& [iface, s] : ifs) sumW += std::max(1, s.policy.weight);
        int want = static_cast<int>(
            static_cast<double>(totalCores) *
            static_cast<double>(std::max(1, st.policy.weight)) /
            static_cast<double>(std::max(1, sumW)));
        return std::clamp(std::max(1, want), minQ, maxQ);
    }
}

std::vector<int> TxQueueManager::buildCoreOrder(const IfState&) const
{
    std::vector<int> out = cores;
    if (out.empty()) out.push_back(0);
    return out;
}

void TxQueueManager::ensureQueueCount(IfState& st, int target,
                                       const std::vector<int>& coreOrder)
{
    int cur = static_cast<int>(st.queueAmount);
    if (target > cur)
    {
        size_t rr = 0;
        for (int i = 0; i < target - cur; ++i)
        {
            TxQueueOpts q  = st.policy.defaultQueueOpts;
            q.ifname       = st.ifname;
            q.cpuId        = coreOrder[rr % coreOrder.size()];
            rr = (rr + 1) % coreOrder.size();
            startOne(st, q);
        }
    }
    else if (target < cur)
    {
        for (int i = 0; i < cur - target; ++i)
            stopLast(st);
    }
}

void TxQueueManager::startOne(IfState& st, TxQueueOpts qopts)
{
    if (st.queueAmount >= st.queueCap)
        throw std::runtime_error("TxQueueManager: exceeded pre-allocated queue capacity");

    auto* qs  = new QueueState{};
    qs->opts  = qopts;
    qs->egress = hardware::egress::create(st.iface, qs->opts);
    if (!qs->egress)
    {
        delete qs;
        throw std::runtime_error("egress factory returned null");
    }
    // Cap the software queue depth to the egress backend's actual frame count
    // so the queue can never hold more frames than the backend has in flight.
    const uint32_t queueDepth = hardware::ceilPow2(std::min(qopts.frameCount, qs->egress->getFrameCount()));
    qs->queue = txqueuefactory::create(TxQueueType::FIFO, queueDepth, *qs->egress);
    if (!qs->queue)
    {
        delete qs->egress;
        delete qs;
        throw std::runtime_error("queue factory returned null");
    }

    st.queues[st.queueAmount] = qs;
    ++st.queueAmount;

    if (st.iface && st.iface->tx)
        st.iface->tx->appendQueue();
}

void TxQueueManager::stopLast(IfState& st)
{
    if (st.queueAmount == 0) return;

    // Decrement N in the distributor FIRST so no new packets are routed here.
    if (st.iface && st.iface->tx)
        st.iface->tx->popQueue();

    --st.queueAmount;
    stopAndDelete(st.queues[st.queueAmount]);
    st.queues[st.queueAmount] = nullptr;
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
}

} // namespace qos::egress
