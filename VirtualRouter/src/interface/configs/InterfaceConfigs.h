/**
 * @file InterfaceConfigs.h
 * @brief Per-interface address, MTU, and protocol configuration state.
 */

/**
 * @defgroup INTERFACE_CONFIGS Interface Configs
 * @ingroup INTERFACE
 * @brief Per-interface address, MTU, type, and protocol configuration state.
 */

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
class Internal_NdpTest;

namespace interface
{
class MockInterface;
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
     * object is initialized with the owning interface's timer service so it can
     * schedule DAD and address-lifetime timers. Binds to a pre-built
     * @ref config::InterfaceRegistry (created by the INTERFACE applier via
     * Global::createInterface) rather than creating one via `emplaceBack`.
     *
     * @param iface        Interface that owns this configuration state.
     * @param type         Logical interface type (Ethernet, Loopback, etc.).
     * @param id           Interface number; may include a fractional sub-interface component.
     * @param info         Hardware descriptor; must outlive this object.
     * @param cfg          Registry slot already created for this interface.
     */
    InterfaceConfigs(interface::Interface& iface, InterfaceType type, float id, const hardware::HwIfaceInfo& info, config::InterfaceRegistry& cfg);

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

    /// Returns the configured transmit bandwidth for this interface in kbps.
    uint32_t getBandwidth();

    /// Returns the configured receive bandwidth for this interface in kbps.
    uint32_t getReceiveBandwidth();

    /// Returns a mutable reference to the interface config registry.
    config::InterfaceRegistry& getConfigs() { return configs; }

    /// Returns a read-only reference to the interface config registry.
    const config::InterfaceRegistry& getConfigs() const { return configs; }

    /**
     * @brief Re-reads the hardware MAC address and updates the cached atomic value.
     *
     * Called when the MAC changes (e.g. after a `mac-address` config command).
     * Writes to the `macAddress` atomic so the data plane picks up the new value
     * without a lock.
     */
    void syncMac();

    /**
     * @brief Re-reads the primary IPv4 address from config and updates the atomic.
     *
     * Must be called after any change to the primary address field in the
     * interface registry so that the data-plane fast path and ARP see the
     * current address immediately.
     */
    void syncPrimaryIP();

    /**
     * @brief Re-reads all secondary IPv4 addresses from config and updates
     *        the secondary address list.
     */
    void syncSecondaryIP();

    /**
     * @brief Re-reads the IPv6 link-local address from config and updates
     *        the link-local state.
     *
     * Triggers DAD for the new link-local address if one is configured.
     */
    void syncLocalLink();

    /**
     * @brief Re-reads all global IPv6 addresses from config and reconciles
     *        the global address list.
     *
     * Adds newly configured addresses (triggering DAD) and removes any that
     * have been deleted from config. Called when the IPv6 address config
     * changes on the interface.
     */
    void syncIPv6();

    /**
     * @brief Re-reads dhcpv6 from config and reconciles
     *        the global dhcp state.
     *
     * Adds dhcp client and removes old primary address.
     * changes on the interface.
     */
    void syncDhcpv6();

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

        /// Clears the primary IPv4 address, resetting address and mask atomics to zero.
        void removePrimaryAddress();

        /**
         * @brief Removes a specific secondary address.
         *
         * @param prefix  Address and prefix length to remove.
         */
        void removeSecondaryAddress(types::IPv4Prefix prefix);

        /**
         * @brief Removes all secondary address.
         */
        void clearSecondaryAddresses();

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

        /// Returns the primary IPv4 address.
        types::IPv4Address getPrimaryAddress() const;

        /// Returns the first secondary IPv4 address, or `std::nullopt` if none is configured.
        std::optional<types::IPv4Address> getSecondaryAddress() const;

        /// Returns true if a primary IPv4 address is configured.
        bool hasPrimaryAddress() const;

        /// Returns true if the primary address matches @p prefix exactly.
        bool hasPrimaryAddress(types::IPv4Prefix prefix) const;

        /// Returns true if the primary address matches the given raw address and mask length.
        bool hasPrimaryAddress(const uint8_t* addr, uint8_t mask) const;

        /// Returns true if any secondary address matches @p prefix exactly.
        bool hasSecondaryAddress(types::IPv4Prefix prefix) const;

        /// Returns true if any secondary address matches the given raw address and mask length.
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

        /// Returns the primary address as a prefix (address + length).
        types::IPv4Prefix getPrimaryPrefix(bool maintainAddress = false) const;

        /// Returns the first secondary prefix, or `std::nullopt` if none is configured.
        std::optional<types::IPv4Prefix> getSecondaryPrefix(bool maintainAddress);

        /// Returns the primary address prefix length in bits.
        uint8_t getPrimaryMask() const;

        /// Returns the first secondary address prefix length, or `std::nullopt` if none is configured.
        std::optional<uint8_t> getSecondaryMask() const;

        /// Returns a list of all secondary IPv4 addresses (without prefix lengths).
        std::vector<types::IPv4Address> getSecondaryList() const;

        /**
         * @brief Returns the list of secondary prefixes.
         *
         * @param maintainAddress  When true, each prefix retains the host bits of
         *                         the assigned address rather than being masked to
         *                         network address form.
         */
        std::vector<types::IPv4Prefix> getSecondaryPrefixList(bool maintainAddress = false) const;

