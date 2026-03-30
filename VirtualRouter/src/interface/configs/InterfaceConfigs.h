/**
 * @file InterfaceConfigs.h
 * @brief Per-interface address, MTU, and protocol configuration state.
 */

/**
 * @defgroup INTERFACE_CONFIGS Interface Configs
 * @ingroup INTERFACE
 * @brief Per-interface address, MTU, type, and protocol configuration state.
 */

// TODO UPDATE DOXY

#ifndef INTERFACE_CONFIGS_H
#define INTERFACE_CONFIGS_H

#include <atomic>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <cstring>
#include <unordered_set>
#include <IPAddress.h>
#include <Mac.hpp>
#include <optional>

#include "configs/registry/router/OspfInterfaceRegistry.h"
#include "configs/registry/router/EigrpInterfaceRegistry.h"
#include "configs/registry/interface/InterfaceRegistry.h"
#include "InterfaceType.hpp"

namespace core { class Global; class TimeManager; }
namespace hardware { struct HwIfaceInfo; }
namespace infrastructure { class Ndp; }
namespace services::dhcp { struct InterfaceConfigs; struct DhcpNetwork; }

class MockInterface;
class Internal_NdpTest;

namespace interface
{

enum class InterfaceType : uint8_t;
class Interface;

// INTERFACE CONFIGS

/**
 * @brief Holds all mutable configuration and address state for a single interface.
 * @ingroup INTERFACE_CONFIGS
 *
 * `InterfaceConfigs` is the data plane for a logical interface. It stores the
 * current IPv4 and IPv6 address assignments, QoS metric parameters (bandwidth,
 * delay, load, reliability), VLAN tag, MTU, and per-protocol sub-configuration
 * (EIGRP, OSPF, DHCPv6).
 *
 * Each instance contains:
 * - @ref ipv4 — primary and secondary IPv4 addresses with prefix lengths.
 * - @ref ipv6 — link-local, global unicast, and unique-local IPv6 addresses
 *   with DAD (Duplicate Address Detection) state tracking.
 * - @ref eigrp — per-AS EIGRP interface configuration references.
 * - @ref ospf — enabled OSPF processes and interface registry reference.
 * - @ref dhcpv6 — DHCPv6 client/relay state.
 *
 * ## Architectural Role
 * `InterfaceConfigs` is aggregated inside the `Interface` class, which adds
 * behavior (DAD timers, packet RX dispatch, NDP/ARP cache). Protocol instances
 * access configs via `Interface::configs` and hold their own per-interface
 * objects (e.g. `EigrpInterface`) that keep a reference back to this struct.
 *
 * ## Concurrency Model
 * IPv4 addresses are guarded by the inner `IPv4State::ipMutex`. IPv6 addresses
 * are guarded by `IPv6State::ipMutex` (a `shared_mutex`). The top-level
 * `ipMutex` (also a `shared_mutex`) may be held by callers who need to read
 * both address families atomically. Atomic members (`bandwidth`, `delay`,
 * `load`, `reliability`, `ttl`, etc.) are safe to read without a lock.
 *
 * @warning The `Interface` class and `MockInterface` are declared as friends of
 * the inner IPv4 and IPv6 state structs; no other type should access the private
 * address fields directly.
 *
 * @see Interface
 */
class InterfaceConfigs
{
public:
    /**
     * @brief Constructs interface configuration state from hardware metadata.
     * @ingroup INTERFACE_CONFIGS
     *
     * Initializes address storage, computes the @ref key from @p type and @p id,
     * and stores a reference to the hardware interface descriptor. The IPv6 state
     * object is initialized with a reference to @p timeManager so it can schedule
     * DAD and address-lifetime timers.
     *
     * @param timeManager  System timer service; used by @ref IPv6State for DAD timers.
     * @param type         Logical interface type (Ethernet, Loopback, etc.).
     * @param id           Interface number; may include a fractional sub-interface component.
     * @param info         Hardware descriptor; must outlive this object.
     */
    InterfaceConfigs(interface::Interface& iface, InterfaceType type, float id, const hardware::HwIfaceInfo& info);

    /**
     * @brief Destroys the interface configuration, cancelling any pending address timers.
     *
     * The @ref IPv6State destructor cancels all in-flight DAD and lifetime timers
     * before releasing address storage.
     */
    ~InterfaceConfigs();

