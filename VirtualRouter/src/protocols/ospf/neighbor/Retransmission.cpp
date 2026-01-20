// Retransmission.cpp

#include "Retransmission.h"
#include <LSDB.hpp>

namespace OSPF
{
std::vector<std::pair<FloodInfo, LsaRecordRef>>::iterator Retransmission::addLsu(LsaRecordRef& ref)
{
    auto it = std::find_if(outboundLsus.begin(), outboundLsus.end(),
        [&](const auto& r) { return r.second.key == ref.key; });
    if (it == outboundLsus.end())
    {
        outboundLsus.emplace_back(FloodInfo{.reason = FloodReason::UPDATE}, std::move(ref));
        it = std::find_if(outboundLsus.begin(), outboundLsus.end(),
            [&](const auto& r) { return r.second.key == ref.key; });
    }
    return it;
}

std::vector<LsaKey>::iterator Retransmission::addLsr(const LsaKey& key)
{
    auto it = std::find(outboundLsrs.begin(), outboundLsrs.end(), key);
    if (it == outboundLsrs.end())
    {
        outboundLsrs.emplace_back(key);
        it = std::find(outboundLsrs.begin(), outboundLsrs.end(), key);
    }
    return it;
}

bool Retransmission::hasLsu(LsaKey& key)
{
    std::lock_guard<std::mutex> lock(reliableMtx);
    return std::find_if(outboundLsus.begin(), outboundLsus.end(),
        [&](const auto& r) { return r.second.key == key; }) != outboundLsus.end();
}

bool Retransmission::hasLsr(LsaKey& key)
{
    std::lock_guard<std::mutex> lock(reliableMtx);
    return std::find(outboundLsrs.begin(), outboundLsrs.end(), key) != outboundLsrs.end();
}

std::optional<LsaRecordRef> Retransmission::getLsu(const LsaKey& key)
{
    auto it = std::find_if(outboundLsus.begin(), outboundLsus.end(),
        [&](const auto& r) { return r.second.key == key; });
    if (it == outboundLsus.end()) return std::nullopt;
    return it->second;
}

void Retransmission::eraseLsu(LsaKey& key)
{
    std::erase_if(outboundLsus, [&](const auto& k) { return k.second.key == key; });
}

void Retransmission::eraseLsr(LsaKey& key)
{
    std::erase_if(outboundLsrs, [&](const LsaKey& k) { return k == key; });
}

void Retransmission::clearLsu()
{
    std::lock_guard<std::mutex> lock(reliableMtx);
    outboundLsus.clear();
}

void Retransmission::clearLsr()
{
    std::lock_guard<std::mutex> lock(reliableMtx);
    outboundLsrs.clear();
}

bool Retransmission::getLsuActive()
{
    return !outboundLsus.empty();
}

bool Retransmission::getLsrActive()
{
    return !outboundLsrs.empty();
}
}
