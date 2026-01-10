// OspfFlagManager

#ifndef OSPF_FLAG_MANAGER_HPP
#define OSPF_FLAG_MANAGER_HPP

#include <atomic>
#include <Ospfv2LSAHeader.hpp>

namespace OSPF
{
class OspfFlagManager
{
    enum class Options
    {
        EXTERNAL_ROUTING = 1,
        MULTICAST = 2,
        NSSA = 3,
        DEMAND_CIRCUITS = 5
    };

    enum class V2Option : uint8_t
    {
        MULTI_TOPOLOGY = 0,
        EXTERNAL_ATTR = 4,
        OPAQUE = 6
    };

    enum class V3Option : uint8_t
    {
        V6 = 0,
        ROUTER_BIT = 4,
        ADDRESS_FAMILY_SUPPORT = 6,
        L_BIT = 7,
        ATTACHED = 8
    };

    enum class Flags : uint8_t
    {
        ABR = 0,
        ASBR = 1,
        V_LINK = 2,
        WILDCARD = 3
    };

public:
    OspfFlagManager(bool v3)
        : isV3(v3)
    {
        setExternalRouting(true);
        setOpaque(true);
    }

    uint32_t getFlags()
    {
        return flags.load(std::memory_order_relaxed);
    }

    void setExternalRouting(bool val)
        { setBit(static_cast<uint8_t>(Options::EXTERNAL_ROUTING), val); }
    void setMulticast(bool val)
        { setBit(static_cast<uint8_t>(Options::MULTICAST), val); }
    void setNssa(bool val)
        { setBit(static_cast<uint8_t>(Options::NSSA), val); }
    void setDemandCircuits(bool val)
        { setBit(static_cast<uint8_t>(Options::DEMAND_CIRCUITS), val); }

    void setMultiTopology(bool val)
        { if (!isV3) setBit(static_cast<uint8_t>(V2Option::MULTI_TOPOLOGY), val); }
    void setExternalAttribute(bool val)
        { if (!isV3) setBit(static_cast<uint8_t>(V2Option::EXTERNAL_ATTR), val); }
    void setOpaque(bool val)
        { if (!isV3) setBit(static_cast<uint8_t>(V2Option::OPAQUE), val); }

    void setV6(bool val)
        { if (isV3) setBit(static_cast<uint8_t>(V3Option::V6), val); }
    void setRouterBit(bool val)
        { if (isV3) setBit(static_cast<uint8_t>(V3Option::ROUTER_BIT), val); }
    void setAddressFamilySupport(bool val)
        { if (isV3) setBit(static_cast<uint8_t>(V3Option::ADDRESS_FAMILY_SUPPORT), val); }
    void setLBit(bool val)
        { if (isV3) setBit(static_cast<uint8_t>(V3Option::L_BIT), val); }
    void setAttached(bool val)
        { if (isV3) setBit(static_cast<uint8_t>(V3Option::ATTACHED), val); }

    bool getExternalRouting()
        { return testBit(static_cast<uint8_t>(Options::EXTERNAL_ROUTING)); }
    bool getMulticast()
        { return testBit(static_cast<uint8_t>(Options::MULTICAST)); }
    bool getNssa()
        { return testBit(static_cast<uint8_t>(Options::NSSA)); }
    bool getDemandCircuits()
        { return testBit(static_cast<uint8_t>(Options::DEMAND_CIRCUITS)); }

    bool getMultiTopology()
        { return testBit(static_cast<uint8_t>(V2Option::MULTI_TOPOLOGY)); }
    bool getExternalAttribute()
        { return testBit(static_cast<uint8_t>(V2Option::EXTERNAL_ATTR)); }
    bool getOpaque()
        { return testBit(static_cast<uint8_t>(V2Option::OPAQUE)); }

    bool getV6()
        { return testBit(static_cast<uint8_t>(V3Option::V6)); }
    bool getRouterBit()
        { return testBit(static_cast<uint8_t>(V3Option::ROUTER_BIT)); }
    bool getAddressFamilySupport()
        { return testBit(static_cast<uint8_t>(V3Option::ADDRESS_FAMILY_SUPPORT)); }
    bool getLBit()
        { return testBit(static_cast<uint8_t>(V3Option::L_BIT)); }
    bool getAttached()
        { return testBit(static_cast<uint8_t>(V3Option::ATTACHED)); }

    const bool isV3;

private:
    void setBit(uint8_t bit, bool val)
    {
        uint8_t mask = static_cast<uint8_t>(1u << bit);
        if (val)
            flags.fetch_or(mask, std::memory_order_relaxed);
        else
            flags.fetch_and(static_cast<uint8_t>(~mask), std::memory_order_relaxed);
    }

    bool testBit(uint8_t bit)
    {
        return flags.load(std::memory_order_relaxed) &
            static_cast<uint32_t>(1u << bit);
    }

private:
    std::atomic<uint32_t> flags;
};
}

#endif
