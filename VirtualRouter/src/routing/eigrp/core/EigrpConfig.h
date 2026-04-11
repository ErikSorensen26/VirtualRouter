/**
 * @file EigrpConfig.h
 * @brief Process-level EIGRP configuration facade over the registry.
 */

#ifndef EIGRP_CONFIG_H
#define EIGRP_CONFIG_H

#include <cstdint>
#include <unordered_set>
#include <IPAddress.h>

#include "eigrp/EigrpTypes.hpp"
#include "configs/registry/router/EigrpRegistry.h"
#include "interface/configs/InterfaceType.hpp"

namespace routing::eigrp
{
class Eigrp;

/**
 * @brief Process-level EIGRP configuration facade.
 * @ingroup EIGRP_CORE
 *
 * `EigrpConfig` provides a typed, ergonomic API over the raw `EigrpRegistry`
 * config store for all settings that apply to a single EIGRP process (AS +
 * address family).  It does not own any configuration state; all values are
 * stored in the registry and accessed through a @ref config::Reference.
 *
 * Responsibilities include:
 * - Network statement management (`addNetworkRange` / `delNetworkRange`)
 * - Passive interface and unicast peer tracking
 * - Stub mode configuration
 * - Typed read accessors for all process-level tunable parameters
 *
 * ## Architectural Role
 * Sits between the CLI command parsers and the EIGRP runtime.  CLI parsers
 * write to the registry; `EigrpConfig` methods read from it and trigger the
 * appropriate runtime reactions (e.g., calling `Eigrp::restart()` on a
 * K-value change).
 *
 * ## Lifecycle & Ownership
 * Owned by and lives inside the @ref Eigrp object.  The `base` reference
 * must remain valid for the lifetime of this object.
 *
 * @see EigrpRegistry
 * @see KValue
 * @see StubConfig
 */
class EigrpConfig
{
public:

    /**
     * @brief Constructs the config facade bound to the given EIGRP process.
     *
     * @param base The @ref Eigrp process this configuration belongs to.
     */
    EigrpConfig(Eigrp& base);

    /**
     * @brief Adds an IPv4 network range to this process.
     *
     * Any interface whose primary address falls within `newNetwork` will be
     * activated for EIGRP.  Triggers an interface list refresh.
     *
     * @param newNetwork The network prefix to add.
     */
    void addNetworkRange(const types::IPv4Prefix& newNetwork);

    /**
     * @brief Removes a previously configured IPv4 network range.
     *
     * Interfaces that no longer match any network range are deactivated.
     *
     * @param delNetwork The network prefix to remove.
     */
    void delNetworkRange(const types::IPv4Prefix& delNetwork);

    /**
     * @brief Checks whether an IPv4 address is covered by a configured
     *        `network` statement.
     *
     * @param testIp Address to test.
     * @return True if `testIp` matches at least one configured network range.
     */
    bool isInNetworkRange(types::IPv4Address testIp) const;

    /**
     * @brief Removes all configured network ranges and deactivates all
     *        interfaces.
     */
    void clearNetworks();

    /**
     * @brief Configures or disables stub router mode for this process.
     *
     * When stub mode is enabled, this router will not be sent queries by
     * non-stub neighbors.  The flags control which route types are still
     * advertised.
     *
     * @param isStub              True to enable stub mode.
     * @param advertiseConnected  Advertise connected prefixes (default true).
     * @param advertiseStatic     Advertise static redistributed routes (default true).
     * @param advertiseSummary    Advertise summary routes (default true).
     * @param advertiseRedistributed Advertise all redistributed routes (default true).
     */
    void enableStub(bool isStub, bool advertiseConnected = true, bool advertiseStatic = true, bool advertiseSummary = true, bool advertiseRedistributed = true);

    /**
     * @brief Marks or unmarks an interface as passive.
     *
     * Passive interfaces do not send or receive EIGRP hellos; connected
     * prefixes on them are still redistributed into the topology table.
     *
     * @param key Interface key.
     * @param add True to make passive, false to remove the passive flag.
     */
    void setPassiveInterface(interface::InterfaceKey key, bool add = true);

    /**
     * @brief Enables a unicast static neighbor relationship on an interface.
     *
     * Disables multicast hellos on the interface and instead sends unicast
     * hellos only to the configured peer addresses.
     *
     * @param neighborIp IP address of the peer.
     * @param key        Interface key the peer is reachable through.
     */
    void enableUnicastPeer(const types::IPAddress& neighborIp, interface::InterfaceKey key);

