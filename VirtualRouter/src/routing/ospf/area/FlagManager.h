/**
 * @file FlagManager.h
 * @brief Per-area and per-interface OSPF options/flags bitfield managers.
 */

#ifndef OSPF_FLAG_MANAGER_H
#define OSPF_FLAG_MANAGER_H

#include <atomic>

namespace routing::ospf
{
class Area;
class OspfInterface;

/// @brief Internal bitfield primitives used by AreaFlagManager and InterfaceFlagManager.
namespace flagmanager
{
static constexpr uint8_t FLAGS_BASE = 16;   ///< Bit offset for the flags region.
static constexpr uint8_t OPTIONS_BASE = 0; ///< Bit offset for the options region.

/**
 * @brief Atomically set or clear a single bit in a flag word.
 * @param flags Target atomic flag word.
 * @param bit   Zero-based bit index.
 * @param val   @c true to set, @c false to clear.
 */
static void setBit(std::atomic<uint32_t>& flags, uint8_t bit, bool val)
{
    const uint32_t mask = (static_cast<uint32_t>(1u) << bit);
    if (val)
        flags.fetch_or(mask, std::memory_order_relaxed);
    else
        flags.fetch_and(~mask, std::memory_order_relaxed);
}

/**
 * @brief Set or clear a single bit in a non-atomic flag word.
 * @param flags Target flag word.
 * @param bit   Zero-based bit index.
 * @param val   @c true to set, @c false to clear.
 */
static void setBit(uint32_t& flags, uint8_t bit, bool val)
{
    const uint32_t mask = (static_cast<uint32_t>(1u) << bit);
    if (val)
        flags |= mask;
    else
        flags &= ~mask;
}

/**
 * @brief Test whether a single bit is set.
 * @param fgs Flag word snapshot.
 * @param bit Zero-based bit index.
 * @return @c true if the bit is set.
 */
static bool testBit(uint32_t fgs, uint8_t bit)
{
    return (fgs & (static_cast<uint32_t>(1u) << bit)) != 0;
}

/**
 * @brief Atomically set or clear a flag-region bit.
 * @param flags   Target atomic flag word.
 * @param flagBit Bit index within the flags region.
 * @param val     @c true to set, @c false to clear.
 */
static void setFlag(std::atomic<uint32_t>& flags, uint8_t flagBit, bool val)
{
    setBit(flags, static_cast<uint8_t>(FLAGS_BASE + flagBit), val);
}

/**
 * @brief Set or clear a flag-region bit in a non-atomic word.
 * @param flags   Target flag word.
 * @param flagBit Bit index within the flags region.
 * @param val     @c true to set, @c false to clear.
 */
static void setFlag(uint32_t& flags, uint8_t flagBit, bool val)
{
    setBit(flags, static_cast<uint8_t>(FLAGS_BASE + flagBit), val);
}

/**
 * @brief Test whether a flag-region bit is set.
 * @param flags   Flag word snapshot.
 * @param flagBit Bit index within the flags region.
 * @return @c true if the bit is set.
 */
static bool testFlag(uint32_t flags, uint8_t flagBit)
{
    return testBit(flags, static_cast<uint8_t>(FLAGS_BASE + flagBit));
}

/**
 * @brief Atomically set or clear an options-region bit.
 * @param flags     Target atomic flag word.
 * @param optionBit Bit index within the options region.
 * @param val       @c true to set, @c false to clear.
 */
static void setOption(std::atomic<uint32_t>& flags, uint8_t optionBit, bool val)
{
    setBit(flags, static_cast<uint8_t>(OPTIONS_BASE + optionBit), val);
}

/**
 * @brief Set or clear an options-region bit in a non-atomic word.
 * @param flags     Target flag word.
 * @param optionBit Bit index within the options region.
 * @param val       @c true to set, @c false to clear.
 */
static void setOption(uint32_t& flags, uint8_t optionBit, bool val)
{
    setBit(flags, static_cast<uint8_t>(OPTIONS_BASE + optionBit), val);
}

/**
 * @brief Test whether an options-region bit is set.
 * @param flags     Flag word snapshot.
 * @param optionBit Bit index within the options region.
 * @return @c true if the bit is set.
 */
static bool testOption(uint32_t flags, uint8_t optionBit)
{
    return testBit(flags, static_cast<uint8_t>(OPTIONS_BASE + optionBit));
}
}

/**
 * @brief Manages the packed OSPF options and router-role flags for one area.
 *
 * Encodes area-scope capability bits (E-bit, N-bit, opaque, MT, OSPFv3
 * options) and the ABR/ASBR role flags into a single atomic @c uint32_t.
 * The lower 8 bits are flag bits (ABR, ASBR); bits 8 and above are option bits.
 *
 * Provides both instance methods (operating on the internal atomic word) and
 * static helpers (operating on a caller-supplied @c uint32_t snapshot) for
 * use in lock-free read paths.
 *
 * @ingroup OSPF_AREA
 */
class AreaFlagManager
{
    /// @brief Common area options (E-bit, N-bit).
    enum class Options : uint8_t
    {
        EXTERNAL_ROUTING = 1, ///< E-bit: area accepts AS-external LSAs.
        NSSA = 3,             ///< N-bit: area is an NSSA.
    };

