// InterfaceContext.cpp

#ifndef INTERFACE_CONTEXT_HPP
#define INTERFACE_CONTEXT_HPP

#include "ContextBase.hpp"

class Interface;

namespace Cli
{
struct InterfaceContext : ContextBase
{
    InterfaceContext(ContextBase& base, Interface& iface)
        : ContextBase(base), currentInterface(iface) {}
    Interface& currentInterface;
};
}

#endif // INTERFACE_CONTEXT_HPP
