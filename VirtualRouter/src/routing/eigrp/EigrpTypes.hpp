/**
 * @file EigrpTypes.hpp
 * @brief Shared EIGRP value types: composite-metric K-values and stub configuration.
 */

#ifndef EIGRP_TYPES_HPP
#define EIGRP_TYPES_HPP

#include <cstdint>

namespace config { class EigrpRegistry; }

namespace routing::eigrp
{

/**
 * @brief EIGRP composite-metric K-value coefficients.
 * @ingroup EIGRP
 *
 * These six weights control how the EIGRP composite metric formula combines
 * the individual link attributes (bandwidth, load, delay, reliability, MTU,
 * and extended power).  The classic formula is:
 *
 * @code
 *   metric = [K1*BW + (K2*BW)/(256-load) + K3*delay]
 *             * [K5/(reliability + K4)]   (if K5 != 0)
 * @endcode
 *
 * All neighbors in an EIGRP process **must** share identical K-values.
 * A K-value mismatch causes the neighbor to be rejected at the PENDING state.
 *
 * Default values (K1=1, K2=0, K3=1, K4=0, K5=0, K6=0) produce a metric
 * based solely on bandwidth and delay, which is the Cisco IOS default.
 *
 * @see StubConfig
 */
struct KValue
{
    /**
     * @brief Constructs a KValue set with explicit coefficients.
     *
     * @param k1 Bandwidth coefficient (default 1).
     * @param k2 Load coefficient (default 0).
     * @param k3 Delay coefficient (default 1).
     * @param k4 Reliability coefficient (default 0).
     * @param k5 MTU coefficient (default 0; enables reliability term when non-zero).
     * @param k6 Extended power/jitter coefficient (default 0; wide metrics only).
     */
    KValue(uint8_t k1 = 1, uint8_t k2 = 0, uint8_t k3 = 1, uint8_t k4 = 0, uint8_t k5 = 0, uint8_t k6 = 0)
        : k1_Bandwidth(k1), k2_Load(k2), k3_Delay(k3), k4_Reliability(k4), k5_MTU(k5), k6_Power(k6) {}

    uint8_t k1_Bandwidth;   ///< Weight applied to the inverse-bandwidth term.
    uint8_t k2_Load;        ///< Weight applied to the load-adjusted bandwidth term.
    uint8_t k3_Delay;       ///< Weight applied to the cumulative delay term.
    uint8_t k4_Reliability; ///< Additive term in the reliability divisor when K5 != 0.
    uint8_t k5_MTU;         ///< Enables the reliability scaling factor when non-zero.
    uint8_t k6_Power;       ///< Extended K-value used only in wide (named-mode) metrics.
};

/**
 * @brief EIGRP stub router configuration flags.
 * @ingroup EIGRP
 *
 * A stub router limits what route types it advertises to its EIGRP neighbors
 * and suppresses query propagation; non-stub neighbors will not send queries
 * to a stub router. This reduces convergence overhead at the network edge.
 *
 * When `receiveOnly` is true, all `advertise*` flags are ignored and no
 * routes are advertised, regardless of their individual settings.
 *
 * @see Eigrp::enableStub
 */
struct StubConfig
{
    bool isStub = false;                 ///< True if stub mode is enabled for this process.
    bool advertiseConnected = true;      ///< Advertise directly connected prefixes.
    bool advertiseLeakMap = false;       ///< Advertise routes permitted by a leak-map.
    bool advertiseStatic = true;         ///< Advertise redistributed static routes.
    bool advertiseSummary = true;        ///< Advertise summary routes.
    bool advertiseRedistributed = true;  ///< Advertise all other redistributed routes.
    bool receiveOnly = false;            ///< Suppress all outbound advertisements.
};

/**
 * @brief Reads the current K-value set for composite metric calculation
 *        from a process's raw configuration registry.
 */
KValue getKValues(const config::EigrpRegistry& configs);

/**
 * @brief Reads the full stub configuration from a process's raw
 *        configuration registry.
 */
StubConfig getStubConfig(const config::EigrpRegistry& configs);

} // namespace routing::eigrp

#endif // EIGRP_TYPES_HPP