    /**
     * @brief Returns true if the interface currently holds the given IPv4 address and prefix.
     *
     * Checks both primary and secondary IPv4 addresses.
     *
     * @param address  Network-order IPv4 address bytes.
     * @param len      Prefix length in bits.
     */
    bool hasAddress(const uint8_t* address, uint8_t len);
    /**
     * @brief Returns true if the interface currently holds the given IPv6 address and prefix.
     *
     * @param address  128-bit IPv6 address in host byte order.
     * @param len      Prefix length in bits.
     */
    bool hasAddress(__uint128_t address, uint8_t len);

    /**
     * @brief Writes the interface MAC address into @p mac and returns @p mac.
     *
     * @param mac  6-byte output buffer; must not be null.
     * @return Pointer to @p mac for convenience chaining.
     */
    uint8_t* getMac(uint8_t* mac);

    /**
     * @brief Gets the interface MAC address.
     *
     * @return Mac
     */
    types::Mac getMac();

    // Getters
    uint32_t getBandwidth();
    uint32_t getReceiveBandwidth();

    config::InterfaceRegistry& getConfigs() { return configs.get(); }
    const config::InterfaceRegistry& getConfigs() const { return configs.get(); }

    // TODO finish doxy
    void syncMac();
    void syncPrimaryIP();
    void syncSecondaryIP();
    void syncLocalLink();
    void syncIPv6();

    float         id;            ///< Interface number, including sub-interface fraction.
    InterfaceType interfaceType; ///< Logical interface type.
    InterfaceKey  key;           ///< Composite key encoding type and id; used for global interface lookup.

    const hardware::HwIfaceInfo& hwInfo; ///< Immutable hardware descriptor; owned externally.

    // IPv4 STATE

    /**
     * @brief Holds the primary and secondary IPv4 address assignments for the interface.
     *
     * At most one primary address and any number of secondary addresses may be
     * configured simultaneously. The primary address is stored atomically (address
     * and mask as separate atomics); secondary addresses are kept in a vector
     * guarded by the inner mutex.
     *
     * ## Concurrency Model
     * All mutations and non-trivial reads are serialized under `ipMutex`. Atomic
     * members (`address`, `mask`) may be sampled without the lock when only the
     * primary address is needed and exact consistency with secondaries is not
     * required.
     */
    struct IPv4State
    {
        IPv4State() : mask(0), address(0) {}

        std::atomic<uint16_t> mtu{1500};      ///< IPv4 MTU; may differ from @ref InterfaceConfigs::globalMtu if locally overridden.
        std::atomic<bool>     mtuLocal{false}; ///< True if the IPv4 MTU was explicitly configured rather than inherited.

        /**
         * @brief Sets the primary IPv4 address, replacing any previous primary.
         * @ingroup INTERFACE_CONFIGS
         *
         * @param prefix  New primary address and prefix length.
         */
        void setPrimaryAddress(types::IPv4Prefix prefix);

        /**
         * @brief Adds a secondary IPv4 address.
         *
         * Secondary addresses are used when a single interface must serve
         * multiple subnets (e.g. router-on-a-stick configurations).
         *
         * @param prefix  Secondary address and prefix length to add.
         */
        void addSecondaryAddress(types::IPv4Prefix prefix);

        void removePrimaryAddress();

        /**
         * @brief Removes a specific secondary address.
         *
         * @param prefix  Address and prefix length to remove.
         */
        void removeSecondaryAddress(types::IPv4Prefix prefix);

        /**
         * @brief Writes the primary IPv4 address into @p out and returns @p out.
         *
         * @param out  4-byte output buffer in network order.
         * @return Pointer to @p out.
         */
        uint8_t* getPrimaryAddress(uint8_t* out) const;

        /**
         * @brief Writes the first secondary IPv4 address into @p out and returns @p out.
         *
         * @param out  4-byte output buffer in network order.
         * @return Pointer to @p out, or nullptr if no secondary address is configured.
         */
        uint8_t* getSecondaryAddress(uint8_t* out) const;

        types::IPv4Address getPrimaryAddress() const;
        std::optional<types::IPv4Address> getSecondaryAddress() const;

        bool hasPrimaryAddress() const;
        bool hasPrimaryAddress(types::IPv4Prefix prefix) const;
        bool hasPrimaryAddress(const uint8_t* addr, uint8_t mask) const;
        bool hasSecondaryAddress(types::IPv4Prefix prefix) const;
        bool hasSecondaryAddress(const uint8_t* addr, uint8_t mask) const;

        /**
         * @brief Writes the primary prefix length into @p out and returns it.
         *
         * @param out  Output byte for the prefix length.
         * @return The primary prefix length in bits.
         */
        uint8_t getPrimaryPrefix(uint8_t* out) const;

