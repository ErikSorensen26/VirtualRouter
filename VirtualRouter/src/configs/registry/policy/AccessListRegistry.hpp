/**
 * @file AccessListRegistry.hpp
 * @brief Standard and extended ACL configuration schemas.
 * @ingroup CONFIG_POLICY
 */

/**
 * @defgroup CONFIG_POLICY Policy Configuration
 * @ingroup CONFIG
 * @brief Configuration schemas for route-maps, access-lists, and prefix-lists.
 */

#ifndef ACCESS_LIST_REGISTRY_HPP
#define ACCESS_LIST_REGISTRY_HPP

#include <IPAddress.h>
#include <EnumBitMap.hpp>
#include <variant>

#include "configs/RegistryBuilder.hpp"
#include "configs/TupleSchema.hpp"

namespace config
{
namespace policy::acl
{
/**
 * @brief Specifies how an ACE source or destination is matched.
 * @ingroup CONFIG_POLICY
 */
enum class AclMatchType
{
    PREFIX,       ///< Match against an IP prefix (address + wildcard/mask).
    OBJECT_GROUP, ///< Match against a named object-group.
    PREFIX_LIST   ///< Match against a named prefix-list.
};

/**
 * @brief Port comparison operator for TCP/UDP ACE matching.
 * @ingroup CONFIG_POLICY
 */
enum class Operation { EQ, GT, LT, NEQ };

/**
 * @brief IGMP message type for extended ACE IGMP matching.
 * @ingroup CONFIG_POLICY
 */
enum class Igmp { HQ, HR, PIM };

/**
 * @brief TCP control flag bits used in extended ACE TCP matching.
 * @ingroup CONFIG_POLICY
 */
enum class TcpFlag { ACK, FIN, PSH, RST, SYN, URG, COUNT };

using MatchIcmp = uint8_t;

using MatchIgmp = std::tuple<
    uint8_t, Igmp
>;

using MatchTcp = std::tuple<
    std::tuple<Operation, std::vector<uint16_t>>,
    std::tuple<Operation, std::vector<uint16_t>>,
    std::tuple<bool, types::EnumBitMap<TcpFlag>, types::EnumBitMap<TcpFlag>>
>;

using MatchUdp = std::tuple<
    std::tuple<Operation, std::vector<uint16_t>>,
    std::tuple<Operation, std::vector<uint16_t>>
>;

using ProtocolMatch = std::variant<
    std::monostate,
    MatchIcmp, MatchIgmp,
    MatchTcp, MatchUdp
>;
}

/**
 * @brief Fields for a single standard ACL entry (permit/deny a source prefix).
 * @ingroup CONFIG_POLICY
 */
#define STANDARD_ACE_FIELD_LIST(X, Y) \
    ATOMIC_FIELD(X, Y, PERMIT, bool, true) \
    VALUE_FIELD(X, Y, PREFIX, types::IPv4Prefix) \
    ATOMIC_FIELD(X, Y, LOG, bool, false)

DEFINE_CONFIG_GROUP(StandardACE, STANDARD_ACE_FIELD_LIST)


/**
 * @brief Fields for a named standard access-list (ordered sequence of StandardACEs).
 * @ingroup CONFIG_POLICY
 */
#define STANDARD_ACL_FIELD_LIST(X, Y) \
    OWNED_LIST_FIELD(X, Y, SEQUENCE, StandardACERegistry, uint32_t)

DEFINE_CONFIG_GROUP(StandardACL, STANDARD_ACL_FIELD_LIST)



#define ACL_ENDPOINT_MATCH_FIELDS(X) \
    X(policy::acl::AclMatchType, kind) \
    X((std::variant<types::IPPrefix, std::string>), value)

DEFINE_TUPLE_SCHEMA(AclEndpointMatch, ACL_ENDPOINT_MATCH_FIELDS);

#define ACL_TTL_MATCH_FIELDS(X) \
    X(policy::acl::Operation,   op) \
    X(std::vector<uint8_t>,     values)

DEFINE_TUPLE_SCHEMA(AclTtlMatch, ACL_TTL_MATCH_FIELDS);

/**
 * @brief Fields for a single extended ACL entry (full 5-tuple + option matching).
 * @ingroup CONFIG_POLICY
 */
#define EXTENDED_ACE_FIELD_LIST(X, Y) \
    ATOMIC_FIELD(X, Y, PROTOCOL, uint8_t, 0/*Invalid protocol*/) \
    OPTIONAL_ATOMIC_FIELD(X, Y, TIMEOUT, uint16_t) \
    ATOMIC_FIELD(X, Y, REFLECT, bool, false) \
    ATOMIC_FIELD(X, Y, AUTH_ACTIVATED, bool, false) \
    VALUE_FIELD(X, Y, SRC, AclEndpointMatch) \
    VALUE_FIELD(X, Y, DST, AclEndpointMatch) \
    OPTIONAL_ATOMIC_FIELD(X, Y, DSCP, uint8_t) \
    ATOMIC_FIELD(X, Y, LOG, bool, false) \
    ATOMIC_FIELD(X, Y, LOG_INPUT, bool, false) \
    OPTIONAL_ATOMIC_FIELD(X, Y, IP_OPTION, uint8_t) \
    OPTIONAL_ATOMIC_FIELD(X, Y, PRECEDENCE, uint8_t) \
    VALUE_FIELD(X, Y, TIME_RANGE, std::string) \
    OPTIONAL_ATOMIC_FIELD(X, Y, TOS, uint8_t) \
    VALUE_FIELD(X, Y, TTL, AclTtlMatch) \
    VALUE_FIELD(X, Y, PROTOCOL_MATCH, policy::acl::ProtocolMatch)

DEFINE_CONFIG_GROUP(ExtendedACE, EXTENDED_ACE_FIELD_LIST)


/**
 * @brief Fields for a named extended access-list (ordered sequence of ExtendedACEs).
 * @ingroup CONFIG_POLICY
 */
#define EXTENDED_ACL_FIELD_LIST(X, Y) \
    OWNED_LIST_FIELD(X, Y, SEQUENCES, ExtendedACERegistry, uint32_t)

DEFINE_CONFIG_GROUP(ExtendedACL, EXTENDED_ACL_FIELD_LIST)

}

#endif // ACCESS_LIST_REGISTRY_HPP