        /// Returns the set of all secondary IPv4 addresses (without prefix lengths).
        std::unordered_set<types::IPv4Address> getSecondarySet() const;

        /**
         * @brief Returns the set of secondary prefixes.
         *
         * @param maintainAddress  See @ref getSecondaryPrefixList.
         */
        std::unordered_set<types::IPv4Prefix> getSecondaryPrefixSet(bool maintainAddress = false) const;

        /// Returns true if the primary address matches the raw 4-byte network-order address @p ip.
        bool comparePrimaryAddress(const uint8_t* ip);

        /// Returns true if the primary address matches @p ip.
        bool comparePrimaryAddress(types::IPv4Address ip);

        /// Returns true if the primary address and prefix length both match @p prefix.
        bool comparePrimaryPrefix(types::IPv4Prefix prefix);

        /// Returns true if a secondary address matches the raw 4-byte network-order address @p ip.
        bool compareSecondaryAddress(const uint8_t* ip);

        /// Returns true if the secondary address matches @p ip.
        bool compareSecondaryAddress(types::IPv4Address ip);

        /// Returns true if a secondary address and prefix length both match @p prefix.
        bool compareSecondaryAddress(types::IPv4Prefix prefix);

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
        friend class ::Internal_NdpTest;

        /**
         * @brief Constructs IPv6 state bound to @p tmgr for address lifetime management.
         * @ingroup INTERFACE_CONFIGS
         *
         * @param tmgr  Timer service; used to schedule DAD retransmissions and
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

        /// Removes the link-local address and cancels any associated DAD timers.
        void removeLocalAddress();

        /**
         * @brief Removes the address matching @p prefix from whichever address list contains it.
         *
         * @param prefix  Address and prefix length to remove.
         */
        void removeAddress(const types::IPv6Prefix& prefix);

        /// Removes all link-local, global unicast, and ULA addresses, cancelling their timers.
        void removeAllAddresses(bool local = false);

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

        /// Returns the link-local address (unspecified if none is assigned).
        types::IPv6Address getLocalAddress() const;

        /// Returns the first valid global unicast address (unspecified if none is valid).
        types::IPv6Address getGlobalUnicast() const;

        /// Returns the first valid unique-local address (unspecified if none is valid).
        types::IPv6Address getLocalUnicast() const;

        /// Returns true if the interface holds the given 16-byte network-order IPv6 address (any scope).
        bool hasAddress(const uint8_t* addr);

        /// Returns true if the interface holds @p addr in any address list.
        bool hasAddress(types::IPv6Address addr);

        /// Returns true if the link-local address matches the given raw address and prefix length.
        bool hasLocalAddress(const uint8_t* addr, uint8_t len) const;

        /// Returns true if any global unicast address matches the given raw address and prefix length.
        bool hasGlobalUnicast(const uint8_t* addr, uint8_t len) const;

        /// Returns true if any ULA matches the given raw address and prefix length.
        bool hasLocalUnicast(const uint8_t* addr, uint8_t len) const;

        /// Returns true if the link-local address matches @p prefix.
        bool hasLocalAddress(const types::IPv6Prefix& prefix) const;

        /// Returns true if any global unicast address matches @p prefix.
        bool hasGlobalUnicast(const types::IPv6Prefix& prefix) const;

        /// Returns true if any ULA matches @p prefix.
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

        /// Returns the link-local address as a prefix (address + length).
        types::IPv6Prefix getLocalPrefix() const;

        /// Returns the first global unicast address as a prefix.
        types::IPv6Prefix getGlobalUnicastPrefix() const;

        /// Returns the first ULA as a prefix.
        types::IPv6Prefix getLocalUnicastPrefix() const;

        /// Returns the link-local address prefix length in bits.
        uint8_t getLocalMask() const;

        /// Returns the first global unicast address prefix length in bits.
        uint8_t getGlobalUnicastMask() const;

        /// Returns the first ULA prefix length in bits.
        uint8_t getLocalUnicastMask() const;

        /// Returns a list of all routable (non-link-local) IPv6 addresses.
        std::vector<types::IPv6Address> getRoutableList() const;

        /// Returns a list of all global unicast addresses.
        std::vector<types::IPv6Address> getGlobalList() const;

        /// Returns a list of all ULA addresses.
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

        /// Returns the set of all routable (non-link-local) IPv6 addresses.
        std::unordered_set<types::IPv6Address> getRoutableSet() const;

        /// Returns the set of all global unicast addresses.
        std::unordered_set<types::IPv6Address> getGlobalSet() const;

        /// Returns the set of all ULA addresses.
        std::unordered_set<types::IPv6Address> getUniqueSet() const;

        /// Returns the set of routable prefixes; host bits are preserved when @p maintainAddress is true.
        std::unordered_set<types::IPv6Prefix> getRoutablePrefixSet(bool maintainAddress = false) const;

        /// Returns the set of global unicast prefixes; host bits are preserved when @p maintainAddress is true.
        std::unordered_set<types::IPv6Prefix> getGlobalPrefixSet(bool maintainAddress = false) const;

        /// Returns the set of ULA prefixes; host bits are preserved when @p maintainAddress is true.
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

    config::InterfaceRegistry& configs; ///< Owning reference to the interface config registry; source of truth for all configurable parameters.

    std::atomic<types::Mac> macAddress; ///< Cached MAC address; updated by @ref syncMac when the config changes.
};

} // namespace interface

#endif // INTERFACE_CONFIGS_H
