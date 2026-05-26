/**
 * @file RouteMap.hpp
 */

#ifndef ROUTE_MAP_HPP
#define ROUTE_MAP_HPP

#include <vector>
#include <cstdint>
#include <configs/registry/policy/RouteMapRegistry.h>

namespace policy
{
using Operator = bool (*)(uint8_t*, void*);

template <auto F>
struct FieldType
{
    using type = config::DefType<typename config::RouteMapSequence>
}

template <auto Operator Func>
struct RouteMapOperation
{
}

class RouteMap
{
public:

};
}

#endif // ROUTE_MAP_HPP