    /// @brief OSPFv2-only option bits.
    enum class V2Option : uint8_t
    {
        MULTI_TOPOLOGY = 0, ///< MT-bit: multi-topology routing support.
        EXTERNAL_ATTR = 4,  ///< EA-bit: external-attributes LSA support.
        OPAQUE = 6          ///< O-bit: opaque LSA support (RFC 5250).
    };

    /// @brief OSPFv3-only option bits.
    enum class V3Option : uint8_t
    {
        V6 = 0,                    ///< V6-bit: process IPv6 routing only.
        ROUTER_BIT = 4,            ///< R-bit: router is active.
        ADDRESS_FAMILY_SUPPORT = 8,///< AF-bit: address-family support.
        L_BIT = 9,                 ///< L-bit: LLS data present.
        AUTH_TRAILER = 10          ///< AT-bit: authentication trailer.
    };

    /// @brief Router role flags encoded in the low byte.
    enum class Flags : uint8_t
    {
        ABR = 0,  ///< This router is an Area Border Router.
        ASBR = 1  ///< This router is an AS Boundary Router.
    };

public:
    /**
     * @brief Construct an AreaFlagManager with sane defaults.
     *
     * Enables external routing and (for OSPFv2) the opaque LSA option
     * immediately upon construction.
     *
     * @param v3 @c true when operating in OSPFv3 mode.
     */
    AreaFlagManager(bool v3)
        : isV3(v3)
    {
        setExternalRouting(true);
        setOpaque(true);
    }

    /** @brief Return a snapshot of the packed flags/options word. */
    uint32_t getFlags()
    {
        return flags.load(std::memory_order_relaxed);
    }

    void setExternalRouting(bool val)
        { flagmanager::setOption(flags, static_cast<uint8_t>(Options::EXTERNAL_ROUTING), val); }
    void setNssa(bool val)
        { flagmanager::setOption(flags, static_cast<uint8_t>(Options::NSSA), val); }
    static void setExternalRouting(uint32_t& fgs, bool val)
        { flagmanager::setOption(fgs, static_cast<uint8_t>(Options::EXTERNAL_ROUTING), val); }
    static void setNssa(uint32_t& fgs, bool val)
        { flagmanager::setOption(fgs, static_cast<uint8_t>(Options::NSSA), val); }

    void setMultiTopology(bool val)
        { if (!isV3) flagmanager::setOption(flags, static_cast<uint8_t>(V2Option::MULTI_TOPOLOGY), val); }
    void setExternalAttribute(bool val)
        { if (!isV3) flagmanager::setOption(flags, static_cast<uint8_t>(V2Option::EXTERNAL_ATTR), val); }
    void setOpaque(bool val)
        { if (!isV3) flagmanager::setOption(flags, static_cast<uint8_t>(V2Option::OPAQUE), val); }
    static void setMultiTopology(uint32_t& fgs, bool val)
        { flagmanager::setOption(fgs, static_cast<uint8_t>(V2Option::MULTI_TOPOLOGY), val); }
    static void setExternalAttribute(uint32_t& fgs, bool val)
        { flagmanager::setOption(fgs, static_cast<uint8_t>(V2Option::EXTERNAL_ATTR), val); }
    static void setOpaque(uint32_t& fgs, bool val)
        { flagmanager::setOption(fgs, static_cast<uint8_t>(V2Option::OPAQUE), val); }

    void setV6(bool val)
        { if (isV3) flagmanager::setOption(flags, static_cast<uint8_t>(V3Option::V6), val); }
    void setRouterBit(bool val)
        { if (isV3) flagmanager::setOption(flags, static_cast<uint8_t>(V3Option::ROUTER_BIT), val); }
    void setAddressFamilySupport(bool val)
        { if (isV3) flagmanager::setOption(flags, static_cast<uint8_t>(V3Option::ADDRESS_FAMILY_SUPPORT), val); }
    void setLBit(bool val)
        { if (isV3) flagmanager::setOption(flags, static_cast<uint8_t>(V3Option::L_BIT), val); }
    void setAuthTrailer(bool val)
        { if (isV3) flagmanager::setOption(flags, static_cast<uint8_t>(V3Option::AUTH_TRAILER), val); }
    static void setV6(uint32_t& fgs, bool val)
        { flagmanager::setOption(fgs, static_cast<uint8_t>(V3Option::V6), val); }
    static void setRouterBit(uint32_t& fgs, bool val)
        { flagmanager::setOption(fgs, static_cast<uint8_t>(V3Option::ROUTER_BIT), val); }
    static void setAddressFamilySupport(uint32_t& fgs, bool val)
        { flagmanager::setOption(fgs, static_cast<uint8_t>(V3Option::ADDRESS_FAMILY_SUPPORT), val); }
    static void setLBit(uint32_t& fgs, bool val)
        { flagmanager::setOption(fgs, static_cast<uint8_t>(V3Option::L_BIT), val); }
    static void setAuthTrailer(uint32_t& fgs, bool val)
        { flagmanager::setOption(fgs, static_cast<uint8_t>(V3Option::AUTH_TRAILER), val); }

