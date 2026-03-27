/**
 * @file OspfTypes.hpp
 * @brief Protocol-level OSPF constants shared across OSPFv2 and OSPFv3.
 */

#ifndef OSPF_TYPES_HPP
#define OSPF_TYPES_HPP

#include <cstdint>

namespace routing
{

static constexpr uint16_t OSPF_MAX_AGE    = 3600; ///< MaxAge (seconds): an LSA reaching this age is considered expired (RFC 2328 §A.4.1).
static constexpr uint16_t OSPF_REFRESH_AGE = 1800; ///< LSRefreshTime (seconds): self-originated LSAs are refreshed before this age (RFC 2328 §A.4.1).
static constexpr uint16_t OSPF_MAX_DIFF   = 900;  ///< MaxAgeDiff: maximum acceptable age difference between two instances of the same LSA.

static constexpr uint16_t OSPF_HELLO_TIME    = 10; ///< Default HelloInterval (seconds) for broadcast/NBMA networks (RFC 2328 §C.3).
static constexpr uint16_t OSPF_MU_HELLO_TIME = 30; ///< Default HelloInterval (seconds) for non-broadcast multi-access networks.

static constexpr uint32_t OSPF_INITIAL_SEQUENCE = 0x80000001u; ///< InitialSequenceNumber: the smallest valid LSA sequence number (RFC 2328 §A.4.1).
static constexpr uint32_t OSPF_MAX_SEQUENCE     = 0x7FFFFFFFu; ///< MaxSequenceNumber: when reached the LSA must be flushed and re-originated (RFC 2328 §A.4.1).

} // namespace routing

#endif // OSPF_TYPES_HPP

