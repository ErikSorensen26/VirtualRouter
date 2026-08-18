// RxQueueManager.cpp

#include <algorithm>
#include <stdexcept>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <net/if.h>

#include "RxQueueManager.h"
#include "hardware/ingress/Ingress.h"

namespace qos::ingress
{

RxQueueManager::RxQueueManager() {}
RxQueueManager::~RxQueueManager() { shutdown(); }

void RxQueueManager::setCorePool(std::vector<int> cpuIds)
{
    std::lock_guard<std::mutex> lk(mu);
    cores = std::move(cpuIds);
    reoptimize();
}

void RxQueueManager::setCpuPolicy(CpuPolicy p)
{
    std::lock_guard<std::mutex> lk(mu);
    cpuPolicy = p;
    reoptimize();
}

uint16_t RxQueueManager::assignFanoutGroup()
{
    uint16_t g = static_cast<uint16_t>(fanoutSeed.fetch_add(1, std::memory_order_relaxed) & 0xFFFFu);
    return g ? g : 1;
}

void RxQueueManager::addInterface(interface::Interface& iface, const std::string& ifname, const IfacePolicy& policy)
{
    std::lock_guard<std::mutex> lk(mu);
    if (ifs.count(&iface)) return;

    IfState st;
    st.iface = &iface;
    st.ifname = ifname;
    st.policy = policy;
    st.fanoutGroup = assignFanoutGroup();

    st.policy.defaultQueueOpts.ifname = ifname;
    if (st.policy.defaultQueueOpts.fanoutGroup == 0)
        st.policy.defaultQueueOpts.fanoutGroup = st.fanoutGroup;

    ifs.emplace(&iface, std::move(st));
    reoptimize();
}

void RxQueueManager::removeInterface(interface::Interface& iface)
{
    std::lock_guard<std::mutex> lk(mu);
    auto it = ifs.find(&iface);
    if (it == ifs.end()) return;
    for (auto& qs : it->second.queues) stopAndDelete(qs);
    it->second.queues.clear();
    ifs.erase(it);
    reoptimize();
}

void RxQueueManager::updateInterfacePolicy(interface::Interface& iface, const IfacePolicy& policy)
{
    std::lock_guard<std::mutex> lk(mu);
    auto it = ifs.find(&iface);
    if (it == ifs.end()) return;
    auto& st = it->second;
    st.policy = policy;
    st.policy.defaultQueueOpts.ifname = st.ifname;
    if (st.policy.defaultQueueOpts.fanoutGroup == 0)
        st.policy.defaultQueueOpts.fanoutGroup = st.fanoutGroup;
    reoptimize();
}

RxQueueManager::HwQueueCaps RxQueueManager::getHwRxQueues(const std::string& ifname)
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
        caps.maxRx = (int)(ec.max_rx ? ec.max_rx : ec.max_combined);
        caps.curRx = (int)(ec.rx_count ? ec.rx_count : ec.combined_count);
        if (caps.maxRx <= 0) caps.maxRx = 1;
        if (caps.curRx <= 0) caps.curRx = 1;
    }

    ::close(s);
    return caps;
}

bool RxQueueManager::setHwRxQueues(const std::string& ifname, int num)
{
    int s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return false;

    ifreq ifr{};
    std::strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);

    ethtool_channels ec{};
    ec.cmd = ETHTOOL_SCHANNELS;
    ec.rx_count = (__u32)num;

    ifr.ifr_data = reinterpret_cast<char*>(&ec);
    bool ok = (ioctl(s, SIOCETHTOOL, &ifr) == 0);

    ::close(s);
    return ok;
}

void RxQueueManager::reoptimize()
{
    if (cores.empty()) return;

    for (auto& kv : ifs)
    {
        auto& st = kv.second;
        auto order = buildCoreOrder(st);

        int desired = computeTargetFor(st, (int)order.size());
        HwQueueCaps caps = getHwRxQueues(st.ifname);
        if (desired > caps.maxRx) desired = caps.maxRx;

        int applied = 1;
        if (setHwRxQueues(st.ifname, desired))
            applied = desired;

        st.hwRxQueues = applied;
        ensureQueueCount(st, applied, order);
    }

}

int RxQueueManager::computeTargetFor(const IfState& st, int totalCores) const
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

std::vector<int> RxQueueManager::buildCoreOrder(const IfState& st) const
{
    std::vector<int> out = cores;
    if (out.empty()) out.push_back(0);
    return out;
}

void RxQueueManager::ensureQueueCount(IfState& st, int target, const std::vector<int>& coreOrder)
{
    int cur = (int)st.queues.size();
    if (target > cur)
    {
        size_t rr = 0;
        for (int i = 0; i < target - cur; ++i)
        {
            RxQueueOpts q = st.policy.defaultQueueOpts;
            q.ifname = st.ifname;
            q.fanoutGroup = st.fanoutGroup;
            q.cpuId = coreOrder[(rr++) % coreOrder.size()];
            startOne(st, q);
        }
    }
    else if (target < cur)
    {
        for (int i = 0; i < cur - target; ++i) stopLast(st);
    }
}

void RxQueueManager::startOne(IfState& st, RxQueueOpts qopts)
{
    QueueState qs;
    qs.opts = qopts;
    qs.ingress = hardware::ingress::create(st.iface, qs.opts);
    if (!qs.ingress) throw std::runtime_error("ingress factory returned null");
    st.queues.push_back(qs);
}

void RxQueueManager::stopLast(IfState& st)
{
    if (st.queues.empty()) return;
    auto& qs = st.queues.back();
    stopAndDelete(qs);
    st.queues.pop_back();
}

void RxQueueManager::stopAndDelete(QueueState& qs)
{
    if (qs.ingress)
    {
        qs.ingress->stop();
        delete qs.ingress;
        qs.ingress = nullptr;
    }
}

void RxQueueManager::shutdown()
{
    std::lock_guard<std::mutex> lk(mu);
    for (auto& kv :  ifs)
    {
        for (auto& qs : kv.second.queues) stopAndDelete(qs);
        kv.second.queues.clear();
    }
    ifs.clear();
}

void RxQueueManager::start(interface::Interface* iface)
{
    std::lock_guard<std::mutex> lk(mu);
    auto it = ifs.find(iface);
    if (it == ifs.end()) return;
    for (auto& q : it->second.queues)
    {
        q.ingress->start();
    }
}

void RxQueueManager::stop(interface::Interface* iface)
{
    std::lock_guard<std::mutex> lk(mu);
    auto it = ifs.find(iface);
    if (it == ifs.end()) return;
    for (auto& q : it->second.queues)
    {
        q.ingress->stop();
    }
}

} // namespace qos
