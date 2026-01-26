// OspfFlagManager

#ifndef OSPF_FLAG_MANAGER_H
#define OSPF_FLAG_MANAGER_H

#include <atomic>

namespace OSPF
{
class OspfArea;
class OspfInterface;

namespace FlagManager
{
static constexpr uint8_t FLAGS_BASE = 0;
static constexpr uint8_t OPTIONS_BASE = 8;

static void setBit(std::atomic<uint32_t>& flags, uint8_t bit, bool val)
{
    const uint32_t mask = (static_cast<uint32_t>(1u) << bit);
    if (val)
        flags.fetch_or(mask, std::memory_order_relaxed);
    else
        flags.fetch_and(~mask, std::memory_order_relaxed);
}

static void setBit(uint32_t& flags, uint8_t bit, bool val)
{
    const uint32_t mask = (static_cast<uint32_t>(1u) << bit);
    if (val)
        flags |= mask;
    else
        flags |= ~mask;
}

static bool testBit(uint32_t fgs, uint8_t bit)
{
    return (fgs & (static_cast<uint32_t>(1u) << bit)) != 0;
}

static void setFlag(std::atomic<uint32_t>& flags, uint8_t flagBit, bool val)
{
    setBit(flags, static_cast<uint8_t>(FLAGS_BASE + flagBit), val);
}

static void setFlag(uint32_t& flags, uint8_t flagBit, bool val)
{
    setBit(flags, static_cast<uint8_t>(FLAGS_BASE + flagBit), val);
}

static bool testFlag(uint32_t flags, uint8_t flagBit)
{
    return testBit(flags, static_cast<uint8_t>(FLAGS_BASE + flagBit));
}

static void setOption(std::atomic<uint32_t>& flags, uint8_t optionBit, bool val)
{
    setBit(flags, static_cast<uint8_t>(OPTIONS_BASE + optionBit), val);
}

static void setOption(uint32_t& flags, uint8_t optionBit, bool val)
{
    setBit(flags, static_cast<uint8_t>(OPTIONS_BASE + optionBit), val);
}

static bool testOption(uint32_t flags, uint8_t optionBit)
{
    return testBit(flags, static_cast<uint8_t>(OPTIONS_BASE + optionBit));
}
}

class AreaFlagManager
{
    enum class Options : uint8_t
    {
        EXTERNAL_ROUTING = 1,
        NSSA = 3,
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
        ADDRESS_FAMILY_SUPPORT = 8,
        L_BIT = 9,
        AUTH_TRAILER = 10
    };

    enum class Flags : uint8_t
    {
        ABR = 0,
        ASBR = 1
    };

public:
    AreaFlagManager(bool v3)
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
        { FlagManager::setOption(flags, static_cast<uint8_t>(Options::EXTERNAL_ROUTING), val); }
    void setNssa(bool val)
        { FlagManager::setOption(flags, static_cast<uint8_t>(Options::NSSA), val); }
    static void setExternalRouting(uint32_t fgs, bool val)
        { FlagManager::setOption(fgs, static_cast<uint8_t>(Options::EXTERNAL_ROUTING), val); }
    static void setNssa(uint32_t fgs, bool val)
        { FlagManager::setOption(fgs, static_cast<uint8_t>(Options::NSSA), val); }

    void setMultiTopology(bool val)
        { if (!isV3) FlagManager::setOption(flags, static_cast<uint8_t>(V2Option::MULTI_TOPOLOGY), val); }
    void setExternalAttribute(bool val)
        { if (!isV3) FlagManager::setOption(flags, static_cast<uint8_t>(V2Option::EXTERNAL_ATTR), val); }
    void setOpaque(bool val)
        { if (!isV3) FlagManager::setOption(flags, static_cast<uint8_t>(V2Option::OPAQUE), val); }
    static void setMultiTopology(uint32_t fgs, bool val)
        { FlagManager::setOption(fgs, static_cast<uint8_t>(V2Option::MULTI_TOPOLOGY), val); }
    static void setExternalAttribute(uint32_t fgs, bool val)
        { FlagManager::setOption(fgs, static_cast<uint8_t>(V2Option::EXTERNAL_ATTR), val); }
    static void setOpaque(uint32_t fgs, bool val)
        { FlagManager::setOption(fgs, static_cast<uint8_t>(V2Option::OPAQUE), val); }

