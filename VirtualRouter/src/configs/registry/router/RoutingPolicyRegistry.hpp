// RoutingPolicyRegistry.hpp

#ifndef ROUTING_POLICY_REGISTRY_HPP
#define ROUTING_POLICY_REGISTRY_HPP

#include <TupleSchema.hpp>

namespace Config
{
#define ROUTING_POLICY_DISTRIBUTE_LIST(X) \
    X(std::string, DISTRIBUTE_LIST_IN) \
    X(uint32_t,    DISTRIBUTE_LIST_IN_INTERFACE) \
    X(bool,        DISTRIBUTE_LIST_IN_PREFIX) \
    X(std::string, DISTRIBUTE_LIST_OUT) \
    X(uint32_t,    DISTRIBUTE_LIST_OUT_INTERFACE) \
    X(bool,        DISTRIBUTE_LIST_OUT_PREFIX) \
    X(std::string, DISTRIBUTE_LIST_GATEWAY)
}

#endif
