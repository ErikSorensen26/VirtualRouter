#include <vector>
#include <string>
#include <CliSession.h>

#ifndef COMMAND_PROCESSOR_H
#define COMMAND_PROCESSOR_H

class VirtualRouter;
class Interface;
namespace Protocol
{
    class Eigrp;
    struct EigrpNamed;


    namespace Dhcp
    {
        struct DhcpNetworkConfig;
    }
}
namespace EigrpConfigs 
{
    struct InterfaceConfigs;
}

class CommandProcessor
{
public:
    CommandProcessor(CliSession* term) : terminal(*term) {}

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
    Global& global = Global::getInstance();

    Interface* currentInterface;

    Protocol::Eigrp* currentEigrp;
    Protocol::EigrpNamed* currentEigrpNamed;
    EigrpConfigs::InterfaceConfigs* currentEigrpInterface;

    Protocol::Dhcp::DhcpNetworkConfig* currentDhcpPool;
};

#endif //COMMAND_PROCESSOR_H
