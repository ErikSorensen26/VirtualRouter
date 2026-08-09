/**
 * @file PolicyHelpers.hpp
 * @brief Shared policy types used by more than one routing protocol registry.
 *
 * Declares the `DistributeListType` config enum and the `DistributeList` tuple
 * schema. These live here rather than in a protocol registry because the
 * `distribute_list` grammar definition is shared across EIGRP, OSPF and RIP.
 */

#ifndef POLICY_HELPERS_HPP
#define POLICY_HELPERS_HPP

#include "configs/EnumSchema.hpp"
#include "configs/TupleSchema.hpp"
#include "interface/configs/InterfaceType.hpp"

namespace config
{
namespace policy {
#define DISTRIBUTE_LIST_TYPES(X) \
    X(ACL) \
    X(GATEWAY) \
    X(PREFIX) \
    X(PREFIX_GATEWAY) \
    X(ROUTE_MAP)

DEFINE_CONFIG_ENUM_NS(policy, DistributeListType, DISTRIBUTE_LIST_TYPES);

#define DISTRIBUTE_LIST_FIELDS(X) \
    X(DistributeListType, distribution) \
    X(interface::InterfaceKey, interface) \
    X(std::string, filterList) \
    X(std::string, prefixList)

DEFINE_TUPLE_SCHEMA(DistributeList, DISTRIBUTE_LIST_FIELDS);
}
}

#endif // POLICY_HELPERS_HPP
