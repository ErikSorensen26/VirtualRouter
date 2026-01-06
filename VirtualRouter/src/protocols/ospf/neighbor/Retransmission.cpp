// Retransmission.cpp

#include "Retransmission.h"
#include <LSDB.hpp>

namespace OSPF
{
std::vector<LsaRecordRef>::iterator Retransmission::addLsu(LsaRecordRef& ref)
{
    auto it = std::find_if(outboundLsus.begin(), outboundLsus.end(),
        [&](const LsaRecordRef& r) { return r.key == ref.key; });
    if (it == outboundLsus.end())
    {
        outboundLsus.push_back(std::move(ref));
        it = std::find_if(outboundLsus.begin(), outboundLsus.end(),
            [&](const LsaRecordRef& r) { return r.key == ref.key; });
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
        [&](const LsaRecordRef& r) { return r.key == key; }) != outboundLsus.end();
}

bool Retransmission::hasLsr(LsaKey& key)
{
    std::lock_guard<std::mutex> lock(reliableMtx);
    return std::find(outboundLsrs.begin(), outboundLsrs.end(), key) != outboundLsrs.end();
}

std::optional<LsaRecordRef> Retransmission::moveLsu(const LsaKey& key)
{
    auto it = std::find_if(outboundLsus.begin(), outboundLsus.end(),
        [&](const LsaRecordRef& r) { return r.key == key; });
    if (it == outboundLsus.end()) return std::nullopt;
    LsaRecordRef ref = std::move(*it);
    outboundLsus.erase(it);
    return ref;
}

void Retransmission::eraseLsr(LsaKey& key)
{
    std::erase_if(outboundLsrs, [&](const LsaKey& k) { return k == key; });
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
