/*
#ifndef COMMAND_PROCESSOR_H
#define COMMAND_PROCESSOR_H

#include <CliSession.h>
#include <CliEngine.h>
#include <Global.h>

class Global;
class VirtualRouter;
class Interface;
namespace Eigrp
{
    class Eigrp;
    struct EigrpNamed;
}
namespace Protocol
{
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
    Global& global;
    CliSession& terminal;

private:


    Interface* currentInterface;

    Eigrp::Eigrp* currentEigrp;
    Eigrp::EigrpNamed* currentEigrpNamed;
    EigrpConfigs::InterfaceConfigs* currentEigrpInterface;

    Protocol::Dhcp::DhcpNetwork* currentDhcpPool;
};

#endif //COMMAND_PROCESSOR_H*/