    bool getExternalRouting()
        { return flagmanager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(Options::EXTERNAL_ROUTING)); }
    bool getNssa()
        { return flagmanager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(Options::NSSA)); }
    static bool getExternalRouting(uint32_t fgs)
        { return flagmanager::testOption(fgs, static_cast<uint8_t>(Options::EXTERNAL_ROUTING)); }
    static bool getNssa(uint32_t fgs)
        { return flagmanager::testOption(fgs, static_cast<uint8_t>(Options::NSSA)); }

    bool getMultiTopology()
        { return flagmanager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(V2Option::MULTI_TOPOLOGY)); }
    bool getExternalAttribute()
        { return flagmanager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(V2Option::EXTERNAL_ATTR)); }
    bool getOpaque()
        { return flagmanager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(V2Option::OPAQUE)); }
    static bool getMultiTopology(uint32_t fgs)
        { return flagmanager::testOption(fgs, static_cast<uint8_t>(V2Option::MULTI_TOPOLOGY)); }
    static bool getExternalAttribute(uint32_t fgs)
        { return flagmanager::testOption(fgs, static_cast<uint8_t>(V2Option::EXTERNAL_ATTR)); }
    static bool getOpaque(uint32_t fgs)
        { return flagmanager::testOption(fgs, static_cast<uint8_t>(V2Option::OPAQUE)); }

    bool getV6()
        { return flagmanager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(V3Option::V6)); }
    bool getRouterBit()
        { return flagmanager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(V3Option::ROUTER_BIT)); }
    bool getAddressFamilySupport()
        { return flagmanager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(V3Option::ADDRESS_FAMILY_SUPPORT)); }
    bool getLBit()
        { return flagmanager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(V3Option::L_BIT)); }
    bool getAuthTrailer()
        { return flagmanager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(V3Option::AUTH_TRAILER)); }
    static bool getV6(uint32_t fgs)
        { return flagmanager::testOption(fgs, static_cast<uint8_t>(V3Option::V6)); }
    static  bool getRouterBit(uint32_t fgs)
        { return flagmanager::testOption(fgs, static_cast<uint8_t>(V3Option::ROUTER_BIT)); }
    static bool getAddressFamilySupport(uint32_t fgs)
        { return flagmanager::testOption(fgs, static_cast<uint8_t>(V3Option::ADDRESS_FAMILY_SUPPORT)); }
    static bool getLBit(uint32_t fgs)
        { return flagmanager::testOption(fgs, static_cast<uint8_t>(V3Option::L_BIT)); }
    static bool getAuthTrailer(uint32_t fgs)
        { return flagmanager::testOption(fgs, static_cast<uint8_t>(V3Option::AUTH_TRAILER)); }

    void setAbr(bool val)
        { flagmanager::setFlag(flags, static_cast<uint8_t>(Flags::ABR), val); }
    void setAsbr(bool val)
        { flagmanager::setFlag(flags, static_cast<uint8_t>(Flags::ASBR), val); }
    static void setAbr(uint32_t& fgs, bool val)
        { flagmanager::setFlag(fgs, static_cast<uint8_t>(Flags::ABR), val); }
    static void setAsbr(uint32_t& fgs, bool val)
        { flagmanager::setFlag(fgs, static_cast<uint8_t>(Flags::ASBR), val); }

    bool getAbr()
        { return flagmanager::testFlag(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(Flags::ABR)); }
    bool getAsbr()
        { return flagmanager::testFlag(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(Flags::ASBR)); }
    static bool getAbr(uint32_t fgs)
        { return flagmanager::testFlag(fgs, static_cast<uint8_t>(Flags::ABR)); }
    static bool getAsbr(uint32_t fgs)
        { return flagmanager::testFlag(fgs, static_cast<uint8_t>(Flags::ASBR)); }

    const bool isV3; ///< @c true when this area runs OSPFv3.

