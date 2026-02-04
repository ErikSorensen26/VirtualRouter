// RetransmissionList.hpp

#ifndef RETRNASMISSION_LIST_HPP
#define RETRNASMISSION_LIST_HPP

#include <vector>
#include <optional>
#include <OspfPacket.hpp>
#include <FloodTypes.hpp>

namespace OSPF
{
template <typename Key, typename Record>
class RetransmissionList
{
public:
    uint32_t retransmitTimerId = 0;
    uint32_t pacingTimerId = 0;

    // Reliability
    bool add(Key& key, Record& record)
    {
        auto it = outboundIndex.find(key);
        if (it != outboundIndex.end())
        {
            auto& slot = outbound[it->second];
            slot = std::move(record);
            return false;
        }

        outboundIndex.emplace(key, outbound.size());
        outbound.emplace_back(std::move(record));
        return true;
    }

    bool add(const Key& key, const Record& record)
    {
        auto it = outboundIndex.find(key);
        if (it != outboundIndex.end())
        {
            auto& slot = outbound[it->second];
            slot = record;
            return false;
        }

        outboundIndex.emplace(key, outbound.size());
        outbound.emplace_back(record);
        return true;
    }

    bool has(const Key& key)
    {
        return outboundIndex.contains(key);
    }

    std::optional<Record> get(const Key& key)
    {
        auto it = outboundIndex.find(key);
        if (it == outboundIndex.end())
            return std::nullopt;
        return outbound[it->second].second;
    }

    const std::vector<Record>& getAll() const
    {
        return outbound;
    }

    bool erase(const Key& key)
    {
        auto it = outboundIndex.find(key);
        if (it == outboundIndex.end())
            return false;

        const size_t idx = it->second;
        const size_t last = outbound.size() - 1;

        if (idx != last)
        {
            outbound[idx] = outbound[last];
            outboundIndex[outbound[idx]] = idx;
        }

        outbound.pop_back();
        outboundIndex.erase(it);

        if (cursor >= outbound.size()) cursor = 0;
        if (burstRemaining > 0) --burstRemaining;

        if (outbound.empty())
        {
            cursor = 0;
            burstRemaining = 0;
        }
        return true;
    }

    void clear()
    {
        outbound.clear();
        outboundIndex.clear();
        cursor = 0;
        burstRemaining = 0;
    }

    bool getActive() const
    {
        return !outbound.empty();
    }

    void beginRetransmitBurst()
    {
        burstRemaining = static_cast<uint32_t>(outbound.size());
        if (cursor >= outbound.size()) cursor = 0;
    }

    bool nextInBurst(Record& recordOut)
    {
        if (burstRemaining == 0 || outbound.empty())
            return false;

        if (cursor >= outbound.size())
            cursor = 0;

        recordOut = outbound[cursor];

        return true;
    }

    bool markBurst()
    {
        cursor = (cursor + 1) % outbound.size();
        --burstRemaining;
    }

    bool burstActive() const
    {
        return burstRemaining != 0;
    }

private:
    // Reliability
    size_t cursor = 0;
    uint32_t burstRemaining = 0;

    std::unordered_map<Key, size_t> outboundIndex;
    std::vector<Record> outbound;
};
}

#endif // RETRNASMISSION_LIST_HPP
