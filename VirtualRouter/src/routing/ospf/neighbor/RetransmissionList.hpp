// RetransmissionList.hpp

#ifndef RETRNASMISSION_LIST_HPP
#define RETRNASMISSION_LIST_HPP

#include <vector>
#include <unordered_map>
#include <optional>

#include "ospf/OspfProcess.h"
#include "ospf/interface/OspfInterface.h"

namespace routing::ospf
{
template <typename Key, typename Record>
class RetransmissionList
{
public:
    RetransmissionList(OspfProcess& process, OspfInterface& iface)
        : process(process), iface(iface)
    {}

    uint32_t retransmitTimerId = 0;
    uint32_t pacingTimerId = 0;

    // Reliability
    bool add(Key& key, Record& record)
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
        outboundInfo.emplace(key, OutboundInfo{outbound.size(), 0});
        return true;
    }

    bool add(const Key& key, const Record& record)
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
        outboundInfo.emplace(key, OutboundInfo{outbound.size(), 0});
        return true;
    }

    bool has(const Key& key)
    {
        return outboundInfo.contains(key);
    }

    std::optional<Record> get(const Key& key)
    {
        auto it = outboundInfo.find(key);
        if (it == outboundInfo.end())
            return std::nullopt;
        return outbound[it->second.index];
    }

    const std::vector<Record>& getAll() const
    {
        return outbound;
    }

    bool erase(const Key& key)
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

    void clear()
    {
        outbound.clear();
        outboundKeys.clear();
        outboundInfo.clear();
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

    void markBurst(Key& key)
    {
        if (auto it = outboundInfo.find(key); it != outboundInfo.end())
        {
            cursor = (cursor + 1) % outbound.size();
            --burstRemaining;

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

    bool burstActive() const
    {
        return burstRemaining != 0;
    }

private:
    uint8_t getMaxRetransmission()
    {
        return iface.getConfigs().get<config::OspfInterface::DEMAND_CIRCUIT>().load()
            ? process.getConfigs().get<config::Ospf::RETRANSMISSION_DC_LIMIT>().load()
            : process.getConfigs().get<config::Ospf::RETRANSMISSION_NON_DC_LIMIT>().load();
    }

    // Reliability
    size_t cursor = 0;
    uint32_t burstRemaining = 0;
    
    struct OutboundInfo
    {
        size_t index;
        uint8_t retransmissions{0};
    };

    std::unordered_map<Key,  OutboundInfo> outboundInfo;
    std::vector<Key> outboundKeys;
    std::vector<Record> outbound;

    OspfProcess& process;
    OspfInterface& iface;
};
} // namespace routing

#endif // RETRNASMISSION_LIST_HPP