private:
    std::atomic<uint32_t> flags{0}; ///< Packed options and role flags.
};

/**
 * @brief Manages the packed OSPF options and interface-type flags for one
 *        OSPF interface.
 *
 * Encodes interface-scope capability bits (multicast, demand-circuit,
 * OSPFv2 propagate bit) and structural flags (virtual-link, wildcard
 * receiver) into a single atomic @c uint32_t, following the same
 * FLAGS_BASE / OPTIONS_BASE layout as @ref AreaFlagManager.
 *
 * @ingroup OSPF_AREA
 */
class InterfaceFlagManager
{
    /// @brief Common interface options.
    enum class Options : uint8_t
    {
        MULTICAST = 2,       ///< Interface uses multicast (AllSPFRouters/AllDRouters).
        DEMAND_CIRCUITS = 5  ///< Demand-circuit suppression enabled (RFC 1793).
    };

    /// @brief OSPFv2-only interface options.
    enum class v2Options : uint8_t
    {
        PROPAGATE = 3, ///< P-bit: NSSA LSA should be propagated to backbone.
    };

    /// @brief Interface structural flags.
    enum class Flags : uint8_t
    {
        V_LINK = 2,  ///< Interface is a virtual link.
        WILDCARD = 3 ///< Interface is a wildcard (point-to-multipoint) receiver.
    };

public:
    /**
     * @brief Construct an InterfaceFlagManager, initialising options from
     *        the interface configuration.
     * @param iface The owning OspfInterface.
     */
    InterfaceFlagManager(OspfInterface& iface);

    /** @brief Return a snapshot of the packed flags/options word. */
    uint32_t getFlags();

    void setMulticast(bool val)
        { flagmanager::setOption(flags, static_cast<uint8_t>(Options::MULTICAST), val); }
    void setDemandCircuits(bool val)
        { flagmanager::setOption(flags, static_cast<uint8_t>(Options::DEMAND_CIRCUITS), val); }
    static void setMulticast(uint32_t& fgs, bool val)
        { flagmanager::setOption(fgs, static_cast<uint8_t>(Options::MULTICAST), val); }
    static void setDemandCircuits(uint32_t& fgs, bool val)
        { flagmanager::setOption(fgs, static_cast<uint8_t>(Options::DEMAND_CIRCUITS), val); }

    void setPropagate(bool val)
        { flagmanager::setOption(flags, static_cast<uint8_t>(v2Options::PROPAGATE), val); }
    static void setPropagate(uint32_t& fgs, bool val)
        { flagmanager::setOption(fgs, static_cast<uint8_t>(v2Options::PROPAGATE), val); }

    bool getMulticast()
        { return flagmanager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(Options::MULTICAST)); }
    bool getDemandCircuits()
        { return flagmanager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(Options::DEMAND_CIRCUITS)); }
    static bool getMulticast(uint32_t fgs)
        { return flagmanager::testOption(fgs, static_cast<uint8_t>(Options::MULTICAST)); }
    static bool getDemandCircuits(uint32_t fgs)
        { return flagmanager::testOption(fgs, static_cast<uint8_t>(Options::DEMAND_CIRCUITS)); }

    bool getPropagate()
        { return flagmanager::testOption(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(v2Options::PROPAGATE)); }
    static bool getPropagate(uint32_t fgs)
        { return flagmanager::testOption(fgs, static_cast<uint8_t>(v2Options::PROPAGATE)); }

    void setVLink(bool val)
        { flagmanager::setFlag(flags, static_cast<uint8_t>(Flags::V_LINK), val); }
    void setWildcard(bool val)
        { flagmanager::setFlag(flags, static_cast<uint8_t>(Flags::WILDCARD), val); }
    static void setVLink(uint32_t fgs, bool val)
        { flagmanager::setFlag(fgs, static_cast<uint8_t>(Flags::V_LINK), val); }
    static void setWildcard(uint32_t fgs, bool val)
        { flagmanager::setFlag(fgs, static_cast<uint8_t>(Flags::WILDCARD), val); }

    bool getVLink()
        { return flagmanager::testFlag(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(Flags::V_LINK)); }
    bool getWildcard()
        { return flagmanager::testFlag(flags.load(std::memory_order_relaxed), static_cast<uint8_t>(Flags::WILDCARD)); }
    static bool getVLink(uint32_t fgs)
        { return flagmanager::testFlag(fgs, static_cast<uint8_t>(Flags::V_LINK)); }
    static bool getWildcard(uint32_t fgs)
        { return flagmanager::testFlag(fgs, static_cast<uint8_t>(Flags::WILDCARD)); }

private:
    Area& area;                      ///< The area that owns this interface.
    std::atomic<uint32_t> flags;     ///< Packed options and interface-type flags.
};
} // namespace routing

#endif