    /**
     * @brief Removes a unicast static neighbor relationship.
     *
     * @param neighborIp IP address of the peer to remove.
     * @param key        Interface key the peer was configured on.
     */
    void disableUnicastPeer(const types::IPAddress& neighborIp, interface::InterfaceKey key);

    config::EigrpRegistry& getConfigs() { return configs; }

    // PROCESS-LEVEL CONFIG ACCESSORS

    bool stubEnabled() const { return configs.get<config::Eigrp::STUB>().load(); }

    /**
     * @brief Returns the full stub configuration as a @ref StubConfig value.
     */
    StubConfig getStubConfig() const;

    /**
     * @brief Returns the current K-value set for composite metric calculation.
     */
    KValue getKValues() const;

    /**
     * @brief Returns true if the given interface key is configured as passive.
     */
    bool isPassive(interface::InterfaceKey key) const;

    /**
     * @brief Returns the set of unicast peer addresses configured on the
     *        given interface key.
     */
    std::unordered_set<types::IPAddress> getUnicastNeighbors(interface::InterfaceKey key) const;

    // DAMPENING CONFIG ACCESSORS

    bool getDampening() const          { return configs.get<config::Eigrp::DAMPENING>().load(); }
    bool getDampeningWarning() const   { return configs.get<config::Eigrp::DAMPENING_WARNINGS>().load(); }
    uint8_t getDampeningInterval() const  { return configs.get<config::Eigrp::DAMPENING_THRESHOLD>().load(); }
    uint16_t getDampeningResetTime() const { return configs.get<config::Eigrp::DAMPENING_RESET_TIME>().load(); }
    uint16_t getDampeningRestart() const   { return configs.get<config::Eigrp::DAMPENING_RESTART>().load(); }     ///< Seconds before a dampened neighbor is allowed to restart.
    uint16_t getDampeningRestartCount() const { return configs.get<config::Eigrp::DAMPENING_RESTART_COUNT>().load(); } ///< Maximum restart attempts before the neighbor is suppressed indefinitely.

    // METRIC AND PATH CONFIG ACCESSORS

    uint32_t getMaximumPrefixes() const { return configs.get<config::Eigrp::MAXIMUM_PREFIX>().load(); }
    uint8_t getRibScale() const         { return configs.get<config::Eigrp::RIB_SCALE>().load(); }  ///< Divisor applied to the EIGRP feasible distance before writing to the RIB.
    uint8_t getAD() const               { return configs.get<config::Eigrp::INTERNAL_ADMIN_DISTANCE>().load(); }
    uint8_t getExternalAD() const       { return configs.get<config::Eigrp::EXTERNAL_ADMIN_DISTANCE>().load(); }
    uint8_t getMaxPaths() const         { return configs.get<config::Eigrp::MAX_PATHS>().load(); }
    uint8_t getMaxHops() const          { return configs.get<config::Eigrp::MAX_HOPS>().load(); }
    uint8_t getVariance() const         { return configs.get<config::Eigrp::VARIANCE>().load(); }    ///< EIGRP unequal-cost load-balancing multiplier (1 = equal-cost only).
    config::eigrp::TrafficShareMode getTrafficMode() const { return configs.get<config::Eigrp::TRAFFIC_SHARE>().load(); }

    // NSF / AUTO-SUMMARY / SIA ACCESSORS

    bool isNonStopForwarding() const { return configs.get<config::Eigrp::NON_STOP_FORWARDING>().load(); }
    uint16_t getPurgeTime() const    { return configs.get<config::Eigrp::GRACEFUL_PURGE_TIME>().load(); } ///< Seconds that NSF-restarting routes are kept in the RIB during graceful restart.
    bool isAutoSummarized() const    { return configs.get<config::Eigrp::AUTO_SUMMARIZATION>().load(); }
    void setAutoSummary(bool enable) { configs.get<config::Eigrp::AUTO_SUMMARIZATION>().set(enable); }

    /**
     * @brief Returns the Stuck-In-Active (SIA) timeout in seconds.
     *
     * Defaults to 90 seconds if the registry field has not been explicitly set.
     */
    uint16_t getSIATime() const
    {
        auto& field = configs.get<config::Eigrp::ACTIVE_TIME>();
        return field.hasValue() ? field.load() : 90;
    }

    uint16_t getDelTimer() const { return 120; } ///< Route deletion timer in seconds; hard-coded to 3× the default hold time (3 × 40 s).

private:

    Eigrp& base;                                          ///< Owning EIGRP process.
    config::EigrpRegistry& configs;    ///< Live reference to the process registry.
};
} // namespace routing::eigrp

#endif // EIGRP_CONFIG_H
