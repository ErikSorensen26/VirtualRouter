// Covers the name tables the grammar resolves against: the enum member tables
// from DEFINE_CONFIG_ENUM and the tuple member tables from DEFINE_TUPLE_SCHEMA.
// Both are consumed by the flattener at build time, so most of what matters here
// is a constant expression and is asserted as one -- a runtime EXPECT would pass
// on a table the flattener could not actually have used.

#include <gtest/gtest.h>

#include "configs/EnumSchema.hpp"
#include "configs/TupleSchema.hpp"
#include "configs/registry/global/VrfRegistry.h"
#include "configs/registry/policy/RouteMapRegistry.h"
#include "configs/registry/router/BgpRegistry.h"
#include "configs/registry/router/EigrpRegistry.h"
#include "configs/registry/router/OspfRegistry.h"

using namespace config;

namespace
{

// The member index the flattener stores is the enum's own value, so a member
// added in the middle silently renumbers an existing command tree binary. These
// pin the mapping rather than just checking it is self-consistent.
TEST(Internal_ConfigSchemaTest, EnumMemberIndexIsTheEnumValue)
{
    static_assert(hasEnumSchemaV<ospf::AreaType>);

    static_assert(findEnumMember<ospf::AreaType>(tokenHash("NORMAL")) == 0);
    static_assert(findEnumMember<ospf::AreaType>(tokenHash("STUB")) == 1);
    static_assert(findEnumMember<ospf::AreaType>(tokenHash("TOTALLY_STUB")) == 2);
    static_assert(findEnumMember<ospf::AreaType>(tokenHash("NSSA")) == 3);
    static_assert(findEnumMember<ospf::AreaType>(tokenHash("TOTALLY_NSSA")) == 4);

    static_assert(static_cast<std::size_t>(ospf::AreaType::NORMAL) == 0);
    static_assert(static_cast<std::size_t>(ospf::AreaType::TOTALLY_NSSA) == 4);

    SUCCEED();
}

// COUNT terminates the enum but is not a value a grammar may name, so it must
// not appear in the table -- resolving "COUNT" would otherwise write an
// out-of-range value into the field.
TEST(Internal_ConfigSchemaTest, EnumCountIsNotAMember)
{
    static_assert(enumMemberCount<ospf::AreaType>() == 5);
    static_assert(findEnumMember<ospf::AreaType>(tokenHash("COUNT")) == ENUM_NOT_FOUND);
    SUCCEED();
}

TEST(Internal_ConfigSchemaTest, UnknownEnumMemberDoesNotResolve)
{
    static_assert(findEnumMember<ospf::AreaType>(tokenHash("")) == ENUM_NOT_FOUND);
    static_assert(findEnumMember<ospf::AreaType>(tokenHash("stub")) == ENUM_NOT_FOUND);
    static_assert(findEnumMember<ospf::AreaType>(tokenHash("TOTALLY")) == ENUM_NOT_FOUND);
    SUCCEED();
}

// The type name is what a grammar's "AreaType::STUB" is checked against, so it
// has to be carried as data and match the enum it was generated from.
TEST(Internal_ConfigSchemaTest, EnumCarriesItsTypeName)
{
    static_assert(EnumTableOf<ospf::AreaType>::typeHash == tokenHash("AreaType"));
    static_assert(EnumTableOf<ospf::AreaType>::typeName == "AreaType");
    SUCCEED();
}

TEST(Internal_ConfigSchemaTest, EnumNamesAreForDiagnosticsOnly)
{
    static_assert(enumMemberName<ospf::AreaType>(1) == "STUB");
    static_assert(enumMemberName<ospf::AreaType>(4) == "TOTALLY_NSSA");

    // Out of range is empty rather than undefined; a bad index reaches this from
    // a corrupt binary, where reading past the table would be worse than a blank.
    static_assert(enumMemberName<ospf::AreaType>(5).empty());
    static_assert(enumMemberName<ospf::AreaType>(0xFFFF).empty());
    SUCCEED();
}

// A type with no REGISTER_CONFIG_ENUM must report absent rather than fail to
// compile, because the flattener asks about every field type it meets.
TEST(Internal_ConfigSchemaTest, UnregisteredTypeHasNoEnumTable)
{
    static_assert(!hasEnumSchemaV<int>);
    static_assert(!hasEnumSchemaV<ospf::AreaType*>);
    static_assert(enumMemberCount<int>() == 0);
    static_assert(findEnumMember<int>(tokenHash("NORMAL")) == ENUM_NOT_FOUND);
    SUCCEED();
}

// The tuple member index is a std::get index, so the same renumbering concern
// applies as for enums.
TEST(Internal_ConfigSchemaTest, TupleMemberIndexIsThePosition)
{
    static_assert(OspfAreaRange::Count == 3);

    static_assert(OspfAreaRange::members[0] == tokenHash("prefix"));
    static_assert(OspfAreaRange::members[1] == tokenHash("advertise"));
    static_assert(OspfAreaRange::members[2] == tokenHash("cost"));

    static_assert(OspfAreaRange::names[0] == "prefix");
    static_assert(OspfAreaRange::names[2] == "cost");
    SUCCEED();
}

// The member tables have to stay parallel to the tuple itself: the index found
// by name is handed straight to std::get, so a table one element short or long
// would read the wrong member rather than fail.
TEST(Internal_ConfigSchemaTest, TupleTablesAreParallelToTheTuple)
{
    static_assert(OspfAreaRange::members.size() == OspfAreaRange::Count);
    static_assert(OspfAreaRange::names.size() == OspfAreaRange::Count);
    static_assert(std::tuple_size_v<OspfAreaRange::Tuple> == OspfAreaRange::Count);

    static_assert(std::is_same_v<
        std::tuple_element_t<1, OspfAreaRange::Tuple>, bool>);
    SUCCEED();
}

// The accessors and the name table must agree on which element a name means.
TEST(Internal_ConfigSchemaTest, TupleAccessorMatchesTheNamedIndex)
{
    OspfAreaRange::Tuple t{};

    OspfAreaRange::advertise(t) = true;
    OspfAreaRange::cost(t) = 42u;

    EXPECT_TRUE(std::get<1>(t));
    ASSERT_TRUE(std::get<2>(t).has_value());
    EXPECT_EQ(*std::get<2>(t), 42u);

    EXPECT_TRUE(OspfAreaRange::advertise(t));
    EXPECT_EQ(*OspfAreaRange::cost(t), 42u);
}

// Two schemas over the same element types are the same std::tuple, which is why
// the schema is keyed on the field and not on the tuple type. If this ever stops
// being true the keying can be simplified, so it is asserted rather than assumed.
TEST(Internal_ConfigSchemaTest, TupleTypeDoesNotIdentifyASchema)
{
    static_assert(std::is_same_v<
        OspfTrafEngInterface::Tuple, RouteMapMetricRange::Tuple>);

    // ...and they genuinely name their members differently, so answering one for
    // the other would resolve to the wrong element rather than merely be untidy.
    static_assert(OspfTrafEngInterface::members[0] != RouteMapMetricRange::members[0]);
    SUCCEED();
}

// The point of keying on the field: two fields whose tuples are the same type
// resolve to their own schemas. Under a type-keyed map one of these would have
// answered for both, and "Ospf::MPLS_TRAF_ENG_INTERFACES::metric" would have
// resolved against the route-map's names.
TEST(Internal_ConfigSchemaTest, FieldsSharingATupleTypeKeepSeparateSchemas)
{
    static_assert(std::is_same_v<
        TupleSchemaT<RouteMapSequence, RouteMapSequence::MATCH_METRIC>,
        RouteMapMetricRange>);

    // Named against the route-map schema, so its own members resolve...
    static_assert(findTupleMember<RouteMapSequence, RouteMapSequence::MATCH_METRIC>(
        tokenHash("deviation")) == 1);

    // ...and the OSPF schema's members, which share the tuple type, do not.
    static_assert(findTupleMember<RouteMapSequence, RouteMapSequence::MATCH_METRIC>(
        tokenHash("interfaceId")) == TUPLE_NOT_FOUND);
    SUCCEED();
}

// One schema answering for two fields is the harmless direction, and is used:
// MATCH_METRIC and MATCH_EXTERNAL_METRIC have the same members.
TEST(Internal_ConfigSchemaTest, TwoFieldsMayShareOneSchema)
{
    static_assert(std::is_same_v<
        TupleSchemaT<RouteMapSequence, RouteMapSequence::MATCH_METRIC>,
        TupleSchemaT<RouteMapSequence, RouteMapSequence::MATCH_EXTERNAL_METRIC>>);

    static_assert(findTupleMember<RouteMapSequence, RouteMapSequence::MATCH_EXTERNAL_METRIC>(
        tokenHash("metric")) == 0);
    SUCCEED();
}

// A live tuple field resolves its members; this is the path the grammar's
// "Registry::field::member" takes.
TEST(Internal_ConfigSchemaTest, LiveFieldsResolveTheirMembers)
{
    static_assert(hasTupleSchemaV<OspfArea, OspfArea::RANGE>);
    static_assert(findTupleMember<OspfArea, OspfArea::RANGE>(tokenHash("cost")) == 2);
    static_assert(tupleMemberName<OspfArea, OspfArea::RANGE>(0) == "prefix");

    static_assert(hasTupleSchemaV<Eigrp, Eigrp::NETWORK>);
    static_assert(hasTupleSchemaV<Bgp, Bgp::BGP_LISTEN_RANGE>);
    static_assert(hasTupleSchemaV<Vrf, Vrf::ARP_STATIC_ENTRY>);

    // The schema a field resolves to must be the one whose tuple the field
    // actually stores, or the index would be a std::get into a different shape.
    static_assert(std::is_same_v<
        TupleSchemaT<OspfArea, OspfArea::RANGE>::Tuple, OspfAreaRange::Tuple>);
    SUCCEED();
}

// A field with no TUPLE_SCHEMA_FOR must report absent rather than fail to
// compile, for the same reason as the enum case.
TEST(Internal_ConfigSchemaTest, FieldWithoutSchemaReportsAbsent)
{
    static_assert(!hasTupleSchemaV<Ospf, Ospf::ROUTER_ID>);
    static_assert(findTupleMember<Ospf, Ospf::ROUTER_ID>(tokenHash("prefix"))
                  == TUPLE_NOT_FOUND);
    static_assert(tupleMemberName<Ospf, Ospf::ROUTER_ID>(0).empty());
    SUCCEED();
}

}