        /**
         * @brief Writes the first secondary prefix length into @p out.
         *
         * @param out  Output byte for the prefix length.
         * @return The prefix length if a secondary exists, otherwise `std::nullopt`.
         */
        std::optional<uint8_t> getSecondaryPrefix(uint8_t* out) const;

        types::IPv4Prefix getPrimaryPrefix() const;
        std::optional<types::IPv4Prefix> getSecondaryPrefix();

        uint8_t getPrimaryMask() const;
        std::optional<uint8_t> getSecondaryMask() const;

        std::vector<types::IPv4Address> getSecondaryList() const;

        /**
         * @brief Returns the list of secondary prefixes.
         *
         * @param maintainAddress  When true, each prefix retains the host bits of
         *                         the assigned address rather than being masked to
         *                         network address form.
         */
        std::vector<types::IPv4Prefix> getSecondaryPrefixList(bool maintainAddress = false) const;

        std::unordered_set<types::IPv4Address> getSecondarySet() const;

        /**
         * @brief Returns the set of secondary prefixes.
         *
         * @param maintainAddress  See @ref getSecondaryPrefixList.
         */
        std::unordered_set<types::IPv4Prefix> getSecondaryPrefixSet(bool maintainAddress = false) const;

        bool comparePrimaryAddress(const uint8_t* ip);
        bool comparePrimaryAddress(types::IPv4Address ip);

    private:
        mutable std::mutex ipMutex;          ///< Guards secondary address vector and coordinated primary reads.
        std::atomic<uint8_t>  mask{0};       ///< Primary address prefix length in bits.
        std::atomic<uint32_t> address;       ///< Primary IPv4 address in host byte order.

        std::vector<types::IPv4Prefix> secondary; ///< Secondary IPv4 address/prefix list.

        friend class MockInterface;
        friend class Interface;
    } ipv4;

    // IPv6 STATE

    /**
     * @brief Holds all IPv6 address assignments and DAD state for the interface.
     *
     * Manages up to one link-local address, any number of global unicast
     * addresses, and any number of unique-local (ULA) addresses. Each address
     * tracks Duplicate Address Detection (DAD) state through `tentative` and
     * `valid` flags, and may have associated preferred/valid lifetime timers
     * managed by the @ref core::TimeManager reference.
     *
     * ## Concurrency Model
     * All address list mutations and reads that span more than one address field
     * are serialized under the internal `shared_mutex ipMutex`. The `mtu` and
     * `mtuLocal` atomics may be read without the lock.
     *
     * @see infrastructure::Ndp
     */
    struct IPv6State
    {
        /**
         * @brief Constructs IPv6 state bound to @p time for address lifetime management.
         * @ingroup INTERFACE_CONFIGS
         *
         * @param time  Timer service; used to schedule DAD retransmissions and
         *              preferred/valid lifetime expiry events.
         */
        explicit IPv6State(core::TimeManager& tmgr);

        /**
         * @brief Destroys IPv6 state and cancels all pending address timers.
         */
        ~IPv6State();

        /**
         * @brief Tracks the state of a single IPv6 address assignment through DAD and lifetime stages.
         *
         * An address progresses from tentative → valid → deprecated as its
         * preferred lifetime expires. The `globalTentative` / `globalValid`
         * flags mirror the link-local flags for global scope addresses that
         * require separate global-scope DAD.
         */
        struct IPv6Address
        {
            types::IPv6Prefix prefix;

            bool     tentative{false};       ///< Address is undergoing link-local DAD; not yet usable as source.
            bool     valid{false};           ///< Link-local DAD has completed; address is usable.
            bool     globalTentative{false}; ///< Address is undergoing global-scope DAD.
            bool     globalValid{false};     ///< Global-scope DAD has completed.
            bool     deprecated{false};      ///< Preferred lifetime has expired; address stays valid but not preferred for new connections.
            uint32_t expirationId   = 0;     ///< Timer ID for the valid-lifetime expiry event.
            uint32_t preferredLifetime = 0;  ///< Preferred lifetime in seconds.
            uint32_t preferedExpirationId = 0; ///< Timer ID for the preferred-lifetime expiry event.

            /**
             * @brief Transitions the address from tentative to valid after DAD completes.
             * @ingroup INTERFACE_CONFIGS
             *
             * @param local  When true, validates only the link-local DAD flags;
             *               when false, validates the global scope flags.
             */
            void validateAddress(bool local = false);

            friend class MockInterface;
            friend class Internal_NdpTest;
        };

        /**
         * @brief Returns true if the interface has at least one routable (non-link-local) IPv6 address.
         */
        bool hasRoutableAddress();

        // ADD / REMOVE