    void setV6(bool val)
        { if (isV3) FlagManager::setOption(flags, static_cast<uint8_t>(V3Option::V6), val); }
    void setRouterBit(bool val)
        { if (isV3) FlagManager::setOption(flags, static_cast<uint8_t>(V3Option::ROUTER_BIT), val); }
    void setAddressFamilySupport(bool val)
        { if (isV3) FlagManager::setOption(flags, static_cast<uint8_t>(V3Option::ADDRESS_FAMILY_SUPPORT), val); }
    void setLBit(bool val)
        { if (isV3) FlagManager::setOption(flags, static_cast<uint8_t>(V3Option::L_BIT), val); }
    void setAuthTrailer(bool val)
        { if (isV3) FlagManager::setOption(flags, static_cast<uint8_t>(V3Option::AUTH_TRAILER), val); }
    static void setV6(uint32_t fgs, bool val)
        { FlagManager::setOption(fgs, static_cast<uint8_t>(V3Option::V6), val); }
    static void setRouterBit(uint32_t fgs, bool val)
        { FlagManager::setOption(fgs, static_cast<uint8_t>(V3Option::ROUTER_BIT), val); }
    static void setAddressFamilySupport(uint32_t fgs, bool val)
        { FlagManager::setOption(fgs, static_cast<uint8_t>(V3Option::ADDRESS_FAMILY_SUPPORT), val); }
    static void setLBit(uint32_t fgs, bool val)
        { FlagManager::setOption(fgs, static_cast<uint8_t>(V3Option::L_BIT), val); }
    static void setAuthTrailer(uint32_t fgs, bool val)
        { FlagManager::setOption(fgs, static_cast<uint8_t>(V3Option::AUTH_TRAILER), val); }

    bool getExternalRouting()
        { return FlagManager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(Options::EXTERNAL_ROUTING)); }
    bool getNssa()
        { return FlagManager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(Options::NSSA)); }
    static bool getExternalRouting(uint32_t fgs)
        { return FlagManager::testOption(fgs, static_cast<uint8_t>(Options::EXTERNAL_ROUTING)); }
    static bool getNssa(uint32_t fgs)
        { return FlagManager::testOption(fgs, static_cast<uint8_t>(Options::NSSA)); }

    bool getMultiTopology()
        { return FlagManager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(V2Option::MULTI_TOPOLOGY)); }
    bool getExternalAttribute()
        { return FlagManager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(V2Option::EXTERNAL_ATTR)); }
    bool getOpaque()
        { return FlagManager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(V2Option::OPAQUE)); }
    static bool getMultiTopology(uint32_t fgs)
        { return FlagManager::testOption(fgs, static_cast<uint8_t>(V2Option::MULTI_TOPOLOGY)); }
    static bool getExternalAttribute(uint32_t fgs)
        { return FlagManager::testOption(fgs, static_cast<uint8_t>(V2Option::EXTERNAL_ATTR)); }
    static bool getOpaque(uint32_t fgs)
        { return FlagManager::testOption(fgs, static_cast<uint8_t>(V2Option::OPAQUE)); }

    bool getV6()
        { return FlagManager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(V3Option::V6)); }
    bool getRouterBit()
        { return FlagManager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(V3Option::ROUTER_BIT)); }
    bool getAddressFamilySupport()
        { return FlagManager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(V3Option::ADDRESS_FAMILY_SUPPORT)); }
    bool getLBit()
        { return FlagManager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(V3Option::L_BIT)); }
    bool getAuthTrailer()
        { return FlagManager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(V3Option::AUTH_TRAILER)); }
    static bool getV6(uint32_t fgs)
        { return FlagManager::testOption(fgs, static_cast<uint8_t>(V3Option::V6)); }
    static  bool getRouterBit(uint32_t fgs)
        { return FlagManager::testOption(fgs, static_cast<uint8_t>(V3Option::ROUTER_BIT)); }
    static bool getAddressFamilySupport(uint32_t fgs)
        { return FlagManager::testOption(fgs, static_cast<uint8_t>(V3Option::ADDRESS_FAMILY_SUPPORT)); }
    static bool getLBit(uint32_t fgs)
        { return FlagManager::testOption(fgs, static_cast<uint8_t>(V3Option::L_BIT)); }
    static bool getAuthTrailer(uint32_t fgs)
        { return FlagManager::testOption(fgs, static_cast<uint8_t>(V3Option::AUTH_TRAILER)); }

    void setAbr(bool val)
        { FlagManager::setFlag(flags, static_cast<uint8_t>(Flags::ABR), val); }
    void setAsbr(bool val)
        { FlagManager::setFlag(flags, static_cast<uint8_t>(Flags::ASBR), val); }
    static void setAbr(uint32_t fgs, bool val)
        { FlagManager::setFlag(fgs, static_cast<uint8_t>(Flags::ABR), val); }
    static void setAsbr(uint32_t fgs, bool val)
        { FlagManager::setFlag(fgs, static_cast<uint8_t>(Flags::ASBR), val); }

    bool getAbr()
        { return FlagManager::testFlag(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(Flags::ABR)); }
    bool getAsbr()
        { return FlagManager::testFlag(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(Flags::ASBR)); }
    static bool getAbr(uint32_t fgs)
        { return FlagManager::testFlag(fgs, static_cast<uint8_t>(Flags::ABR)); }
    static bool getAsbr(uint32_t fgs)
        { return FlagManager::testFlag(fgs, static_cast<uint8_t>(Flags::ASBR)); }

    const bool isV3;

