// RetransmissionList.cpp

#include "RetransmissionList.h"
#include "ospf/OspfProcess.h"

namespace routing::ospf
{

template <typename Key, typename Record>
bool RetransmissionList<Key, Record>::add(Key& key, Record& record)
{
    auto it = outboundInfo.find(key);
    if (it != outboundInfo.end())
    {
        auto& slot = outbound[it->second.index];
        slot = std::move(record);
        return false;
    }

    outbound.emplace_back(std::move(record));
    outboundKeys.emplace_back(key);
    outboundInfo.emplace(key, OutboundInfo{outbound.size() - 1, 0});
    return true;
}

template <typename Key, typename Record>
bool RetransmissionList<Key, Record>::add(const Key& key, const Record& record)
{
    auto it = outboundInfo.find(key);
    if (it != outboundInfo.end())
    {
        auto& slot = outbound[it->second.index];
        slot = record;
        return false;
    }

    outbound.emplace_back(record);
    outboundKeys.emplace_back(key);
    outboundInfo.emplace(key, OutboundInfo{outbound.size() - 1, 0});
    return true;
}

template <typename Key, typename Record>
bool RetransmissionList<Key, Record>::has(const Key& key)
{
    return outboundInfo.contains(key);
}

template <typename Key, typename Record>
std::optional<Record> RetransmissionList<Key, Record>::get(const Key& key)
{
    auto it = outboundInfo.find(key);
    if (it == outboundInfo.end())
        return std::nullopt;
    if (it->second.index >= outbound.size())
        return std::nullopt;
    return outbound[it->second.index];
}

template <typename Key, typename Record>
const std::vector<Record>& RetransmissionList<Key, Record>::getAll() const
{
    return outbound;
}

template <typename Key, typename Record>
bool RetransmissionList<Key, Record>::erase(const Key& key)
{
    auto it = outboundInfo.find(key);
    if (it == outboundInfo.end())
        return false;

    const size_t idx = it->second.index;
    const size_t last = outbound.size() - 1;

    if (idx != last)
    {
        outbound[idx] = std::move(outbound[last]);
        outboundKeys[idx] = std::move(outboundKeys[last]);
        outboundInfo[outboundKeys[idx]].index = idx;
    }

    outbound.pop_back();
    outboundKeys.pop_back();
    outboundInfo.erase(it);

    if (cursor >= outbound.size()) cursor = 0;
    if (burstRemaining > 0) --burstRemaining;

    if (outbound.empty())
    {
        cursor = 0;
        burstRemaining = 0;
    }
    return true;
}

template <typename Key, typename Record>
void RetransmissionList<Key, Record>::clear()
{
    outbound.clear();
    outboundKeys.clear();
    outboundInfo.clear();
    cursor = 0;
    burstRemaining = 0;
}

template <typename Key, typename Record>
bool RetransmissionList<Key, Record>::getActive() const
{
    return !outbound.empty();
}

template <typename Key, typename Record>
void RetransmissionList<Key, Record>::beginRetransmitBurst()
{
    burstRemaining = static_cast<uint32_t>(outbound.size());
    if (cursor >= outbound.size()) cursor = 0;
}

template <typename Key, typename Record>
bool RetransmissionList<Key, Record>::nextInBurst(Record& recordOut)
{
    if (burstRemaining == 0 || outbound.empty())
        return false;

    if (cursor >= outbound.size())
        cursor = 0;

    recordOut = outbound[cursor];

    return true;
}

template <typename Key, typename Record>
void RetransmissionList<Key, Record>::markBurst(Key& key)
{
    if (auto it = outboundInfo.find(key); it != outboundInfo.end())
    {
        it->second.retransmissions++;
        if (it->second.retransmissions >= getMaxRetransmission())
        {
            erase(key);
            return;
        }

        cursor = (cursor + 1) % outbound.size();
        --burstRemaining;
    }
}

template <typename Key, typename Record>
bool RetransmissionList<Key, Record>::burstActive() const
{
    return burstRemaining != 0;
}

template <typename Key, typename Record>
uint8_t RetransmissionList<Key, Record>::getMaxRetransmission()
{
    static constexpr uint8_t kDefaultMaxRetransmission = 5;

    auto limit = iface.area.isDcCompatible()
        ? processCfgs.get<config::Ospf::RETRANSMISSION_DC_LIMIT>()
        : processCfgs.get<config::Ospf::RETRANSMISSION_NON_DC_LIMIT>();
    return limit.hasValue() ? limit.load() : kDefaultMaxRetransmission;
}

template class RetransmissionList<LsaKey, LsaKey>;
template class RetransmissionList<LsaKey, LsaRecordRef>;
} // namespace routing::ospf