        /**
         * @brief Adds an IPv6 address, selecting global or link-local storage based on scope.
         *
         * @param ip     Prefix to assign.
         * @param local  When true, treats the address as link-local.
         * @return Pointer to the newly created @ref IPv6Address entry.
         */
        IPv6Address* addAddress(const types::IPv6Prefix& ip, bool local);

        /**
         * @brief Adds a Unique Local Address (FC00::/7 range).
         *
         * @param ip  ULA prefix to assign.
         * @return Pointer to the newly created entry.
         */
        IPv6Address* addUniqueLocalAddress(const types::IPv6Prefix& ip);

        /**
         * @brief Adds a global unicast address (outside link-local and ULA ranges).
         *
         * @param ip  Global unicast prefix to assign.
         * @return Pointer to the newly created entry.
         */
        IPv6Address* addGlobalAddress(const types::IPv6Prefix& ip);

        void removeLocalAddress();

        /**
         * @brief Removes the address matching @p prefix from whichever address list contains it.
         *
         * @param prefix  Address and prefix length to remove.
         */
        void removeAddress(const types::IPv6Prefix& prefix);

        void removeAllAddresses();

        /**
         * @brief Calls @ref IPv6Address::validateAddress on all global unicast entries.
         *
         * Used after global-scope DAD completes to mark all pending global
         * addresses as valid simultaneously.
         */
        void validateGlobalAddresses();

        /**
         * @brief Calls @ref IPv6Address::validateAddress on the link-local address.
         */
        void validateLinkLocalAddress();

        // ADDRESS ACCESSORS

        /**
         * @brief Writes the link-local address into @p out and returns @p out.
         *
         * @param out  16-byte output buffer in network order.
         * @return Pointer to @p out, or nullptr if no link-local address exists.
         */
        uint8_t* getLocalAddress(uint8_t* out) const;

        /**
         * @brief Writes the first valid global unicast address into @p out.
         *
         * @param out  16-byte output buffer in network order.
         * @return Pointer to @p out, or nullptr if no global unicast address is valid.
         */
        uint8_t* getGlobalUnicast(uint8_t* out) const;

        /**
         * @brief Writes the first valid unique-local address into @p out.
         *
         * @param out  16-byte output buffer in network order.
         * @return Pointer to @p out, or nullptr if no ULA is valid.
         */
        uint8_t* getLocalUnicast(uint8_t* out) const;

        types::IPv6Address getLocalAddress() const;
        types::IPv6Address getGlobalUnicast() const;
        types::IPv6Address getLocalUnicast() const;

        bool hasAddress(const uint8_t* addr);
        bool hasAddress(types::IPv6Address addr);

        bool hasLocalAddress(const uint8_t* addr, uint8_t len) const;
        bool hasGlobalUnicast(const uint8_t* addr, uint8_t len) const;
        bool hasLocalUnicast(const uint8_t* addr, uint8_t len) const;

        bool hasLocalAddress(const types::IPv6Prefix& prefix) const;
        bool hasGlobalUnicast(const types::IPv6Prefix& prefix) const;
        bool hasLocalUnicast(const types::IPv6Prefix& prefix) const;

        /**
         * @brief Writes the link-local prefix length into @p out and returns it.
         *
         * @param out  Output byte for the prefix length.
         * @return The link-local prefix length in bits.
         */
        uint8_t getLocalPrefix(uint8_t* out) const;

        /**
         * @brief Writes the first global unicast prefix length into @p out.
         *
         * @param out  Output byte for the prefix length.
         * @return The global unicast prefix length in bits.
         */
        uint8_t getGlobalUnicastPrefix(uint8_t* out) const;

        /**
         * @brief Writes the first unique-local prefix length into @p out.
         *
         * @param out  Output byte for the prefix length.
         * @return The ULA prefix length in bits.
         */
        uint8_t getLocalUnicastPrefix(uint8_t* out) const;

        types::IPv6Prefix getLocalPrefix() const;
        types::IPv6Prefix getGlobalUnicastPrefix() const;
        types::IPv6Prefix getLocalUnicastPrefix() const;

        uint8_t getLocalMask() const;
        uint8_t getGlobalUnicastMask() const;
        uint8_t getLocalUnicastMask() const;

        std::vector<types::IPv6Address> getRoutableList() const;
        std::vector<types::IPv6Address> getGlobalList() const;
        std::vector<types::IPv6Address> getLocalList() const;

        /**
         * @brief Returns the list of routable (non-link-local) prefixes.
         *
         * @param maintainAddress  When true, host bits are preserved in the returned prefixes.
         */
        std::vector<types::IPv6Prefix> getRoutablePrefixList(bool maintainAddress = false) const;

