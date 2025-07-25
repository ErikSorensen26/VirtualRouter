
#ifndef COMMAND_PROCESSOR_H
#define COMMAND_PROCESSOR_H

#include <vector>
#include <string>
#include <CliSession.h>
#include <CliEngine.h>
#include <Global.h>

class Global;
class VirtualRouter;
class Interface;
namespace Protocol
{
    class Eigrp;
    struct EigrpNamed;

    namespace Dhcp
    {
        struct DhcpNetwork;
    }
}
namespace EigrpConfigs 
{
    struct InterfaceConfigs;
}

class CommandProcessor
{
public:
    CommandProcessor(CliSession& term) : terminal(term), global(term.engine.global) {}

    bool handleUserExec(const std::vector<std::string>& command);
    bool handlePriviledgedExec(const std::vector<std::string>& command);
    bool handleGlobalConfiguration(const std::vector<std::string> commandStream);
    bool handleInterfaceConfiguration(const std::vector<std::string>& commandStream);
    bool handleRoutingConfiguration(const std::vector<std::string>& commandStream);
    bool handleAddressFamilyInterface(const std::vector<std::string>& commandStream);
    bool handleDhcpConfiguration(const std::vector<std::string>& commandStream);
    
    bool negate = false;
    
    VirtualRouter* currentVrf;

private:

    CliSession& terminal;
    Global& global;

    Interface* currentInterface;

    Protocol::Eigrp* currentEigrp;
    Protocol::EigrpNamed* currentEigrpNamed;
    EigrpConfigs::InterfaceConfigs* currentEigrpInterface;

    Protocol::Dhcp::DhcpNetwork* currentDhcpPool;
};

#endif //COMMAND_PROCESSOR_H
