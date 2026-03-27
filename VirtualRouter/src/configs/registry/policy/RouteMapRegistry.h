/**
 * @file RouteMapRegistry.h
 */

#ifndef ROUTE_MAP_REGISTRY_H
#define ROUTE_MAP_REGISTRY_H

#include <routing/rib/RouteSource.hpp>
#include "configs/TupleSchema.hpp"
#include "configs/RegistryTypes.hpp"
#include "configs/RegistryReference.hpp"
#include "configs/RegistryDefaultTable.hpp"
#include "configs/SubRegistry.hpp"

namespace config
{
enum class RouteMap
{
    DESCRIPTION,
    MATCH_APATHS_ADVERTISE_SET_ALL,
    MATCH_APATHS_ADVERTISE_SET_BEST, // uint8
    MATCH_APATHS_ADVERTISE_SET_BEST_RANGE, // pair uint8 uinr8
    MATCH_APATHS_ADVERTISE_SET_GROUP_BEST,
    MATCH_AS_PATH, // vector<uint32>
    MATCH_CLNS_ADDR, // vector<string>
    MATCH_CLNS_NEXT_HOP, // vector<string>
    MATCH_CLNS_ROUTE_SOURCE, // vector<string>
    MATCH_COMMUNITY_NUMBER, // vector<uint8_t>
    MATCH_COMMUNITY_NAME, // vector<string>
    MATCH_EXTCOMMUNITY, // vector<string>
    MATCH_INTERFACE, // vector<uint32>
    MATCH_IP_ADDRESS_ACL_NUMBER, // std::vector<uint16_t>
    MATCH_IP_ADDRESS_ACL_NAME, // std::vector<string>
    MATCH_IP_ADDRESS_PREFIX_LIST, // std::vector<string> % prefix-list and access-list can not co-exist in one route-map sequence.
    MATCH_IP_NEXT_HOP_ACL_NUMBER, // std::vector<uint16_t>
    MATCH_IP_NEXT_HOP_ACL_NAME, // std::vector<string>
    MATCH_IP_NEXT_HOP_PREFIX_LIST, // std::vector<string> % prefix-list and access-list can not co-exist in one route-map sequence.
    MATCH_IP_REDISTRIBUTION_SOURCE_ACL_NUMBER, // std::vector<uint16_t>
    MATCH_IP_REDISTRIBUTION_SOURCE_ACL_NAME, // std::vector<string>
    MATCH_IP_REDISTRIBUTION_SOURCE_PREFIX_LIST, // std::vector<string> % prefix-list and access-list can not co-exist in one route-map sequence.
    MATCH_IP_ROUTE_SOURCE_ACL_NUMBER, // std::vector<uint16_t>
    MATCH_IP_ROUTE_SOURCE_ACL_NAME, // std::vector<string>
    MATCH_IP_ROUTE_SOURCE_PREFIX_LIST, // std::vector<string> % prefix-list and access-list can not co-exist in one route-map sequence.
    MATCH_IP_ROUTE_REDISTRIBUTION_SOURCE_ACL_NUMBER, // std::vector<uint16_t>
    MATCH_IP_ROUTE_REDISTRIBUTION_SOURCE_ACL_NAME, // std::vector<string>
    MATCH_IP_ROUTE_REDISTRIBUTION_SOURCE_PREFIX_LIST, // std::vector<string> % prefix-list and access-list can not co-exist in one route-map sequence.
    MATCH_IPV6_ADDRESS_ACL_NAME,
    MATCH_IPV6_ADDRESS_PREFIX_LIST,
    MATCH_IPV6_NEXT_HOP_ACL_NAME,
    MATCH_IPV6_NEXT_HOP_PREFIX_LIST,
    MATCH_IPV6_ROUTE_SOURCE_ACL_NAME,
    MATCH_IPV6_ROUTE_SOURCE_PREFIX_LIST,
    MATCH_MIN_PACKET_LENGTH,
    MATCH_MAX_PACKET_LENGTH,
    MATCH_LOCAL_PREFERENCE, // optional std::vector<uint32>
    MATCH_MDT_NUMBER, // std::vector<uint32>
    MATCH_MDT_NAME, // std::vector<string>
    MATCH_METRIC, // std::vector<uint32>
    MATCH_EXTERNAL_METRIC, // std::vector<uint32>
    MATCH_MPLS_LABEL, // bool
    MATCH_POLICY_LIST, // std::vector<string>
    MATCH_ROUTE_TYPE_EXTERNAL_TYPE_1, // ExternalType
    MATCH_ROUTE_TYPE_EXTERNAL_TYPE_2, // ExternalType
    MATCH_ROUTE_TYPE_INTERNAL,
    MATCH_ROUTE_TYPE_LEVEL_1,
    MATCH_ROUTE_TYPE_LEVEL_2,
    MATCH_ROUTE_TYPE_LOCAL,
    MATCH_ROUTE_TYPE_NSSA_TYPE_1,
    MATCH_ROUTE_TYPE_NSSA_TYPE_2,
    MATCH_RKPI_INVALID,
    MATCH_RKPI_NOT_FOUND,
    MATCH_RKPI_VALID,
    MATCH_SOURCE_PROTOCOL, // RouteSource, uint32, string
    MATCH_TAG, // vector<uitn32>
};
}

#endif // ROUTE_MAP_REGISTRY_H