private:
    std::atomic<uint32_t> flags{0};
};

class InterfaceFlagManager
{
    enum class Options : uint8_t
    {
        MULTICAST = 2,
        DEMAND_CIRCUITS = 5
    };

    enum class Flags : uint8_t
    {
        V_LINK = 2,
        WILDCARD = 3
    };

public:
    InterfaceFlagManager(OspfInterface& iface);

    uint32_t getFlags();

    void setMulticast(bool val)
        { FlagManager::setOption(flags, static_cast<uint8_t>(Options::MULTICAST), val); }
    void setDemandCircuits(bool val)
        { FlagManager::setOption(flags, static_cast<uint8_t>(Options::DEMAND_CIRCUITS), val); }
    static void setMulticast(uint32_t fgs, bool val)
        { FlagManager::setOption(fgs, static_cast<uint8_t>(Options::MULTICAST), val); }
    static void setDemandCircuits(uint32_t fgs, bool val)
        { FlagManager::setOption(fgs, static_cast<uint8_t>(Options::DEMAND_CIRCUITS), val); }

    bool getMulticast()
        { return FlagManager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(Options::MULTICAST)); }
    bool getDemandCircuits()
        { return FlagManager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(Options::DEMAND_CIRCUITS)); }
    static bool getMulticast(uint32_t fgs)
        { return FlagManager::testOption(fgs, static_cast<uint8_t>(Options::MULTICAST)); }
    static bool getDemandCircuits(uint32_t fgs)
        { return FlagManager::testOption(fgs, static_cast<uint8_t>(Options::DEMAND_CIRCUITS)); }

    void setVLink(bool val)
        { FlagManager::setFlag(flags, static_cast<uint8_t>(Flags::V_LINK), val); }
    void setWildcard(bool val)
        { FlagManager::setFlag(flags, static_cast<uint8_t>(Flags::WILDCARD), val); }
    static void setVLink(uint32_t fgs, bool val)
        { FlagManager::setFlag(fgs, static_cast<uint8_t>(Flags::V_LINK), val); }
    static void setWildcard(uint32_t fgs, bool val)
        { FlagManager::setFlag(fgs, static_cast<uint8_t>(Flags::WILDCARD), val); }

    bool getVLink()
        { return FlagManager::testFlag(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(Flags::V_LINK)); }
    bool getWildcard()
        { return FlagManager::testFlag(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(Flags::WILDCARD)); }
    static bool getVLink(uint32_t fgs)
        { return FlagManager::testFlag(fgs, static_cast<uint8_t>(Flags::V_LINK)); }
    static bool getWildcard(uint32_t fgs)
        { return FlagManager::testFlag(fgs, static_cast<uint8_t>(Flags::WILDCARD)); }

private:
    std::atomic<OspfArea*>& area;
    std::atomic<uint32_t> flags;
};
}

#endif
