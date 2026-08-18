/**
 * @file RouteSource.hpp
 * @brief Enumeration of routing protocol sources that can install routes.
 * @ingroup CORE_ROUTING_RIB
 */

#ifndef RIB_SOURCE_HPP
#define RIB_SOURCE_HPP

#include <cstdint>

namespace core
{

/**
 * @brief Identifies the protocol or mechanism that originated a route.
 *
 * Each `RibEntry` carries a `RouteSource` tag so the RIB can apply the
 * correct administrative distance and so `RouteWatcher` protocol-level
 * subscriptions can filter by source.
 */
enum class RouteSource : uint8_t
{
    DYNAMIC,         ///< Dynamically learned routes.
    CONNECTED,       ///< Directly connected interface prefix.
    STATIC,          ///< Administratively configured static route.
    EIGRP_INTERNAL,  ///< EIGRP internal (intra-AS) route.
    EIGRP_EXTERNAL,  ///< EIGRP external (redistributed) route.
    OSPF_INTRA,      ///< OSPF intra-area route (LSA type 1/2).
    OSPF_INTER,      ///< OSPF inter-area route (LSA type 3).
    OSPF_EXTERNAL,   ///< OSPF external route (LSA type 5).
    OSPF_NSSA,       ///< OSPF NSSA external route (LSA type 7).
    BGP,             ///< Border Gateway Protocol route.
    RIP,             ///< Routing Information Protocol route.
    UNKNOWN          ///< Source not identified; lowest priority.
};

} // namespace core

#endif // RIB_SOURCE_HPP
