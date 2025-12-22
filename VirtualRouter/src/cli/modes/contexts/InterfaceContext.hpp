// InterfaceContext.cpp

#ifndef INTERFACE_CONTEXT_HPP
#define INTERFACE_CONTEXT_HPP

#include "ContextBase.hpp"

#define INTERFACE_PARAMS InterfaceContext& ctx, const std::vector<std::string>& args

class Interface;

namespace Cli
{
struct InterfaceContext : ContextBase
{
    InterfaceContext(const ContextBase& base, Interface& iface)
        : ContextBase(base), currentInterface(iface) {}
    Interface& currentInterface;
};
}

#endif // INTERFACE_CONTEXT_HPP