        /**
         * @brief Returns the list of global unicast prefixes.
         *
         * @param maintainAddress  When true, host bits are preserved in the returned prefixes.
         */
        std::vector<types::IPv6Prefix> getGlobalPrefixList(bool maintainAddress = false) const;

        /**
         * @brief Returns the list of unique-local prefixes.
         *
         * @param maintainAddress  When true, host bits are preserved in the returned prefixes.
         */
        std::vector<types::IPv6Prefix> getLocalPrefixList(bool maintainAddress = false) const;

        std::unordered_set<types::IPv6Address> getRoutableSet() const;
        std::unordered_set<types::IPv6Address> getGlobalSet() const;
        std::unordered_set<types::IPv6Address> getUniqueSet() const;

        std::unordered_set<types::IPv6Prefix> getRoutablePrefixSet(bool maintainAddress = false) const;
        std::unordered_set<types::IPv6Prefix> getGlobalPrefixSet(bool maintainAddress = false) const;
        std::unordered_set<types::IPv6Prefix> getUniquePrefixSet(bool maintainAddress = false) const;

        std::atomic<uint16_t> mtu{1500};       ///< IPv6 MTU; may differ from globalMtu if locally overridden.
        std::atomic<bool>     mtuLocal{false};  ///< True if IPv6 MTU was explicitly configured.

    private:
        core::TimeManager& timeManager;
        mutable std::mutex ipMutex;          ///< Guards local/global address vector.

        IPv6Address*              linkLocalAddress   = nullptr; ///< The single link-local address, if assigned.
        std::vector<IPv6Address*> globalAddresses;              ///< Heap-allocated global unicast entries.
        std::vector<IPv6Address*> uniqueLocalAddresses{};       ///< Heap-allocated ULA entries.

        /**
         * @brief Cancels any pending lifetime or DAD timers associated with @p addr.
         *
         * @param addr  Address whose timer IDs should be cancelled.
         */
        void cancelTimers(IPv6Address& addr);

        friend class MockInterface;
        friend class Interface;
        friend class infrastructure::Ndp;
        friend class Internal_NdpTest;
    } ipv6;

    // PROTOCOL SUB-CONFIGURATIONS

    /**
     * @brief Per-interface EIGRP configuration and enabled autonomous-system tracking.
     *
     * Stores which IPv6 autonomous systems have this interface enabled, and
     * holds a reference-counted handle to the per-AS interface registry entry
     * for each enabled AS.
     */
    struct Eigrp
    {
        std::unordered_set<uint32_t> ipv6AutonomousSystems; ///< Set of IPv6 AS numbers that have activated this interface.
        std::unordered_map<uint32_t, config::Reference<config::EigrpInterfaceRegistry>> eigrpIfaceConfigs; ///< Per-AS EIGRP interface config registry references, keyed by AS number.
    } eigrp;

    /**
     * @brief Per-interface OSPF process membership and interface registry reference.
     * @ingroup INTERFACE_CONFIGS
     *
     * `enabledProcesses` maps OSPF process ID to the area ID this interface is
     * assigned to. `ospfInterfaceConfigs` holds the registry reference once the
     * interface has been placed under an OSPF process.
     */
    struct Ospf
    {
        std::unordered_map<uint32_t, uint32_t> enabledProcesses;  ///< Maps OSPF process ID → area ID for each enabled process.
        std::optional<config::Reference<config::OspfInterfaceBaseRegistry>> ospfInterfaceConfigs = std::nullopt; ///< OSPF interface config registry entry; set when the interface joins a process.
    } ospf;

    /**
     * @brief DHCPv6 client/relay configuration attached to this interface.
     * @ingroup INTERFACE_CONFIGS
     *
     * `configs` points to the DHCPv6 interface options (IA type, prefix
     * delegation, etc.). `dhcpNetworks` lists the DHCP network scopes this
     * interface participates in as a relay or server.
     */
    struct Dhcpv6
    {
        services::dhcp::InterfaceConfigs*       configs      = nullptr; ///< DHCPv6 interface options; null if DHCPv6 is not enabled.
        std::vector<services::dhcp::DhcpNetwork*> dhcpNetworks;         ///< DHCP network scopes served or relayed on this interface.
    } dhcpv6;

private:
    friend class Interface;

    // TODO finish doxy
    config::Reference<config::InterfaceRegistry> configs;

    std::atomic<types::Mac> macAddress;
};

} // namespace interface

#endif // INTERFACE_CONFIGS_H
