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

#include "configs/RegistryTypes.hpp"
#include "configs/RegistryDefaultTable.hpp"
#include "configs/SubRegistry.hpp"

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
enum class TcpFlag { ACK, FIN, PSH, RST, SYN, URG };

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
enum class StandardACE
{
    PERMIT, ///< True = permit, false = deny.
    PREFIX, ///< Source IPv4 prefix to match.
    LOG,    ///< Generate a log message on match.
    COUNT
};

#define STANDARD_ACE_DEFAULTS(X) \
    X(StandardACE, PERMIT, true) \
    X(StandardACE, LOG, false)

CONFIG_DEFAULT_TABLE(STANDARD_ACE_DEFAULTS);

/**
 * @brief Registry slot for one standard ACL entry.
 * @ingroup CONFIG_POLICY
 */
struct StandardACERegistry
{
    SubRegistry<StandardACE, nullptr,
        AtomicField<bool CONFIG_INDEX_ARG(StandardACE::PERMIT)>,
        ValueField<types::IPv4Prefix CONFIG_INDEX_ARG(StandardACE::PREFIX)>,
        AtomicField<bool CONFIG_INDEX_ARG(StandardACE::LOG)>
    > reg;
};

/**
 * @brief Fields for a named standard access-list (ordered sequence of StandardACEs).
 * @ingroup CONFIG_POLICY
 */
enum class StandardACL
{
    SEQUENCE, ///< Ordered list of ACEs keyed by sequence number.
    COUNT
};

/**
 * @brief Registry slot for one named standard ACL.
 * @ingroup CONFIG_POLICY
 */
struct StandardACLRegistry
{
    SubRegistry<StandardACL, nullptr,
        OwnedListField<StandardACERegistry, uint32_t CONFIG_INDEX_ARG(StandardACL::SEQUENCE)>
    > reg;
};

/**
 * @brief Fields for a single extended ACL entry (full 5-tuple + option matching).
 * @ingroup CONFIG_POLICY
 */
enum class ExtendedACE
{
    PROTOCOL,
    TIMEOUT,
    REFLECT,
    AUTH_ACTIVATED,
    SRC,
    DST,
    DSCP,
    LOG,
    LOG_INPUT,
    IP_OPTION,
    PRECEDENCE,
    TIME_RANGE,
    TOS,
    TTL,
    PROTOCOL_MATCH,
    COUNT
};

#define EXTENDED_ACE_DEFAULTS(X) \
    X(ExtendedACE, PROTOCOL, 0/*Invalid protocol*/) \
    X(ExtendedACE, REFLECT, false) \
    X(ExtendedACE, AUTH_ACTIVATED, false) \
    X(ExtendedACE, LOG, false) \
    X(ExtendedACE, LOG_INPUT, false)

CONFIG_DEFAULT_TABLE(EXTENDED_ACE_DEFAULTS);

/**
 * @brief Registry slot for one extended ACL entry.
 * @ingroup CONFIG_POLICY
 */
struct ExtendedACERegistry
{
    SubRegistry<ExtendedACE, nullptr,
        AtomicField<uint8_t CONFIG_INDEX_ARG(ExtendedACE::PROTOCOL)>,
        OptionalAtomicField<uint16_t CONFIG_INDEX_ARG(ExtendedACE::TIMEOUT)>,
        AtomicField<bool CONFIG_INDEX_ARG(ExtendedACE::REFLECT)>,
        AtomicField<bool CONFIG_INDEX_ARG(ExtendedACE::AUTH_ACTIVATED)>,
        ValueField<std::tuple<policy::acl::AclMatchType, std::variant<types::IPPrefix, std::string>> CONFIG_INDEX_ARG(ExtendedACE::SRC)>,
        ValueField<std::tuple<policy::acl::AclMatchType, std::variant<types::IPPrefix, std::string>> CONFIG_INDEX_ARG(ExtendedACE::DST)>,
        OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(ExtendedACE::DSCP)>,
        AtomicField<bool CONFIG_INDEX_ARG(ExtendedACE::LOG)>,
        AtomicField<bool CONFIG_INDEX_ARG(ExtendedACE::LOG_INPUT)>,
        OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(ExtendedACE::IP_OPTION)>,
        OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(ExtendedACE::PRECEDENCE)>,
        ValueField<std::string CONFIG_INDEX_ARG(ExtendedACE::TIME_RANGE)>,
        OptionalAtomicField<uint8_t CONFIG_INDEX_ARG(ExtendedACE::TOS)>,
        ValueField<std::tuple<policy::acl::Operation, std::vector<uint8_t>> CONFIG_INDEX_ARG(ExtendedACE::TTL)>,
        ValueField<policy::acl::ProtocolMatch CONFIG_INDEX_ARG(ExtendedACE::PROTOCOL_MATCH)>
    > reg;
};

/**
 * @brief Fields for a named extended access-list (ordered sequence of ExtendedACEs).
 * @ingroup CONFIG_POLICY
 */
enum class ExtendedACL
{
    SEQUENCES, ///< Ordered list of ACEs keyed by sequence number.
    COUNT
};

/**
 * @brief Registry slot for one named extended ACL.
 * @ingroup CONFIG_POLICY
 */
struct ExtendedACLRegistry
{
    SubRegistry<ExtendedACL, nullptr,
        OwnedListField<ExtendedACERegistry, uint32_t CONFIG_INDEX_ARG(ExtendedACL::SEQUENCES)>
    > reg;
};
}

#endif // ACCESS_LIST_REGISTRY_HPP
