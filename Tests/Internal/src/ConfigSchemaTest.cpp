// ConfigSchemaTest.cpp

#include <gtest/gtest.h>

#include "configs/EnumSchema.hpp"
#include "configs/RegistryTable.hpp"
#include "configs/TupleSchema.hpp"
#include "configs/registry/global/VrfRegistry.h"
#include "configs/registry/policy/RouteMapRegistry.h"
#include "configs/registry/router/BgpRegistry.h"
#include "configs/registry/router/EigrpRegistry.h"
#include "configs/registry/router/OspfRegistry.h"

using namespace config;

namespace
{

TEST(Internal_ConfigSchemaTest, EnumMemberIndexIsTheEnumValue)
{
    static_assert(hasEnumSchemaV<ospf::AreaType>);

    static_assert(findEnumMember<ospf::AreaType>(tokenHash("NORMAL")) == 0);
    static_assert(findEnumMember<ospf::AreaType>(tokenHash("STUB")) == 1);
    static_assert(findEnumMember<ospf::AreaType>(tokenHash("NSSA")) == 2);

    static_assert(static_cast<std::size_t>(ospf::AreaType::NORMAL) == 0);
    static_assert(static_cast<std::size_t>(ospf::AreaType::NSSA) == 2);

    SUCCEED();
}

TEST(Internal_ConfigSchemaTest, EnumCountIsNotAMember)
{
    static_assert(enumMemberCount<ospf::AreaType>() == 3);
    static_assert(findEnumMember<ospf::AreaType>(tokenHash("COUNT")) == ENUM_NOT_FOUND);
    SUCCEED();
}

TEST(Internal_ConfigSchemaTest, UnknownEnumMemberDoesNotResolve)
{
    static_assert(findEnumMember<ospf::AreaType>(tokenHash("")) == ENUM_NOT_FOUND);
    static_assert(findEnumMember<ospf::AreaType>(tokenHash("stub")) == ENUM_NOT_FOUND);
    static_assert(findEnumMember<ospf::AreaType>(tokenHash("TOTALLY_STUB")) == ENUM_NOT_FOUND);
    SUCCEED();
}

TEST(Internal_ConfigSchemaTest, EnumCarriesItsTypeName)
{
    static_assert(EnumTableOf<ospf::AreaType>::typeHash == tokenHash("AreaType"));
    static_assert(EnumTableOf<ospf::AreaType>::typeName == "AreaType");
    SUCCEED();
}

TEST(Internal_ConfigSchemaTest, EnumNamesAreForDiagnosticsOnly)
{
    static_assert(enumMemberName<ospf::AreaType>(1) == "STUB");
    static_assert(enumMemberName<ospf::AreaType>(2) == "NSSA");

    // Out of range is empty rather than undefined; a bad index reaches this from
    // a corrupt binary, where reading past the table would be worse than a blank.
    static_assert(enumMemberName<ospf::AreaType>(3).empty());
    static_assert(enumMemberName<ospf::AreaType>(0xFFFF).empty());
    SUCCEED();
}

TEST(Internal_ConfigSchemaTest, UnregisteredTypeHasNoEnumTable)
{
    static_assert(!hasEnumSchemaV<int>);
    static_assert(!hasEnumSchemaV<ospf::AreaType*>);
    static_assert(enumMemberCount<int>() == 0);
    static_assert(findEnumMember<int>(tokenHash("NORMAL")) == ENUM_NOT_FOUND);
    SUCCEED();
}

TEST(Internal_ConfigSchemaTest, TupleMemberIndexIsThePosition)
{
    static_assert(OspfAreaRange::count == 3);

    static_assert(OspfAreaRange::members[0] == tokenHash("prefix"));
    static_assert(OspfAreaRange::members[1] == tokenHash("notAdvertise"));
    static_assert(OspfAreaRange::members[2] == tokenHash("cost"));

    static_assert(OspfAreaRange::names[0] == "prefix");
    static_assert(OspfAreaRange::names[2] == "cost");
    SUCCEED();
}

TEST(Internal_ConfigSchemaTest, TupleTablesAreParallelToTheTuple)
{
    static_assert(OspfAreaRange::members.size() == OspfAreaRange::count);
    static_assert(OspfAreaRange::names.size() == OspfAreaRange::count);
    static_assert(std::tuple_size_v<OspfAreaRange::Tuple> == OspfAreaRange::count);

    static_assert(std::is_same_v<
        std::tuple_element_t<1, OspfAreaRange::Tuple>, bool>);
    SUCCEED();
}

TEST(Internal_ConfigSchemaTest, TupleAccessorMatchesTheNamedIndex)
{
    OspfAreaRange t{};

    t.notAdvertise() = true;
    t.cost() = 42u;

    EXPECT_TRUE(std::get<1>(t));
    ASSERT_TRUE(std::get<2>(t).has_value());
    EXPECT_EQ(*std::get<2>(t), 42u);

    EXPECT_TRUE(t.notAdvertise());
    EXPECT_EQ(*t.cost(), 42u);
}

TEST(Internal_ConfigSchemaTest, TupleTypeDoesNotIdentifyASchema)
{
    static_assert(std::is_same_v<
        OspfTrafEngInterface::Tuple, RouteMapMetricRange::Tuple>);

    // ...and they genuinely name their members differently, so answering one for
    // the other would resolve to the wrong element rather than merely be untidy.
    static_assert(OspfTrafEngInterface::members[0] != RouteMapMetricRange::members[0]);
    SUCCEED();
}

template <typename ENUM, ENUM F>
using SchemaAt = SchemaOf<typename RegistryOfT<ENUM>::template FieldTypeAt<F>>;

TEST(Internal_ConfigSchemaTest, FieldsSharingATupleTypeKeepSeparateSchemas)
{
    static_assert(std::is_same_v<
        SchemaAt<RouteMapSequence, RouteMapSequence::MATCH_METRIC>,
        RouteMapMetricRange>);

    // Named against the route-map schema, so its own members resolve...
    static_assert(findTupleMember<
        SchemaAt<RouteMapSequence, RouteMapSequence::MATCH_METRIC>>(
            tokenHash("deviation")) == 1);

    // ...and the OSPF schema's members, which share the tuple type, do not.
    static_assert(findTupleMember<
        SchemaAt<RouteMapSequence, RouteMapSequence::MATCH_METRIC>>(
            tokenHash("interfaceId")) == TUPLE_NOT_FOUND);
    SUCCEED();
}

TEST(Internal_ConfigSchemaTest, TwoFieldsMayShareOneSchema)
{
    static_assert(std::is_same_v<
        SchemaAt<RouteMapSequence, RouteMapSequence::MATCH_METRIC>,
        SchemaAt<RouteMapSequence, RouteMapSequence::MATCH_EXTERNAL_METRIC>>);

    static_assert(findTupleMember<
        SchemaAt<RouteMapSequence, RouteMapSequence::MATCH_EXTERNAL_METRIC>>(
            tokenHash("metric")) == 0);
    SUCCEED();
}

TEST(Internal_ConfigSchemaTest, LiveFieldsResolveTheirMembers)
{
    static_assert(hasTupleSchemaV<SchemaAt<OspfArea, OspfArea::RANGE>>);
    static_assert(findTupleMember<SchemaAt<OspfArea, OspfArea::RANGE>>(
        tokenHash("cost")) == 2);
    static_assert(tupleMemberName<SchemaAt<OspfArea, OspfArea::RANGE>>(0) == "prefix");

    static_assert(hasTupleSchemaV<SchemaAt<Eigrp, Eigrp::ADMIN_DISTANCE_RANGES>>);
    static_assert(hasTupleSchemaV<SchemaAt<Bgp, Bgp::BGP_LISTEN_RANGE>>);
    static_assert(hasTupleSchemaV<SchemaAt<Vrf, Vrf::ARP_STATIC_ENTRY>>);

    // A field stores the tuple its declared schema names, so the index a name
    // resolves to is a std::get into the shape actually held.
    static_assert(std::is_same_v<
        SchemaAt<OspfArea, OspfArea::RANGE>::Tuple, OspfAreaRange::Tuple>);
    SUCCEED();
}

TEST(Internal_ConfigSchemaTest, FieldWithoutSchemaReportsAbsent)
{
    static_assert(!hasTupleSchemaV<SchemaAt<Ospf, Ospf::ROUTER_ID>>);
    static_assert(findTupleMember<SchemaAt<Ospf, Ospf::ROUTER_ID>>(tokenHash("prefix"))
                  == TUPLE_NOT_FOUND);
    static_assert(tupleMemberName<SchemaAt<Ospf, Ospf::ROUTER_ID>>(0).empty());
    SUCCEED();
}
}
