// InterfaceContext.cpp

#ifndef INTERFACE_CONTEXT_HPP
#define INTERFACE_CONTEXT_HPP

#include "ContextBase.hpp"

namespace interface { class Interface; }

#define INTERFACE_PARAMS InterfaceContext& ctx, const std::vector<std::string>& args

namespace cli
{
struct InterfaceContext : ContextBase
{
    InterfaceContext(const ContextBase& base, interface::Interface& iface)
        : ContextBase(base), currentInterface(iface) {}
    interface::Interface& currentInterface;
};
}

#endif // INTERFACE_CONTEXT_HPP
